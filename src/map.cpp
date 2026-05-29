#include "types.h"
#include <algorithm>
#include <random>
#include <utility>
#include <vector>

namespace {
using namespace Protocol;

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

MapNode::NodeType get_random_type(int col, int totalCol) {
  float p = (float)col / totalCol;
  int r = random_range(0, 9);
  if (p < 0.3f)
    return r < 8 ? MapNode::NodeType::NORMAL : MapNode::NodeType::EVENT;
  if (p < 0.7f)
    return r < 5   ? MapNode::NodeType::NORMAL
           : r < 8 ? MapNode::NodeType::ELITE
                   : MapNode::NodeType::EVENT;
  return r < 4   ? MapNode::NodeType::NORMAL
         : r < 8 ? MapNode::NodeType::ELITE
                 : MapNode::NodeType::EVENT;
}

std::vector<std::pair<int, int>> get_random_valid_path_fast(int outputCount,
                                                            int inputCount) {
  std::vector<std::pair<int, int>> result;
  if (outputCount <= 0 || inputCount <= 0) {
    return result;
  }

  result.reserve(outputCount);
  int lastEnd = 0;
  for (int i = 0; i < outputCount; i++) {
    int start = 0;
    if (i > 0) {
      const int minStart = lastEnd;
      const int maxStart = std::min(lastEnd + 1, inputCount - 1);
      start = random_range(minStart, maxStart);
    }

    const bool isLastOutput = i == outputCount - 1;
    const int end =
        isLastOutput ? inputCount - 1 : random_range(start, inputCount - 1);

    result.push_back(std::make_pair(start, end));
    lastEnd = end;
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

  // Start node (single column)
  {
    MapNode start;
    start.nodeId = nextNodeId++;
    start.type = MapNode::NodeType::NORMAL;
    map.columns.push_back({std::move(start)});
  }

  // Inner columns
  int columnCount = random_range(minCol, maxCol);
  for (int col = 0; col < columnCount; col++) {
    int rowCount = random_range(minRow, maxRow);
    std::vector<MapNode> column;

    for (int row = 0; row < rowCount; row++) {
      MapNode node;
      node.nodeId = nextNodeId++;
      node.type = get_random_type(col, columnCount);
      column.push_back(std::move(node));
    }

    map.columns.push_back(std::move(column));
  }

  // End node (single column)
  {
    MapNode end;
    end.nodeId = nextNodeId++;
    end.type = MapNode::NodeType::BOSS;
    map.columns.push_back({std::move(end)});
  }

  // Connect all columns (start→inner→end)
  connect_paths(map);

  std::vector<MapNode> flattened;
  for (auto &column : map.columns) {
    for (auto &node : column) {
      flattened.push_back(std::move(node));
    }
  }
  return flattened;
}