#include "Analysis.h"
#include "json.h"
#include "GoBoard.h"
#include "UctSearch.h"
#include "DynamicKomi.h"
#include "Command.h"
#include "TreeToJson.h"

#include <iostream>
#include <iomanip>
#include <map>

using json = nlohmann::json;

typedef void (*RequestHandler)(const json&);

void AnalysePositionRequest(const json &request);
void AnalyseGameRequest(const json &request);
void QuitRequest(const json &request);
void UpdateTimeSettingsRequest(const json &request);

void UpdateTimeSettings(const json& request);
void ErrorResponse(const std::string& message);
void ApplyGameSettings(const json &request);
void SetFixedHandicap(int stones);
void SetFreeHandicap(const json& stones);
void WarnResponse(const std::string& message);
bool SetupGame(const std::vector<Move>& moves);
bool GameFromJson(const json& jgame, std::vector<Move> moves);

static bool stop_analysis = false;

static game_info_t * game;
static stone color_to_play;

static std::map<std::string, RequestHandler> request_handlers;

#define OUT (*out)
static std::ostream* out;

void QuitRequest(const json &request){
  OUT << json({{"response", "quit"}}) << std::endl;
  stop_analysis = true;
}

void ErrorResponse(const std::string& message){
  OUT << json({{"response", "error"}, {"message", message}}) << std::endl;
}

void WarnResponse(const std::string& message){
  if (GetWarningsEnabled())
    OUT << json({{"response", "warning"}, {"message", message}}) << std::endl;
}

void AnalysePositionRequest(const json &request){
  UpdateTimeSettingsRequest(request);
  ApplyGameSettings(request);

  std::vector<Move> moves;
  if (!GameFromJson(request["game"], moves) || !SetupGame(moves)) return;

  int rayMove = UctSearchGenmove(game, color_to_play);
  json response = {
      {"response", "analyse-position"},
      {"rayMove", Move::FromBoardPosition(rayMove).ToJson()}
  };
  TreeToJson(response["tree"], uct_node, current_root, game);
  OUT << response << std::endl;
}

bool SetupGame(const std::vector<Move>& moves){
  for(int i = 0; i < moves.size(); ++i){
    const Move& move = moves[i];
    if (!IsLegal(game, move.ToBoardPosition(), color_to_play)) {
      ErrorResponse("Move " + std::to_string(i + 1) + " is illegal.");
      return false;
    }
    PutStone(game, move.ToBoardPosition(), color_to_play);
    FLIP_COLOR(color_to_play);
  }
  return true;
}

void AnalyseGameRequest(const json &request) {
  UpdateTimeSettings(request);
  ApplyGameSettings(request);

  std::vector<Move> moves;
  if (!GameFromJson(request["game"], moves) || !SetupGame(moves)) return;

  for (int i = 0; i < moves.size(); ++i){
    const Move& move = moves[i];
    if (!IsLegal(game, move.ToBoardPosition(), color_to_play)) {
      ErrorResponse("Move " + std::to_string(i + 1) + " is illegal.");
      return;
    }
    PutStone(game, move.ToBoardPosition(), color_to_play);
    FLIP_COLOR(color_to_play);
    int rayMove = UctSearchGenmove(game, color_to_play);
    json response = {
        {"response", "analyse-position"},
        {"rayMove", Move::FromBoardPosition(rayMove).ToJson()},
        {"moveNumber", i}
    };
    TreeToJson(response["tree"], uct_node, current_root, game);
    OUT << response << std::endl;
  }

  OUT << json({"response", "analyse-game"}) << std::endl;
}

void UpdateTimeSettingsRequest(const json &request) {
  UpdateTimeSettings(request);
  OUT << json({{"response", "update-time-settings"}}) << std::endl;
}

void UpdateTimeSettings(const json& request){
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

bool GameFromJson(const json& jgame, std::vector<Move> moves) {
  if (!jgame.is_array()){
    ErrorResponse("game needs to be an array of moves.");
    return false;
  }

  moves.reserve(jgame.size());
  for (int i = 0; i < jgame.size(); ++i){
    moves.push_back(Move::FromJson(jgame[i]));
    if (moves[i].type == Move::Invalid){
      ErrorResponse("Move " + std::to_string(i + 1) + " in game is invalid.");
      return false;
    }

    if (i > 0 && moves[i].type == Move::Pass && moves[i - 1].type == Move::Pass){
      ErrorResponse("Game is already over.");
      return false;
    }
  }
  return true;
}

void ApplyGameSettings(const json &request) {
  FreeGame(game);
  game = AllocateGame();
  InitializeBoard(game);
  color_to_play = S_BLACK;
  SetConstHandicapNum(0);
  SetHandicapNum(0);

  const json& settings = request["gameSettings"];
  if (!settings.is_object()) {
    WarnResponse("No gameSettings provided. Using previous one.");
  };

  const json& handicap = settings["handicap"];
  if (handicap.is_number()){
    SetFixedHandicap(handicap);
  } else if (handicap.is_array()){
    SetFreeHandicap(handicap);
  } else if (!handicap.is_null()){
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
  std::vector<Move> moves;
  moves.reserve(stones.size());


  for (const json& stone : stones){
    auto move = Move::FromJson(stone);
    if (move.type != Move::Play){
      WarnResponse("Invalid handicap position.");
      return;
    }
    moves.push_back(move);
  }

  int legal_stones = 0;
  for (const auto& position : moves){
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
  if (legal_stones > 0) color_to_play = S_WHITE;
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
    color_to_play = S_WHITE;
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
      {"quit", QuitRequest},
      {"analyse-position", AnalysePositionRequest},
      {"analyse-game", AnalyseGameRequest},
      {"update-time-settings", UpdateTimeSettingsRequest}
  };

  out = &(std::cout << std::setprecision(3));

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