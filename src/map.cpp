#include "protocol.h"
#include <algorithm>
#include <random>
#include <utility>
#include <vector>

using namespace Protocol;

namespace {

std::mt19937 rng(std::random_device{}());

// [min, max] inclusive，对应 Unity Random.Range(min, max+1)
int random_range(int min, int max_inclusive) {
  std::uniform_int_distribution<int> dist(min, max_inclusive);
  return dist(rng);
}

MapNode::NodeType get_random_type(int col, int totalCol) {
  float p = (float)col / totalCol;
  int r = random_range(0, 9); // 对应 Random.Range(0, 10)
  if (p < 0.3f)
    return r < 8 ? MapNode::NodeType::Normal : MapNode::NodeType::Event;
  if (p < 0.7f)
    return r < 5   ? MapNode::NodeType::Normal
           : r < 8 ? MapNode::NodeType::Elite
                   : MapNode::NodeType::Event;
  return r < 4   ? MapNode::NodeType::Normal
         : r < 8 ? MapNode::NodeType::Elite
                 : MapNode::NodeType::Event;
}

float get_difficulty(int col, int totalCol) {
  float t = (float)col / (totalCol - 1);
  // 可调曲线：前期平缓，后期陡增（Mathf.Lerp(1f, 10f, t*t)）
  return 1.0f + (10.0f - 1.0f) * t * t;
}

std::vector<std::pair<int, int>> get_random_valid_path_fast(int outputCount,
                                                            int inputCount) {
  std::vector<std::pair<int, int>> result;
  result.reserve(outputCount);

  int lastEnd = 0;

  for (int i = 0; i < outputCount; i++) {
    int remainingOutputs = outputCount - i - 1;

    int minStart = lastEnd;
    int maxStart = std::min(lastEnd + 1, inputCount - 1);

    int start = random_range(minStart, maxStart);

    int minEnd = start;
    int maxEnd = inputCount - 1 - remainingOutputs;

    int end = random_range(minEnd, maxEnd);

    result.push_back({start, end});
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
        currCol[i].nextNodes.push_back(nextCol[j]);
      }
    }
  }
}

} // namespace

TowerMap generate_map() {
  TowerMap map;

  int columnCount = random_range(map.minCol, map.maxCol);

  // --- 1. 生成所有节点 ---
  for (int col = 0; col < columnCount; col++) {
    int rowCount = random_range(map.minRow, map.maxRow);
    std::vector<MapNode> column;

    for (int row = 0; row < rowCount; row++) {
      MapNode node;
      node.column = col;
      node.rowInColumn = row;

      // 最后一列强制 Boss
      if (col == columnCount - 1)
        node.type = MapNode::NodeType::Boss;
      else
        node.type = get_random_type(col, columnCount);

      // 难度曲线（线性递增）
      node.difficulty = get_difficulty(col, columnCount);

      column.push_back(std::move(node));
    }

    map.columns.push_back(std::move(column));
  }

  // --- 2. 连接路径 ---
  connect_paths(map);

  // --- 3. 设置起点 & Boss ---
  map.startNode =
      map.columns[0][random_range(0, (int)map.columns[0].size() - 1)];
  map.bossNode = map.columns[columnCount - 1][0]; // 最后一列默认只有一个

  return map;
}