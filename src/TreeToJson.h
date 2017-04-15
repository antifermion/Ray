#ifndef RAY_TREETOJSON_H
#define RAY_TREETOJSON_H

#include <string>
#include "UctSearch.h"
#include "json.h"

using json = nlohmann::json;

struct Move {
  enum Type {Play, Pass, Resign, Invalid};
  const int x, y;
  const Type type;
  Move(const int x, const int y);
  Move(const Type type);
  json ToJson() const;
  const int ToBoardPosition() const;
  static Move FromJson(const json& move);
  static Move FromBoardPosition(const int pos);
};

void TreeToJson(json& tree, const uct_node_t * uct_node, int root, game_info_t * game);


#endif //RAY_TREETOJSON_H
