#include <pch.h>

#include <simulation/PbfFluid.h>
#include <simulation/PoolLayoutJson.h>

#include <utils/ComputeKernelRegistry.h>
#include <utils/Log.h>
#include <utils/ThreadPool.h>
#include <scene/ParticleSystem.h>
#include <utils/ResourceLocator.h>

#include <glaze/glaze.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

namespace t850 {

namespace {

  // Keep byte-for-byte in sync with `cbuffer PbfConstants` in CS_Pbf*.hlsl.
  struct PbfConstants {
    uint32_t particleCount;
    uint32_t gridX;
    uint32_t gridY;
    uint32_t gridZ;
    float deltaTime;
    float smoothingRadius;
    float particleRadius;
    float restDensity;
    float stiffnessEps;
    float artPressureK;
    float artPressureN;
    float viscosityC;
    float gravity;
    float maxVelocity;
    float gridOriginX;
    float gridOriginY;
    float gridOriginZ;
    float basinMinX;
    float basinMinY;
    float basinMinZ;
    float basinMaxX;
    float basinMaxY;
    float basinMaxZ;
    uint32_t boxCount;
    uint32_t iteration;
    uint32_t surfaceWidth;
    uint32_t surfaceDepth;
    float waterPlaneY;
    float restScale;            // rho_lattice: kernel normalisation
    float restDenomReference;   // sum |gradW|^2 for a full lattice (CFM scale)
    float minNeighbours;        // density estimates below this are ignored
    float wallTopY;             // solid walls end here (the pool rim); open above
    float windX;
    float windZ;
    float windStrength;
    // No-slip boundary coefficient; reuses the old Pad7 slot, so the ABI size is unchanged.
    float wallFriction;
  };
  static_assert(sizeof(PbfConstants) == 144, "PbfConstants must be 32 DWORDs (multiple of 4)");

  // Keep byte-for-byte in sync with `cbuffer PbfAnisotropyConstants` in CS_PbfAnisotropy.hlsl.
  struct PbfAnisotropyConstants {
    uint32_t particleCount;
    uint32_t gridX;
    uint32_t gridY;
    uint32_t gridZ;
    float gridOriginX;
    float gridOriginY;
    float gridOriginZ;
    float cellSize;
    float spacing;
    float smoothing;
    float maxRatio;
    float radiusScale;
    float sparseScale;
    float fullNeighbours;
    float sparseNeighbours;
    float maxStretch;
  };
  static_assert(sizeof(PbfAnisotropyConstants) == 64, "PbfAnisotropyConstants must be 16 DWORDs");
  // Three float4 per particle: centre + bound, quadric row 0, quadric rest + flags.
  constexpr uint32_t kAnisotropyFloat4PerParticle = 3;

  // Must equal PBFCellCapacity in the kernels.
  constexpr uint32_t kCellCapacity = 64;
  constexpr uint32_t kWordsPerParticle = 4;
  constexpr uint32_t kThreads = 64;

  uint32_t DivUp(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

}  // namespace

PbfFluid::PbfFluid() = default;
PbfFluid::~PbfFluid() = default;

std::unique_ptr<ComputePipeline> PbfFluid::CreatePipeline(BaseDriver* driver, const char* source) {
  const ComputeKernelDefinition* kernel = FindComputeKernel(source);
  if (!kernel) {
    T8_LOG_ERROR("[PBF] Kernel '%s' is not registered in compute_kernels.json", source);
    return {};
  }
  ComputePipelineDesc desc;
  desc.debugName = kernel->sourceName;
  desc.entryPoint = kernel->entryPoint;
  desc.permutationName = "base";
  desc.bindings = kernel->bindings;
  const std::string sourcePath = "Shaders/" + kernel->sourceName;
  if (!ResourceLocator::Instance().ReadText(sourcePath, desc.source)) {
    T8_LOG_ERROR("[PBF] Cannot read kernel source '%s'", sourcePath.c_str());
    return {};
  }
  return driver->CreateComputePipeline(desc);
}

bool PbfFluid::LoadPoolLayout(const char* path) {
  m_solidBoxes.clear();
  if (!path) return false;
  std::string json;
  if (!ResourceLocator::Instance().ReadText(path, json)) {
    T8_LOG_ERROR("[PBF] No pool layout at '%s'", path);
    return false;
  }
  LayoutJson layout;
  auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(layout, json);
  if (ec) {
    T8_LOG_ERROR("[PBF] Cannot parse pool layout '%s'", path);
    return false;
  }
  // Layout Z lives on the negative side and Blender emits maxY/minY as world Z.
  const double floorY = -std::abs(layout.pool.depth);
  m_settings.basinMinX = static_cast<float>(layout.pool.minX);
  m_settings.basinMaxX = static_cast<float>(layout.pool.maxX);
  m_settings.basinMinZ = static_cast<float>(layout.pool.minY);
  m_settings.basinMaxZ = static_cast<float>(layout.pool.maxY);
  m_settings.basinMinY = static_cast<float>(floorY);
  m_settings.waterPlaneY = static_cast<float>(layout.waterPlaneY);
  m_settings.copingTopY = static_cast<float>(layout.waterPlaneY + std::max(layout.pool.copingHeight, 0.0));
  // The solid rim is the wall (top at the authored water line) plus the coping stone on it:
  // Backyard_Pool.glb has the walls at y 0.00 and the coping top at y 0.08. `freeboard` is the
  // VOF domain's air headroom, NOT a wall height: using it put an invisible wall 0.30 m above
  // the coping, water climbed it in every slosh and rendered as blobs floating over the rim.
  m_settings.wallTopY = static_cast<float>(layout.waterLine + std::max(layout.pool.copingHeight, 0.0));

  // Steps are authored as {minX,maxX,minY,maxY,minZ,maxZ}: X, world Y, world Z.
  for (const LayoutStepJson& step : layout.pool.steps) {
    const float minX = static_cast<float>(std::min(step.minX, step.maxX));
    const float maxX = static_cast<float>(std::max(step.minX, step.maxX));
    const float minY = static_cast<float>(std::min(step.minY, step.maxY));
    const float maxY = static_cast<float>(std::max(step.minY, step.maxY));
    const float minZ = static_cast<float>(std::min(step.minZ, step.maxZ));
    const float maxZ = static_cast<float>(std::max(step.minZ, step.maxZ));
    m_solidBoxes.insert(m_solidBoxes.end(), {minX, minY, minZ, maxX, maxY, maxZ, 0.0f, 0.0f});
  }
  T8_LOG_INFO("[PBF] Pool basin X %.2f..%.2f  Y %.2f..%.2f  Z %.2f..%.2f m, plane %.3f, coping %.3f, "
              "wall top %.3f, %zu step box(es)",
              m_settings.basinMinX, m_settings.basinMaxX, m_settings.basinMinY, m_settings.basinMaxY,
              m_settings.basinMinZ, m_settings.basinMaxZ, m_settings.waterPlaneY, m_settings.copingTopY,
              m_settings.wallTopY, m_solidBoxes.size() / 8);
  return true;
}

bool PbfFluid::CreatePipelines(BaseDriver* driver) {
  m_integrate = CreatePipeline(driver, "CS_PbfIntegrate.hlsl");
  m_clearGrid = CreatePipeline(driver, "CS_PbfClearGrid.hlsl");
  m_buildGrid = CreatePipeline(driver, "CS_PbfBuildGrid.hlsl");
  m_density = CreatePipeline(driver, "CS_PbfDensity.hlsl");
  m_solve = CreatePipeline(driver, "CS_PbfSolve.hlsl");
  m_updateVelocity = CreatePipeline(driver, "CS_PbfUpdateVelocity.hlsl");
  m_surface = CreatePipeline(driver, "CS_PbfSurface.hlsl");
  m_clearHeights = CreatePipeline(driver, "CS_PbfClearHeights.hlsl");
  m_cull = CreatePipeline(driver, "CS_PbfCull.hlsl");
  m_copyFields = CreatePipeline(driver, "CS_PbfCopyFields.hlsl");
  // Optional: without it the renderer falls back to spheres, the solver is unaffected.
  m_anisotropyPipeline = CreatePipeline(driver, "CS_PbfAnisotropy.hlsl");
  if (!m_anisotropyPipeline)
    T8_LOG_ERROR("[PBF] Anisotropy pipeline unavailable; the fluid renders as spheres");
  if (!m_integrate || !m_clearGrid || !m_buildGrid || !m_density || !m_solve || !m_updateVelocity ||
      !m_surface || !m_clearHeights || !m_cull || !m_copyFields) {
    T8_LOG_ERROR("[PBF] Solver pipelines are incomplete (integrate=%d clearGrid=%d buildGrid=%d density=%d "
                 "solve=%d updateVelocity=%d surface=%d clearHeights=%d)",
                 m_integrate ? 1 : 0, m_clearGrid ? 1 : 0, m_buildGrid ? 1 : 0, m_density ? 1 : 0,
                 m_solve ? 1 : 0, m_updateVelocity ? 1 : 0, m_surface ? 1 : 0, m_clearHeights ? 1 : 0);
    return false;
  }
  return true;
}

bool PbfFluid::CreateFieldBuffers(BaseDriver* driver) {
  const uint32_t cells = m_gridX * m_gridY * m_gridZ;

  ComputeBufferDesc gridDesc;
  gridDesc.structureStride = sizeof(uint32_t);
  gridDesc.byteWidth = cells * kCellCapacity * 2u * sizeof(uint32_t);
  gridDesc.access = ComputeBufferAccess::ReadWrite;
  gridDesc.debugName = "PBF Neighbour Grid";
  m_grid = driver->CreateComputeBuffer(gridDesc, nullptr);

  ComputeBufferDesc boxDesc;
  boxDesc.structureStride = 8 * sizeof(float);
  boxDesc.byteWidth = static_cast<uint32_t>((std::max)(m_solidBoxes.size(), size_t(8))) * sizeof(float);
  boxDesc.access = ComputeBufferAccess::ReadOnly;
  boxDesc.debugName = "PBF Solid Boxes";
  std::vector<float> boxData((std::max)(m_solidBoxes.size(), size_t(8)), 0.0f);
  if (!m_solidBoxes.empty()) std::copy(m_solidBoxes.begin(), m_solidBoxes.end(), boxData.begin());
  m_solidBoxBuffer = driver->CreateComputeBuffer(boxDesc, boxData.data());

  ComputeBufferDesc heightDesc;
  heightDesc.structureStride = sizeof(uint32_t);
  // Two words per texel (weighted height sum, weight sum) for the kernel splat.
  heightDesc.byteWidth = m_settings.surfaceWidth * m_settings.surfaceDepth * 2u * sizeof(uint32_t);
  heightDesc.access = ComputeBufferAccess::ReadWrite;
  heightDesc.debugName = "PBF Column Heights";
  m_columnHeights = driver->CreateComputeBuffer(heightDesc, nullptr);


  if (!m_grid || !m_solidBoxBuffer || !m_columnHeights) {
    T8_LOG_ERROR("[PBF] Cannot create the fixed field buffers");
    return false;
  }
  T8_LOG_INFO("[PBF] Neighbour grid %ux%ux%u = %u cells, %u index slots per cell (%.1f MB)",
              m_gridX, m_gridY, m_gridZ, cells, kCellCapacity, gridDesc.byteWidth / 1048576.0);
  return true;
}

std::vector<uint32_t> PbfFluid::BuildConstants(uint32_t iteration) const {
  PbfConstants c{};
  c.particleCount = m_particleCount;
  c.gridX = m_gridX;
  c.gridY = m_gridY;
  c.gridZ = m_gridZ;
  c.deltaTime = (std::max)(m_settings.deltaTime / static_cast<float>((std::max)(m_settings.substeps, 1u)), 1e-5f);
  c.smoothingRadius = m_settings.smoothingRadius;
  c.particleRadius = m_settings.particleRadius;
  c.restDensity = m_settings.restDensity;
  c.stiffnessEps = m_settings.stiffnessEps;
  c.artPressureK = m_settings.artPressureK;
  c.artPressureN = m_settings.artPressureN;
  c.viscosityC = m_settings.viscosityC;
  c.gravity = m_settings.gravity;
  c.maxVelocity = m_settings.maxVelocity;
  c.gridOriginX = m_gridOriginX;
  c.gridOriginY = m_gridOriginY;
  c.gridOriginZ = m_gridOriginZ;
  c.basinMinX = m_settings.basinMinX;
  c.basinMinY = m_settings.basinMinY;
  c.basinMinZ = m_settings.basinMinZ;
  c.basinMaxX = m_settings.basinMaxX;
  c.basinMaxY = m_settings.basinMaxY;
  c.basinMaxZ = m_settings.basinMaxZ;
  c.boxCount = static_cast<uint32_t>(m_solidBoxes.size() / 8);
  c.iteration = iteration;
  c.surfaceWidth = m_settings.surfaceWidth;
  c.surfaceDepth = m_settings.surfaceDepth;
  c.waterPlaneY = m_settings.waterPlaneY;
  c.restScale = m_restScale;
  c.restDenomReference = m_restDenomReference;
  c.minNeighbours = m_minNeighbours;
  c.wallTopY = m_settings.wallTopY;
  c.windX = m_settings.windX;
  c.windZ = m_settings.windZ;
  c.windStrength = m_settings.windStrength;
  c.wallFriction = m_settings.wallFriction;
  std::vector<uint32_t> words(sizeof(PbfConstants) / 4);
  std::memcpy(words.data(), &c, sizeof(PbfConstants));
  return words;
}

bool PbfFluid::TryInitialize(BaseDriver* driver, const PbfSettings& settings, const char* layoutPath) {
  Shutdown(driver);
  if (!driver || !driver->SupportsComputeShaders()) {
    T8_LOG_INFO("[PBF] Compute is unsupported on %s; Backyard8 needs a compute backend",
                driver ? driver->ApiTag() : "none");
    return false;
  }
  m_driver = driver;
  m_settings = settings;
  if (!LoadPoolLayout(layoutPath)) {
    T8_LOG_ERROR("[PBF] The pool layout is required: the basin bounds come from it");
    return false;
  }

  // The domain extends well above the pool so the water sphere can hang in free air:
  // sphere centre + radius + a kernel radius of headroom. The TOP IS OPEN (see Collide in
  // CS_PbfSolve), so this is only the extent of the neighbour grid, not a ceiling.
  m_settings.basinMaxY = m_settings.waterPlaneY + m_settings.sphereHeight +
                         m_settings.sphereRadius + 2.0f * m_settings.smoothingRadius;

  // Neighbour grid cell = h. With h = 2 x spacing a cell holds ~(h/s)^3 = 8 particles at
  // rest, well inside the 64-slot bucket. (h/2 was a workaround for h = 4 x spacing, where
  // a cell held ~125 and overflowed.)
  const float h = m_settings.smoothingRadius;
  m_gridOriginX = m_settings.basinMinX - m_settings.smoothingRadius;
  m_gridOriginY = m_settings.basinMinY - m_settings.smoothingRadius;
  m_gridOriginZ = m_settings.basinMinZ - m_settings.smoothingRadius;
  m_gridX = static_cast<uint32_t>(std::ceil((m_settings.basinMaxX - m_settings.basinMinX) / h)) + 6u;
  m_gridY = static_cast<uint32_t>(std::ceil((m_settings.basinMaxY - m_settings.basinMinY) / h)) + 6u;
  m_gridZ = static_cast<uint32_t>(std::ceil((m_settings.basinMaxZ - m_settings.basinMinZ) / h)) + 6u;
  const uint64_t gridBytes = static_cast<uint64_t>(m_gridX) * m_gridY * m_gridZ * kCellCapacity * 2ull * 4ull;
  if (gridBytes > (256ull << 20)) {
    T8_LOG_ERROR("[PBF] Neighbour grid would need %llu MB; increase the smoothing radius",
                 static_cast<unsigned long long>(gridBytes >> 20));
    return false;
  }

  if (!CreatePipelines(driver) || !CreateFieldBuffers(driver)) return false;
  if (!SeedParticles(driver)) return false;
  m_ready = true;
  T8_LOG_INFO("[PBF] Solver ready on %s: %u particles, %u iteration(s), %u substep(s), h=%.3f m",
              driver->ApiTag(), m_particleCount, m_settings.solverIterations, m_settings.substeps,
              m_settings.smoothingRadius);
  return true;
}

namespace {
  // Kernel-measured rest density for a uniform lattice of unit-mass particles. The PBF
  // constraint is C = rho/rho0 - 1 with rho the raw SPH kernel sum, so rho0 must be the
  // kernel density the seeding lattice actually produces -- not 1000 kg/m^3. Using a
  // physical density there made C ~ -0.65 everywhere and the solver pulled the whole pool
  // inward every iteration (particles ejected to the ceiling, speeds at the clamp).
  // Kernel density of a uniform lattice of unit-mass particles (see the normalisation note
  // in CS_PbfDensity.hlsl). Used to normalise the kernels so a lattice reads rho = 1.
  double LatticeRestDensity(double h, double spacing) {
    const double k = 315.0 / (64.0 * 3.141592653589793 * std::pow(h, 9.0));
    const int n = static_cast<int>(std::ceil(h / spacing));
    double total = 0.0;
    for (int i = -n; i <= n; ++i)
      for (int j = -n; j <= n; ++j)
        for (int z = -n; z <= n; ++z) {
          const double r2 = (static_cast<double>(i) * i + static_cast<double>(j) * j +
                             static_cast<double>(z) * z) * spacing * spacing;
          if (r2 >= h * h) continue;
          const double d = h * h - r2;
          total += k * d * d * d;
        }
    return total > 0.0 ? total : 1.0;
  }

  // Second lattice statistic used for CFM scaling: sum |gradW|^2 of a full
  // neighbourhood, in the same normalised units as the density kernel.
  double LatticeGradSq(double h, double spacing, double restScale) {
    if (h <= 0.0 || spacing <= 0.0) return 1.0;
    const double gk2 = 45.0 / (3.141592653589793 * std::pow(h, 6.0)) / (restScale > 0.0 ? restScale : 1.0);
    const int n = static_cast<int>(std::ceil(h / spacing));
    double total = 0.0;
    for (int i = -n; i <= n; ++i)
      for (int j = -n; j <= n; ++j)
        for (int z = -n; z <= n; ++z) {
          if (i == 0 && j == 0 && z == 0) continue;
          const double r2 = (static_cast<double>(i) * i + static_cast<double>(j) * j +
                             static_cast<double>(z) * z) * spacing * spacing;
          if (r2 >= h * h) continue;
          const double r = std::sqrt(r2);
          // |grad W| = (45/pi h^6)(h-r)^2. The shader forms d * (coeff/r), whose magnitude
          // is exactly this; the earlier /r here computed the radial scalar instead, so the
          // CFM reference denominator was ~1/r^2 too small.
          const double g = gk2 * (h - r) * (h - r);
          total += g * g;
        }
    return total > 0.0 ? total : 1.0;
  }

  struct SeedRelaxResult {
    int iterations = 0;
    float initialMaxConstraint = 0.0f;
    float maxConstraint = 0.0f;
  };

  // A cubic seed is a crystal: until the first impact its rows and planes line up with the
  // camera, and both renderers show them (terraces, serrated silhouettes, moire). Jitter each
  // particle inside its cell, then run the solver's own unilateral projection (eq 11 and 14 on
  // the normalised kernels, no gravity) to 0.5% below rest density, so the GPU constraint stays
  // zero in free fall. (Two-sided relaxation clumps and does not converge.) Deterministic: the
  // jitter is a hash of the particle index.
  SeedRelaxResult RelaxSeed(std::vector<float>& positions, uint32_t count, float spacing, float h,
                            float restScale, float restDensity, float denomRef, float stiffnessEps) {
    constexpr int kMaxIterations = 100;
    constexpr float kTolerance = 0.002f;
    const float targetDensity = 0.995f * restDensity;
    const float jitter = 0.8f * spacing;
    const auto hash = [](uint32_t x) {
      x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
      return x;
    };
    for (uint32_t i = 0; i < count; ++i)
      for (uint32_t a = 0; a < 3; ++a) {
        const float u = static_cast<float>(hash(i * 3u + a + 1u) >> 8) * (1.0f / 16777216.0f);
        positions[static_cast<size_t>(i) * kWordsPerParticle + a] += jitter * (u - 0.5f);
      }

    const auto parallelFor = [](uint32_t n, const auto& body) {
      if (g_threadPool) g_threadPool->ParallelFor(0, static_cast<int>(n), body);
      else for (int i = 0; i < static_cast<int>(n); ++i) body(i);
    };
    const float h2 = h * h;
    const float poly6 = static_cast<float>(315.0 / (64.0 * 3.141592653589793 * std::pow(h, 9.0))) / restScale;
    const float spiky = static_cast<float>(-45.0 / (3.141592653589793 * std::pow(h, 6.0))) / restScale;
    const float maxStep = 0.1f * spacing;   // the solver's per-iteration cap

    std::vector<float> next(positions.begin(), positions.begin() + static_cast<size_t>(count) * kWordsPerParticle);
    std::vector<float> lambda(count, 0.0f), constraint(count, 0.0f);
    std::vector<uint32_t> cellOf(count), order(count), cellStart;
    SeedRelaxResult result;
    for (int it = 0; it <= kMaxIterations; ++it) {
      float lo[3] = {positions[0], positions[1], positions[2]};
      float hi[3] = {lo[0], lo[1], lo[2]};
      for (uint32_t i = 1; i < count; ++i)
        for (int a = 0; a < 3; ++a) {
          lo[a] = (std::min)(lo[a], positions[static_cast<size_t>(i) * kWordsPerParticle + a]);
          hi[a] = (std::max)(hi[a], positions[static_cast<size_t>(i) * kWordsPerParticle + a]);
        }
      int dim[3];
      for (int a = 0; a < 3; ++a) dim[a] = static_cast<int>((hi[a] - lo[a]) / h) + 1;
      const auto cellCoord = [&](uint32_t i, int a) {
        return (std::min)(static_cast<int>((positions[static_cast<size_t>(i) * kWordsPerParticle + a] - lo[a]) / h),
                          dim[a] - 1);
      };
      cellStart.assign(static_cast<size_t>(dim[0]) * dim[1] * dim[2] + 1, 0u);
      for (uint32_t i = 0; i < count; ++i) {
        cellOf[i] = static_cast<uint32_t>(cellCoord(i, 0) + dim[0] * (cellCoord(i, 1) + dim[1] * cellCoord(i, 2)));
        ++cellStart[cellOf[i] + 1];
      }
      for (size_t c = 1; c < cellStart.size(); ++c) cellStart[c] += cellStart[c - 1];
      std::vector<uint32_t> cursor(cellStart.begin(), cellStart.end() - 1);
      for (uint32_t i = 0; i < count; ++i) order[cursor[cellOf[i]]++] = i;

      const auto forNeighbours = [&](uint32_t i, const auto& visit) {
        const float* pi = &positions[static_cast<size_t>(i) * kWordsPerParticle];
        const int cx = cellCoord(i, 0), cy = cellCoord(i, 1), cz = cellCoord(i, 2);
        for (int z = (std::max)(cz - 1, 0); z <= (std::min)(cz + 1, dim[2] - 1); ++z)
          for (int y = (std::max)(cy - 1, 0); y <= (std::min)(cy + 1, dim[1] - 1); ++y)
            for (int x = (std::max)(cx - 1, 0); x <= (std::min)(cx + 1, dim[0] - 1); ++x) {
              const size_t cell = static_cast<size_t>(x) + dim[0] * (static_cast<size_t>(y) + dim[1] * static_cast<size_t>(z));
              for (uint32_t k = cellStart[cell]; k < cellStart[cell + 1]; ++k) {
                const uint32_t j = order[k];
                const float* pj = &positions[static_cast<size_t>(j) * kWordsPerParticle];
                const float d[3] = {pi[0] - pj[0], pi[1] - pj[1], pi[2] - pj[2]};
                const float r2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
                if (r2 < h2) visit(j, d, r2);
              }
            }
      };

      parallelFor(count, [&](int index) {
        const uint32_t i = static_cast<uint32_t>(index);
        float density = 0.0f, gradSqSum = 0.0f;
        float gradSum[3] = {0.0f, 0.0f, 0.0f};
        forNeighbours(i, [&](uint32_t, const float* d, float r2) {
          const float w = h2 - r2;
          density += poly6 * w * w * w;
          const float r = std::sqrt(r2);
          if (r <= 1e-6f) return;
          const float s = spiky * (h - r) * (h - r) / r;
          for (int a = 0; a < 3; ++a) gradSum[a] += s * d[a];
          gradSqSum += s * s * r2;
        });
        const float c = (std::max)(density / targetDensity - 1.0f, 0.0f);
        const float denom = gradSum[0] * gradSum[0] + gradSum[1] * gradSum[1] + gradSum[2] * gradSum[2] +
                            gradSqSum + stiffnessEps * denomRef;
        constraint[i] = c;
        lambda[i] = denom > 0.0f ? -c / denom : 0.0f;
      });
      const float maxConstraint = *std::max_element(constraint.begin(), constraint.end());
      if (it == 0) result.initialMaxConstraint = maxConstraint;
      result.maxConstraint = maxConstraint;
      if (maxConstraint < kTolerance || it == kMaxIterations) break;

      parallelFor(count, [&](int index) {
        const uint32_t i = static_cast<uint32_t>(index);
        float dp[3] = {0.0f, 0.0f, 0.0f};
        forNeighbours(i, [&](uint32_t j, const float* d, float r2) {
          const float r = std::sqrt(r2);
          if (r <= 1e-6f) return;
          const float s = (lambda[i] + lambda[j]) * spiky * (h - r) * (h - r) / r / targetDensity;
          for (int a = 0; a < 3; ++a) dp[a] += s * d[a];
        });
        const float len = std::sqrt(dp[0] * dp[0] + dp[1] * dp[1] + dp[2] * dp[2]);
        const float scale = len > maxStep ? maxStep / len : 1.0f;
        for (int a = 0; a < 3; ++a)
          next[static_cast<size_t>(i) * kWordsPerParticle + a] =
              positions[static_cast<size_t>(i) * kWordsPerParticle + a] + dp[a] * scale;
      });
      std::copy(next.begin(), next.end(), positions.begin());
      result.iterations = it + 1;
    }
    return result;
  }
}

bool PbfFluid::CreateParticleBuffers(BaseDriver* driver, const std::vector<float>& positions,
                                      const std::vector<float>& velocities) {
  if (!driver) return false;
  const uint32_t count = static_cast<uint32_t>(positions.size() / kWordsPerParticle);
  if (count == 0) return false;
  std::vector<float> zero(static_cast<size_t>(count) * kWordsPerParticle, 0.0f);
  const std::vector<float>& vel =
      velocities.size() == positions.size() ? velocities : zero;

  // There is no ComputeBuffer write-back API, so the seed is an initialData upload at
  // creation time: the particle buffers are rebuilt whenever the seed changes.
  ComputeBufferDesc desc;
  desc.structureStride = kWordsPerParticle * sizeof(float);
  desc.byteWidth = count * desc.structureStride;
  desc.access = ComputeBufferAccess::ReadWrite;
  desc.debugName = "PBF Positions";
  m_positions = driver->CreateComputeBuffer(desc, positions.data());
  desc.debugName = "PBF Predicted";
  m_predicted = driver->CreateComputeBuffer(desc, positions.data());
  desc.debugName = "PBF Predicted Next";
  m_predictedNext = driver->CreateComputeBuffer(desc, positions.data());
  desc.debugName = "PBF Velocities";
  m_velocities = driver->CreateComputeBuffer(desc, vel.data());
  desc.debugName = "PBF Velocities Next";
  m_velocitiesNext = driver->CreateComputeBuffer(desc, zero.data());
  desc.debugName = "PBF Lambda";
  // Zero-filled, not uninitialised: CS_PbfIntegrate reads Lambda.w as the free-surface
  // neighbour count, and garbage there would enable wind on the first frame. A count of 0
  // means "not measured yet" and skips the wind.
  m_lambda = driver->CreateComputeBuffer(desc, zero.data());
  if (!m_positions || !m_predicted || !m_predictedNext || !m_velocities || !m_velocitiesNext || !m_lambda) {
    T8_LOG_ERROR("[PBF] Cannot create the particle buffers for %u particles", count);
    return false;
  }
  // Same 16-byte element as the particle fields (D3D11 views use the creation stride), three
  // per particle. Zero-filled: a zero bound draws nothing until ComputeAnisotropy runs.
  {
    ComputeBufferDesc anisotropyDesc = desc;
    anisotropyDesc.byteWidth = count * kAnisotropyFloat4PerParticle * desc.structureStride;
    anisotropyDesc.debugName = "PBF Anisotropy";
    std::vector<float> anisotropyZero(static_cast<size_t>(count) * kAnisotropyFloat4PerParticle * 4u, 0.0f);
    m_anisotropy = driver->CreateComputeBuffer(anisotropyDesc, anisotropyZero.data());
    if (!m_anisotropy) {
      T8_LOG_ERROR("[PBF] Cannot create the anisotropy buffer for %u particles", count);
      return false;
    }
  }
  m_particleCount = count;
  return true;
}

bool PbfFluid::SeedParticles(BaseDriver* driver) {
  m_pendingSphere.clear();
  // ---------------------------------------------------------------------------
  // Initial condition: a resting pool filled to `prefillLevel`, and (when `dropSphere`) a
  // SPHERE of water hanging above it, which then falls and splashes in.
  //
  // The sphere alone cannot fill the pool: the 4.5 m wide basin only takes a 2.25 m sphere
  // (33.5 m3 at r = 2), while the water plane needs about 60 m3, and anything wider lands
  // on the coping and is culled. The resting water below it makes up the difference.
  // ---------------------------------------------------------------------------
  const float spacing = m_settings.particleRadius;
  const float r = m_settings.sphereRadius;
  const float cx = 0.5f * (m_settings.basinMinX + m_settings.basinMaxX);
  const float cz = 0.5f * (m_settings.basinMinZ + m_settings.basinMaxZ);
  const float cy = m_settings.waterPlaneY + m_settings.sphereHeight;

  auto insideStep = [this](float x, float y, float z) {
    for (size_t b = 0; b + 7 < m_solidBoxes.size(); b += 8) {
      if (x > m_solidBoxes[b + 0] && x < m_solidBoxes[b + 3] && y > m_solidBoxes[b + 1] &&
          y < m_solidBoxes[b + 4] && z > m_solidBoxes[b + 2] && z < m_solidBoxes[b + 5])
        return true;
    }
    return false;
  };

  std::vector<float> positions;
  positions.reserve(static_cast<size_t>(m_settings.maxParticles) * kWordsPerParticle);
  bool truncated = false;
  const auto room = [&] { return positions.size() / kWordsPerParticle < m_settings.maxParticles; };

  // Resting pool: a cell-centred lattice, half a spacing off every face. The top layer is
  // filled partially (evenly spread sites) so the volume follows the level, not 0.13 m steps.
  const float poolTop = m_settings.waterPlaneY + m_settings.prefillLevel;
  const float layers = (poolTop - m_settings.basinMinY) / spacing;
  uint32_t poolCount = 0;
  if (layers > 0.0f) {
    const int nx = (std::max)(static_cast<int>((m_settings.basinMaxX - m_settings.basinMinX) / spacing), 1);
    const int nz = (std::max)(static_cast<int>((m_settings.basinMaxZ - m_settings.basinMinZ) / spacing), 1);
    const float x0 = 0.5f * (m_settings.basinMinX + m_settings.basinMaxX) - 0.5f * spacing * (nx - 1);
    const float z0 = 0.5f * (m_settings.basinMinZ + m_settings.basinMaxZ) - 0.5f * spacing * (nz - 1);
    const int fullLayers = static_cast<int>(layers);
    const float partial = layers - static_cast<float>(fullLayers);
    for (int l = 0; l <= fullLayers && !truncated; ++l) {
      const float fraction = l < fullLayers ? 1.0f : partial;
      if (fraction <= 0.0f) break;
      const float y = m_settings.basinMinY + (static_cast<float>(l) + 0.5f) * spacing;
      uint32_t site = 0;
      for (int k = 0; k < nz && !truncated; ++k)
        for (int i = 0; i < nx; ++i) {
          const float x = x0 + static_cast<float>(i) * spacing;
          const float z = z0 + static_cast<float>(k) * spacing;
          if (insideStep(x, y, z)) continue;
          const uint32_t s = site++;
          if (static_cast<uint32_t>((s + 1) * fraction) == static_cast<uint32_t>(s * fraction)) continue;
          if (!room()) { truncated = true; break; }
          positions.insert(positions.end(), {x, y, z, 1.0f});   // w = alive
          ++poolCount;
        }
    }
  }

  std::vector<float> sphere;
  if (m_settings.dropSphere && !truncated) {
    const float r2 = r * r;
    for (float y = cy - r; y <= cy + r && !truncated; y += spacing) {
      for (float z = cz - r; z <= cz + r && !truncated; z += spacing) {
        for (float x = cx - r; x <= cx + r; x += spacing) {
          const float dx = x - cx, dy = y - cy, dz = z - cz;
          if (dx * dx + dy * dy + dz * dz > r2) continue;
          if (insideStep(x, y, z)) continue;
          if (positions.size() + sphere.size() >= static_cast<size_t>(m_settings.maxParticles) * kWordsPerParticle) {
            truncated = true;
            break;
          }
          sphere.insert(sphere.end(), {x, y, z, 1.0f});
        }
      }
    }
  }
  const uint32_t sphereCount = static_cast<uint32_t>(sphere.size() / kWordsPerParticle);
  const uint32_t count = poolCount;
  if (count == 0) {
    T8_LOG_ERROR("[PBF] The seed produced no pool particles (level %.2f m, spacing %.3f m)",
                 poolTop, spacing);
    return false;
  }
  // The water start settles the pool FIRST, then appends the relaxed sphere (3.3).

  // Kernel normalisation: divide the kernels by the lattice density so a full lattice
  // measures rho = 1 and the constraint C = rho/rho0 - 1 vanishes at rest.
  const double rhoLattice = LatticeRestDensity(m_settings.smoothingRadius, m_settings.particleRadius);
  const double denomReference = LatticeGradSq(m_settings.smoothingRadius, m_settings.particleRadius, rhoLattice);
  m_settings.restDensity = m_settings.restDensityScale;
  const auto relaxStart = std::chrono::steady_clock::now();
  SeedRelaxResult relax;
  if (sphereCount > 0)
    relax = RelaxSeed(sphere, sphereCount, spacing, m_settings.smoothingRadius, static_cast<float>(rhoLattice),
                      m_settings.restDensity, static_cast<float>(denomReference), m_settings.stiffnessEps);
  const double relaxMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - relaxStart).count();
  // Keep the relaxed sphere out of the initial upload; AppendPendingSphere() adds it after
  // the pool has settled. `positions` therefore holds the pool only.
  if (!truncated && sphereCount > 0) m_pendingSphere = std::move(sphere);
  if (!CreateParticleBuffers(driver, positions, {})) return false;
  m_time = 0.0f;

  m_restScale = static_cast<float>(rhoLattice);
  m_restDenomReference = static_cast<float>(denomReference);
  T8_LOG_INFO("[PBF] Kernel normalisation: rho_lattice=%.3f -> restScale=%.3f, rho0=%.3f (lattice density reads 1.0)",
              rhoLattice, m_restScale, m_settings.restDensity);
  T8_LOG_INFO("[PBF] Seed relaxed in %d iteration(s), %.1f ms: max compression %.4f -> %.4f",
              relax.iterations, relaxMs, relax.initialMaxConstraint, relax.maxConstraint);
  if (sphereCount > 0)
    T8_LOG_INFO("[PBF] Seeded %u pool particles up to y=%.2f; %u relaxed sphere particles pending "
                "(r %.2f m at y=%.2f, %.2f m above the rim %.2f), spacing %.3f m%s",
                m_particleCount, poolTop, sphereCount, r, cy, cy - m_settings.copingTopY,
                m_settings.copingTopY, spacing, truncated ? ", TRUNCATED at maxParticles" : "");
  else
    T8_LOG_INFO("[PBF] Seeded %u pool particles up to y=%.2f, spacing %.3f m%s",
                m_particleCount, poolTop, spacing, truncated ? ", TRUNCATED at maxParticles" : "");
  if (truncated)
    T8_LOG_ERROR("[PBF] The seed exceeded maxParticles=%u; the pool or the sphere is incomplete",
                 m_settings.maxParticles);
  return true;
}

namespace {
  bool Dispatch(BaseDriver* driver, ComputePipeline& pipeline, const char* stage,
                const std::vector<ComputeBindingDesc>& bindings, uint32_t gx, uint32_t gy, uint32_t gz) {
    if (!pipeline.ValidateBindings(bindings)) {
      T8_LOG_ERROR("[PBF] %s: binding validation failed (bound=%zu expected=%zu)", stage,
                   bindings.size(), pipeline.bindingLayout.size());
      return false;
    }
    return driver->DispatchCompute(pipeline, bindings, gx, gy, gz);
  }
}

void PbfFluid::PublishParticleBuffers(const std::string& positionName, const std::string& velocityName,
                                      const std::string& anisotropyName) {
  m_publishedPositionName = positionName;
  m_publishedVelocityName = velocityName;
  if (!anisotropyName.empty()) m_publishedAnisotropyName = anisotropyName;
  auto& registry = ParticleBufferRegistry::Instance();
  registry.Set(positionName, m_positions.get(), static_cast<int>(m_particleCount));
  registry.Set(velocityName, m_velocities.get(), static_cast<int>(m_particleCount));
  if (!m_publishedAnisotropyName.empty() && m_anisotropy)
    registry.Set(m_publishedAnisotropyName, m_anisotropy.get(),
                 static_cast<int>(m_particleCount * kAnisotropyFloat4PerParticle));
}

bool PbfFluid::ComputeAnisotropy(BaseDriver* driver) {
  if (!m_ready || !driver || m_particleCount == 0 || !m_anisotropyPipeline || !m_anisotropy)
    return false;
  const uint32_t particleGroups = DivUp(m_particleCount, kThreads);
  const uint32_t cells = m_gridX * m_gridY * m_gridZ;
  const uint32_t clearGroups = DivUp(DivUp(cells * kCellCapacity * 2u, 4u), kThreads);
  const std::vector<uint32_t> solverConstants = BuildConstants(0);
  // The grid in memory was built from the predicted positions of the last substep, before
  // the projection moved them. Rebuild it from the committed positions so every neighbour
  // within the radius is found; the cull has already flagged retired particles (w = 0), which
  // the build skips.
  {
    const std::vector<ComputeBindingDesc> bindings = {
      {ComputeBindingType::Constants32, 0, nullptr, solverConstants.data(), static_cast<uint32_t>(solverConstants.size())},
      {ComputeBindingType::ReadWriteBuffer, 0, m_grid.get()},
    };
    if (!Dispatch(driver, *m_clearGrid, "anisotropy-clearGrid", bindings, clearGroups, 1, 1)) return false;
  }
  {
    const std::vector<ComputeBindingDesc> bindings = {
      {ComputeBindingType::Constants32, 0, nullptr, solverConstants.data(), static_cast<uint32_t>(solverConstants.size())},
      {ComputeBindingType::ReadOnlyBuffer, 0, m_positions.get()},
      {ComputeBindingType::ReadWriteBuffer, 0, m_grid.get()},
    };
    if (!Dispatch(driver, *m_buildGrid, "anisotropy-buildGrid", bindings, particleGroups, 1, 1)) return false;
  }
  const PbfAnisotropySettings& a = m_anisotropySettings;
  PbfAnisotropyConstants c{};
  c.particleCount = m_particleCount;
  c.gridX = m_gridX;
  c.gridY = m_gridY;
  c.gridZ = m_gridZ;
  c.gridOriginX = m_gridOriginX;
  c.gridOriginY = m_gridOriginY;
  c.gridOriginZ = m_gridOriginZ;
  c.cellSize = m_settings.smoothingRadius;
  c.spacing = m_settings.particleRadius;
  c.smoothing = (std::min)((std::max)(a.smoothing, 0.0f), 1.0f);
  c.maxRatio = (std::max)(a.maxRatio, 1.0f);
  c.radiusScale = (std::max)(a.radiusScale, 0.05f);
  c.sparseScale = (std::max)(a.sparseScale, 0.05f);
  c.fullNeighbours = (std::max)(a.fullNeighbours, a.sparseNeighbours + 1.0f);
  c.sparseNeighbours = (std::max)(a.sparseNeighbours, 0.0f);
  c.maxStretch = (std::max)(a.maxStretch, 1.0f);
  std::vector<uint32_t> words(sizeof(PbfAnisotropyConstants) / 4);
  std::memcpy(words.data(), &c, sizeof(PbfAnisotropyConstants));
  const std::vector<ComputeBindingDesc> bindings = {
    {ComputeBindingType::Constants32, 0, nullptr, words.data(), static_cast<uint32_t>(words.size())},
    {ComputeBindingType::ReadOnlyBuffer, 0, m_positions.get()},
    {ComputeBindingType::ReadOnlyBuffer, 1, m_grid.get()},
    {ComputeBindingType::ReadWriteBuffer, 0, m_anisotropy.get()},
    {ComputeBindingType::ReadOnlyBuffer, 2, m_velocities.get()},
  };
  return Dispatch(driver, *m_anisotropyPipeline, "anisotropy", bindings, particleGroups, 1, 1);
}

bool PbfFluid::ResetToRest(BaseDriver* driver) {
  if (!m_ready || !driver) return false;
  m_culledTotal = 0;
  return SeedParticles(driver);
}

bool PbfFluid::AppendPendingSphere(BaseDriver* driver) {
  if (!m_ready || !driver || m_pendingSphere.empty()) return false;
  std::vector<float> pos, vel;
  if (!ReadParticleFields(driver, &pos, &vel, nullptr)) return false;
  const uint32_t poolCount = static_cast<uint32_t>(pos.size() / kWordsPerParticle);
  const uint32_t add = static_cast<uint32_t>(m_pendingSphere.size() / kWordsPerParticle);
  if (poolCount + add > m_settings.maxParticles) {
    T8_LOG_ERROR("[PBF] Appending the sphere would exceed maxParticles=%u (%u + %u)",
                 m_settings.maxParticles, poolCount, add);
    return false;
  }
  pos.insert(pos.end(), m_pendingSphere.begin(), m_pendingSphere.end());
  // The sphere starts at rest; the settled pool keeps the velocities read back above.
  vel.resize(pos.size(), 0.0f);
  if (!CreateParticleBuffers(driver, pos, vel)) return false;
  m_pendingSphere.clear();
  T8_LOG_INFO("[PBF] Appended %u relaxed sphere particles after settling: %u total", add, m_particleCount);
  return true;
}

bool PbfFluid::Settle(BaseDriver* driver, float seconds, float maxSpeed) {
  if (!m_ready || !driver) return false;
  const PbfSettings saved = m_settings;
  m_settings.maxVelocity = (std::max)(maxSpeed, 0.0f);
  m_settings.windX = 0.0f;
  m_settings.windZ = 0.0f;
  const int steps = static_cast<int>(seconds / (std::max)(m_settings.deltaTime, 1.0e-4f) + 0.5f);
  bool ok = true;
  for (int i = 0; i < steps && ok; ++i) ok = Step(driver);
  m_settings = saved;
  m_time = 0.0f;
  m_culledTotal = 0;
  return ok;
}

bool PbfFluid::Step(BaseDriver* driver) {
  if (!m_ready || !driver || m_particleCount == 0) return false;
  const uint32_t particleGroups = DivUp(m_particleCount, kThreads);
  const uint32_t cells = m_gridX * m_gridY * m_gridZ;
  const uint32_t clearGroups = DivUp(DivUp(cells * kCellCapacity * 2u, 4u), kThreads);
  const uint32_t iterations = (std::max)(m_settings.solverIterations, 1u);
  const std::vector<uint32_t> constants = BuildConstants(0);

  for (uint32_t substep = 0; substep < (std::max)(m_settings.substeps, 1u); ++substep) {
    {
      const std::vector<ComputeBindingDesc> bindings = {
        {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
        {ComputeBindingType::ReadWriteBuffer, 0, m_positions.get()},
        {ComputeBindingType::ReadWriteBuffer, 1, m_predicted.get()},
        {ComputeBindingType::ReadWriteBuffer, 2, m_velocities.get()},
        {ComputeBindingType::ReadOnlyBuffer, 0, m_solidBoxBuffer.get()},
        {ComputeBindingType::ReadOnlyBuffer, 1, m_lambda.get()},
      };
      if (!Dispatch(driver, *m_integrate, "integrate", bindings, particleGroups, 1, 1)) return false;
    }
    {
      const std::vector<ComputeBindingDesc> bindings = {
        {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
        {ComputeBindingType::ReadWriteBuffer, 0, m_grid.get()},
      };
      if (!Dispatch(driver, *m_clearGrid, "clearGrid", bindings, clearGroups, 1, 1)) return false;
    }
    {
      const std::vector<ComputeBindingDesc> bindings = {
        {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
        {ComputeBindingType::ReadOnlyBuffer, 0, m_predicted.get()},
        {ComputeBindingType::ReadWriteBuffer, 0, m_grid.get()},
      };
      if (!Dispatch(driver, *m_buildGrid, "buildGrid", bindings, particleGroups, 1, 1)) return false;
    }
    for (uint32_t it = 0; it < iterations; ++it) {
      const std::vector<ComputeBindingDesc> densityBindings = {
        {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
        {ComputeBindingType::ReadOnlyBuffer, 0, m_predicted.get()},
        {ComputeBindingType::ReadOnlyBuffer, 1, m_grid.get()},
        {ComputeBindingType::ReadWriteBuffer, 0, m_lambda.get()},
        // Solid intrusions feed the analytic boundary term (t2, see CS_PbfDensity.hlsl).
        {ComputeBindingType::ReadOnlyBuffer, 2, m_solidBoxBuffer.get()},
      };
      if (!Dispatch(driver, *m_density, "density", densityBindings, particleGroups, 1, 1)) return false;
      // Jacobi: read Predicted, write PredictedNext, then swap so the next density pass
      // and the next iteration both see the newest positions.
      const std::vector<ComputeBindingDesc> solveBindings = {
        {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
        {ComputeBindingType::ReadOnlyBuffer, 0, m_predicted.get()},
        {ComputeBindingType::ReadWriteBuffer, 1, m_predictedNext.get()},
        {ComputeBindingType::ReadOnlyBuffer, 1, m_grid.get()},
        {ComputeBindingType::ReadOnlyBuffer, 2, m_lambda.get()},
        {ComputeBindingType::ReadOnlyBuffer, 3, m_solidBoxBuffer.get()},
      };
      if (!Dispatch(driver, *m_solve, "solve", solveBindings, particleGroups, 1, 1)) return false;
      // Copy the corrected positions back in place rather than swapping handles, so
      // m_predicted keeps one address for the whole solve. Done every iteration because the
      // next density pass must read the updated positions.
      {
        const std::vector<ComputeBindingDesc> copyBindings = {
          {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
          {ComputeBindingType::ReadOnlyBuffer, 0, m_predictedNext.get()},
          {ComputeBindingType::ReadWriteBuffer, 0, m_predicted.get()},
        };
        if (!Dispatch(driver, *m_copyFields, "solve-copy", copyBindings, particleGroups, 1, 1)) return false;
      }
    }
    {
      const std::vector<ComputeBindingDesc> bindings = {
        {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
        {ComputeBindingType::ReadWriteBuffer, 0, m_positions.get()},
        {ComputeBindingType::ReadOnlyBuffer, 0, m_predicted.get()},
        {ComputeBindingType::ReadOnlyBuffer, 3, m_velocities.get()},
        {ComputeBindingType::ReadWriteBuffer, 4, m_velocitiesNext.get()},
        {ComputeBindingType::ReadOnlyBuffer, 1, m_grid.get()},
        // Step boxes for the no-slip boundary term (t2, see CS_PbfUpdateVelocity.hlsl).
        {ComputeBindingType::ReadOnlyBuffer, 2, m_solidBoxBuffer.get()},
      };
      if (!Dispatch(driver, *m_updateVelocity, "updateVelocity", bindings, particleGroups, 1, 1)) return false;
      // m_velocities is published to the particle renderer, so its address must never change:
      // copy the XSPH result back into it instead of swapping handles.
      {
        const std::vector<ComputeBindingDesc> copyBindings = {
          {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
          {ComputeBindingType::ReadOnlyBuffer, 0, m_velocitiesNext.get()},
          {ComputeBindingType::ReadWriteBuffer, 0, m_velocities.get()},
        };
        if (!Dispatch(driver, *m_copyFields, "velocity-copy", copyBindings, particleGroups, 1, 1)) return false;
      }
    }
  }

  m_time += m_settings.deltaTime;

  // ---------------------------------------------------------------------------
  // Remove the water that left the pool (splashed onto the deck, fell past the wall).
  // Two passes: stamp the dead flags, then compact the survivors with atomic slot
  // reservation. Only survivors write, so this is race free without a prefix sum.
  // ---------------------------------------------------------------------------
  // ---------------------------------------------------------------------------
  // Retire the water that left the pool (splashed onto the deck, fell past the wall).
  //
  // This deliberately does NOT compact. Compaction needs a second buffer, a readback to
  // learn the new count, and a handle swap, and every one of those invalidated a pointer
  // somebody else was holding (the particle renderer, a recorded descriptor set). Instead the
  // kernel writes retired particles far below the floor and zeroes their velocity, so they
  // are out of every neighbourhood and out of frame while the buffers stay the same size and
  // the same address for the lifetime of the solver.
  // ---------------------------------------------------------------------------
  if (m_cull && m_positions && m_velocities) {
    const std::vector<ComputeBindingDesc> cullBindings = {
      {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
      {ComputeBindingType::ReadWriteBuffer, 0, m_positions.get()},
      {ComputeBindingType::ReadWriteBuffer, 1, m_velocities.get()},
    };
    if (!Dispatch(driver, *m_cull, "cull", cullBindings, particleGroups, 1, 1)) return false;
  }

  if (m_surfaceOutput) {
    const uint32_t texels = m_settings.surfaceWidth * m_settings.surfaceDepth;
    const std::vector<uint32_t> surfaceConstants = BuildConstants(0);

    // 1. Zero the packed column heights: the splat accumulates with InterlockedMax.
    {
      const std::vector<ComputeBindingDesc> bindings = {
        {ComputeBindingType::Constants32, 0, nullptr, surfaceConstants.data(), static_cast<uint32_t>(surfaceConstants.size())},
        {ComputeBindingType::ReadWriteBuffer, 0, m_columnHeights.get()},
      };
      if (!Dispatch(driver, *m_clearHeights, "clearHeights", bindings, DivUp(DivUp(texels * 2u, 4u), kThreads), 1, 1))
        return false;
    }
    // 2. Splat: one thread per particle, atomic-max into its column.
    {
      const std::vector<ComputeBindingDesc> bindings = {
        {ComputeBindingType::Constants32, 0, nullptr, surfaceConstants.data(), static_cast<uint32_t>(surfaceConstants.size())},
        {ComputeBindingType::ReadOnlyBuffer, 0, m_predicted.get()},
        {ComputeBindingType::ReadWriteBuffer, 0, m_columnHeights.get()},
        {ComputeBindingType::ReadWriteTexture, 2, nullptr, nullptr, 0, nullptr, nullptr},
      };
      std::vector<ComputeBindingDesc> withTexture = bindings;
      withTexture.back().texture = m_surfaceOutput;
      if (!Dispatch(driver, *m_surface, "surface", withTexture, particleGroups, 1, 1)) return false;
    }
    // 3. Slopes: one thread per texel, writes the final RGBA16F surface.
    {
      const std::vector<uint32_t> slopeConstants = BuildConstants(1);
      std::vector<ComputeBindingDesc> bindings = {
        {ComputeBindingType::Constants32, 0, nullptr, slopeConstants.data(), static_cast<uint32_t>(slopeConstants.size())},
        {ComputeBindingType::ReadOnlyBuffer, 0, m_predicted.get()},
        {ComputeBindingType::ReadWriteBuffer, 0, m_columnHeights.get()},
        {ComputeBindingType::ReadWriteTexture, 2, nullptr, nullptr, 0, nullptr, nullptr},
      };
      bindings.back().texture = m_surfaceOutput;
      if (!Dispatch(driver, *m_surface, "surface", bindings, DivUp(texels, kThreads), 1, 1)) return false;
    }
  }
  return true;
}

void PbfFluid::Shutdown(BaseDriver* driver) {
  (void)driver;
  // Drop the published handles first: they point at the buffers about to be released.
  ParticleBufferRegistry::Instance().Clear();
  ParticleSystemRegistry::Instance().Clear();
  m_integrate.reset();
  m_clearGrid.reset();
  m_buildGrid.reset();
  m_density.reset();
  m_solve.reset();
  m_updateVelocity.reset();
  m_surface.reset();
  m_clearHeights.reset();
  m_positions.reset();
  m_predicted.reset();
  m_predictedNext.reset();
  m_velocities.reset();
  m_velocitiesNext.reset();
  m_lambda.reset();
  m_anisotropy.reset();
  m_anisotropyPipeline.reset();
  m_grid.reset();
  m_solidBoxBuffer.reset();
  m_columnHeights.reset();
  m_surfaceOutput = nullptr;
  m_ready = false;
  m_particleCount = 0;
  m_time = 0.0f;
  m_driver = nullptr;
}

bool PbfFluid::ReadParticles(BaseDriver* driver, std::vector<float>& outXyz) {
  if (!m_ready || !driver) return false;
  outXyz.assign(static_cast<size_t>(m_particleCount) * kWordsPerParticle, 0.0f);
  return driver->ReadComputeBuffer(*m_positions, outXyz.data(), outXyz.size() * sizeof(float));
}

bool PbfFluid::ReadParticleFields(BaseDriver* driver, std::vector<float>* positions,
                                  std::vector<float>* velocities, std::vector<float>* lambda) {
  if (!m_ready || !driver) return false;
  const size_t floats = static_cast<size_t>(m_particleCount) * kWordsPerParticle;
  if (positions) {
    positions->assign(floats, 0.0f);
    if (!driver->ReadComputeBuffer(*m_positions, positions->data(), floats * sizeof(float)))
      return false;
  }
  if (velocities && m_velocities) {
    velocities->assign(floats, 0.0f);
    if (!driver->ReadComputeBuffer(*m_velocities, velocities->data(), floats * sizeof(float)))
      return false;
  }
  if (lambda && m_lambda) {
    lambda->assign(floats, 0.0f);
    if (!driver->ReadComputeBuffer(*m_lambda, lambda->data(), floats * sizeof(float)))
      return false;
  }
  return true;
}

bool PbfFluid::ReadColumnHeights(BaseDriver* driver, std::vector<float>& outElevation) {
  if (!m_ready || !driver) return false;
  const size_t texels = static_cast<size_t>(m_settings.surfaceWidth) * m_settings.surfaceDepth;
  std::vector<uint32_t> packed(texels * 2u, 0u);
  if (!driver->ReadComputeBuffer(*m_columnHeights, packed.data(), packed.size() * sizeof(uint32_t)))
    return false;
  outElevation.resize(texels);
  // Decode the weighted average back to metres above the water plane.
  for (size_t i = 0; i < texels; ++i) {
    float hMax = 0.0f;
    std::memcpy(&hMax, &packed[i * 2u + 0u], sizeof(float));
    const float wSum = static_cast<float>(packed[i * 2u + 1u]);
    outElevation[i] = wSum > 0.5f ? hMax + m_settings.basinMinY - m_settings.waterPlaneY : 0.0f;
  }
  return true;
}

bool PbfFluid::ReadDiagnostics(BaseDriver* driver, Diagnostics& out) {
  out = Diagnostics{};
  std::vector<float> data;
  if (!ReadParticles(driver, data)) return false;
  std::vector<float> vel(data.size(), 0.0f);
  const bool haveVel = driver->ReadComputeBuffer(*m_velocities, vel.data(), vel.size() * sizeof(float));
  double sumSpeed = 0.0;
  double sumY = 0.0;
  double sumDensity = 0.0;
  float maxY = -1e30f;
  uint32_t alive = 0;
  std::vector<float> lambda;
  if (m_lambda) {
    lambda.assign(static_cast<size_t>(m_particleCount) * kWordsPerParticle, 0.0f);
    driver->ReadComputeBuffer(*m_lambda, lambda.data(), lambda.size() * sizeof(float));
  }
  for (uint32_t i = 0; i < m_particleCount; ++i) {
    // Retired particles are parked at y = -1000. Including them in meanY (and in the speed
    // average) made a settling pool look like it was sinking through the floor.
    if (data[i * 4 + 3] < 0.5f) {
      ++out.dead;
      continue;
    }
    ++alive;
    if (!lambda.empty()) {
      const float density = lambda[i * 4 + 1];
      sumDensity += density;
      out.maxDensity = (std::max)(out.maxDensity, density);
    }
    const float x = data[i * 4 + 0], y = data[i * 4 + 1], z = data[i * 4 + 2];
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
      ++out.nonFinite;
      continue;
    }
    if (y > m_settings.copingTopY) ++out.aboveRim;
    sumY += y;
    maxY = (std::max)(maxY, y);
    if (haveVel) {
      const float s = std::sqrt(vel[i * 4 + 0] * vel[i * 4 + 0] + vel[i * 4 + 1] * vel[i * 4 + 1] +
                                vel[i * 4 + 2] * vel[i * 4 + 2]);
      if (std::isfinite(s)) {
        sumSpeed += s;
        if (s > out.maxSpeed) {
          out.maxSpeed = s;
          out.maxSpeedX = x; out.maxSpeedY = y; out.maxSpeedZ = z;
        }
      }
    }
  }
  const uint32_t n = (std::max)(alive, 1u);
  out.meanSpeed = static_cast<float>(sumSpeed / n);
  out.meanY = static_cast<float>(sumY / n);
  out.meanDensity = static_cast<float>(sumDensity / n);
  out.maxY = maxY;
  return true;
}

}  // namespace t850
