#include "Analysis.h"
#include "json.h"
#include "GoBoard.h"
#include "UctSearch.h"
#include "DynamicKomi.h"
#include "Command.h"

#include <iostream>
#include <map>

using json = nlohmann::json;

typedef void (*RequestHandler)(const json&);

struct Position {
  const int x, y;
  Position(const int x, const int y) : x{x}, y{y} {}
  const int ToBoardPosition() const {
    if (IsPass())
      return POS_PASS;
    return POS(x + OB_SIZE, y + OB_SIZE);
  }
  virtual const bool IsValid() const {
    return IsMove() || IsPass() || IsResign();
  }
  const bool IsMove() const {
    return x >= 0 && y >= 0 && x < pure_board_size && y < pure_board_size;
  }
  const bool IsPass() const {
    return x == POS_PASS && y == POS_PASS;
  }
  const bool IsResign() const {
    return x == POS_RESIGN && y == POS_RESIGN;
  }
  static Position FromJson(const json& pos) {
    int x = -4, y = -4;
    if (pos.is_object()){
      const json& jx = pos["x"];
      const json& jy = pos["y"];
      const json& jaction = pos["action"];
      if (jx.is_number()) x = jx;
      if (jy.is_number()) y = jy;
      if (jaction.is_string()){
        if (jaction == "pass") x = y = POS_PASS;
        if (jaction == "resign") x = y = POS_RESIGN;
      }
    }
    return Position(x, y);
  }
  static Position FromBoardPosition(const int pos){
    if (pos == RESIGN) return Position(POS_RESIGN, POS_RESIGN);
    if (pos == PASS) return Position(POS_PASS, POS_PASS);
    return Position(X(pos) - OB_SIZE, Y(pos) - OB_SIZE);
  }
  virtual void AddToJson(json& j) const{
    if (IsPass()){
      j["action"] = "pass";
    } else if (IsResign()){
      j["action"] = "resign";
    } else {
      j["x"] = x;
      j["y"] = y;
    }
  }
private:
  static const int POS_PASS = -1;
  static const int POS_RESIGN = -2;
};

struct Move : Position {
  const stone color;
  Move(const Position& position, const stone color) : Position(position.x, position.y), color {color} {}
  virtual const bool IsValid() const {
    return Position::IsValid() && (color == S_BLACK || color == S_WHITE);
  }
  static Move FromJson(const json& move) {
    const Position position = Position::FromJson(move);
    stone color = S_EMPTY;
    if (move.is_object()){
      const json& jcolor = move["color"];
      if (jcolor.is_string()){
        const std::string scolor = jcolor;
        if (scolor == "black")
          color = S_BLACK;
        else if (scolor == "white")
          color = S_WHITE;
      }
    }
    return Move(position, color);
  }
  virtual void AddToJson(json& j) const {
    Position::AddToJson(j);
    j["color"] = color == S_BLACK ? "black" : "white";
  }
};

struct MoveTree : Move {
  const std::vector<std::shared_ptr<MoveTree>> next_moves;

  MoveTree(const Move& move, const std::vector<std::shared_ptr<MoveTree>> next_moves)
      : Move({move.x, move.y}, move.color), next_moves {next_moves}{}

  static std::shared_ptr<MoveTree> FromJson(const json& tree){
    const json& next = tree.is_object() ? tree["next"] : tree;
    std::vector<std::shared_ptr<MoveTree>> next_moves;
    if (next.is_object()){
      next_moves.push_back(FromJson(next));
      if (next_moves.back() == nullptr) return nullptr;
    } else if (next.is_array()){
      next_moves.reserve(next.size());
      for (const json& next_move : next){
        if (!next_move.is_object()) return nullptr;
        next_moves.push_back(FromJson(next_move));
      }
      if (next_moves.back() == nullptr) return nullptr;
    } else {
      return nullptr;
    }

    if (tree.is_array()){
      return std::make_shared<MoveTree>(Move({ROOT, ROOT}, S_EMPTY), next_moves);
    }
    Move move = Move::FromJson(tree);
    if (!move.IsValid()) return nullptr;
    return std::make_shared<MoveTree>(move, next_moves);
  }
  virtual const bool IsValid() const {
    return Move::IsValid() || IsRoot();
  }
  const bool IsLeaf() const {
    return next_moves.empty();
  }
  const bool IsRoot() const {
    return x == ROOT && y == ROOT;
  }
private:
  static const int ROOT = -3;
};

void AnalysePosition(const json& request);
void AnalyseGame(const json& request);
void ErrorResponse(const std::string& message);
void Quit(const json& request);
void UpdateTimeSettings(const json &request);
void ApplyGameSettings(const json &request);
void SetFixedHandicap(int stones);
void SetFreeHandicap(const json& stones);
void WarnResponse(const std::string& message);
bool SetupGame(const std::shared_ptr<MoveTree> tree);
bool SetupGame(const std::vector<Move>& moves);

static bool stop_analysis = false;

static game_info_t * game;
static stone last_color;

std::map<std::string, RequestHandler> request_handlers;

void Quit(const json& request){
  std::cout << json({{"response", "quit"}}) << std::endl;
  stop_analysis = true;
}

void ErrorResponse(const std::string& message){
  std::cout << json({{"response", "error"}, {"message", message}}) << std::endl;
}

void WarnResponse(const std::string& message){
  if (GetWarningsEnabled())
    std::cout << json({{"response", "warning"}, {"message", message}}) << std::endl;
}

void AnalysePosition(const json& request){
  UpdateTimeSettings(request);
  ApplyGameSettings(request);

  const json& jtree = request["gameTree"];
  if (!jtree.is_null()){
    auto tree = MoveTree::FromJson(request["gameTree"]);
    if (tree == nullptr){
      ErrorResponse("Invalid gameTree.");
      return;
    }
    if (!SetupGame(tree)){
      ErrorResponse("Invalid move in gameTree.");
      return;
    }
  } else {
    const json& jgame = request["game"];
    if (!jgame.is_array()){
      ErrorResponse("No game provided.");
      return;
    }
    std::vector<Move> moves;
    moves.reserve(jgame.size());
    for (const json& jmove : jgame){
      moves.push_back(Move::FromJson(jmove));
      if (!moves.back().IsValid()){
        ErrorResponse("Invalid move in game.");
        return;
      }
    }
    if (!SetupGame(moves)){
      ErrorResponse("Invalid move in gameTree.");
      return;
    }
  }

  UctSearchGenmove(game, FLIP_COLOR(last_color));
}

bool SetupGame(const std::shared_ptr<MoveTree> tree){
  last_color = tree->color;
  if (!tree->IsMove() && tree->IsLeaf()) return true;
  if (tree->IsMove()){
    if (!IsLegal(game, tree->ToBoardPosition(), tree->color)) return false;
    PutStone(game, tree->ToBoardPosition(), tree->color);
  }
  return tree->IsLeaf() || SetupGame(tree->next_moves[0]);
}

bool SetupGame(const std::vector<Move>& moves){
  for(const Move& move : moves){
    if (!IsLegal(game, move.ToBoardPosition(), move.color)) return false;
    PutStone(game, move.ToBoardPosition(), move.color);
  }
  last_color = moves.back().color;
  return true;
}

void AnalyseGame(const json& request) {
  ErrorResponse("Not yet implemented.");
}

void UpdateTimeSettings(const json &request) {
  const json& settings = request["timeSettings"];
  if (!settings.is_object()) return;

  const json& playouts = settings["playouts"];
  if (playouts.is_number()){
    SetPlayout(playouts);
    SetMode(CONST_PLAYOUT_MODE);
    UpdatePlayout();
  } else {
    WarnResponse("playouts needs to be a number.");
  }

  const json& time = settings["time"];
  if (time.is_number()) {
    SetConstTime(time);
    SetMode(CONST_TIME_MODE);
    UpdatePlayout();
  } else {
    WarnResponse("time needs to be a number.");
  }
}

void ApplyGameSettings(const json &request) {
  FreeGame(game);
  game = AllocateGame();
  InitializeBoard(game);

  const json& settings = request["gameSettings"];
  if (!settings.is_object()) {
    WarnResponse("No gameSettings provided. Using previous one.");
  };

  const json& handicap = settings["handicap"];
  if (handicap.is_number()){
    SetFixedHandicap(handicap);
  } else if (handicap.is_array()){
    SetFreeHandicap(handicap);
  } else {
    WarnResponse("handicap needs to be an array of positions or a number.");
  }

  const json& komi = settings["komi"];
  if (komi.is_number()){
    SetKomi(komi);
  } else {
    WarnResponse("komi needs to be a number.");
  }

  const json& const_handicap = settings["constHandicap"];
  if (const_handicap.is_number()){
    SetConstHandicapNum(const_handicap);
    SetHandicapNum(0);
  } else {
    WarnResponse("constHandicap needs to be a number");
  }
}

void SetFreeHandicap(const json& stones){
  std::vector<Position> positions;
  positions.reserve(stones.size());


  for (const json& stone : stones){
    auto position = Position::FromJson(stone);
    if (!position.IsValid() || position.IsPass()){
      WarnResponse("Invalid handicap position.");
      return;
    }
    positions.push_back(position);
  }

  int legal_stones = 0;
  for (const auto& position : positions){
    int board_pos = position.ToBoardPosition();
    if (IsLegal(game, board_pos, S_BLACK)){
      ++legal_stones;
      PutStone(game, board_pos, S_BLACK);
    } else {
      WarnResponse("Free handicap contains illegal move.");
    }
  }

  SetHandicapNum(legal_stones);
  SetKomi(0.5);
}

void SetFixedHandicap(int stones){
  if (stones < 1 || stones > 9 || pure_board_size < 9 || pure_board_size % 2 == 0) return;

  const int middle = board_start  + (pure_board_size - 1) / 2;
  const int corner_offset = pure_board_size <= 11 ? 2 : 3;
  const int corner1 = board_start + corner_offset;
  const int corner2 = board_start + pure_board_size - 1 - corner_offset;

  const int handicap_positions[9] = {
      POS(corner1, corner1),
      POS(middle, corner1),
      POS(corner2, corner1),
      POS(corner1, middle),
      POS(middle, middle),
      POS(corner2, middle),
      POS(corner1, corner2),
      POS(middle, corner2),
      POS(corner2, corner2)
  };

  const int positions_for_handicap[8][9] = {
      {2, 6}, //2
      {0, 2, 6}, //3
      {0, 2, 6, 8}, //4
      {0, 2, 4, 6, 8}, //5
      {0, 2, 4, 5, 6, 8}, //6
      {0, 2, 3, 4, 5, 6, 8}, //7
      {0, 1, 2, 3, 5, 6, 7, 8}, //8
      {0, 1, 2, 3, 4, 5, 6, 7, 8}, //9
  };

  if (stones >= 2){
    const int * positions = positions_for_handicap[stones - 2];
    for (int i = 0; i < stones; ++i){
      PutStone(game, handicap_positions[positions[i]], S_BLACK);
    }
  }

  SetKomi(0.5);
  SetHandicapNum(stones);
}

void Analysis_main(){
  request_handlers = {
      {"quit", Quit},
      {"analyse-position", AnalysePosition},
      {"analyse-game", AnalyseGame},
      {"update-time-settings", UpdateTimeSettings}
  };

  while(!stop_analysis){
    json request;
    try {
      std::string line;
      std::getline(std::cin, line);
      request = json::parse(line);
    } catch (...) {
      ErrorResponse("Invalid request.");
      continue;
    }

    if (!request.is_object()){
      ErrorResponse("Invalid request.");
      continue;
    }

    const json& request_type = request["request"];
    if (request_type.is_string() && request_handlers.count(request_type)){
      request_handlers[request_type](request);
    } else {
      ErrorResponse("Invalid request type.");
    }
  }
}