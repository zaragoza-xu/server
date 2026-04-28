#include <algorithm>
#include <queue>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "types.h"

std::vector<Protocol::MapNode> generate_map();

namespace {

TEST(MapTest, GeneratedAdjacentLayersCoverAllNodesWithoutCrossing) {
  for (int iteration = 0; iteration < 200; ++iteration) {
    const auto map = generate_map();
    ASSERT_FALSE(map.empty());

    std::unordered_map<int, const Protocol::MapNode *> nodesById;
    std::unordered_map<int, int> indegreeById;
    for (const auto &node : map) {
      nodesById.emplace(node.nodeId, &node);
      indegreeById.emplace(node.nodeId, 0);
    }

    for (const auto &node : map) {
      for (int nextId : node.nextId) {
        auto indegreeIt = indegreeById.find(nextId);
        ASSERT_NE(indegreeIt, indegreeById.end());
        ++indegreeIt->second;
      }
    }

    std::vector<int> roots;
    for (const auto &[nodeId, indegree] : indegreeById) {
      if (indegree == 0) {
        roots.push_back(nodeId);
      }
    }

    ASSERT_EQ(roots.size(), 1U);

    std::unordered_map<int, int> levelById;
    std::queue<int> pending;
    levelById.emplace(roots.front(), 0);
    pending.push(roots.front());

    while (!pending.empty()) {
      const int currentId = pending.front();
      pending.pop();

      const auto *current = nodesById.at(currentId);
      const int nextLevel = levelById.at(currentId) + 1;
      for (int nextId : current->nextId) {
        auto [it, inserted] = levelById.emplace(nextId, nextLevel);
        if (!inserted) {
          EXPECT_EQ(it->second, nextLevel);
          continue;
        }
        pending.push(nextId);
      }
    }

    ASSERT_EQ(levelById.size(), map.size());

    std::unordered_map<int, std::vector<int>> nodesByLevel;
    for (const auto &[nodeId, level] : levelById) {
      nodesByLevel[level].push_back(nodeId);
    }

    const int maxLevel = std::max_element(levelById.begin(), levelById.end(),
                                          [](const auto &lhs, const auto &rhs) {
                                            return lhs.second < rhs.second;
                                          })
                             ->second;

    for (int level = 0; level <= maxLevel; ++level) {
      auto &levelNodes = nodesByLevel[level];
      std::sort(levelNodes.begin(), levelNodes.end());
      if (level < maxLevel) {
        ASSERT_FALSE(levelNodes.empty());
        for (int nodeId : levelNodes) {
          EXPECT_FALSE(nodesById.at(nodeId)->nextId.empty());
        }
      }
    }

    for (int level = 0; level < maxLevel; ++level) {
      auto currentLevelNodes = nodesByLevel[level];
      auto nextLevelNodes = nodesByLevel[level + 1];
      std::sort(currentLevelNodes.begin(), currentLevelNodes.end());
      std::sort(nextLevelNodes.begin(), nextLevelNodes.end());

      std::unordered_map<int, int> nextIndexById;
      for (size_t index = 0; index < nextLevelNodes.size(); ++index) {
        nextIndexById.emplace(nextLevelNodes[index], static_cast<int>(index));
      }

      std::vector<bool> covered(nextLevelNodes.size(), false);
      int previousEnd = -1;
      for (int nodeId : currentLevelNodes) {
        std::vector<int> nextIndices;
        for (int nextId : nodesById.at(nodeId)->nextId) {
          auto nextIndexIt = nextIndexById.find(nextId);
          ASSERT_NE(nextIndexIt, nextIndexById.end());
          nextIndices.push_back(nextIndexIt->second);
        }

        ASSERT_FALSE(nextIndices.empty());
        std::sort(nextIndices.begin(), nextIndices.end());
        nextIndices.erase(std::unique(nextIndices.begin(), nextIndices.end()),
                          nextIndices.end());

        for (size_t index = 1; index < nextIndices.size(); ++index) {
          EXPECT_EQ(nextIndices[index], nextIndices[index - 1] + 1);
        }

        const int start = nextIndices.front();
        const int end = nextIndices.back();
        if (previousEnd >= 0) {
          EXPECT_GE(start, previousEnd);
          EXPECT_LE(start, previousEnd + 1);
        } else {
          EXPECT_EQ(start, 0);
        }

        for (int index = start; index <= end; ++index) {
          covered[static_cast<size_t>(index)] = true;
        }
        previousEnd = end;
      }

      ASSERT_FALSE(covered.empty());
      for (bool isCovered : covered) {
        EXPECT_TRUE(isCovered);
      }
    }
  }
}

} // namespace