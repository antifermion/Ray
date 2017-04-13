#include "TreeToJson.h"
#include <vector>
#include "json.h"

using json = nlohmann::json;

void ScanTree(const uct_node_t * uct_node, const int index, json& root){
  const auto& node = uct_node[index];

  root["win"] = (double)node.win / node.move_count;
  root["playouts"] = (int)node.move_count; //used to determine best sequence

  std::vector<double> owner;
  owner.reserve((unsigned long) pure_board_max);
  for (int y = board_start; y <= board_end; ++y) {
    for (int x = board_start; x <= board_end; ++x) {
      double own_black = (double)node.statistic[POS(x, y)].colors[S_BLACK] / node.move_count;
      owner.push_back(own_black);
    }
  }
  root["owner"] = owner;

  if (!node.evaled || node.value_move_count == 0) return; //We are only interested in moves evalued by nn

  root["policy"] = policy_evals[index];
  root["winValue"] = node.value_win / node.value_move_count;
  root["winPlusValue"] = (node.win + node.value_win * value_scale)
                         / (node.move_count + node.value_move_count * value_scale);

  json& children = root["children"];
  for (int i = 0; i < node.child_num; ++i){
    const auto& child = node.child[i];
    children.push_back({});
    json& c = children.back();
    if (child.pos == PASS)
      c["pos"] = "pass";
    else if (child.pos == RESIGN)
      c["pos"] = "resign";
    else
      c["pos"] = CORRECT_POS(child.pos);
    c["pureValue"] = (double)child.value;
    //std::cerr << c << std::endl;

    if (child.index != NOT_EXPANDED)
      ScanTree(uct_node, child.index, c);
  }
}

void TreeToJson(const uct_node_t * uct_node, const int root) {
  json tree;
  ScanTree(uct_node, root, tree);
  std::cout << tree << std::endl;
}