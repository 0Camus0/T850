#include <pch.h>

#include <simulation/PbfFluid.h>
#include <simulation/PoolLayoutJson.h>

#include <utils/ComputeKernelRegistry.h>
#include <utils/Log.h>
#include <scene/ParticleSystem.h>
#include <utils/ResourceLocator.h>

#include <glaze/glaze.hpp>

#include <algorithm>
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
    float pad6;
    float windX;
    float windZ;
    float windStrength;
    float pad7;
  };
  static_assert(sizeof(PbfConstants) == 144, "PbfConstants must be 32 DWORDs (multiple of 4)");

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
  T8_LOG_INFO("[PBF] Pool basin X %.2f..%.2f  Y %.2f..%.2f  Z %.2f..%.2f m, plane %.3f, coping %.3f, %zu step box(es)",
              m_settings.basinMinX, m_settings.basinMaxX, m_settings.basinMinY, m_settings.basinMaxY,
              m_settings.basinMinZ, m_settings.basinMaxZ, m_settings.waterPlaneY, m_settings.copingTopY,
              m_solidBoxes.size() / 8);
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
  c.windX = m_settings.windX;
  c.windZ = m_settings.windZ;
  c.windStrength = m_settings.windStrength;
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
    const double k = 315.0 / (64.0 * 3.141592653589793 * std::pow(h, 9.0));
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
}

bool PbfFluid::SeedParticles(BaseDriver* driver) {
  // ---------------------------------------------------------------------------
  // Initial condition: a SPHERE of water hanging above the pool, which then falls,
  // splashes into the basin and fills it.
  //
  // The previous version seeded a lattice that filled the pool and stood above the rim,
  // inside a closed box. That box was the problem: a hard clamp at the top and sides made
  // the water bounce off invisible walls, and a full column is already a supported body so
  // it never really fell. A sphere in free air has no support at all, so it falls under
  // gravity like real water; the only solids it feels are the pool's own surfaces.
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
  uint32_t count = 0;
  bool truncated = false;
  const float r2 = r * r;
  for (float y = cy - r; y <= cy + r && !truncated; y += spacing) {
    for (float z = cz - r; z <= cz + r && !truncated; z += spacing) {
      for (float x = cx - r; x <= cx + r; x += spacing) {
        const float dx = x - cx, dy = y - cy, dz = z - cz;
        if (dx * dx + dy * dy + dz * dz > r2) continue;
        if (insideStep(x, y, z)) continue;
        if (count >= m_settings.maxParticles) {
          truncated = true;
          break;
        }
        positions.insert(positions.end(), {x, y, z, 1.0f});   // w = alive
        ++count;
      }
    }
  }
  if (count == 0) {
    T8_LOG_ERROR("[PBF] The water sphere produced no particles (radius %.2f m, spacing %.3f m)",
                 r, spacing);
    return false;
  }

  std::vector<float> rest(static_cast<size_t>(count) * kWordsPerParticle, 0.0f);

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
  m_velocities = driver->CreateComputeBuffer(desc, rest.data());
  desc.debugName = "PBF Velocities Next";
  m_velocitiesNext = driver->CreateComputeBuffer(desc, rest.data());
  desc.debugName = "PBF Lambda";
  // Zero-filled, not uninitialised: CS_PbfIntegrate reads Lambda.y as the free-surface
  // density test, and garbage there would enable wind on the first frame. A density of 0
  // means "not measured yet" and skips the wind.
  m_lambda = driver->CreateComputeBuffer(desc, rest.data());
  if (!m_positions || !m_predicted || !m_predictedNext || !m_velocities || !m_velocitiesNext || !m_lambda) {
    T8_LOG_ERROR("[PBF] Cannot create the particle buffers for %u particles", count);
    return false;
  }

  m_particleCount = count;
  m_time = 0.0f;

  // Kernel normalisation: divide the kernels by the lattice density so a full lattice
  // measures rho = 1 and the constraint C = rho/rho0 - 1 vanishes at rest.
  const double rhoLattice = LatticeRestDensity(m_settings.smoothingRadius, m_settings.particleRadius);
  m_restScale = static_cast<float>(rhoLattice);
  m_restDenomReference = static_cast<float>(
      LatticeGradSq(m_settings.smoothingRadius, m_settings.particleRadius, rhoLattice));
  m_settings.restDensity = m_settings.restDensityScale;
  T8_LOG_INFO("[PBF] Kernel normalisation: rho_lattice=%.3f -> restScale=%.3f, rho0=%.3f (lattice density reads 1.0)",
              rhoLattice, m_restScale, m_settings.restDensity);
  T8_LOG_INFO("[PBF] Seeded %u particles in a %.2f m water sphere centred at y=%.2f (%.2f m above the pool rim %.2f), spacing %.3f m%s",
              m_particleCount, r, cy, cy - m_settings.copingTopY, m_settings.copingTopY, spacing,
              truncated ? ", TRUNCATED at maxParticles" : "");
  if (truncated)
    T8_LOG_ERROR("[PBF] Sphere exceeded maxParticles=%u; the sphere is not a sphere",
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

void PbfFluid::PublishParticleBuffers(const std::string& positionName, const std::string& velocityName) {
  m_publishedPositionName = positionName;
  m_publishedVelocityName = velocityName;
  auto& registry = ParticleBufferRegistry::Instance();
  registry.Set(positionName, m_positions.get(), static_cast<int>(m_particleCount));
  registry.Set(velocityName, m_velocities.get(), static_cast<int>(m_particleCount));
}

bool PbfFluid::ResetToRest(BaseDriver* driver) {
  if (!m_ready || !driver) return false;
  m_culledTotal = 0;
  return SeedParticles(driver);
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
