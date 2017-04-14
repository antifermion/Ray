#include "TreeToJson.h"
#include <vector>
#include <iomanip>
#include "json.h"

using json = nlohmann::json;

void ScanTree(const uct_node_t * uct_node, const int index, json& root){
  const auto& node = uct_node[index];

  root["win"] = (double)node.win / node.move_count;
  root["playouts"] = (int)node.move_count; //used to determine best sequence

  std::vector<double> owner;
  owner.reserve((unsigned long) pure_board_max);
  double score_black = -komi[0];
  for (int y = board_start; y <= board_end; ++y) {
    for (int x = board_start; x <= board_end; ++x) {
      double own_black = (double)node.statistic[POS(x, y)].colors[S_BLACK] / node.move_count;
      owner.push_back(own_black);
      score_black += own_black > 0.5 ? 1 : -1;
    }
  }
  root["owner"] = owner;
  root["score"] = score_black;

  if (!node.evaled || node.value_move_count == 0) return; //We are only interested in moves evalued by nn

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
    if (child.pos == PASS)
      c["pos"] = "pass";
    else if (child.pos == RESIGN)
      c["pos"] = "resign";
    else
      c["pos"] = CORRECT_POS(child.pos);
    c["pureValue"] = (double)child.value;

    if (child.index != NOT_EXPANDED)
      ScanTree(uct_node, child.index, c);
  }
}

void TreeToJson(const uct_node_t * uct_node, const int root, game_info_t * game) {
  json tree;
  tree["finalScore"] = CalculateScore(game);
  ScanTree(uct_node, root, tree);
  std::cout << std::setprecision(3) << tree << std::endl;
}