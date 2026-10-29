#pragma once

#include <scene/LineRenderer.h>
#include <utils/xMaths.h>

#include <cstdint>
#include <vector>

namespace t850 {

  class FluidSimulation;
  class RenderMesh;
  class BaseDriver;

  // What the fluid debug overlay draws. Every layer is read back from the solver each
  // frame it is enabled, so it shows the simulation itself, not the shaded result.
  struct FluidDebugView {
    bool meshWireframe = false;   // water render mesh, displaced exactly as VS_Mesh does
    bool surfaceGrid = false;     // one vertex per surface texel: what the solver resolves
    bool liquidVoxels = false;    // liquid cells (alpha >= 0.5) with air above: the voxel surface
    bool partialCells = false;    // interface cells, box height = alpha * cell
    bool solidVoxels = false;     // wall-mask cells touching fluid; the moving body separately
    bool velocitySlice = false;   // cell-centred velocity on one vertical XY slice
    bool velocityVolume = false;  // velocity arrows for every liquid cell in the 3D volume
    bool domainBounds = false;
    bool xray = false;            // ignore scene depth
    float velocityScale = 0.25f;  // metres of line per m/s
    int velocitySliceZ = -1;      // grid row; -1 follows the dropped body
    int volumeSampling = 0;       // one arrow every N cells per axis; 0 picks N to fit the cap
    float minSpeed = 0.005f;      // m/s; slower cells get no arrow (still-pool noise is ~2 mm/s)
    // Presentation values the vertex shader applies, so the wireframe matches the mesh.
    float displacementScale = 1.0f;
    bool rippleOctaves = false;
    float rippleScale = 1.0f;
    float rippleTime = 0.0f;

    bool Any() const {
      return meshWireframe || surfaceGrid || liquidVoxels || partialCells ||
             solidVoxels || velocitySlice || velocityVolume || domainBounds;
    }
  };

  struct FluidDebugStats {
    uint32_t meshLines = 0;
    // Candidates per layer, and how many were drawn (layers are capped, see kMaxBoxesPerLayer).
    uint32_t liquidVoxels = 0, liquidDrawn = 0;
    uint32_t partialCells = 0, partialDrawn = 0;
    uint32_t solidVoxels = 0, solidDrawn = 0;
    uint32_t bodyVoxels = 0, bodyDrawn = 0;
    uint32_t velocityLines = 0;
    int velocitySliceZ = -1;
    // Whole-volume velocity: liquid cells, those at or above minSpeed, arrows drawn, sampling step.
    uint32_t volumeLiquidCells = 0;
    uint32_t volumeMoving = 0;
    uint32_t volumeArrows = 0;
    uint32_t volumeStep = 0;
    float volumeMaxSpeed = 0.0f;
    int volumeMaxCell[3] = {-1, -1, -1};
    float volumeMaxAlpha = 0.0f;
    // Liquid cells with no air neighbour; their alpha should stay 1.
    uint32_t bulkCells = 0;
    uint32_t bulkBelow98 = 0;
    float bulkMeanAlpha = 0.0f;
    float bulkMinAlpha = 0.0f;
  };

  class FluidDebugRenderer {
  public:
    bool Create();
    void Destroy();
    bool IsReady() const { return m_lineRenderer.IsReady(); }

    // Captures the water mesh's CPU positions, UV0 and unique edges once.
    bool SetWaterMesh(const RenderMesh& mesh);

    void Draw(BaseDriver* driver, FluidSimulation& fluid, const FluidDebugView& view,
              const XMATRIX44& meshWorld, const XMATRIX44& viewProjection, float farPlane,
              Texture* sceneDepth, int viewWidth, int viewHeight);

    const FluidDebugStats& Stats() const { return m_stats; }

  private:
    struct LineBatch {
      VertexBuffer* vb = nullptr;
      IndexBuffer* ib = nullptr;
      unsigned vertexCapacity = 0;
      unsigned indexCapacity = 0;
      unsigned indexCount = 0;
      std::vector<float> vertices;        // xyzw
      std::vector<unsigned int> indices;  // line list

      void Clear() { vertices.clear(); indices.clear(); }
      void Release();
      bool Upload();
    };

    enum Layer { kMesh, kGrid, kLiquid, kPartial, kSolid, kBody, kVelocity,
                 kVolumeSlow, kVolumeMedium, kVolumeFast, kBounds, kLayerCount };

    // D3D12 streams dynamic vertex buffers through a 16 MB per-frame ring shared with the
    // constant buffers; uncapped voxel layers (one box per water cell) overflowed it.
    static constexpr size_t kMaxBoxesPerLayer = 8000;
    static constexpr size_t kMaxVolumeArrows = 15000;
    struct Box { float x0, y0, z0, x1, y1, z1; };
    struct Arrow { uint16_t i, j, k; float u, v, w; };
    static uint32_t EmitBoxes(LineBatch& batch, const std::vector<Box>& boxes);
    std::vector<Box> m_boxScratch[4];
    std::vector<Arrow> m_arrowScratch;

    LineRenderer m_lineRenderer;
    LineBatch m_batches[kLayerCount];
    FluidDebugStats m_stats;

    std::vector<float> m_meshPositions;  // xyz per vertex, object space
    std::vector<float> m_meshUV;         // uv0 per vertex
    std::vector<unsigned int> m_meshEdges;

    std::vector<float> m_surface;
    std::vector<float> m_alpha;
    std::vector<float> m_velocity;
    std::vector<uint32_t> m_mask;
  };

} // namespace t850
