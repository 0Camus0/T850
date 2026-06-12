#include <pch.h>

#include <navigation/NavigationSystem.h>

#include <debug/RuntimeTelemetry.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>
#include <utils/XDataBase.h>
#include <utils/xDefs.h>
#include <utils/ThreadPool.h>
#include <scene/PrimitiveInstance.h>
#include <scene/RenderMesh.h>
#include <scene/RenderSkinnedMesh.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#if defined(T850_ENABLE_RECAST)
#include <recastnavigation/DetourCrowd.h>
#include <recastnavigation/DetourNavMesh.h>
#include <recastnavigation/DetourNavMeshBuilder.h>
#include <recastnavigation/DetourNavMeshQuery.h>
#include <recastnavigation/DetourStatus.h>
#include <recastnavigation/DetourTileCache.h>
#include <recastnavigation/Recast.h>
#include <recastnavigation/version.h>
#endif

namespace t850 {
namespace navigation {

namespace {

void SetError(std::string* out, const std::string& message) {
  if (out) *out = message;
  T8_LOG_ERROR("[Navigation] %s", message.c_str());
}

bool IsFiniteVec3(const XVECTOR3& v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

XVECTOR3 TransformPoint(const XVECTOR3& point, const XMATRIX44& matrix) {
  return XVECTOR3(
      point.x * matrix.m11 + point.y * matrix.m21 + point.z * matrix.m31 + matrix.m41,
      point.x * matrix.m12 + point.y * matrix.m22 + point.z * matrix.m32 + matrix.m42,
      point.x * matrix.m13 + point.y * matrix.m23 + point.z * matrix.m33 + matrix.m43,
      1.0f);
}

#if defined(T850_ENABLE_RECAST)
template <typename T, void(*FreeFn)(T*)>
struct RecastDeleter {
  void operator()(T* ptr) const {
    if (ptr) FreeFn(ptr);
  }
};

using HeightfieldPtr = std::unique_ptr<rcHeightfield, RecastDeleter<rcHeightfield, rcFreeHeightField>>;
using CompactHeightfieldPtr = std::unique_ptr<rcCompactHeightfield, RecastDeleter<rcCompactHeightfield, rcFreeCompactHeightfield>>;
using ContourSetPtr = std::unique_ptr<rcContourSet, RecastDeleter<rcContourSet, rcFreeContourSet>>;
using PolyMeshPtr = std::unique_ptr<rcPolyMesh, RecastDeleter<rcPolyMesh, rcFreePolyMesh>>;
using PolyMeshDetailPtr = std::unique_ptr<rcPolyMeshDetail, RecastDeleter<rcPolyMeshDetail, rcFreePolyMeshDetail>>;

struct NavMeshDeleter {
  void operator()(dtNavMesh* ptr) const { if (ptr) dtFreeNavMesh(ptr); }
};

struct NavMeshQueryDeleter {
  void operator()(dtNavMeshQuery* ptr) const { if (ptr) dtFreeNavMeshQuery(ptr); }
};

constexpr unsigned short kNavPolyFlagWalk = 0x01;
constexpr unsigned short kNavPolyFlagDrop = 0x02;
constexpr unsigned short kNavPolyFlagJump = 0x04;
constexpr unsigned short kNavPolyFlagJumpPad = 0x08;
constexpr unsigned short kNavPolyFlagJumpIntent = 0x10;
constexpr unsigned short kNavPolyFlagAllTraversal =
    kNavPolyFlagWalk | kNavPolyFlagDrop | kNavPolyFlagJump | kNavPolyFlagJumpPad | kNavPolyFlagJumpIntent;
constexpr unsigned char kNavAreaWalk = 0;
constexpr unsigned char kNavAreaDrop = 1;
constexpr unsigned char kNavAreaJump = 2;
constexpr unsigned char kNavAreaJumpPad = 3;
constexpr unsigned char kNavAreaJumpIntent = 4;
constexpr int kMaxPathPolys = 256;
constexpr int kMaxStraightPath = 256;
constexpr std::array<char, 8> kNavMeshCacheMagic = { 'T', '8', 'N', 'A', 'V', 'C', 'H', 'E' };
constexpr uint32_t kNavMeshCacheVersion = 5;
constexpr uint32_t kOffMeshUserIdBase = 0x85000000u;

template <typename T>
void HashBytes(uint64_t& hash, const T& value) {
  const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&value);
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    hash ^= bytes[i];
    hash *= 0x100000001b3ull;
  }
}

void HashData(uint64_t& hash, const void* data, std::size_t byteCount) {
  const unsigned char* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t i = 0; i < byteCount; ++i) {
    hash ^= bytes[i];
    hash *= 0x100000001b3ull;
  }
}

uint64_t ComputeNavMeshCacheKey(const NavMeshGeometry& geometry,
                                const NavMeshBuildSettings& settings) {
  uint64_t hash = 0xcbf29ce484222325ull;
  HashBytes(hash, kNavMeshCacheVersion);
  HashBytes(hash, settings.cellSize);
  HashBytes(hash, settings.cellHeight);
  HashBytes(hash, settings.agentHeight);
  HashBytes(hash, settings.agentRadius);
  HashBytes(hash, settings.agentMaxClimb);
  HashBytes(hash, settings.agentMaxSlope);
  HashBytes(hash, settings.regionMinSize);
  HashBytes(hash, settings.regionMergeSize);
  HashBytes(hash, settings.edgeMaxLen);
  HashBytes(hash, settings.edgeMaxError);
  HashBytes(hash, settings.vertsPerPoly);
  HashBytes(hash, settings.detailSampleDist);
  HashBytes(hash, settings.detailSampleMaxError);
  HashBytes(hash, settings.queryExtents.x);
  HashBytes(hash, settings.queryExtents.y);
  HashBytes(hash, settings.queryExtents.z);
  HashBytes(hash, settings.queryExtents.w);
  HashBytes(hash, settings.enableAutoDropLinks);
  HashBytes(hash, settings.dropLinkMinHeight);
  HashBytes(hash, settings.dropLinkMaxHeight);
  HashBytes(hash, settings.dropLinkMaxHorizontalDistance);
  HashBytes(hash, settings.dropLinkSampleSpacing);
  HashBytes(hash, settings.dropLinkRadius);
  HashBytes(hash, settings.enableAutoJumpLinks);
  HashBytes(hash, settings.jumpLinkMaxHorizontalDistance);
  HashBytes(hash, settings.jumpLinkSampleSpacing);
  HashBytes(hash, settings.jumpLinkRadius);
  HashBytes(hash, settings.enableHybridJumpLinks);
  HashBytes(hash, settings.hybridJumpMaxLinks);
  HashBytes(hash, settings.offMeshLinkValidationKey);
  const uint64_t vertexCount = static_cast<uint64_t>(geometry.vertices.size());
  const uint64_t indexCount = static_cast<uint64_t>(geometry.indices.size());
  const uint64_t offMeshLinkCount = static_cast<uint64_t>(geometry.offMeshLinks.size());
  HashBytes(hash, vertexCount);
  HashBytes(hash, indexCount);
  HashBytes(hash, offMeshLinkCount);
  for (const XVECTOR3& vertex : geometry.vertices) {
    HashBytes(hash, vertex.x);
    HashBytes(hash, vertex.y);
    HashBytes(hash, vertex.z);
  }
  if (!geometry.indices.empty()) {
    HashData(hash, geometry.indices.data(), geometry.indices.size() * sizeof(int));
  }
  for (const NavOffMeshLink& link : geometry.offMeshLinks) {
    HashBytes(hash, link.start.x);
    HashBytes(hash, link.start.y);
    HashBytes(hash, link.start.z);
    HashBytes(hash, link.end.x);
    HashBytes(hash, link.end.y);
    HashBytes(hash, link.end.z);
    HashBytes(hash, link.radius);
    HashBytes(hash, link.bidirectional);
    const uint8_t type = static_cast<uint8_t>(link.type);
    HashBytes(hash, type);
    HashBytes(hash, link.userId);
  }
  return hash;
}

std::filesystem::path NavMeshCachePath(uint64_t key) {
  std::ostringstream name;
  name << "Navigation/.t8cache/navmesh_"
       << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << key
       << ".t8nav";
  return ResourceLocator::Instance().ResolveCachePath(name.str());
}

uint32_t EncodeOffMeshUserId(NavTraversalType type, uint32_t index) {
  return kOffMeshUserIdBase |
      ((static_cast<uint32_t>(type) & 0xffu) << 16u) |
      (index & 0xffffu);
}

NavTraversalType DecodeOffMeshUserId(uint32_t userId) {
  if ((userId & 0xff000000u) != kOffMeshUserIdBase) {
    return NavTraversalType::Walk;
  }

  const uint32_t type = (userId >> 16u) & 0xffu;
  if (type == static_cast<uint32_t>(NavTraversalType::Drop)) {
    return NavTraversalType::Drop;
  }
  if (type == static_cast<uint32_t>(NavTraversalType::Jump)) {
    return NavTraversalType::Jump;
  }
  if (type == static_cast<uint32_t>(NavTraversalType::JumpIntent)) {
    return NavTraversalType::Jump;
  }
  if (type == static_cast<uint32_t>(NavTraversalType::JumpPad)) {
    return NavTraversalType::JumpPad;
  }
  return NavTraversalType::Walk;
}

unsigned short PolyFlagsForTraversal(NavTraversalType type) {
  switch (type) {
    case NavTraversalType::Drop:
      return kNavPolyFlagDrop;
    case NavTraversalType::Jump:
      return kNavPolyFlagJump;
    case NavTraversalType::JumpIntent:
      return kNavPolyFlagJumpIntent;
    case NavTraversalType::JumpPad:
      return kNavPolyFlagJumpPad;
    case NavTraversalType::Walk:
    default:
      return kNavPolyFlagWalk;
  }
}

unsigned char PolyAreaForTraversal(NavTraversalType type) {
  switch (type) {
    case NavTraversalType::Drop:
      return kNavAreaDrop;
    case NavTraversalType::Jump:
      return kNavAreaJump;
    case NavTraversalType::JumpIntent:
      return kNavAreaJumpIntent;
    case NavTraversalType::JumpPad:
      return kNavAreaJumpPad;
    case NavTraversalType::Walk:
    default:
      return kNavAreaWalk;
  }
}

void ConfigureTraversalFilter(dtQueryFilter& filter) {
  filter.setIncludeFlags(kNavPolyFlagAllTraversal);
  filter.setExcludeFlags(0);
  filter.setAreaCost(kNavAreaWalk, 1.0f);
  filter.setAreaCost(kNavAreaDrop, 0.35f);
  filter.setAreaCost(kNavAreaJump, 0.20f);
  filter.setAreaCost(kNavAreaJumpPad, 0.15f);
  filter.setAreaCost(kNavAreaJumpIntent, 1.75f);
}

void ConfigureWalkFilter(dtQueryFilter& filter) {
  filter.setIncludeFlags(kNavPolyFlagWalk);
  filter.setExcludeFlags(0);
  filter.setAreaCost(kNavAreaWalk, 1.0f);
}

float DistanceSq3(const XVECTOR3& a, const XVECTOR3& b) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  const float dz = a.z - b.z;
  return dx * dx + dy * dy + dz * dz;
}

float HorizontalDistanceSq3(const XVECTOR3& a, const XVECTOR3& b) {
  const float dx = a.x - b.x;
  const float dz = a.z - b.z;
  return dx * dx + dz * dz;
}

XVECTOR3 NormalizeHorizontalOr(XVECTOR3 value, const XVECTOR3& fallback) {
  value.y = 0.0f;
  value.w = 0.0f;
  const float lengthSq = value.x * value.x + value.z * value.z;
  if (lengthSq <= 0.000001f) {
    return fallback;
  }
  const float invLength = 1.0f / std::sqrt(lengthSq);
  value.x *= invLength;
  value.z *= invLength;
  return value;
}

XVECTOR3 PolyMeshVertexWorld(const rcPolyMesh& polyMesh, unsigned short vertexIndex) {
  if (vertexIndex >= static_cast<unsigned short>(polyMesh.nverts)) {
    return XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
  }

  const unsigned short* v = &polyMesh.verts[static_cast<std::size_t>(vertexIndex) * 3u];
  return XVECTOR3(
      polyMesh.bmin[0] + static_cast<float>(v[0]) * polyMesh.cs,
      polyMesh.bmin[1] + static_cast<float>(v[1]) * polyMesh.ch,
      polyMesh.bmin[2] + static_cast<float>(v[2]) * polyMesh.cs,
      1.0f);
}

XVECTOR3 PolyMeshPolyCenter(const rcPolyMesh& polyMesh, int polyIndex) {
  const unsigned short* poly = &polyMesh.polys[static_cast<std::size_t>(polyIndex) * polyMesh.nvp * 2u];
  XVECTOR3 center(0.0f, 0.0f, 0.0f, 1.0f);
  int validCount = 0;
  for (int vertexIndex = 0; vertexIndex < polyMesh.nvp; ++vertexIndex) {
    if (poly[vertexIndex] == RC_MESH_NULL_IDX) {
      break;
    }
    center += PolyMeshVertexWorld(polyMesh, poly[vertexIndex]);
    ++validCount;
  }
  if (validCount > 0) {
    const float invCount = 1.0f / static_cast<float>(validCount);
    center.x *= invCount;
    center.y *= invCount;
    center.z *= invCount;
    center.w = 1.0f;
  }
  return center;
}

bool ProjectLinkEndpoint(dtNavMeshQuery& query,
                         const XVECTOR3& point,
                         const XVECTOR3& extents,
                         XVECTOR3& outPoint,
                         dtPolyRef& outRef) {
  dtQueryFilter filter;
  ConfigureWalkFilter(filter);
  const float position[3] = { point.x, point.y, point.z };
  const float queryExtents[3] = {
    (std::max)(0.05f, extents.x),
    (std::max)(0.05f, extents.y),
    (std::max)(0.05f, extents.z)
  };
  float nearestPoint[3] = {};
  outRef = 0;
  const dtStatus status = query.findNearestPoly(position, queryExtents, &filter, &outRef, nearestPoint);
  if (dtStatusFailed(status) || !outRef) {
    return false;
  }
  outPoint = XVECTOR3(nearestPoint[0], nearestPoint[1], nearestPoint[2], 1.0f);
  return true;
}

bool IsDuplicateOffMeshLink(const std::vector<NavOffMeshLink>& links,
                            const XVECTOR3& start,
                            const XVECTOR3& end,
                            NavTraversalType type,
                            float thresholdSq) {
  for (const NavOffMeshLink& link : links) {
    if (link.type != type) {
      continue;
    }
    if (DistanceSq3(link.start, start) <= thresholdSq &&
        DistanceSq3(link.end, end) <= thresholdSq) {
      return true;
    }
  }
  return false;
}

bool PassesOffMeshLinkValidator(const NavOffMeshLink& link,
                                const std::function<bool(const NavOffMeshLink&)>& validator,
                                int& rejectedCount) {
  if (!validator) {
    return true;
  }
  if (validator(link)) {
    return true;
  }
  ++rejectedCount;
  return false;
}

bool PassesOffMeshHybridLinkValidator(const NavOffMeshLink& link,
                                      const std::function<bool(const NavOffMeshLink&)>& validator,
                                      int& rejectedCount) {
  if (!validator) {
    return true;
  }
  if (validator(link)) {
    return true;
  }
  ++rejectedCount;
  return false;
}

std::vector<NavOffMeshLink> NormalizeExplicitOffMeshLinks(const std::vector<NavOffMeshLink>& sourceLinks,
                                                          dtNavMeshQuery& query,
                                                          const NavMeshBuildSettings& settings,
                                                          const std::function<bool(const NavOffMeshLink&)>& validator,
                                                          int& outSkippedCount,
                                                          int& outRejectedCount) {
  std::vector<NavOffMeshLink> links;
  links.reserve(sourceLinks.size());
  outSkippedCount = 0;

  const XVECTOR3 extents(
      (std::max)(settings.queryExtents.x, settings.agentRadius + settings.dropLinkRadius),
      (std::max)(settings.queryExtents.y, settings.dropLinkMaxHeight),
      (std::max)(settings.queryExtents.z, settings.agentRadius + settings.dropLinkRadius),
      0.0f);
  const float duplicateThresholdSq = (std::max)(0.10f, settings.agentRadius * 0.5f);
  const float duplicateThresholdSqFinal = duplicateThresholdSq * duplicateThresholdSq;

  for (const NavOffMeshLink& sourceLink : sourceLinks) {
    if (!IsFiniteVec3(sourceLink.start) ||
        !IsFiniteVec3(sourceLink.end) ||
        sourceLink.radius <= 0.0f ||
        sourceLink.type == NavTraversalType::Walk) {
      ++outSkippedCount;
      continue;
    }

    XVECTOR3 projectedStart;
    XVECTOR3 projectedEnd;
    dtPolyRef startRef = 0;
    dtPolyRef endRef = 0;
    if (!ProjectLinkEndpoint(query, sourceLink.start, extents, projectedStart, startRef) ||
        !ProjectLinkEndpoint(query, sourceLink.end, extents, projectedEnd, endRef) ||
        startRef == endRef) {
      ++outSkippedCount;
      continue;
    }

    if (IsDuplicateOffMeshLink(links, projectedStart, projectedEnd, sourceLink.type, duplicateThresholdSqFinal)) {
      continue;
    }

    NavOffMeshLink link = sourceLink;
    link.start = projectedStart;
    link.end = projectedEnd;
    link.radius = (std::max)(0.05f, sourceLink.radius);
    link.userId = EncodeOffMeshUserId(link.type, static_cast<uint32_t>(links.size()));
    links.push_back(link);
  }
  return links;
}

int GenerateAutoDropLinks(const rcPolyMesh& polyMesh,
                          dtNavMeshQuery& query,
                          const NavMeshBuildSettings& settings,
                          const std::function<bool(const NavOffMeshLink&)>& validator,
                          const std::function<bool(const NavOffMeshLink&)>& hybridValidator,
                          int& rejectedByValidatorCount,
                          int& generatedJumpLinks,
                          std::vector<NavOffMeshLink>& links) {
  generatedJumpLinks = 0;
  const bool canGenerateDrops =
      settings.enableAutoDropLinks &&
      settings.dropLinkMaxHeight > settings.dropLinkMinHeight &&
      settings.dropLinkMaxHorizontalDistance > 0.0f &&
      settings.dropLinkRadius > 0.0f;
  const bool canGenerateJumps =
      settings.enableAutoJumpLinks &&
      settings.jumpLinkMaxHorizontalDistance > 0.0f &&
      settings.jumpLinkRadius > 0.0f;
  if (!canGenerateDrops && !canGenerateJumps) {
    return 0;
  }

  const int existingCount = static_cast<int>(links.size());
  const float sampleSpacing = (std::max)(0.25f, settings.dropLinkSampleSpacing);
  const float maxHorizontal = (std::max)(0.25f, settings.dropLinkMaxHorizontalDistance);
  const float jumpSampleSpacing = (std::max)(0.25f, settings.jumpLinkSampleSpacing);
  const float maxJumpHorizontal = (std::max)(0.25f, settings.jumpLinkMaxHorizontalDistance);
  const float maxCandidateHorizontal = canGenerateJumps
      ? (std::max)(maxHorizontal, maxJumpHorizontal)
      : maxHorizontal;
  const float minDropHeight = (std::max)(settings.agentMaxClimb + 0.05f, settings.dropLinkMinHeight);
  const float maxDropHeight = (std::max)(minDropHeight + 0.05f, settings.dropLinkMaxHeight);
  const float duplicateThreshold = (std::max)(0.25f, settings.dropLinkRadius * 0.75f);
  const float duplicateThresholdSq = duplicateThreshold * duplicateThreshold;
  const int maxHybridJumpLinks = (std::max)(0, settings.hybridJumpMaxLinks);
  int generatedHybridJumpLinks = 0;
  const XVECTOR3 startExtents(
      (std::max)(0.25f, settings.agentRadius + settings.dropLinkRadius),
      (std::max)(settings.agentHeight, settings.queryExtents.y),
      (std::max)(0.25f, settings.agentRadius + settings.dropLinkRadius),
      0.0f);
  const XVECTOR3 endExtents(
      (std::max)(0.35f, settings.agentRadius + settings.dropLinkRadius),
      (std::max)(maxDropHeight + settings.agentHeight, settings.queryExtents.y),
      (std::max)(0.35f, settings.agentRadius + settings.dropLinkRadius),
      0.0f);
  const XVECTOR3 jumpEndExtents(
      (std::max)(0.35f, settings.agentRadius + settings.jumpLinkRadius),
      (std::max)(maxDropHeight + settings.agentHeight, settings.queryExtents.y),
      (std::max)(0.35f, settings.agentRadius + settings.jumpLinkRadius),
      0.0f);

  const std::array<float, 5> horizontalFractions = { 0.30f, 0.45f, 0.60f, 0.80f, 1.0f };
  constexpr int kMaxAutoDropLinks = 4096;
  constexpr int kMaxAutoJumpLinks = 4096;

  for (int polyIndex = 0; polyIndex < polyMesh.npolys; ++polyIndex) {
    const unsigned short* poly = &polyMesh.polys[static_cast<std::size_t>(polyIndex) * polyMesh.nvp * 2u];
    const unsigned short* neis = poly + polyMesh.nvp;
    const XVECTOR3 polyCenter = PolyMeshPolyCenter(polyMesh, polyIndex);
    int vertexCount = 0;
    while (vertexCount < polyMesh.nvp && poly[vertexCount] != RC_MESH_NULL_IDX) {
      ++vertexCount;
    }
    if (vertexCount < 3) {
      continue;
    }

    for (int edgeIndex = 0; edgeIndex < vertexCount; ++edgeIndex) {
      const unsigned short v0Index = poly[edgeIndex];
      const unsigned short v1Index = poly[(edgeIndex + 1) % vertexCount];
      if (v0Index == RC_MESH_NULL_IDX ||
          v1Index == RC_MESH_NULL_IDX ||
          neis[edgeIndex] != RC_MESH_NULL_IDX) {
        continue;
      }

      const XVECTOR3 v0 = PolyMeshVertexWorld(polyMesh, v0Index);
      const XVECTOR3 v1 = PolyMeshVertexWorld(polyMesh, v1Index);
      XVECTOR3 edge = v1 - v0;
      edge.y = 0.0f;
      edge.w = 0.0f;
      const float edgeLength = edge.Length();
      if (edgeLength <= 0.05f) {
        continue;
      }

      XVECTOR3 outward(edge.z, 0.0f, -edge.x, 0.0f);
      outward = NormalizeHorizontalOr(outward, XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
      const XVECTOR3 edgeMid = (v0 + v1) * 0.5f;
      XVECTOR3 centerToEdge = edgeMid - polyCenter;
      centerToEdge.y = 0.0f;
      centerToEdge.w = 0.0f;
      if (outward.x * centerToEdge.x + outward.z * centerToEdge.z < 0.0f) {
        outward *= -1.0f;
      }

      const int sampleCount = (std::max)(1, static_cast<int>(std::ceil(edgeLength / (std::min)(sampleSpacing, jumpSampleSpacing))));
      for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        const float t = (static_cast<float>(sampleIndex) + 0.5f) / static_cast<float>(sampleCount);
        XVECTOR3 edgePoint = v0 + (v1 - v0) * t;
        edgePoint.w = 1.0f;

        XVECTOR3 startProbe = edgePoint - outward * (std::max)(0.05f, settings.agentRadius * 0.25f);
        startProbe.w = 1.0f;
        XVECTOR3 projectedStart;
        dtPolyRef startRef = 0;
        if (!ProjectLinkEndpoint(query, startProbe, startExtents, projectedStart, startRef)) {
          continue;
        }

        for (float fraction : horizontalFractions) {
          const float horizontal = maxCandidateHorizontal * fraction;
          XVECTOR3 endProbe = edgePoint + outward * horizontal;
          endProbe.y = edgePoint.y - (minDropHeight + maxDropHeight) * 0.5f;
          endProbe.w = 1.0f;

          XVECTOR3 projectedEnd;
          dtPolyRef endRef = 0;
          if (!ProjectLinkEndpoint(query, endProbe, canGenerateJumps ? jumpEndExtents : endExtents, projectedEnd, endRef) ||
              !endRef ||
              endRef == startRef) {
            continue;
          }

          bool acceptedAnyLink = false;
          const float dropHeight = projectedStart.y - projectedEnd.y;
          if (canGenerateDrops &&
              dropHeight >= minDropHeight &&
              dropHeight <= maxDropHeight &&
              HorizontalDistanceSq3(projectedStart, projectedEnd) <= maxHorizontal * maxHorizontal * 1.25f &&
              !IsDuplicateOffMeshLink(links, projectedStart, projectedEnd, NavTraversalType::Drop, duplicateThresholdSq)) {
            NavOffMeshLink link;
            link.start = projectedStart;
            link.end = projectedEnd;
            link.radius = (std::max)(0.05f, settings.dropLinkRadius);
            link.bidirectional = false;
            link.type = NavTraversalType::Drop;
            link.userId = EncodeOffMeshUserId(link.type, static_cast<uint32_t>(links.size()));
            if (PassesOffMeshLinkValidator(link, validator, rejectedByValidatorCount)) {
              links.push_back(link);
              acceptedAnyLink = true;
            }
          }

          const float jumpHorizontalSq = HorizontalDistanceSq3(projectedStart, projectedEnd);
          if (canGenerateJumps &&
              jumpHorizontalSq > settings.agentRadius * settings.agentRadius &&
              jumpHorizontalSq <= maxJumpHorizontal * maxJumpHorizontal * 1.25f &&
              !IsDuplicateOffMeshLink(links, projectedStart, projectedEnd, NavTraversalType::Jump, duplicateThresholdSq)) {
            NavOffMeshLink link;
            link.start = projectedStart;
            link.end = projectedEnd;
            link.radius = (std::max)(0.05f, settings.jumpLinkRadius);
            link.bidirectional = false;
            link.type = NavTraversalType::Jump;
            link.userId = EncodeOffMeshUserId(link.type, static_cast<uint32_t>(links.size()));
            if (PassesOffMeshLinkValidator(link, validator, rejectedByValidatorCount)) {
              links.push_back(link);
              ++generatedJumpLinks;
              acceptedAnyLink = true;
            } else if (settings.enableHybridJumpLinks &&
                       generatedHybridJumpLinks < maxHybridJumpLinks &&
                       !IsDuplicateOffMeshLink(links, projectedStart, projectedEnd, NavTraversalType::JumpIntent, duplicateThresholdSq)) {
              link.type = NavTraversalType::JumpIntent;
              link.userId = EncodeOffMeshUserId(link.type, static_cast<uint32_t>(links.size()));
              if (PassesOffMeshHybridLinkValidator(link, hybridValidator, rejectedByValidatorCount)) {
                links.push_back(link);
                ++generatedJumpLinks;
                ++generatedHybridJumpLinks;
                acceptedAnyLink = true;
              }
            }
          }

          if (acceptedAnyLink) {
            break;
          }
        }

        const int generatedLinks = static_cast<int>(links.size()) - existingCount;
        if (generatedLinks >= kMaxAutoDropLinks + kMaxAutoJumpLinks) {
          return static_cast<int>(links.size()) - existingCount;
        }
      }
    }
  }

  return static_cast<int>(links.size()) - existingCount;
}

template <typename T>
bool ReadPod(std::ifstream& file, T& value) {
  file.read(reinterpret_cast<char*>(&value), sizeof(T));
  return file.good();
}

template <typename T>
void WritePod(std::ofstream& file, const T& value) {
  file.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

bool InitializeDetourMeshFromData(unsigned char* navData,
                                  int navDataSize,
                                  const NavMeshBuildSettings& settings,
                                  const NavMeshBuildStats& stats,
                                  std::unique_ptr<dtNavMesh, NavMeshDeleter>& outNavMesh,
                                  std::unique_ptr<dtNavMeshQuery, NavMeshQueryDeleter>& outQuery,
                                  std::string* error) {
  outNavMesh.reset();
  outQuery.reset();
  if (!navData || navDataSize <= 0) {
    SetError(error, "Cached Detour navmesh data is empty");
    return false;
  }

  std::unique_ptr<dtNavMesh, NavMeshDeleter> navMesh(dtAllocNavMesh());
  if (!navMesh) {
    dtFree(navData);
    SetError(error, "Failed to allocate Detour navmesh");
    return false;
  }

  dtStatus status = navMesh->init(navData, navDataSize, DT_TILE_FREE_DATA);
  if (dtStatusFailed(status)) {
    dtFree(navData);
    SetError(error, "Failed to initialize Detour navmesh");
    return false;
  }

  std::unique_ptr<dtNavMeshQuery, NavMeshQueryDeleter> query(dtAllocNavMeshQuery());
  if (!query) {
    SetError(error, "Failed to allocate Detour navmesh query");
    return false;
  }

  status = query->init(navMesh.get(), 2048);
  if (dtStatusFailed(status)) {
    SetError(error, "Failed to initialize Detour navmesh query");
    return false;
  }

  (void)settings;
  (void)stats;
  outNavMesh = std::move(navMesh);
  outQuery = std::move(query);
  return true;
}

bool LoadNavMeshCache(const std::filesystem::path& path,
                      uint64_t expectedKey,
                      const NavMeshBuildSettings& settings,
                      NavMeshBuildStats& outStats,
                      std::unique_ptr<dtNavMesh, NavMeshDeleter>& outNavMesh,
                      std::unique_ptr<dtNavMeshQuery, NavMeshQueryDeleter>& outQuery) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }

  std::array<char, 8> magic = {};
  file.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  uint32_t version = 0;
  uint64_t key = 0;
  uint32_t dataSize = 0;
  NavMeshBuildStats stats;
  if (!file.good() ||
      magic != kNavMeshCacheMagic ||
      !ReadPod(file, version) ||
      !ReadPod(file, key) ||
      !ReadPod(file, stats.vertexCount) ||
      !ReadPod(file, stats.triangleCount) ||
      !ReadPod(file, stats.width) ||
      !ReadPod(file, stats.height) ||
      !ReadPod(file, stats.polygonCount) ||
      !ReadPod(file, stats.detailTriangleCount) ||
      !ReadPod(file, stats.offMeshLinkCount) ||
      !ReadPod(file, stats.dropLinkCount) ||
      !ReadPod(file, stats.jumpLinkCount) ||
      !ReadPod(file, stats.jumpPadLinkCount) ||
      !ReadPod(file, dataSize)) {
    return false;
  }
  if (version != kNavMeshCacheVersion || key != expectedKey || dataSize == 0) {
    return false;
  }

  unsigned char* navData = static_cast<unsigned char*>(dtAlloc(dataSize, DT_ALLOC_PERM));
  if (!navData) {
    return false;
  }
  file.read(reinterpret_cast<char*>(navData), dataSize);
  if (!file.good()) {
    dtFree(navData);
    return false;
  }

  std::string error;
  if (!InitializeDetourMeshFromData(navData, static_cast<int>(dataSize), settings, stats,
                                    outNavMesh, outQuery, &error)) {
    T8_LOG_INFO("[Navigation] Ignoring invalid navmesh cache '%s': %s",
                path.string().c_str(), error.c_str());
    return false;
  }

  outStats = stats;
  T8_LOG_INFO("[Navigation] Loaded navmesh cache '%s': verts=%d tris=%d grid=%dx%d polys=%d detailTris=%d offMesh=%d drop=%d jump=%d jumpPad=%d",
              path.string().c_str(),
              outStats.vertexCount,
              outStats.triangleCount,
              outStats.width,
              outStats.height,
              outStats.polygonCount,
              outStats.detailTriangleCount,
              outStats.offMeshLinkCount,
              outStats.dropLinkCount,
              outStats.jumpLinkCount,
              outStats.jumpPadLinkCount);
  return true;
}

void SaveNavMeshCache(const std::filesystem::path& path,
                      uint64_t key,
                      const NavMeshBuildStats& stats,
                      const unsigned char* navData,
                      int navDataSize) {
  if (!navData || navDataSize <= 0) {
    return;
  }

  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    T8_LOG_INFO("[Navigation] Cannot create navmesh cache dir '%s': %s",
                path.parent_path().string().c_str(), ec.message().c_str());
    return;
  }

  std::filesystem::path tmpPath = path;
  tmpPath += ".tmp";
  std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    T8_LOG_INFO("[Navigation] Cannot write navmesh cache '%s'", tmpPath.string().c_str());
    return;
  }

  file.write(kNavMeshCacheMagic.data(), static_cast<std::streamsize>(kNavMeshCacheMagic.size()));
  WritePod(file, kNavMeshCacheVersion);
  WritePod(file, key);
  WritePod(file, stats.vertexCount);
  WritePod(file, stats.triangleCount);
  WritePod(file, stats.width);
  WritePod(file, stats.height);
  WritePod(file, stats.polygonCount);
  WritePod(file, stats.detailTriangleCount);
  WritePod(file, stats.offMeshLinkCount);
  WritePod(file, stats.dropLinkCount);
  WritePod(file, stats.jumpLinkCount);
  WritePod(file, stats.jumpPadLinkCount);
  const uint32_t dataSize = static_cast<uint32_t>(navDataSize);
  WritePod(file, dataSize);
  file.write(reinterpret_cast<const char*>(navData), navDataSize);
  file.close();
  if (!file.good()) {
    std::filesystem::remove(tmpPath, ec);
    T8_LOG_INFO("[Navigation] Failed while writing navmesh cache '%s'", tmpPath.string().c_str());
    return;
  }

  std::filesystem::remove(path, ec);
  ec.clear();
  std::filesystem::rename(tmpPath, path, ec);
  if (ec) {
    std::filesystem::remove(tmpPath, ec);
    T8_LOG_INFO("[Navigation] Cannot finalize navmesh cache '%s': %s",
                path.string().c_str(), ec.message().c_str());
    return;
  }

  T8_LOG_INFO("[Navigation] Wrote navmesh cache '%s' (%d bytes)", path.string().c_str(), navDataSize);
}

NavPathResult FindPathWithQuery(dtNavMeshQuery* query,
                                const NavMeshBuildSettings& settings,
                                const NavPathRequest& request) {
  T8_TELEMETRY_SCOPE("navigation.detour.find_path");
  if (RuntimeTelemetry::IsFrameActive()) {
    RuntimeTelemetry::AddCounter("navigation.detour.find_path.count", 1.0);
  }
  NavPathResult result;
  if (!query) {
    RuntimeTelemetry::AddCounter("navigation.detour.find_path.fail", 1.0);
    result.error = "Navigation query is not available";
    return result;
  }

  const float startPos[3] = { request.start.x, request.start.y, request.start.z };
  const float endPos[3] = { request.end.x, request.end.y, request.end.z };
  const float extents[3] = {
    request.queryExtents.x > 0.0f ? request.queryExtents.x : settings.queryExtents.x,
    request.queryExtents.y > 0.0f ? request.queryExtents.y : settings.queryExtents.y,
    request.queryExtents.z > 0.0f ? request.queryExtents.z : settings.queryExtents.z
  };

  dtQueryFilter filter;
  ConfigureTraversalFilter(filter);

  dtPolyRef startRef = 0;
  dtPolyRef endRef = 0;
  float nearestStart[3] = {};
  float nearestEnd[3] = {};
  dtStatus status = query->findNearestPoly(startPos, extents, &filter, &startRef, nearestStart);
  if (dtStatusFailed(status) || !startRef) {
    RuntimeTelemetry::AddCounter("navigation.detour.find_path.fail", 1.0);
    result.error = "Failed to find nearest start nav polygon";
    return result;
  }
  status = query->findNearestPoly(endPos, extents, &filter, &endRef, nearestEnd);
  if (dtStatusFailed(status) || !endRef) {
    RuntimeTelemetry::AddCounter("navigation.detour.find_path.fail", 1.0);
    result.error = "Failed to find nearest end nav polygon";
    return result;
  }

  dtPolyRef pathPolys[kMaxPathPolys] = {};
  int pathPolyCount = 0;
  status = query->findPath(startRef, endRef, nearestStart, nearestEnd,
                           &filter, pathPolys, &pathPolyCount, kMaxPathPolys);
  if (dtStatusFailed(status) || pathPolyCount <= 0) {
    RuntimeTelemetry::AddCounter("navigation.detour.find_path.fail", 1.0);
    result.error = "Detour failed to find a path";
    return result;
  }

  float straightPath[kMaxStraightPath * 3] = {};
  unsigned char straightFlags[kMaxStraightPath] = {};
  dtPolyRef straightPolys[kMaxStraightPath] = {};
  int straightPathCount = 0;
  status = query->findStraightPath(nearestStart, nearestEnd,
                                   pathPolys, pathPolyCount,
                                   straightPath, straightFlags, straightPolys,
                                   &straightPathCount, kMaxStraightPath);
  if (dtStatusFailed(status) || straightPathCount <= 0) {
    RuntimeTelemetry::AddCounter("navigation.detour.find_path.fail", 1.0);
    result.error = "Detour failed to straighten path";
    return result;
  }

  result.points.reserve(static_cast<std::size_t>(straightPathCount));
  for (int i = 0; i < straightPathCount; ++i) {
    const float* p = &straightPath[i * 3];
    result.points.emplace_back(p[0], p[1], p[2], 1.0f);
  }
  int specialSegmentCount = 0;
  if (straightPathCount > 1) {
    result.segments.reserve(static_cast<std::size_t>(straightPathCount - 1));
    const dtNavMesh* navMesh = query->getAttachedNavMesh();
    for (int i = 0; i + 1 < straightPathCount; ++i) {
      NavPathResult::Segment segment;
      segment.startPointIndex = i;
      segment.endPointIndex = i + 1;
      segment.type = NavTraversalType::Walk;
      if ((straightFlags[i] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) != 0 && navMesh) {
        const dtOffMeshConnection* connection = navMesh->getOffMeshConnectionByRef(straightPolys[i]);
        if (!connection && i + 1 < straightPathCount) {
          connection = navMesh->getOffMeshConnectionByRef(straightPolys[i + 1]);
        }
        if (connection) {
          segment.type = DecodeOffMeshUserId(connection->userId);
        }
      }
      if (segment.type != NavTraversalType::Walk) {
        ++specialSegmentCount;
      }
      result.segments.push_back(segment);
    }
  }
  if (RuntimeTelemetry::IsFrameActive()) {
    RuntimeTelemetry::AddCounter("navigation.detour.find_path.success", 1.0);
    RuntimeTelemetry::AddCounter("navigation.detour.find_path.path_polys", static_cast<double>(pathPolyCount));
    RuntimeTelemetry::AddCounter("navigation.detour.find_path.points", static_cast<double>(straightPathCount));
    RuntimeTelemetry::AddCounter("navigation.detour.find_path.special_segments", static_cast<double>(specialSegmentCount));
  }
  result.success = true;
  return result;
}
#endif

} // namespace

NavigationBackendInfo GetNavigationBackendInfo() {
  NavigationBackendInfo info;
#if defined(T850_ENABLE_RECAST)
  info.recastAvailable = true;
  info.detourAvailable = true;
  info.detourCrowdAvailable = true;
  info.detourTileCacheAvailable = true;
  info.recastVersion = RECASTNAV_VERSION;
#else
  info.recastVersion = "unavailable";
#endif
  return info;
}

bool ValidateNavigationBackend() {
#if defined(T850_ENABLE_RECAST)
  rcHeightfield* heightfield = rcAllocHeightfield();
  dtNavMesh* navMesh = dtAllocNavMesh();
  dtCrowd* crowd = dtAllocCrowd();
  dtTileCache* tileCache = dtAllocTileCache();

  const bool ok = heightfield && navMesh && crowd && tileCache;

  if (tileCache) dtFreeTileCache(tileCache);
  if (crowd) dtFreeCrowd(crowd);
  if (navMesh) dtFreeNavMesh(navMesh);
  if (heightfield) rcFreeHeightField(heightfield);

  return ok;
#else
  return false;
#endif
}

struct NavMesh::Impl {
#if defined(T850_ENABLE_RECAST)
  std::unique_ptr<dtNavMesh, NavMeshDeleter> navMesh;
  std::unique_ptr<dtNavMeshQuery, NavMeshQueryDeleter> query;
  NavMeshBuildSettings settings;
#endif
};

NavMesh::NavMesh()
  : m_impl(std::make_unique<Impl>()) {
}

NavMesh::~NavMesh() = default;
NavMesh::NavMesh(NavMesh&&) noexcept = default;
NavMesh& NavMesh::operator=(NavMesh&&) noexcept = default;

void NavMesh::Clear() {
  m_impl = std::make_unique<Impl>();
  m_stats = NavMeshBuildStats{};
}

bool NavMesh::IsReady() const {
#if defined(T850_ENABLE_RECAST)
  return m_impl && m_impl->navMesh && m_impl->query;
#else
  return false;
#endif
}

bool BuildGeometryFromXDataBase(const xF::XDataBase& database, NavMeshGeometry& outGeometry, std::string* error) {
  outGeometry.vertices.clear();
  outGeometry.indices.clear();

  XMATRIX44 identity;
  identity.Identity();
  return AppendGeometryFromXDataBase(database, identity, outGeometry, error);
}

bool AppendGeometryFromXDataBase(const xF::XDataBase& database,
                                 const XMATRIX44& worldTransform,
                                 NavMeshGeometry& outGeometry,
                                 std::string* error) {
  if (database.XMeshDataBase.empty() || !database.XMeshDataBase[0]) {
    SetError(error, "XDataBase has no mesh container");
    return false;
  }

  const xF::xMeshContainer* meshContainer = database.XMeshDataBase[0];
  const std::size_t geometryCount = (std::min)(database.MeshInfo.size(), meshContainer->Geometry.size());
  if (geometryCount == 0) {
    SetError(error, "XDataBase has no geometry");
    return false;
  }

  for (std::size_t gi = 0; gi < geometryCount; ++gi) {
    const xF::xFinalGeometry& finalGeometry = database.MeshInfo[gi];
    const xF::xMeshGeometry& sourceGeometry = meshContainer->Geometry[gi];
    const unsigned int stride = finalGeometry.VertexSize / sizeof(float);
    if (!finalGeometry.pData || stride < 3 || finalGeometry.NumVertex == 0) {
      continue;
    }

    const int baseVertex = static_cast<int>(outGeometry.vertices.size());
    outGeometry.vertices.reserve(outGeometry.vertices.size() + finalGeometry.NumVertex);
    for (unsigned int v = 0; v < finalGeometry.NumVertex; ++v) {
      const float* vertex = &finalGeometry.pData[v * stride];
      XVECTOR3 pos(vertex[0], vertex[1], vertex[2], 1.0f);
      if (IsFiniteVec3(pos)) {
        outGeometry.vertices.push_back(TransformPoint(pos, worldTransform));
      } else {
        outGeometry.vertices.push_back(TransformPoint(XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f), worldTransform));
      }
    }

    const std::size_t triangleIndexCount = sourceGeometry.Indices32Bit
      ? sourceGeometry.Triangles32.size()
      : sourceGeometry.Triangles.size();
    outGeometry.indices.reserve(outGeometry.indices.size() + triangleIndexCount);
    if (sourceGeometry.Indices32Bit) {
      for (std::size_t i = 0; i + 2 < sourceGeometry.Triangles32.size(); i += 3) {
        const xF::xDWORD i0 = sourceGeometry.Triangles32[i + 0];
        const xF::xDWORD i1 = sourceGeometry.Triangles32[i + 1];
        const xF::xDWORD i2 = sourceGeometry.Triangles32[i + 2];
        if (i0 >= finalGeometry.NumVertex || i1 >= finalGeometry.NumVertex || i2 >= finalGeometry.NumVertex) continue;
        outGeometry.indices.push_back(baseVertex + static_cast<int>(i0));
        outGeometry.indices.push_back(baseVertex + static_cast<int>(i1));
        outGeometry.indices.push_back(baseVertex + static_cast<int>(i2));
      }
    } else {
      for (std::size_t i = 0; i + 2 < sourceGeometry.Triangles.size(); i += 3) {
        const xF::xWORD i0 = sourceGeometry.Triangles[i + 0];
        const xF::xWORD i1 = sourceGeometry.Triangles[i + 1];
        const xF::xWORD i2 = sourceGeometry.Triangles[i + 2];
        if (i0 >= finalGeometry.NumVertex || i1 >= finalGeometry.NumVertex || i2 >= finalGeometry.NumVertex) continue;
        outGeometry.indices.push_back(baseVertex + static_cast<int>(i0));
        outGeometry.indices.push_back(baseVertex + static_cast<int>(i1));
        outGeometry.indices.push_back(baseVertex + static_cast<int>(i2));
      }
    }
  }

  if (outGeometry.vertices.empty() || outGeometry.indices.size() < 3) {
    SetError(error, "Extracted navigation geometry is empty");
    return false;
  }
  return !outGeometry.indices.empty();
}

bool BuildGeometryFromPrimitiveInstances(const PrimitiveInst* instances,
                                         int instanceCount,
                                         NavMeshGeometry& outGeometry,
                                         NavSourceBuildStats* stats,
                                         std::string* error) {
  outGeometry.vertices.clear();
  outGeometry.indices.clear();
  if (stats) {
    *stats = NavSourceBuildStats{};
  }

  if (!instances || instanceCount <= 0) {
    SetError(error, "No primitive instances were provided for navigation geometry");
    return false;
  }

  for (int instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex) {
    const PrimitiveInst& instance = instances[instanceIndex];
    if (stats) ++stats->considered;

    if (!instance.Visible || !instance.pBase) {
      if (stats) ++stats->skippedInvisible;
      continue;
    }

    if (instance.GetSkinnedMesh()) {
      if (stats) ++stats->skippedSkinned;
      continue;
    }

    const RenderMesh* mesh = dynamic_cast<const RenderMesh*>(instance.pBase);
    if (!mesh || !mesh->xFile) {
      if (stats) ++stats->skippedInvalid;
      continue;
    }

    const std::size_t indicesBefore = outGeometry.indices.size();
    std::string sourceError;
    if (!AppendGeometryFromXDataBase(*mesh->xFile, instance.Final, outGeometry, &sourceError) ||
        outGeometry.indices.size() == indicesBefore) {
      if (stats) ++stats->skippedInvalid;
      continue;
    }

    if (stats) ++stats->included;
  }

  if (outGeometry.vertices.empty() || outGeometry.indices.size() < 3) {
    SetError(error, "Primitive instances produced no navigation geometry");
    return false;
  }

  if (stats) {
    stats->vertexCount = static_cast<int>(outGeometry.vertices.size());
    stats->triangleCount = static_cast<int>(outGeometry.indices.size() / 3);
  }
  return true;
}

bool BuildGeometryFromNavSources(const std::vector<NavSourceInstance>& sources,
                                 NavMeshGeometry& outGeometry,
                                 NavSourceBuildStats* stats,
                                 std::string* error) {
  outGeometry.vertices.clear();
  outGeometry.indices.clear();
  if (stats) {
    *stats = NavSourceBuildStats{};
  }

  if (sources.empty()) {
    SetError(error, "No navigation sources were provided");
    return false;
  }

  for (const NavSourceInstance& source : sources) {
    if (stats) ++stats->considered;
    if (!source.includeInNavigation || !source.visible || !source.navigationStatic) {
      if (stats) ++stats->skippedInvisible;
      continue;
    }

    const xF::XDataBase* database = source.database;
    XMATRIX44 worldTransform = source.worldTransform;
    if (source.instance) {
      if (!source.instance->Visible || !source.instance->pBase) {
        if (stats) ++stats->skippedInvisible;
        continue;
      }
      if (source.instance->GetSkinnedMesh()) {
        if (stats) ++stats->skippedSkinned;
        continue;
      }
      const RenderMesh* mesh = dynamic_cast<const RenderMesh*>(source.instance->pBase);
      if (!mesh || !mesh->xFile) {
        if (stats) ++stats->skippedInvalid;
        continue;
      }
      database = mesh->xFile;
      worldTransform = source.instance->Final;
    }

    if (!database) {
      if (stats) ++stats->skippedInvalid;
      continue;
    }

    const std::size_t indicesBefore = outGeometry.indices.size();
    std::string sourceError;
    if (!AppendGeometryFromXDataBase(*database, worldTransform, outGeometry, &sourceError) ||
        outGeometry.indices.size() == indicesBefore) {
      if (stats) ++stats->skippedInvalid;
      continue;
    }
    if (stats) ++stats->included;
  }

  if (outGeometry.vertices.empty() || outGeometry.indices.size() < 3) {
    SetError(error, "Navigation sources produced no navigation geometry");
    return false;
  }

  if (stats) {
    stats->vertexCount = static_cast<int>(outGeometry.vertices.size());
    stats->triangleCount = static_cast<int>(outGeometry.indices.size() / 3);
  }
  return true;
}

bool NavMesh::BuildFromXDataBase(const xF::XDataBase& database,
                                 const NavMeshBuildSettings& settings,
                                 std::string* error) {
  NavMeshGeometry geometry;
  if (!BuildGeometryFromXDataBase(database, geometry, error)) {
    return false;
  }
  return Build(geometry, settings, error);
}

bool NavMesh::Build(const NavMeshGeometry& geometry,
                    const NavMeshBuildSettings& settings,
                    std::string* error) {
  return BuildCached(geometry, settings, 0, error);
}

bool NavMesh::LoadCached(uint64_t cacheKey,
                         const NavMeshBuildSettings& settings,
                         std::string* error) {
#if !defined(T850_ENABLE_RECAST)
  SetError(error, "RecastNavigation is not enabled in this build");
  return false;
#else
  Clear();
  m_impl->settings = settings;
  if (cacheKey == 0) {
    if (error) *error = "Navigation cache key is zero";
    return false;
  }

  NavMeshBuildStats cachedStats;
  std::unique_ptr<dtNavMesh, NavMeshDeleter> cachedNavMesh;
  std::unique_ptr<dtNavMeshQuery, NavMeshQueryDeleter> cachedQuery;
  if (!LoadNavMeshCache(NavMeshCachePath(cacheKey), cacheKey, settings, cachedStats, cachedNavMesh, cachedQuery)) {
    if (error) *error = "Navigation cache is not available";
    return false;
  }

  m_stats = cachedStats;
  m_impl->navMesh = std::move(cachedNavMesh);
  m_impl->query = std::move(cachedQuery);
  return true;
#endif
}

bool NavMesh::BuildCached(const NavMeshGeometry& geometry,
                          const NavMeshBuildSettings& settings,
                          uint64_t cacheKey,
                          std::string* error) {
#if !defined(T850_ENABLE_RECAST)
  SetError(error, "RecastNavigation is not enabled in this build");
  return false;
#else
  Clear();
  m_impl->settings = settings;

  if (geometry.vertices.size() < 3 || geometry.indices.size() < 3) {
    SetError(error, "Navigation geometry must contain vertices and triangle indices");
    return false;
  }
  if ((geometry.indices.size() % 3) != 0) {
    SetError(error, "Navigation geometry indices must be a triangle list");
    return false;
  }
  if (settings.cellSize <= 0.0f || settings.cellHeight <= 0.0f ||
      settings.agentHeight <= 0.0f || settings.agentRadius < 0.0f ||
      settings.vertsPerPoly < 3 ||
      settings.dropLinkRadius <= 0.0f ||
      settings.dropLinkSampleSpacing <= 0.0f ||
      settings.dropLinkMaxHorizontalDistance < 0.0f) {
    SetError(error, "Invalid navigation build settings");
    return false;
  }

  std::vector<float> verts;
  verts.reserve(geometry.vertices.size() * 3);
  for (const XVECTOR3& v : geometry.vertices) {
    if (!IsFiniteVec3(v)) {
      SetError(error, "Navigation geometry contains non-finite vertex");
      return false;
    }
    verts.push_back(v.x);
    verts.push_back(v.y);
    verts.push_back(v.z);
  }

  std::vector<int> tris = geometry.indices;
  for (int idx : tris) {
    if (idx < 0 || idx >= static_cast<int>(geometry.vertices.size())) {
      SetError(error, "Navigation geometry contains out-of-range index");
      return false;
    }
  }

  const uint64_t effectiveCacheKey = cacheKey != 0 ? cacheKey : ComputeNavMeshCacheKey(geometry, settings);
  const std::filesystem::path cachePath = NavMeshCachePath(effectiveCacheKey);
  NavMeshBuildStats cachedStats;
  std::unique_ptr<dtNavMesh, NavMeshDeleter> cachedNavMesh;
  std::unique_ptr<dtNavMeshQuery, NavMeshQueryDeleter> cachedQuery;
  const bool hasExplicitOffMeshLinks = !geometry.offMeshLinks.empty();
  if (!hasExplicitOffMeshLinks &&
      LoadNavMeshCache(cachePath, effectiveCacheKey, settings, cachedStats, cachedNavMesh, cachedQuery)) {
    m_stats = cachedStats;
    m_impl->navMesh = std::move(cachedNavMesh);
    m_impl->query = std::move(cachedQuery);
    return true;
  }

  rcContext context;
  rcConfig config;
  std::memset(&config, 0, sizeof(config));

  config.cs = settings.cellSize;
  config.ch = settings.cellHeight;
  config.walkableSlopeAngle = settings.agentMaxSlope;
  config.walkableHeight = static_cast<int>(std::ceil(settings.agentHeight / config.ch));
  config.walkableClimb = static_cast<int>(std::floor(settings.agentMaxClimb / config.ch));
  config.walkableRadius = static_cast<int>(std::ceil(settings.agentRadius / config.cs));
  config.maxEdgeLen = static_cast<int>(settings.edgeMaxLen / settings.cellSize);
  config.maxSimplificationError = settings.edgeMaxError;
  config.minRegionArea = static_cast<int>(settings.regionMinSize * settings.regionMinSize);
  config.mergeRegionArea = static_cast<int>(settings.regionMergeSize * settings.regionMergeSize);
  config.maxVertsPerPoly = settings.vertsPerPoly;
  config.detailSampleDist = settings.detailSampleDist < 0.9f ? 0.0f : settings.cellSize * settings.detailSampleDist;
  config.detailSampleMaxError = settings.cellHeight * settings.detailSampleMaxError;

  rcCalcBounds(verts.data(), static_cast<int>(geometry.vertices.size()), config.bmin, config.bmax);
  rcCalcGridSize(config.bmin, config.bmax, config.cs, &config.width, &config.height);
  if (config.width <= 0 || config.height <= 0) {
    SetError(error, "Navigation geometry bounds produced an empty Recast grid");
    return false;
  }

  HeightfieldPtr heightfield(rcAllocHeightfield());
  if (!heightfield || !rcCreateHeightfield(&context, *heightfield, config.width, config.height,
                                           config.bmin, config.bmax, config.cs, config.ch)) {
    SetError(error, "Failed to create Recast heightfield");
    return false;
  }

  const int triangleCount = static_cast<int>(tris.size() / 3);
  std::vector<unsigned char> areas(static_cast<std::size_t>(triangleCount), RC_NULL_AREA);
  rcMarkWalkableTriangles(&context, config.walkableSlopeAngle, verts.data(),
                          static_cast<int>(geometry.vertices.size()), tris.data(), triangleCount, areas.data());
  if (!rcRasterizeTriangles(&context, verts.data(), static_cast<int>(geometry.vertices.size()),
                            tris.data(), areas.data(), triangleCount, *heightfield, config.walkableClimb)) {
    SetError(error, "Failed to rasterize navigation triangles");
    return false;
  }

  rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *heightfield);
  rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *heightfield);
  rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *heightfield);

  CompactHeightfieldPtr compact(rcAllocCompactHeightfield());
  if (!compact || !rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb, *heightfield, *compact)) {
    SetError(error, "Failed to build compact navigation heightfield");
    return false;
  }

  if (!rcErodeWalkableArea(&context, config.walkableRadius, *compact)) {
    SetError(error, "Failed to erode walkable navigation area");
    return false;
  }
  if (!rcBuildDistanceField(&context, *compact)) {
    SetError(error, "Failed to build navigation distance field");
    return false;
  }
  if (!rcBuildRegions(&context, *compact, 0, config.minRegionArea, config.mergeRegionArea)) {
    SetError(error, "Failed to build navigation regions");
    return false;
  }

  ContourSetPtr contours(rcAllocContourSet());
  if (!contours || !rcBuildContours(&context, *compact, config.maxSimplificationError, config.maxEdgeLen, *contours)) {
    SetError(error, "Failed to build navigation contours");
    return false;
  }

  PolyMeshPtr polyMesh(rcAllocPolyMesh());
  if (!polyMesh || !rcBuildPolyMesh(&context, *contours, config.maxVertsPerPoly, *polyMesh)) {
    SetError(error, "Failed to build navigation polygon mesh");
    return false;
  }

  PolyMeshDetailPtr detailMesh(rcAllocPolyMeshDetail());
  if (!detailMesh || !rcBuildPolyMeshDetail(&context, *polyMesh, *compact,
                                            config.detailSampleDist, config.detailSampleMaxError, *detailMesh)) {
    SetError(error, "Failed to build navigation detail mesh");
    return false;
  }

  if (polyMesh->npolys <= 0) {
    SetError(error, "Navigation build produced no polygons");
    return false;
  }

  for (int i = 0; i < polyMesh->npolys; ++i) {
    if (polyMesh->areas[i] == RC_WALKABLE_AREA) {
      polyMesh->areas[i] = 0;
    }
    polyMesh->flags[i] = kNavPolyFlagWalk;
  }

  dtNavMeshCreateParams params;
  std::memset(&params, 0, sizeof(params));
  params.verts = polyMesh->verts;
  params.vertCount = polyMesh->nverts;
  params.polys = polyMesh->polys;
  params.polyAreas = polyMesh->areas;
  params.polyFlags = polyMesh->flags;
  params.polyCount = polyMesh->npolys;
  params.nvp = polyMesh->nvp;
  params.detailMeshes = detailMesh->meshes;
  params.detailVerts = detailMesh->verts;
  params.detailVertsCount = detailMesh->nverts;
  params.detailTris = detailMesh->tris;
  params.detailTriCount = detailMesh->ntris;
  params.walkableHeight = settings.agentHeight;
  params.walkableRadius = settings.agentRadius;
  params.walkableClimb = settings.agentMaxClimb;
  rcVcopy(params.bmin, polyMesh->bmin);
  rcVcopy(params.bmax, polyMesh->bmax);
  params.cs = config.cs;
  params.ch = config.ch;
  params.buildBvTree = true;

  std::vector<NavOffMeshLink> offMeshLinks;
  int skippedExplicitLinks = 0;
  int generatedDropLinks = 0;
  int generatedJumpLinks = 0;
  int rejectedByValidatorLinks = 0;
  if (!geometry.offMeshLinks.empty() || settings.enableAutoDropLinks) {
    dtNavMeshCreateParams tempParams = params;
    tempParams.offMeshConVerts = nullptr;
    tempParams.offMeshConRad = nullptr;
    tempParams.offMeshConFlags = nullptr;
    tempParams.offMeshConAreas = nullptr;
    tempParams.offMeshConDir = nullptr;
    tempParams.offMeshConUserID = nullptr;
    tempParams.offMeshConCount = 0;

    unsigned char* tempNavData = nullptr;
    int tempNavDataSize = 0;
    if (dtCreateNavMeshData(&tempParams, &tempNavData, &tempNavDataSize) && tempNavData && tempNavDataSize > 0) {
      std::unique_ptr<dtNavMesh, NavMeshDeleter> tempNavMesh(dtAllocNavMesh());
      if (tempNavMesh) {
        dtStatus tempStatus = tempNavMesh->init(tempNavData, tempNavDataSize, DT_TILE_FREE_DATA);
        if (dtStatusSucceed(tempStatus)) {
          tempNavData = nullptr;
          std::unique_ptr<dtNavMeshQuery, NavMeshQueryDeleter> tempQuery(dtAllocNavMeshQuery());
          if (tempQuery && dtStatusSucceed(tempQuery->init(tempNavMesh.get(), 2048))) {
            offMeshLinks = NormalizeExplicitOffMeshLinks(
                geometry.offMeshLinks,
                *tempQuery,
                settings,
                geometry.offMeshLinkValidator,
                skippedExplicitLinks,
                rejectedByValidatorLinks);
            const int generatedTraversalLinks = GenerateAutoDropLinks(
                *polyMesh,
                *tempQuery,
                settings,
                geometry.offMeshLinkValidator,
                geometry.offMeshHybridLinkValidator,
                rejectedByValidatorLinks,
                generatedJumpLinks,
                offMeshLinks);
            generatedDropLinks = (std::max)(0, generatedTraversalLinks - generatedJumpLinks);
          }
        }
      }
      if (tempNavData) {
        dtFree(tempNavData);
      }
    }
  }

  std::vector<float> offMeshConVerts;
  std::vector<float> offMeshConRad;
  std::vector<unsigned short> offMeshConFlags;
  std::vector<unsigned char> offMeshConAreas;
  std::vector<unsigned char> offMeshConDir;
  std::vector<unsigned int> offMeshConUserIds;
  int jumpPadLinkCount = 0;
  int dropLinkCount = 0;
  int jumpLinkCount = 0;
  if (!offMeshLinks.empty()) {
    offMeshConVerts.reserve(offMeshLinks.size() * 6u);
    offMeshConRad.reserve(offMeshLinks.size());
    offMeshConFlags.reserve(offMeshLinks.size());
    offMeshConAreas.reserve(offMeshLinks.size());
    offMeshConDir.reserve(offMeshLinks.size());
    offMeshConUserIds.reserve(offMeshLinks.size());
    for (std::size_t linkIndex = 0; linkIndex < offMeshLinks.size(); ++linkIndex) {
      NavOffMeshLink& link = offMeshLinks[linkIndex];
      link.userId = EncodeOffMeshUserId(link.type, static_cast<uint32_t>(linkIndex));
      if (link.type == NavTraversalType::Drop) {
        ++dropLinkCount;
      } else if (link.type == NavTraversalType::Jump) {
        ++jumpLinkCount;
      } else if (link.type == NavTraversalType::JumpIntent) {
        ++jumpLinkCount;
      } else if (link.type == NavTraversalType::JumpPad) {
        ++jumpPadLinkCount;
      }

      offMeshConVerts.push_back(link.start.x);
      offMeshConVerts.push_back(link.start.y);
      offMeshConVerts.push_back(link.start.z);
      offMeshConVerts.push_back(link.end.x);
      offMeshConVerts.push_back(link.end.y);
      offMeshConVerts.push_back(link.end.z);
      offMeshConRad.push_back((std::max)(0.05f, link.radius));
      offMeshConFlags.push_back(PolyFlagsForTraversal(link.type));
      offMeshConAreas.push_back(PolyAreaForTraversal(link.type));
      offMeshConDir.push_back(link.bidirectional ? DT_OFFMESH_CON_BIDIR : 0);
      offMeshConUserIds.push_back(link.userId);
    }

    params.offMeshConVerts = offMeshConVerts.data();
    params.offMeshConRad = offMeshConRad.data();
    params.offMeshConFlags = offMeshConFlags.data();
    params.offMeshConAreas = offMeshConAreas.data();
    params.offMeshConDir = offMeshConDir.data();
    params.offMeshConUserID = offMeshConUserIds.data();
    params.offMeshConCount = static_cast<int>(offMeshLinks.size());
  }

  unsigned char* navData = nullptr;
  int navDataSize = 0;
  if (!dtCreateNavMeshData(&params, &navData, &navDataSize) || !navData || navDataSize <= 0) {
    SetError(error, "Failed to create Detour navmesh data");
    return false;
  }

  std::unique_ptr<dtNavMesh, NavMeshDeleter> navMesh(dtAllocNavMesh());
  if (!navMesh) {
    dtFree(navData);
    SetError(error, "Failed to allocate Detour navmesh");
    return false;
  }
  dtStatus status = navMesh->init(navData, navDataSize, DT_TILE_FREE_DATA);
  if (dtStatusFailed(status)) {
    dtFree(navData);
    SetError(error, "Failed to initialize Detour navmesh");
    return false;
  }

  std::unique_ptr<dtNavMeshQuery, NavMeshQueryDeleter> query(dtAllocNavMeshQuery());
  if (!query) {
    SetError(error, "Failed to allocate Detour navmesh query");
    return false;
  }
  status = query->init(navMesh.get(), 2048);
  if (dtStatusFailed(status)) {
    SetError(error, "Failed to initialize Detour navmesh query");
    return false;
  }

  m_stats.vertexCount = static_cast<int>(geometry.vertices.size());
  m_stats.triangleCount = triangleCount;
  m_stats.width = config.width;
  m_stats.height = config.height;
  m_stats.polygonCount = polyMesh->npolys;
  m_stats.detailTriangleCount = detailMesh->ntris;
  m_stats.offMeshLinkCount = static_cast<int>(offMeshLinks.size());
  m_stats.dropLinkCount = dropLinkCount;
  m_stats.jumpLinkCount = jumpLinkCount;
  m_stats.jumpPadLinkCount = jumpPadLinkCount;
  SaveNavMeshCache(cachePath, effectiveCacheKey, m_stats, navData, navDataSize);
  m_impl->navMesh = std::move(navMesh);
  m_impl->query = std::move(query);

  T8_LOG_INFO("[Navigation] Built navmesh: verts=%d tris=%d grid=%dx%d polys=%d detailTris=%d offMesh=%d drop=%d jump=%d jumpPad=%d skippedExplicit=%d rejectedByValidator=%d generatedDrop=%d generatedJump=%d",
              m_stats.vertexCount, m_stats.triangleCount, m_stats.width, m_stats.height,
              m_stats.polygonCount, m_stats.detailTriangleCount,
              m_stats.offMeshLinkCount, m_stats.dropLinkCount, m_stats.jumpLinkCount, m_stats.jumpPadLinkCount,
              skippedExplicitLinks, rejectedByValidatorLinks, generatedDropLinks, generatedJumpLinks);
  return true;
#endif
}

bool NavMesh::FindPath(const XVECTOR3& start,
                       const XVECTOR3& end,
                       std::vector<XVECTOR3>& outPath,
                       std::string* error) const {
  outPath.clear();
#if !defined(T850_ENABLE_RECAST)
  SetError(error, "RecastNavigation is not enabled in this build");
  return false;
#else
  if (!IsReady()) {
    SetError(error, "Navigation mesh is not ready");
    return false;
  }

  const float startPos[3] = { start.x, start.y, start.z };
  const float endPos[3] = { end.x, end.y, end.z };
  const float extents[3] = { m_impl->settings.queryExtents.x, m_impl->settings.queryExtents.y, m_impl->settings.queryExtents.z };

  dtQueryFilter filter;
  ConfigureTraversalFilter(filter);

  dtPolyRef startRef = 0;
  dtPolyRef endRef = 0;
  float nearestStart[3] = {};
  float nearestEnd[3] = {};
  dtStatus status = m_impl->query->findNearestPoly(startPos, extents, &filter, &startRef, nearestStart);
  if (dtStatusFailed(status) || !startRef) {
    SetError(error, "Failed to find nearest start nav polygon");
    return false;
  }
  status = m_impl->query->findNearestPoly(endPos, extents, &filter, &endRef, nearestEnd);
  if (dtStatusFailed(status) || !endRef) {
    SetError(error, "Failed to find nearest end nav polygon");
    return false;
  }

  dtPolyRef pathPolys[kMaxPathPolys] = {};
  int pathPolyCount = 0;
  status = m_impl->query->findPath(startRef, endRef, nearestStart, nearestEnd,
                                   &filter, pathPolys, &pathPolyCount, kMaxPathPolys);
  if (dtStatusFailed(status) || pathPolyCount <= 0) {
    SetError(error, "Detour failed to find a path");
    return false;
  }

  float straightPath[kMaxStraightPath * 3] = {};
  unsigned char straightFlags[kMaxStraightPath] = {};
  dtPolyRef straightPolys[kMaxStraightPath] = {};
  int straightPathCount = 0;
  status = m_impl->query->findStraightPath(nearestStart, nearestEnd,
                                           pathPolys, pathPolyCount,
                                           straightPath, straightFlags, straightPolys,
                                           &straightPathCount, kMaxStraightPath);
  if (dtStatusFailed(status) || straightPathCount <= 0) {
    SetError(error, "Detour failed to straighten path");
    return false;
  }

  outPath.reserve(static_cast<std::size_t>(straightPathCount));
  for (int i = 0; i < straightPathCount; ++i) {
    const float* p = &straightPath[i * 3];
    outPath.emplace_back(p[0], p[1], p[2], 1.0f);
  }
  return true;
#endif
}

bool NavMesh::ProjectPoint(const XVECTOR3& point,
                           XVECTOR3& outPoint,
                           const XVECTOR3& queryExtents,
                           std::string* error) const {
  T8_TELEMETRY_SCOPE("navigation.project_point");
  if (RuntimeTelemetry::IsFrameActive()) {
    RuntimeTelemetry::AddCounter("navigation.project_point.count", 1.0);
  }
#if !defined(T850_ENABLE_RECAST)
  if (error) *error = "RecastNavigation is not enabled in this build";
  outPoint = point;
  return false;
#else
  outPoint = point;
  if (!IsReady()) {
    if (error) *error = "Navigation mesh is not ready";
    return false;
  }
  if (!IsFiniteVec3(point)) {
    if (error) *error = "Navigation projection point is not finite";
    return false;
  }

  const float position[3] = { point.x, point.y, point.z };
  const float extents[3] = {
    queryExtents.x > 0.0f ? queryExtents.x : m_impl->settings.queryExtents.x,
    queryExtents.y > 0.0f ? queryExtents.y : m_impl->settings.queryExtents.y,
    queryExtents.z > 0.0f ? queryExtents.z : m_impl->settings.queryExtents.z
  };

  dtQueryFilter filter;
  ConfigureWalkFilter(filter);

  dtPolyRef nearestRef = 0;
  float nearestPoint[3] = {};
  const dtStatus status = m_impl->query->findNearestPoly(position, extents, &filter, &nearestRef, nearestPoint);
  if (dtStatusFailed(status) || !nearestRef) {
    if (RuntimeTelemetry::IsFrameActive()) {
      RuntimeTelemetry::AddCounter("navigation.project_point.fail", 1.0);
    }
    if (error) *error = "Failed to project point onto nearest nav polygon";
    return false;
  }

  outPoint = XVECTOR3(nearestPoint[0], nearestPoint[1], nearestPoint[2], 1.0f);
  if (RuntimeTelemetry::IsFrameActive()) {
    RuntimeTelemetry::AddCounter("navigation.project_point.success", 1.0);
  }
  return true;
#endif
}

NavPathResult NavMesh::FindPath(const NavPathRequest& request) const {
#if !defined(T850_ENABLE_RECAST)
  NavPathResult result;
  result.error = "RecastNavigation is not enabled in this build";
  SetError(nullptr, result.error);
  return result;
#else
  if (!IsReady()) {
    NavPathResult result;
    result.error = "Navigation mesh is not ready";
    SetError(nullptr, result.error);
    return result;
  }

  NavPathResult result = FindPathWithQuery(m_impl->query.get(), m_impl->settings, request);
  if (!result.success && !result.error.empty()) {
    SetError(nullptr, result.error);
  }
  return result;
#endif
}

void NavMesh::FindPaths(const std::vector<NavPathRequest>& requests,
                        std::vector<NavPathResult>& outResults) const {
  T8_TELEMETRY_SCOPE("navigation.find_paths_batch");
  if (RuntimeTelemetry::IsFrameActive()) {
    RuntimeTelemetry::AddCounter("navigation.find_paths_batch.calls", 1.0);
    RuntimeTelemetry::AddCounter("navigation.find_paths_batch.requests", static_cast<double>(requests.size()));
  }
  outResults.clear();
  outResults.resize(requests.size());
  if (requests.empty()) {
    return;
  }

#if !defined(T850_ENABLE_RECAST)
  for (NavPathResult& result : outResults) {
    result.error = "RecastNavigation is not enabled in this build";
  }
  SetError(nullptr, "RecastNavigation is not enabled in this build");
  return;
#else
  if (!IsReady()) {
    for (NavPathResult& result : outResults) {
      result.error = "Navigation mesh is not ready";
    }
    SetError(nullptr, "Navigation mesh is not ready");
    return;
  }

  auto runRequest = [&](int requestIndex) {
    std::unique_ptr<dtNavMeshQuery, NavMeshQueryDeleter> query(dtAllocNavMeshQuery());
    if (!query) {
      outResults[static_cast<std::size_t>(requestIndex)].error = "Failed to allocate Detour navmesh query";
      return;
    }

    dtStatus status = query->init(m_impl->navMesh.get(), 2048);
    if (dtStatusFailed(status)) {
      outResults[static_cast<std::size_t>(requestIndex)].error = "Failed to initialize Detour navmesh query";
      return;
    }

    outResults[static_cast<std::size_t>(requestIndex)] =
        FindPathWithQuery(query.get(), m_impl->settings, requests[static_cast<std::size_t>(requestIndex)]);
  };

  if (g_threadPool &&
      requests.size() > 1 &&
      g_threadPool->NumWorkers() > 0 &&
      !g_threadPool->IsWorkerThread()) {
    if (RuntimeTelemetry::IsFrameActive()) {
      RuntimeTelemetry::AddCounter("navigation.find_paths_batch.threaded", 1.0);
    }
    g_threadPool->ParallelForHeavy(0, static_cast<int>(requests.size()), runRequest);
  } else {
    for (int i = 0; i < static_cast<int>(requests.size()); ++i) {
      runRequest(i);
    }
  }
  if (RuntimeTelemetry::IsFrameActive()) {
    int successCount = 0;
    int pointCount = 0;
    int specialSegmentCount = 0;
    for (const NavPathResult& result : outResults) {
      if (result.success) {
        ++successCount;
      }
      pointCount += static_cast<int>(result.points.size());
      for (const NavPathResult::Segment& segment : result.segments) {
        if (segment.type != NavTraversalType::Walk) {
          ++specialSegmentCount;
        }
      }
    }
    RuntimeTelemetry::AddCounter("navigation.find_paths_batch.success", static_cast<double>(successCount));
    RuntimeTelemetry::AddCounter("navigation.find_paths_batch.fail", static_cast<double>(outResults.size() - static_cast<std::size_t>(successCount)));
    RuntimeTelemetry::AddCounter("navigation.find_paths_batch.points", static_cast<double>(pointCount));
    RuntimeTelemetry::AddCounter("navigation.find_paths_batch.special_segments", static_cast<double>(specialSegmentCount));
  }
#endif
}

bool NavMesh::GetDebugWireframe(std::vector<XVECTOR3>& outVertices,
                                std::vector<unsigned int>& outIndices,
                                float verticalOffset) const {
  outVertices.clear();
  outIndices.clear();
#if !defined(T850_ENABLE_RECAST)
  return false;
#else
  if (!IsReady()) {
    return false;
  }

  const dtNavMesh* detourMesh = m_impl->navMesh.get();
  const int maxTiles = detourMesh ? detourMesh->getMaxTiles() : 0;
  for (int tileIndex = 0; tileIndex < maxTiles; ++tileIndex) {
    const dtMeshTile* tile = detourMesh->getTile(tileIndex);
    if (!tile || !tile->header || !tile->verts || !tile->polys) {
      continue;
    }

    for (int polyIndex = 0; polyIndex < tile->header->polyCount; ++polyIndex) {
      const dtPoly& poly = tile->polys[polyIndex];
      if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION || poly.vertCount < 2) {
        continue;
      }

      auto appendEdge = [&](const float* v0, const float* v1) {
        const unsigned int base = static_cast<unsigned int>(outVertices.size());
        outVertices.emplace_back(v0[0], v0[1] + verticalOffset, v0[2], 1.0f);
        outVertices.emplace_back(v1[0], v1[1] + verticalOffset, v1[2], 1.0f);
        outIndices.push_back(base);
        outIndices.push_back(base + 1);
      };

      if (tile->detailMeshes && tile->detailTris) {
        const dtPolyDetail& detail = tile->detailMeshes[polyIndex];
        auto detailVertex = [&](int localIndex) -> const float* {
          if (localIndex < static_cast<int>(poly.vertCount)) {
            const unsigned short polyVertexIndex = poly.verts[localIndex];
            if (polyVertexIndex < tile->header->vertCount) {
              return &tile->verts[polyVertexIndex * 3];
            }
            return nullptr;
          }

          const int detailVertexIndex = detail.vertBase + localIndex - static_cast<int>(poly.vertCount);
          if (detailVertexIndex >= 0 && detailVertexIndex < tile->header->detailVertCount) {
            return &tile->detailVerts[detailVertexIndex * 3];
          }
          return nullptr;
        };

        for (unsigned int detailTriIndex = 0; detailTriIndex < detail.triCount; ++detailTriIndex) {
          const unsigned char* tri = &tile->detailTris[(detail.triBase + detailTriIndex) * 4];
          const float* v0 = detailVertex(tri[0]);
          const float* v1 = detailVertex(tri[1]);
          const float* v2 = detailVertex(tri[2]);
          if (!v0 || !v1 || !v2) {
            continue;
          }
          appendEdge(v0, v1);
          appendEdge(v1, v2);
          appendEdge(v2, v0);
        }
        continue;
      }

      for (unsigned char edgeIndex = 0; edgeIndex < poly.vertCount; ++edgeIndex) {
        const unsigned short v0Index = poly.verts[edgeIndex];
        const unsigned short v1Index = poly.verts[(edgeIndex + 1) % poly.vertCount];
        if (v0Index >= tile->header->vertCount || v1Index >= tile->header->vertCount) {
          continue;
        }

        const float* v0 = &tile->verts[v0Index * 3];
        const float* v1 = &tile->verts[v1Index * 3];
        appendEdge(v0, v1);
      }
    }
  }

  return !outVertices.empty() && !outIndices.empty();
#endif
}

bool NavMesh::GetDebugNodeMarkers(std::vector<XVECTOR3>& outVertices,
                                  std::vector<unsigned int>& outIndices,
                                  float verticalOffset) const {
  outVertices.clear();
  outIndices.clear();
#if !defined(T850_ENABLE_RECAST)
  return false;
#else
  if (!IsReady()) {
    return false;
  }

  const dtNavMesh* detourMesh = m_impl->navMesh.get();
  const int maxTiles = detourMesh ? detourMesh->getMaxTiles() : 0;
  const float nodeMarkerRadius = (std::max)(0.10f, m_impl->settings.agentRadius * 0.25f);
  constexpr int kNodeSphereSegments = 12;
  constexpr float kTwoPi = 6.28318530717958647692f;
  for (int tileIndex = 0; tileIndex < maxTiles; ++tileIndex) {
    const dtMeshTile* tile = detourMesh->getTile(tileIndex);
    if (!tile || !tile->header || !tile->verts || !tile->polys) {
      continue;
    }

    for (int polyIndex = 0; polyIndex < tile->header->polyCount; ++polyIndex) {
      const dtPoly& poly = tile->polys[polyIndex];
      if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION || poly.vertCount < 2) {
        continue;
      }

      auto appendMarkerLine = [&](const XVECTOR3& a, const XVECTOR3& b) {
        const unsigned int base = static_cast<unsigned int>(outVertices.size());
        outVertices.push_back(a);
        outVertices.push_back(b);
        outIndices.push_back(base);
        outIndices.push_back(base + 1);
      };

      XVECTOR3 center(0.0f, 0.0f, 0.0f, 1.0f);
      int validVertices = 0;
      for (unsigned char vertexIndex = 0; vertexIndex < poly.vertCount; ++vertexIndex) {
        const unsigned short polyVertexIndex = poly.verts[vertexIndex];
        if (polyVertexIndex >= tile->header->vertCount) {
          continue;
        }
        const float* v = &tile->verts[polyVertexIndex * 3];
        center.x += v[0];
        center.y += v[1];
        center.z += v[2];
        ++validVertices;
      }
      if (validVertices <= 0) {
        continue;
      }
      const float invVertCount = 1.0f / static_cast<float>(validVertices);
      center.x *= invVertCount;
      center.y = center.y * invVertCount + verticalOffset;
      center.z *= invVertCount;
      for (int segment = 0; segment < kNodeSphereSegments; ++segment) {
        const float a0 = kTwoPi * static_cast<float>(segment) / static_cast<float>(kNodeSphereSegments);
        const float a1 = kTwoPi * static_cast<float>(segment + 1) / static_cast<float>(kNodeSphereSegments);
        const float c0 = std::cos(a0) * nodeMarkerRadius;
        const float s0 = std::sin(a0) * nodeMarkerRadius;
        const float c1 = std::cos(a1) * nodeMarkerRadius;
        const float s1 = std::sin(a1) * nodeMarkerRadius;
        appendMarkerLine(XVECTOR3(center.x + c0, center.y, center.z + s0, 1.0f),
                         XVECTOR3(center.x + c1, center.y, center.z + s1, 1.0f));
        appendMarkerLine(XVECTOR3(center.x + c0, center.y + s0, center.z, 1.0f),
                         XVECTOR3(center.x + c1, center.y + s1, center.z, 1.0f));
        appendMarkerLine(XVECTOR3(center.x, center.y + c0, center.z + s0, 1.0f),
                         XVECTOR3(center.x, center.y + c1, center.z + s1, 1.0f));
      }
    }
  }

  return !outVertices.empty() && !outIndices.empty();
#endif
}

bool NavMesh::GetDebugGraphEdges(std::vector<XVECTOR3>& outVertices,
                                 std::vector<unsigned int>& outIndices,
                                 float verticalOffset) const {
  outVertices.clear();
  outIndices.clear();
#if !defined(T850_ENABLE_RECAST)
  return false;
#else
  if (!IsReady()) {
    return false;
  }

  const dtNavMesh* detourMesh = m_impl->navMesh.get();
  const int maxTiles = detourMesh ? detourMesh->getMaxTiles() : 0;
  for (int tileIndex = 0; tileIndex < maxTiles; ++tileIndex) {
    const dtMeshTile* tile = detourMesh->getTile(tileIndex);
    if (!tile || !tile->header || !tile->verts || !tile->polys) {
      continue;
    }

    std::vector<XVECTOR3> centers(static_cast<std::size_t>(tile->header->polyCount));
    std::vector<unsigned char> centerValid(static_cast<std::size_t>(tile->header->polyCount), 0);
    for (int polyIndex = 0; polyIndex < tile->header->polyCount; ++polyIndex) {
      const dtPoly& poly = tile->polys[polyIndex];
      if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION || poly.vertCount < 2) {
        continue;
      }

      XVECTOR3 center(0.0f, 0.0f, 0.0f, 1.0f);
      int validVertices = 0;
      for (unsigned char vertexIndex = 0; vertexIndex < poly.vertCount; ++vertexIndex) {
        const unsigned short polyVertexIndex = poly.verts[vertexIndex];
        if (polyVertexIndex >= tile->header->vertCount) {
          continue;
        }
        const float* v = &tile->verts[polyVertexIndex * 3];
        center.x += v[0];
        center.y += v[1];
        center.z += v[2];
        ++validVertices;
      }
      if (validVertices <= 0) {
        continue;
      }

      const float invVertexCount = 1.0f / static_cast<float>(validVertices);
      center.x *= invVertexCount;
      center.y = center.y * invVertexCount + verticalOffset;
      center.z *= invVertexCount;
      centers[static_cast<std::size_t>(polyIndex)] = center;
      centerValid[static_cast<std::size_t>(polyIndex)] = 1;
    }

    auto appendGraphEdge = [&](const XVECTOR3& a, const XVECTOR3& b) {
      const unsigned int base = static_cast<unsigned int>(outVertices.size());
      outVertices.push_back(a);
      outVertices.push_back(b);
      outIndices.push_back(base);
      outIndices.push_back(base + 1);
    };

    for (int polyIndex = 0; polyIndex < tile->header->polyCount; ++polyIndex) {
      if (!centerValid[static_cast<std::size_t>(polyIndex)]) {
        continue;
      }
      const dtPoly& poly = tile->polys[polyIndex];
      for (unsigned char edgeIndex = 0; edgeIndex < poly.vertCount; ++edgeIndex) {
        const unsigned short neighbor = poly.neis[edgeIndex];
        if (!neighbor || (neighbor & DT_EXT_LINK)) {
          continue;
        }

        const int neighborPolyIndex = static_cast<int>(neighbor & ~DT_EXT_LINK) - 1;
        if (neighborPolyIndex <= polyIndex ||
            neighborPolyIndex < 0 ||
            neighborPolyIndex >= tile->header->polyCount ||
            !centerValid[static_cast<std::size_t>(neighborPolyIndex)]) {
          continue;
        }
        appendGraphEdge(centers[static_cast<std::size_t>(polyIndex)],
                        centers[static_cast<std::size_t>(neighborPolyIndex)]);
      }
    }
  }

  return !outVertices.empty() && !outIndices.empty();
#endif
}

bool NavMesh::GetDebugNodePositions(std::vector<XVECTOR3>& outPositions,
                                    float verticalOffset) const {
  outPositions.clear();
#if !defined(T850_ENABLE_RECAST)
  (void)verticalOffset;
  return false;
#else
  if (!IsReady()) {
    return false;
  }

  const dtNavMesh* detourMesh = m_impl->navMesh.get();
  const int maxTiles = detourMesh ? detourMesh->getMaxTiles() : 0;
  for (int tileIndex = 0; tileIndex < maxTiles; ++tileIndex) {
    const dtMeshTile* tile = detourMesh->getTile(tileIndex);
    if (!tile || !tile->header || !tile->verts || !tile->polys) {
      continue;
    }

    for (int polyIndex = 0; polyIndex < tile->header->polyCount; ++polyIndex) {
      const dtPoly& poly = tile->polys[polyIndex];
      if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION || poly.vertCount < 2) {
        continue;
      }

      XVECTOR3 center(0.0f, 0.0f, 0.0f, 1.0f);
      int validVertices = 0;
      for (unsigned char vertexIndex = 0; vertexIndex < poly.vertCount; ++vertexIndex) {
        const unsigned short polyVertexIndex = poly.verts[vertexIndex];
        if (polyVertexIndex >= tile->header->vertCount) {
          continue;
        }
        const float* v = &tile->verts[polyVertexIndex * 3];
        center.x += v[0];
        center.y += v[1];
        center.z += v[2];
        ++validVertices;
      }
      if (validVertices <= 0) {
        continue;
      }

      const float invVertexCount = 1.0f / static_cast<float>(validVertices);
      center.x *= invVertexCount;
      center.y = center.y * invVertexCount + verticalOffset;
      center.z *= invVertexCount;
      outPositions.push_back(center);
    }
  }
  return !outPositions.empty();
#endif
}

bool NavMesh::GetDebugOffMeshLinks(NavTraversalType type,
                                   std::vector<XVECTOR3>& outVertices,
                                   std::vector<unsigned int>& outIndices,
                                   float verticalOffset) const {
  outVertices.clear();
  outIndices.clear();
#if !defined(T850_ENABLE_RECAST)
  (void)type;
  (void)verticalOffset;
  return false;
#else
  if (!IsReady() || type == NavTraversalType::Walk) {
    return false;
  }

  const dtNavMesh* detourMesh = m_impl->navMesh.get();
  const int maxTiles = detourMesh ? detourMesh->getMaxTiles() : 0;
  const float arrowLength = (std::max)(0.20f, m_impl->settings.agentRadius * 0.75f);
  const float arrowHalfWidth = arrowLength * 0.45f;

  auto appendLine = [&](const XVECTOR3& a, const XVECTOR3& b) {
    const unsigned int base = static_cast<unsigned int>(outVertices.size());
    outVertices.push_back(a);
    outVertices.push_back(b);
    outIndices.push_back(base);
    outIndices.push_back(base + 1);
  };

  for (int tileIndex = 0; tileIndex < maxTiles; ++tileIndex) {
    const dtMeshTile* tile = detourMesh->getTile(tileIndex);
    if (!tile || !tile->header || !tile->offMeshCons) {
      continue;
    }

    for (int linkIndex = 0; linkIndex < tile->header->offMeshConCount; ++linkIndex) {
      const dtOffMeshConnection& connection = tile->offMeshCons[linkIndex];
      if (DecodeOffMeshUserId(connection.userId) != type) {
        continue;
      }

      XVECTOR3 start(connection.pos[0], connection.pos[1] + verticalOffset, connection.pos[2], 1.0f);
      XVECTOR3 end(connection.pos[3], connection.pos[4] + verticalOffset, connection.pos[5], 1.0f);
      XVECTOR3 direction = end - start;
      appendLine(start, end);

      direction.y = 0.0f;
      direction.w = 0.0f;
      direction = NormalizeHorizontalOr(direction, XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
      const XVECTOR3 right(-direction.z, 0.0f, direction.x, 0.0f);
      const XVECTOR3 arrowBase = end - direction * arrowLength;
      appendLine(end, arrowBase + right * arrowHalfWidth);
      appendLine(end, arrowBase - right * arrowHalfWidth);
    }
  }

  return !outVertices.empty() && !outIndices.empty();
#endif
}

void NavigationWorld::ClearSources() {
  m_sources.clear();
  m_lastSourceStats = NavSourceBuildStats{};
  m_navMesh.Clear();
  m_dirty = true;
}

void NavigationWorld::RegisterSource(const NavSourceInstance& source) {
  auto existing = std::find_if(m_sources.begin(), m_sources.end(),
      [&](const NavSourceInstance& item) { return item.entityId == source.entityId && source.entityId != 0; });
  if (existing != m_sources.end()) {
    *existing = source;
  } else {
    m_sources.push_back(source);
  }
  m_dirty = true;
}

bool NavigationWorld::UnregisterSource(unsigned int entityId) {
  const auto oldSize = m_sources.size();
  m_sources.erase(std::remove_if(m_sources.begin(), m_sources.end(),
      [&](const NavSourceInstance& source) { return source.entityId == entityId; }), m_sources.end());
  if (m_sources.size() == oldSize) {
    return false;
  }
  m_dirty = true;
  return true;
}

bool NavigationWorld::UpdateSourceTransform(unsigned int entityId, const XMATRIX44& worldTransform) {
  auto existing = std::find_if(m_sources.begin(), m_sources.end(),
      [&](const NavSourceInstance& source) { return source.entityId == entityId; });
  if (existing == m_sources.end()) {
    return false;
  }
  existing->worldTransform = worldTransform;
  m_dirty = true;
  return true;
}

void NavigationWorld::SetBuildSettings(const NavMeshBuildSettings& settings) {
  m_settings = settings;
  m_dirty = true;
}

bool NavigationWorld::Rebuild(std::string* error) {
  NavMeshGeometry geometry;
  m_lastSourceStats = NavSourceBuildStats{};

  for (const NavSourceInstance& source : m_sources) {
    ++m_lastSourceStats.considered;
    if (!source.includeInNavigation || !source.visible || !source.navigationStatic) {
      ++m_lastSourceStats.skippedInvisible;
      continue;
    }

    const xF::XDataBase* database = source.database;
    XMATRIX44 worldTransform = source.worldTransform;

    if (source.instance) {
      if (!source.instance->Visible || !source.instance->pBase) {
        ++m_lastSourceStats.skippedInvisible;
        continue;
      }
      if (source.instance->GetSkinnedMesh()) {
        ++m_lastSourceStats.skippedSkinned;
        continue;
      }

      const RenderMesh* mesh = dynamic_cast<const RenderMesh*>(source.instance->pBase);
      if (!mesh || !mesh->xFile) {
        ++m_lastSourceStats.skippedInvalid;
        continue;
      }
      database = mesh->xFile;
      worldTransform = source.instance->Final;
    }

    if (!database) {
      ++m_lastSourceStats.skippedInvalid;
      continue;
    }

    const std::size_t indicesBefore = geometry.indices.size();
    std::string sourceError;
    if (!AppendGeometryFromXDataBase(*database, worldTransform, geometry, &sourceError) ||
        geometry.indices.size() == indicesBefore) {
      ++m_lastSourceStats.skippedInvalid;
      continue;
    }
    ++m_lastSourceStats.included;
  }

  if (geometry.vertices.empty() || geometry.indices.size() < 3) {
    SetError(error, "NavigationWorld sources produced no navigation geometry");
    m_navMesh.Clear();
    m_dirty = true;
    return false;
  }

  m_lastSourceStats.vertexCount = static_cast<int>(geometry.vertices.size());
  m_lastSourceStats.triangleCount = static_cast<int>(geometry.indices.size() / 3);
  if (!m_navMesh.Build(geometry, m_settings, error)) {
    m_dirty = true;
    return false;
  }

  m_dirty = false;
  return true;
}

} // namespace navigation
} // namespace t850
