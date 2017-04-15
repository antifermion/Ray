#include "TreeToJson.h"
#include <vector>

Move::Move(const int x, const int y)
    : x{x}, y{y}, type{x >= 0 && x < pure_board_size && y >= 0 && y < pure_board_size ? Play : Invalid} {}

Move::Move(const Type type) : x{-1}, y{-1}, type{type} {}

const int Move::ToBoardPosition() const {
  if (type == Pass) return PASS;
  return POS(x + OB_SIZE, y + OB_SIZE);
}

Move Move::FromJson(const json& move) {
  if (move.is_string()){
    if (move == "pass") return Move(Pass);
    if (move == "resign") return Move(Resign);
  } else if(move.is_object() && move["x"].is_number() && move["y"].is_number()) {
    return Move(move["x"], move["y"]);
  }
  return Move(Invalid);
}

json Move::ToJson() const {
  switch (type){
    case Pass: return "pass";
    case Resign: return "resign";
    case Play: return {{"x", x}, {"y", y}};
    default: return "invalid";
  }
}

Move Move::FromBoardPosition(const int pos) {
  if (pos == PASS) return Move(Pass);
  if (pos == RESIGN) return Move(Resign);
  return Move(CORRECT_X(pos) - 1, CORRECT_Y(pos) - 1);
}

void ScanTree(const uct_node_t * uct_node, const int index, json& root){
  const auto& node = uct_node[index];

  root["win"] = (double)node.win / node.move_count;
  root["playouts"] = (int)node.move_count; //used to determine best sequence

  std::vector<std::vector<double>> owner (
      (unsigned long) pure_board_size, std::vector<double>((unsigned long) pure_board_size, 0));
  double score_black = -komi[0];
  for (int y = board_start; y <= board_end; ++y) {
    for (int x = board_start; x <= board_end; ++x) {
      double own_black = (double)node.statistic[POS(x, y)].colors[S_BLACK] / node.move_count;
      owner[x - board_start][y - board_start] = own_black;
      score_black += own_black > 0.5 ? 1 : -1;
    }
  }
  root["owner"] = owner;
  root["score"] = score_black;

  if (!node.evaled || node.value_move_count == 0) return; //We are only interested in moves evaluated by nn

  root["policy"] = policy_evals[index];
  root["winValue"] = node.value_win / node.value_move_count;
  root["winPlusValue"] = (node.win + node.value_win * value_scale)
                         / (node.move_count + node.value_move_count * value_scale);

  json& children = root["children"];
  for (int i = 0; i < node.child_num; ++i){
    const auto& child = node.child[i];
    if (child.value == -1) continue;

    children.push_back({});
    json& c = children.back();
    c["pos"] = Move::FromBoardPosition(child.pos).ToJson();
    c["pureValue"] = (double)child.value;

    if (child.index != NOT_EXPANDED)
      ScanTree(uct_node, child.index, c);
  }
}

void TreeToJson(json& tree, const uct_node_t * uct_node, const int root, game_info_t * game) {
  tree["finalScore"] = CalculateScore(game) - komi[0];
  ScanTree(uct_node, root, tree);
}