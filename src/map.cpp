#include "protocol.h"

#include <algorithm>
#include <random>
#include <utility>
#include <vector>

using namespace Protocol;

namespace {

struct TowerMap {

  std::vector<std::vector<MapNode>> columns;
};

int minCol = 12, maxCol = 15;

int minRow = 1, maxRow = 3;

std::mt19937 rng(std::random_device{}());

int random_range(int min, int max_inclusive) {
  std::uniform_int_distribution<int> dist(min, max_inclusive);
  return dist(rng);
}

NodeType get_random_type(int col, int totalCol) {
  float p = (float)col / totalCol;
  int r = random_range(0, 9);
  if (p < 0.3f)
    return r < 8 ? NodeType::NORMAL : NodeType::EVENT;
  if (p < 0.7f)
    return r < 5 ? NodeType::NORMAL : r < 8 ? NodeType::ELITE : NodeType::EVENT;
  return r < 4 ? NodeType::NORMAL : r < 8 ? NodeType::ELITE : NodeType::EVENT;
}

std::vector<std::pair<int, int>> get_random_valid_path_fast(int outputCount,
                                                            int inputCount) {
  std::vector<std::pair<int, int>> result;
  result.reserve(outputCount);

  for (int i = 0; i < outputCount; i++) {
    const int base = (i * inputCount) / outputCount;
    int start = std::clamp(base, 0, inputCount - 1);
    int end = start;

    if (inputCount > 1 && start + 1 < inputCount && random_range(0, 1) == 1) {
      end = start + 1;
    }

    result.push_back({start, end});
  }

  return result;
}

void connect_paths(TowerMap &map) {
  for (size_t col = 0; col < map.columns.size() - 1; col++) {
    auto &currCol = map.columns[col];
    auto &nextCol = map.columns[col + 1];

    auto path =
        get_random_valid_path_fast((int)currCol.size(), (int)nextCol.size());

    for (size_t i = 0; i < currCol.size(); i++) {
      auto [s, e] = path[i];

      for (int j = s; j <= e; j++) {
        currCol[i].nextId.push_back(nextCol[j].nodeId);
      }
    }
  }
}

} // namespace

std::vector<MapNode> generate_map() {
  TowerMap map;
  int nextNodeId = 0;

  int columnCount = random_range(minCol, maxCol);

  for (int col = 0; col < columnCount; col++) {
    int rowCount = random_range(minRow, maxRow);
    std::vector<MapNode> column;

    for (int row = 0; row < rowCount; row++) {
      MapNode node;
      node.nodeId = nextNodeId++;

      if (col == columnCount - 1)
        node.type = NodeType::BOSS;
      else
        node.type = get_random_type(col, columnCount);

      column.push_back(std::move(node));
    }

    map.columns.push_back(std::move(column));
  }

  connect_paths(map);

  std::vector<MapNode> flattened;
  for (auto &column : map.columns) {
    for (auto &node : column) {
      flattened.push_back(std::move(node));
    }
  }
  return flattened;
}