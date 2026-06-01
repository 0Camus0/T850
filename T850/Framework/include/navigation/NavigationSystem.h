#pragma once

#include <memory>
#include <string>
#include <vector>

#include <utils/xMaths.h>

namespace xF {
class XDataBase;
}

namespace t850 {
class PrimitiveInst;

namespace navigation {

struct NavigationBackendInfo {
  bool recastAvailable = false;
  bool detourAvailable = false;
  bool detourCrowdAvailable = false;
  bool detourTileCacheAvailable = false;
  std::string recastVersion;
};

NavigationBackendInfo GetNavigationBackendInfo();
bool ValidateNavigationBackend();

struct NavMeshBuildSettings {
  float cellSize = 0.30f;
  float cellHeight = 0.20f;
  float agentHeight = 2.0f;
  float agentRadius = 0.6f;
  float agentMaxClimb = 0.9f;
  float agentMaxSlope = 45.0f;
  float regionMinSize = 8.0f;
  float regionMergeSize = 20.0f;
  float edgeMaxLen = 12.0f;
  float edgeMaxError = 1.3f;
  int vertsPerPoly = 6;
  float detailSampleDist = 6.0f;
  float detailSampleMaxError = 1.0f;
  XVECTOR3 queryExtents = XVECTOR3(2.0f, 4.0f, 2.0f, 0.0f);
};

struct NavMeshGeometry {
  std::vector<XVECTOR3> vertices;
  std::vector<int> indices;
};

struct NavMeshBuildStats {
  int vertexCount = 0;
  int triangleCount = 0;
  int width = 0;
  int height = 0;
  int polygonCount = 0;
  int detailTriangleCount = 0;
};

struct NavSourceBuildStats {
  int considered = 0;
  int included = 0;
  int skippedInvisible = 0;
  int skippedSkinned = 0;
  int skippedInvalid = 0;
  int vertexCount = 0;
  int triangleCount = 0;
};

struct NavSourceInstance {
  unsigned int entityId = 0;
  const PrimitiveInst* instance = nullptr;
  const xF::XDataBase* database = nullptr;
  XMATRIX44 worldTransform;
  bool visible = true;
  bool includeInNavigation = true;
  bool navigationStatic = true;
  bool navigationWalkable = true;
  int area = 0;
  int flags = 0;
  std::string debugName;
};

struct NavPathRequest {
  XVECTOR3 start;
  XVECTOR3 end;
  XVECTOR3 queryExtents = XVECTOR3(2.0f, 4.0f, 2.0f, 0.0f);
};

struct NavPathResult {
  bool success = false;
  std::vector<XVECTOR3> points;
  std::string error;
};

class INavigationMesh {
public:
  virtual ~INavigationMesh() = default;
  virtual bool IsReady() const = 0;
  virtual const NavMeshBuildStats& GetStats() const = 0;
  virtual bool ProjectPoint(const XVECTOR3& point,
                            XVECTOR3& outPoint,
                            const XVECTOR3& queryExtents = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f),
                            std::string* error = nullptr) const = 0;
  virtual NavPathResult FindPath(const NavPathRequest& request) const = 0;
  virtual void FindPaths(const std::vector<NavPathRequest>& requests,
                         std::vector<NavPathResult>& outResults) const {
    outResults.resize(requests.size());
    for (std::size_t i = 0; i < requests.size(); ++i) {
      outResults[i] = FindPath(requests[i]);
    }
  }
};

class NavMesh : public INavigationMesh {
public:
  NavMesh();
  ~NavMesh();
  NavMesh(NavMesh&&) noexcept;
  NavMesh& operator=(NavMesh&&) noexcept;
  NavMesh(const NavMesh&) = delete;
  NavMesh& operator=(const NavMesh&) = delete;

  bool Build(const NavMeshGeometry& geometry,
             const NavMeshBuildSettings& settings = NavMeshBuildSettings(),
             std::string* error = nullptr);
  bool BuildFromXDataBase(const xF::XDataBase& database,
                          const NavMeshBuildSettings& settings = NavMeshBuildSettings(),
                          std::string* error = nullptr);

  bool IsReady() const override;
  void Clear();

  bool FindPath(const XVECTOR3& start,
                const XVECTOR3& end,
                std::vector<XVECTOR3>& outPath,
                std::string* error = nullptr) const;
  bool ProjectPoint(const XVECTOR3& point,
                    XVECTOR3& outPoint,
                    const XVECTOR3& queryExtents = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f),
                    std::string* error = nullptr) const override;
  NavPathResult FindPath(const NavPathRequest& request) const override;
  void FindPaths(const std::vector<NavPathRequest>& requests,
                 std::vector<NavPathResult>& outResults) const override;
  bool GetDebugWireframe(std::vector<XVECTOR3>& outVertices,
                         std::vector<unsigned int>& outIndices,
                         float verticalOffset = 0.01f) const;
  bool GetDebugNodeMarkers(std::vector<XVECTOR3>& outVertices,
                           std::vector<unsigned int>& outIndices,
                           float verticalOffset = 0.01f) const;
  bool GetDebugGraphEdges(std::vector<XVECTOR3>& outVertices,
                          std::vector<unsigned int>& outIndices,
                          float verticalOffset = 0.015f) const;

  const NavMeshBuildStats& GetStats() const override { return m_stats; }

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
  NavMeshBuildStats m_stats;
};

class NavigationWorld : public INavigationMesh {
public:
  void ClearSources();
  void RegisterSource(const NavSourceInstance& source);
  bool UnregisterSource(unsigned int entityId);
  bool UpdateSourceTransform(unsigned int entityId, const XMATRIX44& worldTransform);
  void MarkDirty() { m_dirty = true; }
  bool IsDirty() const { return m_dirty; }

  void SetBuildSettings(const NavMeshBuildSettings& settings);
  const NavMeshBuildSettings& GetBuildSettings() const { return m_settings; }

  bool Rebuild(std::string* error = nullptr);

  bool IsReady() const override { return m_navMesh.IsReady(); }
  const NavMeshBuildStats& GetStats() const override { return m_navMesh.GetStats(); }
  bool ProjectPoint(const XVECTOR3& point,
                    XVECTOR3& outPoint,
                    const XVECTOR3& queryExtents = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f),
                    std::string* error = nullptr) const override {
    return m_navMesh.ProjectPoint(point, outPoint, queryExtents, error);
  }
  NavPathResult FindPath(const NavPathRequest& request) const override { return m_navMesh.FindPath(request); }
  void FindPaths(const std::vector<NavPathRequest>& requests,
                 std::vector<NavPathResult>& outResults) const override { m_navMesh.FindPaths(requests, outResults); }

  const NavMesh& GetNavMesh() const { return m_navMesh; }
  NavMesh& GetNavMesh() { return m_navMesh; }
  const NavSourceBuildStats& GetLastSourceStats() const { return m_lastSourceStats; }
  const std::vector<NavSourceInstance>& GetSources() const { return m_sources; }

private:
  std::vector<NavSourceInstance> m_sources;
  NavMeshBuildSettings m_settings;
  NavMesh m_navMesh;
  NavSourceBuildStats m_lastSourceStats;
  bool m_dirty = true;
};

bool BuildGeometryFromXDataBase(const xF::XDataBase& database, NavMeshGeometry& outGeometry, std::string* error = nullptr);
bool AppendGeometryFromXDataBase(const xF::XDataBase& database,
                                 const XMATRIX44& worldTransform,
                                 NavMeshGeometry& outGeometry,
                                 std::string* error = nullptr);
bool BuildGeometryFromPrimitiveInstances(const PrimitiveInst* instances,
                                         int instanceCount,
                                         NavMeshGeometry& outGeometry,
                                         NavSourceBuildStats* stats = nullptr,
                                         std::string* error = nullptr);

} // namespace navigation
} // namespace t850
