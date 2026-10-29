#include <pch.h>

#include <simulation/FluidDebugRenderer.h>

#include <scene/RenderMesh.h>
#include <simulation/FluidSimulation.h>
#include <utils/Log.h>
#include <utils/XDataBase.h>

#include <algorithm>
#include <cmath>

namespace t850 {
  extern Device* T8Device;
  extern DeviceContext* T8DeviceContext;

namespace {

  // Voxel placement uses the same step as FluidSimulation::UpdateWallMaskForBody, so a
  // drawn body voxel is exactly a cell the solver treats as solid.
  struct Grid {
    uint32_t w = 0, h = 0, d = 0;
    float minX = 0.0f, minY = 0.0f, minZ = 0.0f;
    float stepX = 1.0f, stepY = 1.0f, stepZ = 1.0f;

    size_t Index(uint32_t i, uint32_t j, uint32_t k) const {
      return i + static_cast<size_t>(w) * (j + static_cast<size_t>(h) * k);
    }
  };

  Grid MakeGrid(const FluidSettings& s) {
    Grid g;
    g.w = s.width;
    g.h = s.height;
    g.d = s.depth;
    g.minX = s.basinMinX;
    g.minY = s.basinMinY;
    g.minZ = s.basinMinZ;
    g.stepX = (s.basinMaxX - s.basinMinX) / static_cast<float>(s.width);
    g.stepY = (s.basinMaxY - s.basinMinY) / static_cast<float>(s.height);
    g.stepZ = (s.basinMaxZ - s.basinMinZ) / static_cast<float>(s.depth);
    return g;
  }

  unsigned PushVertex(std::vector<float>& vertices, float x, float y, float z) {
    const unsigned index = static_cast<unsigned>(vertices.size() / 4);
    vertices.push_back(x);
    vertices.push_back(y);
    vertices.push_back(z);
    vertices.push_back(1.0f);
    return index;
  }

  void AppendLine(std::vector<float>& vertices, std::vector<unsigned int>& indices,
                  float x0, float y0, float z0, float x1, float y1, float z1) {
    indices.push_back(PushVertex(vertices, x0, y0, z0));
    indices.push_back(PushVertex(vertices, x1, y1, z1));
  }

  void AppendBox(std::vector<float>& vertices, std::vector<unsigned int>& indices,
                 float x0, float y0, float z0, float x1, float y1, float z1) {
    const unsigned b = static_cast<unsigned>(vertices.size() / 4);
    for (int c = 0; c < 8; ++c)
      PushVertex(vertices, (c & 1) ? x1 : x0, (c & 2) ? y1 : y0, (c & 4) ? z1 : z0);
    static const unsigned kEdges[24] = {0, 1, 2, 3, 4, 5, 6, 7,   // along X
                                        0, 2, 1, 3, 4, 6, 5, 7,   // along Y
                                        0, 4, 1, 5, 2, 6, 3, 7};  // along Z
    for (unsigned e : kEdges) indices.push_back(b + e);
  }

  float Saturate(float v) { return (std::min)((std::max)(v, 0.0f), 1.0f); }

} // namespace

void FluidDebugRenderer::LineBatch::Release() {
  if (vb) { vb->release(); vb = nullptr; }
  if (ib) { ib->release(); ib = nullptr; }
  vertexCapacity = 0;
  indexCapacity = 0;
  indexCount = 0;
}

bool FluidDebugRenderer::LineBatch::Upload() {
  const unsigned vertexCount = static_cast<unsigned>(vertices.size() / 4);
  const unsigned count = static_cast<unsigned>(indices.size());
  indexCount = 0;
  if (vertexCount == 0 || count == 0) return true;

  if (!vb || !ib || vertexCount > vertexCapacity || count > indexCapacity) {
    Release();
    // Headroom so a growing splash does not recreate the buffers every frame.
    const unsigned newVertexCapacity = vertexCount + vertexCount / 2;
    const unsigned newIndexCapacity = count + count / 2;
    vertices.resize(static_cast<size_t>(newVertexCapacity) * 4, 0.0f);
    indices.resize(newIndexCapacity, 0u);
    vb = LineRenderer::CreatePositionVB(vertices.data(), newVertexCapacity, BufferUsage::DINAMIC);
    BufferDesc indexDesc;
    indexDesc.byteWidth = static_cast<int>(sizeof(unsigned int) * newIndexCapacity);
    indexDesc.usage = BufferUsage::DINAMIC;
    ib = T8Device ? static_cast<IndexBuffer*>(T8Device->CreateBuffer(BufferType::INDEX, indexDesc, indices.data()))
                  : nullptr;
    if (!vb || !ib) {
      T8_LOG_ERROR("[FluidDebug] Failed to create line buffers (%u vertices, %u indices)", newVertexCapacity, newIndexCapacity);
      Release();
      return false;
    }
    vertexCapacity = newVertexCapacity;
    indexCapacity = newIndexCapacity;
    indexCount = count;
    return true;
  }

  if (!T8DeviceContext) return false;
  vertices.resize(static_cast<size_t>(vertexCapacity) * 4, 0.0f);
  indices.resize(indexCapacity, 0u);
  vb->UpdateFromBuffer(*T8DeviceContext, vertices.data());
  ib->UpdateFromBuffer(*T8DeviceContext, indices.data());
  indexCount = count;
  return true;
}

bool FluidDebugRenderer::Create() {
  return m_lineRenderer.Create();
}

void FluidDebugRenderer::Destroy() {
  for (LineBatch& batch : m_batches) batch.Release();
  m_lineRenderer.Destroy();
  m_meshPositions.clear();
  m_meshUV.clear();
  m_meshEdges.clear();
}

bool FluidDebugRenderer::SetWaterMesh(const RenderMesh& mesh) {
  m_meshPositions.clear();
  m_meshUV.clear();
  m_meshEdges.clear();
  if (!mesh.xFile || mesh.xFile->XMeshDataBase.empty() || !mesh.xFile->XMeshDataBase[0]) return false;

  std::vector<uint64_t> edges;
  for (const xF::xMeshGeometry& geom : mesh.xFile->XMeshDataBase[0]->Geometry) {
    const std::vector<XVECTOR2>& uv = geom.TexCoordinates[0];
    if (geom.Positions.empty() || uv.size() != geom.Positions.size()) continue;
    const uint32_t base = static_cast<uint32_t>(m_meshPositions.size() / 3);
    for (size_t v = 0; v < geom.Positions.size(); ++v) {
      m_meshPositions.push_back(geom.Positions[v].x);
      m_meshPositions.push_back(geom.Positions[v].y);
      m_meshPositions.push_back(geom.Positions[v].z);
      m_meshUV.push_back(uv[v].x);
      m_meshUV.push_back(uv[v].y);
    }
    auto addEdge = [&](uint32_t a, uint32_t b) {
      a += base;
      b += base;
      edges.push_back((static_cast<uint64_t>((std::min)(a, b)) << 32) | (std::max)(a, b));
    };
    auto addTriangles = [&](const auto& tris) {
      for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        addEdge(tris[t], tris[t + 1]);
        addEdge(tris[t + 1], tris[t + 2]);
        addEdge(tris[t + 2], tris[t]);
      }
    };
    if (geom.Indices32Bit) addTriangles(geom.Triangles32);
    else addTriangles(geom.Triangles);
  }
  std::sort(edges.begin(), edges.end());
  edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
  m_meshEdges.reserve(edges.size() * 2);
  for (uint64_t e : edges) {
    m_meshEdges.push_back(static_cast<unsigned int>(e >> 32));
    m_meshEdges.push_back(static_cast<unsigned int>(e & 0xFFFFFFFFu));
  }
  T8_LOG_INFO("[FluidDebug] Water mesh: %zu vertices, %zu unique edges",
              m_meshPositions.size() / 3, edges.size());
  return !m_meshEdges.empty();
}

void FluidDebugRenderer::Draw(BaseDriver* driver, FluidSimulation& fluid, const FluidDebugView& view,
                              const XMATRIX44& meshWorld, const XMATRIX44& viewProjection, float farPlane,
                              Texture* sceneDepth, int viewWidth, int viewHeight) {
  m_stats = FluidDebugStats{};
  if (!driver || !IsReady() || !fluid.IsReady() || !view.Any()) return;
  for (LineBatch& batch : m_batches) batch.Clear();

  const FluidSettings& s = fluid.Settings();
  const Grid g = MakeGrid(s);
  const size_t voxels = static_cast<size_t>(g.w) * g.h * g.d;

  const bool wantSurface = view.meshWireframe || view.surfaceGrid;
  const bool wantMask = view.liquidVoxels || view.partialCells || view.solidVoxels || view.velocitySlice;
  const bool wantAlpha = view.liquidVoxels || view.partialCells || view.velocitySlice;
  const bool haveSurface = wantSurface && fluid.ReadSurface(driver, m_surface) &&
                           m_surface.size() == static_cast<size_t>(g.w) * g.d * 4;
  const bool haveMask = wantMask && fluid.ReadWallMask(driver, m_mask) && m_mask.size() == voxels;
  const bool haveAlpha = wantAlpha && fluid.ReadVolumeFraction(driver, m_alpha) && m_alpha.size() == voxels;
  const bool haveVelocity = view.velocitySlice && fluid.ReadVelocity(driver, m_velocity) &&
                            m_velocity.size() == voxels * 4;

  auto surfaceAt = [&](int x, int z) {
    x = (std::min)((std::max)(x, 0), static_cast<int>(g.w) - 1);
    z = (std::min)((std::max)(z, 0), static_cast<int>(g.d) - 1);
    return m_surface[(static_cast<size_t>(x) + static_cast<size_t>(g.w) * z) * 4];
  };

  if (view.meshWireframe && haveSurface && !m_meshEdges.empty()) {
    // Same reconstruction as VS_Mesh's FLUID_SURFACE block: bilinear over texel centres,
    // scaled by the presentation displacement, plus the optional ripple octaves.
    LineBatch& batch = m_batches[kMesh];
    const size_t count = m_meshPositions.size() / 3;
    batch.vertices.reserve(count * 4);
    const int maxBaseX = (std::max)(static_cast<int>(g.w) - 2, 0);
    const int maxBaseZ = (std::max)(static_cast<int>(g.d) - 2, 0);
    for (size_t v = 0; v < count; ++v) {
      const float x = m_meshPositions[v * 3 + 0];
      const float z = m_meshPositions[v * 3 + 2];
      const float gx = Saturate(m_meshUV[v * 2 + 0]) * static_cast<float>(g.w) - 0.5f;
      const float gz = Saturate(m_meshUV[v * 2 + 1]) * static_cast<float>(g.d) - 0.5f;
      const int bx = (std::min)((std::max)(static_cast<int>(std::floor(gx)), 0), maxBaseX);
      const int bz = (std::min)((std::max)(static_cast<int>(std::floor(gz)), 0), maxBaseZ);
      const float fx = Saturate(gx - static_cast<float>(bx));
      const float fz = Saturate(gz - static_cast<float>(bz));
      const float h0 = surfaceAt(bx, bz) + (surfaceAt(bx + 1, bz) - surfaceAt(bx, bz)) * fx;
      const float h1 = surfaceAt(bx, bz + 1) + (surfaceAt(bx + 1, bz + 1) - surfaceAt(bx, bz + 1)) * fx;
      float y = m_meshPositions[v * 3 + 1] + (h0 + (h1 - h0) * fz) * view.displacementScale;
      if (view.rippleOctaves) {
        const float t = view.rippleTime;
        y += view.rippleScale *
             (0.0160f * std::sin((x * 0.86f + z * 0.51f) * 10.134f + t * 5.573f) +
              0.0090f * std::sin((x * -0.60f + z * 0.80f) * 15.322f + t * 11.492f) +
              0.0060f * std::sin((x * 0.31f + z * -0.95f) * 23.271f + t * 24.434f));
      }
      PushVertex(batch.vertices, x, y, z);
    }
    batch.indices = m_meshEdges;
    m_stats.meshLines = static_cast<uint32_t>(m_meshEdges.size() / 2);
  }

  if (view.surfaceGrid && haveSurface) {
    LineBatch& batch = m_batches[kGrid];
    for (uint32_t k = 0; k < g.d; ++k)
      for (uint32_t i = 0; i < g.w; ++i) {
        const float y = s.waterPlaneY + surfaceAt(static_cast<int>(i), static_cast<int>(k)) * view.displacementScale;
        PushVertex(batch.vertices, g.minX + (i + 0.5f) * g.stepX, y, g.minZ + (k + 0.5f) * g.stepZ);
      }
    for (uint32_t k = 0; k < g.d; ++k)
      for (uint32_t i = 0; i < g.w; ++i) {
        const unsigned index = i + g.w * k;
        if (i + 1 < g.w) { batch.indices.push_back(index); batch.indices.push_back(index + 1); }
        if (k + 1 < g.d) { batch.indices.push_back(index); batch.indices.push_back(index + g.w); }
      }
  }

  auto solid = [&](uint32_t i, uint32_t j, uint32_t k) { return m_mask[g.Index(i, j, k)] != 0u; };

  if ((view.liquidVoxels || view.partialCells) && haveMask && haveAlpha) {
    for (uint32_t k = 0; k < g.d; ++k)
      for (uint32_t j = 0; j < g.h; ++j)
        for (uint32_t i = 0; i < g.w; ++i) {
          if (solid(i, j, k)) continue;
          const float a = m_alpha[g.Index(i, j, k)];
          const float x0 = g.minX + i * g.stepX, y0 = g.minY + j * g.stepY, z0 = g.minZ + k * g.stepZ;
          if (view.liquidVoxels && a >= s.airThreshold) {
            const bool airAbove = j + 1 == g.h ||
                (!solid(i, j + 1, k) && m_alpha[g.Index(i, j + 1, k)] < s.airThreshold);
            if (airAbove) {
              AppendBox(m_batches[kLiquid].vertices, m_batches[kLiquid].indices,
                        x0, y0, z0, x0 + g.stepX, y0 + g.stepY, z0 + g.stepZ);
              ++m_stats.liquidVoxels;
            }
          }
          if (view.partialCells && a > 0.02f && a < 0.98f) {
            AppendBox(m_batches[kPartial].vertices, m_batches[kPartial].indices,
                      x0, y0, z0, x0 + g.stepX, y0 + a * g.stepY, z0 + g.stepZ);
            ++m_stats.partialCells;
          }
        }
  }

  if (view.solidVoxels && haveMask) {
    const std::vector<uint32_t>& base = fluid.BaseMask();
    const bool haveBase = base.size() == voxels;
    for (uint32_t k = 0; k < g.d; ++k)
      for (uint32_t j = 0; j < g.h; ++j)
        for (uint32_t i = 0; i < g.w; ++i) {
          if (!solid(i, j, k)) continue;
          // Only the shell that touches fluid; a solid interior adds lines and no information.
          const bool exposed = (i > 0 && !solid(i - 1, j, k)) || (i + 1 < g.w && !solid(i + 1, j, k)) ||
                               (j > 0 && !solid(i, j - 1, k)) || (j + 1 < g.h && !solid(i, j + 1, k)) ||
                               (k > 0 && !solid(i, j, k - 1)) || (k + 1 < g.d && !solid(i, j, k + 1));
          if (!exposed) continue;
          const bool body = haveBase && base[g.Index(i, j, k)] == 0u;
          LineBatch& batch = m_batches[body ? kBody : kSolid];
          const float x0 = g.minX + i * g.stepX, y0 = g.minY + j * g.stepY, z0 = g.minZ + k * g.stepZ;
          AppendBox(batch.vertices, batch.indices, x0, y0, z0, x0 + g.stepX, y0 + g.stepY, z0 + g.stepZ);
          ++(body ? m_stats.bodyVoxels : m_stats.solidVoxels);
        }
  }

  if (view.velocitySlice && haveMask && haveAlpha && haveVelocity) {
    uint32_t k = g.d / 2;
    if (view.velocitySliceZ >= 0) {
      k = (std::min)(static_cast<uint32_t>(view.velocitySliceZ), g.d - 1);
    } else if (fluid.DroppedSphere().enabled) {
      const float row = std::floor((fluid.DroppedSphere().z - g.minZ) / g.stepZ);
      k = static_cast<uint32_t>((std::min)((std::max)(row, 0.0f), static_cast<float>(g.d - 1)));
    }
    m_stats.velocitySliceZ = static_cast<int>(k);
    LineBatch& batch = m_batches[kVelocity];
    const size_t strideY = g.w;
    const size_t strideZ = static_cast<size_t>(g.w) * g.h;
    for (uint32_t j = 0; j < g.h; ++j)
      for (uint32_t i = 0; i < g.w; ++i) {
        const size_t idx = g.Index(i, j, k);
        if (m_mask[idx] != 0u || m_alpha[idx] < 0.1f) continue;
        // Staggered storage: texel (i,j,k) holds the +faces; the -face is the previous texel.
        const float u = 0.5f * (m_velocity[idx * 4 + 0] + (i > 0 ? m_velocity[(idx - 1) * 4 + 0] : 0.0f));
        const float v = 0.5f * (m_velocity[idx * 4 + 1] + (j > 0 ? m_velocity[(idx - strideY) * 4 + 1] : 0.0f));
        const float w = 0.5f * (m_velocity[idx * 4 + 2] + (k > 0 ? m_velocity[(idx - strideZ) * 4 + 2] : 0.0f));
        if (u * u + v * v + w * w < 1e-6f) continue;
        const float cx = g.minX + (i + 0.5f) * g.stepX;
        const float cy = g.minY + (j + 0.5f) * g.stepY;
        const float cz = g.minZ + (k + 0.5f) * g.stepZ;
        AppendLine(batch.vertices, batch.indices, cx, cy, cz,
                   cx + u * view.velocityScale, cy + v * view.velocityScale, cz + w * view.velocityScale);
        ++m_stats.velocityLines;
      }
  }

  if (view.domainBounds) {
    AppendBox(m_batches[kBounds].vertices, m_batches[kBounds].indices, g.minX, g.minY, g.minZ,
              g.minX + g.w * g.stepX, g.minY + g.h * g.stepY, g.minZ + g.d * g.stepZ);
  }

  // Alpha < 1 on the dense layers: at pool distance the full-resolution mesh is denser than
  // the pixels and would otherwise read as a solid sheet.
  static const XVECTOR3 kColors[kLayerCount] = {
    XVECTOR3(0.75f, 0.95f, 1.00f, 0.35f),  // mesh
    XVECTOR3(1.00f, 0.85f, 0.20f, 0.90f),  // surface grid
    XVECTOR3(0.20f, 0.55f, 1.00f, 0.80f),  // liquid voxels
    XVECTOR3(0.20f, 1.00f, 0.80f, 0.60f),  // partial cells
    XVECTOR3(0.65f, 0.65f, 0.65f, 0.70f),  // static solids
    XVECTOR3(1.00f, 0.50f, 0.10f, 1.00f),  // body
    XVECTOR3(1.00f, 0.20f, 0.85f, 1.00f),  // velocity
    XVECTOR3(0.30f, 1.00f, 0.30f, 1.00f),  // bounds
  };

  XMATRIX44 identity;
  identity.Identity();
  driver->SetDepthStencilState(BaseDriver::NONE);
  driver->SetBlendState(BaseDriver::ALPHA_BLEND);
  m_lineRenderer.SetDepthTestEnabled(!view.xray && sceneDepth != nullptr);
  m_lineRenderer.SetDepthTexture(sceneDepth);
  m_lineRenderer.SetSecondaryDepthTexture(nullptr);
  m_lineRenderer.SetViewport(viewWidth, viewHeight);
  m_lineRenderer.SetFarPlane(farPlane);
  m_lineRenderer.SetDepthBias(0.02f);
  for (int layer = 0; layer < kLayerCount; ++layer) {
    LineBatch& batch = m_batches[layer];
    if (!batch.Upload() || batch.indexCount == 0) continue;
    m_lineRenderer.DrawLines(layer == kMesh ? meshWorld : identity, viewProjection, kColors[layer],
                             batch.vb, batch.ib, batch.indexCount, sizeof(float) * 4, IndexBufferFormat::R32);
  }
  driver->SetBlendState(BaseDriver::BLEND_DEFAULT);
}

} // namespace t850
