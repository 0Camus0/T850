#include <pch.h>

#include <navigation/NavigationSystem.h>

#include <utils/Log.h>
#include <utils/XDataBase.h>
#include <utils/xDefs.h>
#include <utils/ThreadPool.h>
#include <scene/PrimitiveInstance.h>
#include <scene/RenderMesh.h>
#include <scene/RenderSkinnedMesh.h>

#include <algorithm>
#include <cmath>
#include <cstring>

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
constexpr int kMaxPathPolys = 256;
constexpr int kMaxStraightPath = 256;

NavPathResult FindPathWithQuery(dtNavMeshQuery* query,
                                const NavMeshBuildSettings& settings,
                                const NavPathRequest& request) {
  NavPathResult result;
  if (!query) {
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
  filter.setIncludeFlags(kNavPolyFlagWalk);
  filter.setExcludeFlags(0);

  dtPolyRef startRef = 0;
  dtPolyRef endRef = 0;
  float nearestStart[3] = {};
  float nearestEnd[3] = {};
  dtStatus status = query->findNearestPoly(startPos, extents, &filter, &startRef, nearestStart);
  if (dtStatusFailed(status) || !startRef) {
    result.error = "Failed to find nearest start nav polygon";
    return result;
  }
  status = query->findNearestPoly(endPos, extents, &filter, &endRef, nearestEnd);
  if (dtStatusFailed(status) || !endRef) {
    result.error = "Failed to find nearest end nav polygon";
    return result;
  }

  dtPolyRef pathPolys[kMaxPathPolys] = {};
  int pathPolyCount = 0;
  status = query->findPath(startRef, endRef, nearestStart, nearestEnd,
                           &filter, pathPolys, &pathPolyCount, kMaxPathPolys);
  if (dtStatusFailed(status) || pathPolyCount <= 0) {
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
    result.error = "Detour failed to straighten path";
    return result;
  }

  result.points.reserve(static_cast<std::size_t>(straightPathCount));
  for (int i = 0; i < straightPathCount; ++i) {
    const float* p = &straightPath[i * 3];
    result.points.emplace_back(p[0], p[1], p[2], 1.0f);
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
      settings.vertsPerPoly < 3) {
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
  m_impl->navMesh = std::move(navMesh);
  m_impl->query = std::move(query);

  T8_LOG_INFO("[Navigation] Built navmesh: verts=%d tris=%d grid=%dx%d polys=%d detailTris=%d",
              m_stats.vertexCount, m_stats.triangleCount, m_stats.width, m_stats.height,
              m_stats.polygonCount, m_stats.detailTriangleCount);
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
  filter.setIncludeFlags(kNavPolyFlagWalk);
  filter.setExcludeFlags(0);

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
  filter.setIncludeFlags(kNavPolyFlagWalk);
  filter.setExcludeFlags(0);

  dtPolyRef nearestRef = 0;
  float nearestPoint[3] = {};
  const dtStatus status = m_impl->query->findNearestPoly(position, extents, &filter, &nearestRef, nearestPoint);
  if (dtStatusFailed(status) || !nearestRef) {
    if (error) *error = "Failed to project point onto nearest nav polygon";
    return false;
  }

  outPoint = XVECTOR3(nearestPoint[0], nearestPoint[1], nearestPoint[2], 1.0f);
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
    g_threadPool->ParallelForHeavy(0, static_cast<int>(requests.size()), runRequest);
  } else {
    for (int i = 0; i < static_cast<int>(requests.size()); ++i) {
      runRequest(i);
    }
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
  const float nodeMarkerSize = (std::max)(0.10f, m_impl->settings.agentRadius * 0.25f);
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
      appendMarkerLine(XVECTOR3(center.x - nodeMarkerSize, center.y, center.z, 1.0f),
                       XVECTOR3(center.x + nodeMarkerSize, center.y, center.z, 1.0f));
      appendMarkerLine(XVECTOR3(center.x, center.y, center.z - nodeMarkerSize, 1.0f),
                       XVECTOR3(center.x, center.y, center.z + nodeMarkerSize, 1.0f));

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
