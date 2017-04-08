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
      return PASS;
    return POS(x + OB_SIZE, y + OB_SIZE);
  }
  virtual const bool IsValid() const {
    return (x >= 0 && y >= 0 && x < pure_board_size && y < pure_board_size) || IsPass();
  }
  const bool IsPass() const {
    return x == -1 && y == -1;
  }
  static Position FromJson(const json& pos) {
    int x = -2, y = -2;
    if (pos.is_object()){
      const json& jx = pos["x"];
      const json& jy = pos["y"];
      const json& jpass = pos["pass"];
      if (jx.is_number()) x = jx;
      if (jy.is_number()) y = jy;
      if (jpass.is_boolean() && jpass == true) x = y = -1;
    }
    return Position(x, y);
  }
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
};

struct MoveTree : Move {
  const std::vector<std::shared_ptr<MoveTree>> next_moves;
  const bool is_root;

  MoveTree(const Move& move, const std::vector<std::shared_ptr<MoveTree>> next_moves, const bool is_root = false)
      : Move({move.x, move.y}, move.color), next_moves {next_moves}, is_root {is_root} {}

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
      return std::make_shared<MoveTree>(Move({-3, -3}, S_EMPTY), next_moves, true);
    }
    Move move = Move::FromJson(tree);
    if (!move.IsValid()) return nullptr;
    return std::make_shared<MoveTree>(move, next_moves);
  }
  virtual const bool IsValid() const {
    return Move::IsValid() || is_root;
  }
  const bool IsLeaf() const {
    return next_moves.empty();
  }
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

static bool stop_analysis = false;

static game_info_t * game;

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
  auto tree = MoveTree::FromJson(request["game"]);
  if (tree == nullptr){
    ErrorResponse("Invalid game.");
    return;
  }
  if (!SetupGame(tree)){
    ErrorResponse("Invalid move in game");
  }
}

bool SetupGame(const std::shared_ptr<MoveTree> tree){
  if (tree->is_root && tree->IsLeaf()) return true;
  if (tree->is_root || tree->IsPass()) return SetupGame(tree->next_moves[0]);
  if (!IsLegal(game, tree->ToBoardPosition(), tree->color)) return false;
  PutStone(game, tree->ToBoardPosition(), tree->color);
  return tree->IsLeaf() || SetupGame(tree->next_moves[0]);
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