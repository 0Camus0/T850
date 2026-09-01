#include <pch.h>

#include <terrain/VoxelNavigation.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>

namespace t850::terrain {
namespace {

struct Cell {
  int x = 0;
  int y = 0;
  int z = 0;

  bool operator==(const Cell& other) const {
    return x == other.x && y == other.y && z == other.z;
  }
};

struct CellHash {
  std::size_t operator()(const Cell& cell) const {
    std::size_t seed = std::hash<int>{}(cell.x);
    seed ^= std::hash<int>{}(cell.y) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
    seed ^= std::hash<int>{}(cell.z) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
    return seed;
  }
};

struct NodeRecord {
  Cell parent;
  float cost = std::numeric_limits<float>::infinity();
  bool hasParent = false;
  bool closed = false;
};

struct OpenEntry {
  Cell cell;
  float cost = 0.0f;
  float estimate = 0.0f;
};

struct OpenEntryGreater {
  bool operator()(const OpenEntry& lhs, const OpenEntry& rhs) const {
    if (lhs.estimate != rhs.estimate) return lhs.estimate > rhs.estimate;
    return lhs.cost > rhs.cost;
  }
};

bool ValidSettings(const VoxelNavigationSettings& settings) {
  return settings.agentRadius > 0.0f && settings.agentRadius <= 4.0f &&
         settings.agentHeight > 0.0f && settings.agentHeight <= 16.0f &&
         settings.minFeetY <= settings.maxFeetY &&
         settings.maxStepUp >= 0 && settings.maxStepDown >= 0 &&
         settings.projectionHorizontalRadius >= 0 &&
         settings.projectionVerticalRadius >= 0 &&
         settings.maxVisitedNodes > 0;
}

bool HasBodyClearance(const Cell& cell,
                      const VoxelNavigationSettings& settings,
                      const VoxelNavigationQuery& query) {
  if (cell.y < settings.minFeetY || cell.y > settings.maxFeetY) return false;

  constexpr float epsilon = 0.0001f;
  const float centerX = static_cast<float>(cell.x) + 0.5f;
  const float centerZ = static_cast<float>(cell.z) + 0.5f;
  const int minX = static_cast<int>(std::floor(centerX - settings.agentRadius + epsilon));
  const int maxX = static_cast<int>(std::floor(centerX + settings.agentRadius - epsilon));
  const int minZ = static_cast<int>(std::floor(centerZ - settings.agentRadius + epsilon));
  const int maxZ = static_cast<int>(std::floor(centerZ + settings.agentRadius - epsilon));
  const int maxBodyY = static_cast<int>(
      std::ceil(static_cast<float>(cell.y) + settings.agentHeight - epsilon)) - 1;

  for (int z = minZ; z <= maxZ; ++z) {
    for (int x = minX; x <= maxX; ++x) {
      if (!query.isColumnLoaded(x, z)) return false;
      for (int y = cell.y; y <= maxBodyY; ++y) {
        if (query.isSolid(x, y, z)) return false;
      }
    }
  }
  return true;
}

bool IsStandable(const Cell& cell,
                 const VoxelNavigationSettings& settings,
                 const VoxelNavigationQuery& query) {
  if (!HasBodyClearance(cell, settings, query)) return false;
  constexpr float epsilon = 0.0001f;
  const float centerX = static_cast<float>(cell.x) + 0.5f;
  const float centerZ = static_cast<float>(cell.z) + 0.5f;
  const int minX = static_cast<int>(std::floor(centerX - settings.agentRadius + epsilon));
  const int maxX = static_cast<int>(std::floor(centerX + settings.agentRadius - epsilon));
  const int minZ = static_cast<int>(std::floor(centerZ - settings.agentRadius + epsilon));
  const int maxZ = static_cast<int>(std::floor(centerZ + settings.agentRadius - epsilon));
  for (int z = minZ; z <= maxZ; ++z)
    for (int x = minX; x <= maxX; ++x)
      if (!query.isSolid(x, cell.y - 1, z)) return false;
  return true;
}

bool ProjectCell(const XVECTOR3& point,
                 const VoxelNavigationSettings& settings,
                 const VoxelNavigationQuery& query,
                 Cell& projected) {
  const Cell origin{static_cast<int>(std::floor(point.x)),
                    static_cast<int>(std::floor(point.y + 0.5f)),
                    static_cast<int>(std::floor(point.z))};
  float bestDistance = std::numeric_limits<float>::infinity();
  bool found = false;

  for (int dz = -settings.projectionHorizontalRadius;
       dz <= settings.projectionHorizontalRadius; ++dz) {
    for (int dx = -settings.projectionHorizontalRadius;
         dx <= settings.projectionHorizontalRadius; ++dx) {
      for (int dy = -settings.projectionVerticalRadius;
           dy <= settings.projectionVerticalRadius; ++dy) {
        const Cell candidate{origin.x + dx, origin.y + dy, origin.z + dz};
        if (!IsStandable(candidate, settings, query)) continue;
        const float deltaX = static_cast<float>(candidate.x) + 0.5f - point.x;
        const float deltaY = static_cast<float>(candidate.y) - point.y;
        const float deltaZ = static_cast<float>(candidate.z) + 0.5f - point.z;
        const float distance = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
        if (distance < bestDistance) {
          bestDistance = distance;
          projected = candidate;
          found = true;
        }
      }
    }
  }
  return found;
}

float Heuristic(const Cell& cell, const Cell& goal) {
  return static_cast<float>(std::abs(cell.x - goal.x) + std::abs(cell.z - goal.z)) +
         static_cast<float>(std::abs(cell.y - goal.y)) * 0.25f;
}

XVECTOR3 CellFeetPosition(const Cell& cell) {
  return XVECTOR3(static_cast<float>(cell.x) + 0.5f,
                  static_cast<float>(cell.y),
                  static_cast<float>(cell.z) + 0.5f, 1.0f);
}

void RemoveCollinearPoints(std::vector<XVECTOR3>& points) {
  if (points.size() < 3) return;
  std::vector<XVECTOR3> reduced;
  reduced.reserve(points.size());
  reduced.push_back(points.front());
  for (std::size_t index = 1; index + 1 < points.size(); ++index) {
    const XVECTOR3 previous = points[index] - points[index - 1];
    const XVECTOR3 next = points[index + 1] - points[index];
    if (previous.x == next.x && previous.y == next.y && previous.z == next.z) continue;
    reduced.push_back(points[index]);
  }
  reduced.push_back(points.back());
  points = std::move(reduced);
}

} // namespace

VoxelNavigationResult FindVoxelPath(const XVECTOR3& startFeet,
                                    const XVECTOR3& endFeet,
                                    const VoxelNavigationSettings& settings,
                                    const VoxelNavigationQuery& query) {
  VoxelNavigationResult result;
  if (!ValidSettings(settings) || !query.isColumnLoaded || !query.isSolid) {
    result.error = "Invalid voxel navigation settings or query";
    return result;
  }

  Cell start;
  Cell goal;
  if (!ProjectCell(startFeet, settings, query, start)) {
    result.error = "Failed to project start onto a standable voxel cell";
    return result;
  }
  if (!ProjectCell(endFeet, settings, query, goal)) {
    result.error = "Failed to project goal onto a standable voxel cell";
    return result;
  }

  using Records = std::unordered_map<Cell, NodeRecord, CellHash>;
  Records records;
  records.reserve((std::min)(settings.maxVisitedNodes, std::size_t{8192}));
  std::priority_queue<OpenEntry, std::vector<OpenEntry>, OpenEntryGreater> open;
  records[start].cost = 0.0f;
  open.push(OpenEntry{start, 0.0f, Heuristic(start, goal)});

  constexpr int directions[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  bool reached = false;
  while (!open.empty() && result.visitedNodes < settings.maxVisitedNodes) {
    const OpenEntry currentEntry = open.top();
    open.pop();
    auto currentIt = records.find(currentEntry.cell);
    if (currentIt == records.end() || currentIt->second.closed ||
        currentEntry.cost > currentIt->second.cost + 0.0001f) continue;
    currentIt->second.closed = true;
    ++result.visitedNodes;
    if (currentEntry.cell == goal) {
      reached = true;
      break;
    }
    const float currentCost = currentIt->second.cost;

    for (const auto& direction : directions) {
      for (int deltaY = -settings.maxStepDown; deltaY <= settings.maxStepUp; ++deltaY) {
        const Cell neighbor{currentEntry.cell.x + direction[0],
                            currentEntry.cell.y + deltaY,
                            currentEntry.cell.z + direction[1]};
        if (!IsStandable(neighbor, settings, query)) continue;
        if (deltaY > 0) {
          bool liftClear = true;
          for (int lift = 1; lift <= deltaY; ++lift) {
            Cell liftedSource = currentEntry.cell;
            liftedSource.y += lift;
            if (!HasBodyClearance(liftedSource, settings, query)) {
              liftClear = false;
              break;
            }
          }
          if (!liftClear) continue;
        }
        const float nextCost = currentCost + 1.0f +
                               static_cast<float>(std::abs(deltaY)) * 0.25f;
        NodeRecord& neighborRecord = records[neighbor];
        if (neighborRecord.closed || nextCost >= neighborRecord.cost) continue;
        neighborRecord.cost = nextCost;
        neighborRecord.parent = currentEntry.cell;
        neighborRecord.hasParent = true;
        open.push(OpenEntry{neighbor, nextCost, nextCost + Heuristic(neighbor, goal)});
      }
    }
  }

  if (!reached) {
    result.error = result.visitedNodes >= settings.maxVisitedNodes
      ? "Voxel path search exceeded its node budget"
      : "No complete voxel path exists";
    return result;
  }

  std::vector<Cell> reversed;
  for (Cell cell = goal;;) {
    reversed.push_back(cell);
    if (cell == start) break;
    const auto found = records.find(cell);
    if (found == records.end() || !found->second.hasParent) {
      result.error = "Voxel path reconstruction failed";
      return result;
    }
    cell = found->second.parent;
  }

  result.points.reserve(reversed.size());
  for (auto it = reversed.rbegin(); it != reversed.rend(); ++it)
    result.points.push_back(CellFeetPosition(*it));
  RemoveCollinearPoints(result.points);
  result.success = true;
  return result;
}

} // namespace t850::terrain