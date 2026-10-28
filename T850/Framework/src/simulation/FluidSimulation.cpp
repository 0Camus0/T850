#include <pch.h>

#include <simulation/FluidSimulation.h>

#include <utils/ComputeKernelRegistry.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>

#include <glaze/glaze.hpp>

#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace t850 {

  // Minimal mirror of the pool section of backyard_layout.json. Only the fields
  // the solver needs; unknown keys are tolerated so the layout can grow.
  // Declared outside the anonymous namespace so glz::meta can specialise on them.
  struct LayoutStepJson {
    std::string name;
    double minX = 0.0, minY = 0.0, minZ = 0.0;
    double maxX = 0.0, maxY = 0.0, maxZ = 0.0;
  };
  struct LayoutPoolJson {
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
    double depth = 0.0;
    // Metres of air the solver domain keeps above the still-water plane so a
    // splash has room to rise into. With no freeboard the top row of the volume
    // is already full and the free surface behaves like a solid lid.
    double freeboard = 0.35;
    std::vector<LayoutStepJson> steps;
  };
  struct LayoutJson {
    double waterLine = 0.0;
    // Absolute world Y of the still-water surface, i.e. the plane the water mesh
    // is authored at. Authoritative: the solver derives its fill level from it.
    double waterPlaneY = 0.0;
    LayoutPoolJson pool;
  };

namespace {

  struct FluidConstants {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t iteration;
    float deltaTime;
    float cellSize;
    float viscosity;
    float gravity;
    float windX;
    float windY;
    float windZ;
    float damping;
    float surfaceLevel;
    float timeValue;
    float pressureScale;
    float padding0;
  };
  static_assert(sizeof(FluidConstants) == 64);

  // Voxels this far inside the wall are treated as the "deep interior" for
  // divergence measurement; projection cannot fix the boundary band.
  constexpr uint32_t kInteriorMargin = 4;

  const ComputeKernelDefinition* FindKernel(const char* source) {
    return FindComputeKernel(source);
  }

} // namespace

} // namespace t850

template <>
struct glz::meta<t850::LayoutStepJson> {
  using T = t850::LayoutStepJson;
  static constexpr auto value = glz::object(
      "name", &T::name, "minX", &T::minX, "minY", &T::minY, "minZ", &T::minZ,
      "maxX", &T::maxX, "maxY", &T::maxY, "maxZ", &T::maxZ);
};

template <>
struct glz::meta<t850::LayoutPoolJson> {
  using T = t850::LayoutPoolJson;
  static constexpr auto value = glz::object(
      "minX", &T::minX, "minY", &T::minY, "maxX", &T::maxX, "maxY", &T::maxY,
      "depth", &T::depth, "freeboard", &T::freeboard, "steps", &T::steps);
};

template <>
struct glz::meta<t850::LayoutJson> {
  using T = t850::LayoutJson;
  static constexpr auto value = glz::object(
      "waterLine", &T::waterLine, "waterPlaneY", &T::waterPlaneY, "pool", &T::pool);
};

namespace t850 {

FluidSimulation::~FluidSimulation() = default;

std::unique_ptr<ComputeTexture3D> FluidSimulation::CreateVolume(BaseDriver* driver, const char* debugName) {
  ComputeTexture3DDesc desc;
  desc.width = m_settings.width;
  desc.height = m_settings.height;
  desc.depth = m_settings.depth;
  desc.format = ComputeStorageFormat::Rgba16Float;
  desc.access = ComputeBufferAccess::ReadWrite;
  desc.debugName = debugName ? debugName : "T850 Fluid Volume";
  return driver->CreateComputeTexture3D(desc);
}

std::unique_ptr<ComputePipeline> FluidSimulation::CreatePipeline(BaseDriver* driver, const char* source) {
  const ComputeKernelDefinition* kernel = FindKernel(source);
  if (!kernel) {
    T8_LOG_ERROR("[Fluid] Kernel '%s' is not registered", source);
    return {};
  }
  ComputePipelineDesc desc;
  desc.debugName = kernel->sourceName;
  desc.entryPoint = kernel->entryPoint;
  desc.permutationName = "base";
  desc.bindings = kernel->bindings;
  const std::string sourcePath = "Shaders/" + kernel->sourceName;
  if (!ResourceLocator::Instance().ReadText(sourcePath, desc.source)) {
    T8_LOG_ERROR("[Fluid] Cannot read kernel source '%s'", sourcePath.c_str());
    return {};
  }
  return driver->CreateComputePipeline(desc);
}

std::vector<uint32_t> FluidSimulation::BuildConstants(uint32_t iterationPacked, float paddingOverride) const {
  FluidConstants constants{};
  constants.width = m_settings.width;
  constants.height = m_settings.height;
  constants.depth = m_settings.depth;
  constants.iteration = iterationPacked;
  constants.deltaTime = m_settings.deltaTime;
  constants.cellSize = m_settings.cellSize;
  constants.viscosity = m_settings.viscosity;
  constants.gravity = m_settings.gravity;
  constants.windX = m_settings.windX;
  constants.windY = m_settings.windY;
  constants.windZ = m_settings.windZ;
  constants.damping = m_settings.damping;
  constants.surfaceLevel = m_settings.surfaceLevel;
  constants.timeValue = m_time;
  // Consistent discrete projection: Lap(p) = div, then u -= grad(p).
  // (Equivalently Lap(p') = div/dt with u -= dt*grad(p').) Mixing the two
  // conventions over-corrects by 1/dt and makes divergence grow with iterations.
  constants.pressureScale = m_settings.cellSize * m_settings.cellSize;
  // padding0 carries the SOR factor to the pressure kernel, or the compression
  // strength to the alpha advection kernel. The forces kernel reads the same
  // field as its sustained forcing gain.
  constants.padding0 = paddingOverride >= 0.0f ? paddingOverride : m_settings.sorOmega;
  (void)constants.padding0;
  std::vector<uint32_t> words(sizeof(constants) / sizeof(uint32_t));
  std::memcpy(words.data(), &constants, sizeof(constants));
  return words;
}

// Mirrors `cbuffer ObjectConstants : register(b1)` in CS_FluidSphere3D.hlsl.
// The word count must match the reflected layout exactly or the dispatch is
// rejected by base binding validation.
namespace {
  struct ObjectConstantsBlob {
    float centreX, centreY, centreZ, radius;
    float velX, velY, velZ, enabled;
    float worldOriginX, worldOriginZ, basinMinY, padding0;
  };
}

std::vector<uint32_t> FluidSimulation::BuildObjectConstants() const {
  ObjectConstantsBlob blob{};
  blob.centreX = m_sphere.x;
  blob.centreY = m_sphere.y;
  blob.centreZ = m_sphere.z;
  blob.radius = m_sphere.radius;
  blob.velX = m_sphere.vx;
  blob.velY = m_sphere.vy;
  blob.velZ = m_sphere.vz;
  blob.enabled = m_sphere.enabled ? 1.0f : 0.0f;
  // Centre of grid cell (0, *, 0) in world space, and the world Y of the bottom
  // face of row 0. Derived from the basin bounds, which come from the pool
  // layout JSON, so nothing about the pool placement is baked into the kernel.
  blob.worldOriginX = m_settings.basinMinX + 0.5f * m_settings.cellSize;
  blob.worldOriginZ = m_settings.basinMinZ + 0.5f * m_settings.cellSize;
  blob.basinMinY = m_settings.basinMinY;
  blob.padding0 = 0.0f;
  std::vector<uint32_t> words(sizeof(blob) / sizeof(uint32_t));
  std::memcpy(words.data(), &blob, sizeof(blob));
  return words;
}

// Mirrors `cbuffer CorrectionConstants : register(b1)` in
// CS_FluidVolumeCorrect3D.hlsl.
namespace {
  struct CorrectionConstantsBlob {
    float interfaceHeadroom;
    float volumeDelta;
    // A cbuffer is padded to a 16-byte boundary, so the reflected word count is
    // 4 even though only two floats are used. The declared count in
    // compute_kernels.json and this blob must both say 4 or the driver silently
    // rejects the pipeline.
    float padding0;
    float padding1;
  };
}

std::vector<uint32_t> FluidSimulation::BuildCorrectionConstants(float deficit) const {
  CorrectionConstantsBlob blob{};
  blob.interfaceHeadroom = m_lastInterfaceHeadroom;
  blob.volumeDelta = deficit;
  blob.padding0 = 0.0f;
  blob.padding1 = 0.0f;
  std::vector<uint32_t> words(sizeof(blob) / sizeof(uint32_t));
  std::memcpy(words.data(), &blob, sizeof(blob));
  return words;
}

// Mirrors `cbuffer SpreadConstants : register(b1)` in CS_FluidSphereSpread3D.hlsl.
namespace {
  struct SpreadConstantsBlob {
    float displacedTotal;
    float totalFreeCapacity;
    float padding0;
    float padding1;
  };
}

std::vector<uint32_t> FluidSimulation::BuildSpreadConstants(float displaced, float totalFree) const {
  SpreadConstantsBlob blob{};
  blob.displacedTotal = displaced;
  blob.totalFreeCapacity = totalFree;
  std::vector<uint32_t> words(sizeof(blob) / sizeof(uint32_t));
  std::memcpy(words.data(), &blob, sizeof(blob));
  return words;
}

namespace {
  struct GeometryConstantsBlob {
    float basinMinY;
    float waterPlaneY;
    float worldOriginX;
    float worldOriginZ;
  };
}

std::vector<uint32_t> FluidSimulation::BuildGeometryConstants() const {
  GeometryConstantsBlob blob{};
  blob.basinMinY = m_settings.basinMinY;
  blob.waterPlaneY = m_settings.waterPlaneY;
  blob.worldOriginX = m_settings.basinMinX + 0.5f * m_settings.cellSize;
  blob.worldOriginZ = m_settings.basinMinZ + 0.5f * m_settings.cellSize;
  std::vector<uint32_t> words(sizeof(blob) / sizeof(uint32_t));
  std::memcpy(words.data(), &blob, sizeof(blob));
  return words;
}

bool FluidSimulation::LoadPoolLayout(const char* layoutPath) {
  m_solidBoxes.clear();
  if (!layoutPath) return false;
  std::string json;
  if (!ResourceLocator::Instance().ReadText(layoutPath, json)) {
    T8_LOG_INFO("[Fluid] No pool layout at '%s'; walls fall back to the closed box", layoutPath);
    return false;
  }
  LayoutJson layout;
  auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(layout, json);
  if (ec) {
    T8_LOG_ERROR("[Fluid] Cannot parse pool layout '%s'", layoutPath);
    return false;
  }

  // The solver domain is the inner basin: X and Z from the pool footprint, Y
  // from the floor up to just above the water line.
  const double floorY = -std::abs(layout.pool.depth);
  m_settings.basinMinX = static_cast<float>(layout.pool.minX);
  m_settings.basinMaxX = static_cast<float>(layout.pool.maxX);
  m_settings.basinMinZ = static_cast<float>(layout.pool.minY);
  m_settings.basinMaxZ = static_cast<float>(layout.pool.maxY);
  m_settings.basinMinY = static_cast<float>(floorY);

  // Domain height. Two rules, both data-driven:
  //  - the top of the volume is the still-water plane plus the freeboard the
  //    pool JSON asks for, so the volume really contains air above the water.
  //    With no air above it the top row is already full and the free surface acts
  //    as a solid lid, which caps any splash at exactly zero height.
  //  - rows are derived from that height and the cell size, so the grid scales
  //    with the pool instead of being a literal.
  const float cellSize = (std::max)(m_settings.cellSize, 0.001f);
  m_settings.waterPlaneY = static_cast<float>(layout.waterPlaneY);
  m_settings.freeboard = static_cast<float>((std::max)(layout.pool.freeboard, 0.0));

  if (m_settings.deriveGridFromLayout) {
    // Domain height. The top of the volume is the still-water plane plus the
    // freeboard the pool JSON asks for, so the volume really contains air above
    // the water. With no air above it the top row is already full and the free
    // surface acts as a solid lid, which caps any splash at zero height. Rows are
    // derived from that height and the cell size, so the grid scales with the pool
    // instead of being a literal.
    const double domainTop = (std::max)(layout.waterPlaneY + (std::max)(layout.pool.freeboard, 0.0),
                                        floorY + static_cast<double>(cellSize) * 4.0);
    const auto rows = static_cast<uint32_t>(std::lround((domainTop - floorY) / static_cast<double>(cellSize)));
    m_settings.height = (std::min)((std::max)(rows, 4u), 64u);
    // Exact multiple of the cell size, so the column integral in the surface
    // kernel converts to metres without a scale error.
    m_settings.basinMaxY = m_settings.basinMinY + static_cast<float>(m_settings.height) * cellSize;
    // Fill level that places the still-water surface exactly on the authored plane.
    m_settings.surfaceLevel = (std::min)(
        (std::max)(static_cast<float>((layout.waterPlaneY - floorY) /
                                      (static_cast<double>(m_settings.height) * cellSize)),
                   0.02f),
        0.99f);
    T8_LOG_INFO("[Fluid] Domain %.2f..%.2f m (%u rows @ %.3f m), water plane %.3f m, fill %.3f",
                m_settings.basinMinY, m_settings.basinMaxY, m_settings.height, cellSize,
                static_cast<float>(layout.waterPlaneY), m_settings.surfaceLevel);
  } else {
    // Caller's grid wins. Keep the domain exactly Height * cellSize so the column
    // integral converts to metres without a scale error.
    m_settings.basinMaxY = m_settings.basinMinY + static_cast<float>(m_settings.height) * cellSize;
  }

  for (const LayoutStepJson& step : layout.pool.steps) {
    FluidSolidBox box;
    box.minX = static_cast<float>(step.minX);
    box.minY = static_cast<float>(step.minY);
    box.minZ = static_cast<float>(step.minZ);
    box.maxX = static_cast<float>(step.maxX);
    box.maxY = static_cast<float>(step.maxY);
    box.maxZ = static_cast<float>(step.maxZ);
    m_solidBoxes.push_back(box);
  }

  T8_LOG_INFO("[Fluid] Pool layout '%s': basin %.2f x %.2f x %.2f m at (%.2f, %.2f, %.2f), %zu solid intrusion(s)",
              layoutPath,
              m_settings.basinMaxX - m_settings.basinMinX,
              m_settings.basinMaxY - m_settings.basinMinY,
              m_settings.basinMaxZ - m_settings.basinMinZ,
              m_settings.basinMinX, m_settings.basinMinY, m_settings.basinMinZ,
              m_solidBoxes.size());
  return true;
}

bool FluidSimulation::BuildWallMask(BaseDriver* driver) {
  if (!driver) return false;
  const uint32_t width = m_settings.width;
  const uint32_t height = m_settings.height;
  const uint32_t depth = m_settings.depth;
  if (width == 0 || height == 0 || depth == 0) return false;

  const float cell = m_settings.cellSize;
  const float sizeX = m_settings.basinMaxX - m_settings.basinMinX;
  const float sizeY = m_settings.basinMaxY - m_settings.basinMinY;
  const float sizeZ = m_settings.basinMaxZ - m_settings.basinMinZ;

  const uint32_t voxelCount = width * height * depth;
  std::vector<uint32_t> mask(static_cast<size_t>(voxelCount), 0u);
  m_solidVoxelCount = 0;

  for (uint32_t k = 0; k < depth; ++k) {
    const float pz = m_settings.basinMinZ + (k + 0.5f) * (sizeZ / static_cast<float>(depth));
    for (uint32_t j = 0; j < height; ++j) {
      const float py = m_settings.basinMinY + (j + 0.5f) * (sizeY / static_cast<float>(height));
      for (uint32_t i = 0; i < width; ++i) {
        const float px = m_settings.basinMinX + (i + 0.5f) * (sizeX / static_cast<float>(width));

        // The outer shell is solid on the walls and the floor, but NOT the
        // ceiling: a free surface needs air above the water, so the top row must
        // stay open. If it were solid the water would have nowhere to rise into
        // and alpha would saturate across every remaining row.
        bool solid = i == 0 || i == width - 1 || j == 0 ||
                     k == 0 || k == depth - 1;

        // Then every authored intrusion (the pool steps). A voxel is solid when
        // its centre lies inside the box, which is the same convention the
        // kernels use for stencil neighbours.
        if (!solid) {
          for (const FluidSolidBox& box : m_solidBoxes) {
            if (px >= box.minX && px <= box.maxX &&
                py >= box.minY && py <= box.maxY &&
                pz >= box.minZ && pz <= box.maxZ) {
              solid = true;
              break;
            }
          }
        }

        if (solid) {
          mask[static_cast<size_t>(i + width * (j + height * k))] = 1u;
          ++m_solidVoxelCount;
        }
      }
    }
  }

  ComputeBufferDesc bufferDesc;
  bufferDesc.byteWidth = voxelCount * sizeof(uint32_t);
  bufferDesc.structureStride = sizeof(uint32_t);
  bufferDesc.access = ComputeBufferAccess::ReadOnly;
  bufferDesc.debugName = "Fluid Wall Mask";
  m_wallMask = driver->CreateComputeBuffer(bufferDesc, mask.data());
  if (!m_wallMask) {
    T8_LOG_ERROR("[Fluid] Cannot allocate the %u-voxel wall mask", voxelCount);
    return false;
  }
  // Keep the authored mask on the CPU. The moving body is stamped into a copy of
  // it each frame, which is what makes the body a solid boundary for EVERY kernel
  // (divergence, pressure, gradient and the alpha advection) without teaching all
  // four about a sphere.
  m_baseMask = std::move(mask);
  m_maskBodyCellCount = 0;
  m_maskBodyValid = false;
  m_maskBodyMin[0] = m_maskBodyMin[1] = m_maskBodyMin[2] = -1;
  m_maskBodyMax[0] = m_maskBodyMax[1] = m_maskBodyMax[2] = -1;

  T8_LOG_INFO("[Fluid] Wall mask %ux%ux%u: %u solid of %u voxels (%.1f%% fluid)",
              width, height, depth, m_solidVoxelCount, voxelCount,
              100.0 * static_cast<double>(voxelCount - m_solidVoxelCount) / static_cast<double>(voxelCount));
  (void)cell;
  return true;
}

bool FluidSimulation::UpdateWallMaskForBody(BaseDriver* driver) {
  if (!driver || m_baseMask.empty()) return false;

  const uint32_t width = m_settings.width;
  const uint32_t height = m_settings.height;
  const uint32_t depth = m_settings.depth;
  const float cell = (std::max)(m_settings.cellSize, 1e-4f);

  const float sizeX = m_settings.basinMaxX - m_settings.basinMinX;
  const float sizeY = m_settings.basinMaxY - m_settings.basinMinY;
  const float sizeZ = m_settings.basinMaxZ - m_settings.basinMinZ;
  const float stepX = sizeX / static_cast<float>(width);
  const float stepY = sizeY / static_cast<float>(height);
  const float stepZ = sizeZ / static_cast<float>(depth);

  const bool wantBody = m_sphere.enabled && m_sphere.radius > 0.0f;

  // Only the cells the body can reach are re-stamped: the authored mask is untouched
  // everywhere else. This matters now that the solver substeps up to a dozen times
  // per frame, so re-scanning the whole volume would cost 12x what it used to.
  int minI = -1, minJ = -1, minK = -1, maxI = -1, maxJ = -1, maxK = -1;
  if (wantBody) {
    auto indexOf = [](float value, float origin, float step, uint32_t count) {
      const int index = static_cast<int>(std::floor((value - origin) / step));
      return (std::min)((std::max)(index, 0), static_cast<int>(count) - 1);
    };
    const float reach = m_sphere.radius + cell;   // one cell of margin
    minI = indexOf(m_sphere.x - reach, m_settings.basinMinX, stepX, width);
    maxI = indexOf(m_sphere.x + reach, m_settings.basinMinX, stepX, width);
    minJ = indexOf(m_sphere.y - reach, m_settings.basinMinY, stepY, height);
    maxJ = indexOf(m_sphere.y + reach, m_settings.basinMinY, stepY, height);
    minK = indexOf(m_sphere.z - reach, m_settings.basinMinZ, stepZ, depth);
    maxK = indexOf(m_sphere.z + reach, m_settings.basinMinZ, stepZ, depth);
  }

  // Identical cell footprint to the previous substep: the buffer already matches.
  if (m_maskBodyValid &&
      minI == m_maskBodyMin[0] && maxI == m_maskBodyMax[0] &&
      minJ == m_maskBodyMin[1] && maxJ == m_maskBodyMax[1] &&
      minK == m_maskBodyMin[2] && maxK == m_maskBodyMax[2])
    return true;

  std::vector<uint32_t> mask = m_baseMask;
  uint32_t stamped = 0;
  if (wantBody) {
    // Cell centres, matching the convention BuildWallMask() uses for the authored
    // intrusions and the kernels use for their stencil neighbours.
    const float radius2 = m_sphere.radius * m_sphere.radius;
    for (int k = minK; k <= maxK; ++k) {
      const float pz = m_settings.basinMinZ + (k + 0.5f) * stepZ;
      const float dz = pz - m_sphere.z;
      for (int j = minJ; j <= maxJ; ++j) {
        const float py = m_settings.basinMinY + (j + 0.5f) * stepY;
        const float dy = py - m_sphere.y;
        for (int i = minI; i <= maxI; ++i) {
          const float px = m_settings.basinMinX + (i + 0.5f) * stepX;
          const float dx = px - m_sphere.x;
          if (dx * dx + dy * dy + dz * dz > radius2) continue;
          const size_t index = static_cast<size_t>(i + width * (j + height * k));
          if (mask[index] == 0u) { mask[index] = 1u; ++stamped; }
        }
      }
    }
  }

  ComputeBufferDesc bufferDesc;
  bufferDesc.byteWidth = static_cast<uint32_t>(mask.size() * sizeof(uint32_t));
  bufferDesc.structureStride = sizeof(uint32_t);
  bufferDesc.access = ComputeBufferAccess::ReadOnly;
  bufferDesc.debugName = "Fluid Wall Mask";
  m_wallMask = driver->CreateComputeBuffer(bufferDesc, mask.data());
  if (!m_wallMask) {
    T8_LOG_ERROR("[Fluid] Cannot reallocate the wall mask for the moving body");
    return false;
  }
  m_maskBodyMin[0] = minI; m_maskBodyMin[1] = minJ; m_maskBodyMin[2] = minK;
  m_maskBodyMax[0] = maxI; m_maskBodyMax[1] = maxJ; m_maskBodyMax[2] = maxK;
  m_maskBodyValid = true;
  m_maskBodyCellCount = stamped;
  (void)cell;
  return true;
}

bool FluidSimulation::EnsureSurfaceTarget(BaseDriver* driver) {
  const int width = static_cast<int>(m_settings.width);
  const int depth = static_cast<int>(m_settings.depth);
  bool complete = true;
  for (int index = 0; index < 2; ++index) {
    if (m_surfaceRT[index] >= 0 && m_surfaceTexture[index] &&
        m_surfaceTexture[index]->x == width && m_surfaceTexture[index]->y == depth)
      continue;
    if (m_surfaceRT[index] >= 0) driver->DestroyRT(m_surfaceRT[index]);
    m_surfaceRT[index] = driver->CreateRT(1, BaseRT::RGBA16F, BaseRT::NOTHING, width, depth, false, true);
    m_surfaceTexture[index] = m_surfaceRT[index] >= 0
      ? driver->GetRTTexture(m_surfaceRT[index], BaseDriver::COLOR0_ATTACHMENT) : nullptr;
    if (!m_surfaceTexture[index]) {
      T8_LOG_ERROR("[Fluid] Cannot create %dx%d surface target %d", width, depth, index);
      complete = false;
    }
  }
  return complete;
}

bool FluidSimulation::TryInitialize(BaseDriver* driver, const FluidSettings& settings) {
  Shutdown(driver);
  if (!driver || !driver->SupportsComputeShaders() || !driver->SupportsComputeTextures3D()) {
    T8_LOG_INFO("[Fluid] Volume compute unsupported on %s; caller must fall back",
                driver ? driver->ApiTag() : "none");
    return false;
  }
  if (settings.width < 8 || settings.height < 4 || settings.depth < 8) {
    T8_LOG_ERROR("[Fluid] Invalid grid %ux%ux%u", settings.width, settings.height, settings.depth);
    return false;
  }

  m_driver = driver;
  m_settings = settings;
  // Load the pool layout into m_settings (basin bounds) before any volume or
  // mask work, so the mask classifies cells against the real basin.
  LoadPoolLayout();

  for (size_t i = 0; i < kVelocityCount; ++i)
    m_velocity[i] = CreateVolume(driver, i == 0 ? "Fluid Velocity A" : "Fluid Velocity B");
  m_divergence = CreateVolume(driver, "Fluid Divergence");
  for (size_t i = 0; i < kPressureCount; ++i)
    m_pressure[i] = CreateVolume(driver, i == 0 ? "Fluid Pressure A" : "Fluid Pressure B");
  for (int i = 0; i < 2; ++i)
    m_alpha[i] = CreateVolume(driver, i == 0 ? "Fluid Alpha A" : "Fluid Alpha B");

  // Wall mask from the authored pool geometry. LoadPoolLayout() must run before
  // this so the basin bounds match the cells being classified.
  BuildWallMask(driver);

  m_advect = CreatePipeline(driver, "CS_FluidAdvect3D.hlsl");
  m_forces = CreatePipeline(driver, "CS_FluidForces3D.hlsl");
  m_divergencePipeline = CreatePipeline(driver, "CS_FluidDivergence3D.hlsl");
  m_pressurePipeline = CreatePipeline(driver, "CS_FluidPressure3D.hlsl");
  m_gradientPipeline = CreatePipeline(driver, "CS_FluidGradient3D.hlsl");
  m_surfacePipeline = CreatePipeline(driver, "CS_FluidSurface3D.hlsl");
  m_seedPipeline = CreatePipeline(driver, "CS_FluidSeed3D.hlsl");
  m_diagnosticsPipeline = CreatePipeline(driver, "CS_FluidDiagnostics3D.hlsl");
  m_copyPipeline = CreatePipeline(driver, "CS_FluidCopy3D.hlsl");
  m_surfaceCopyPipeline = CreatePipeline(driver, "CS_FluidSurfaceCopy.hlsl");
  m_seedAlphaPipeline = CreatePipeline(driver, "CS_FluidSeedAlpha3D.hlsl");
  m_alphaAdvectPipeline = CreatePipeline(driver, "CS_FluidAlphaAdvect3D.hlsl");
  m_volumeCorrectPipeline = CreatePipeline(driver, "CS_FluidVolumeCorrect3D.hlsl");
  m_spherePipeline = CreatePipeline(driver, "CS_FluidSphere3D.hlsl");
  m_objectVelocityPipeline = CreatePipeline(driver, "CS_FluidObstacleVelocity3D.hlsl");
  m_clearPipeline = CreatePipeline(driver, "CS_FluidClear3D.hlsl");

  bool complete = m_advect && m_forces && m_divergencePipeline && m_pressurePipeline &&
                  m_gradientPipeline && m_surfacePipeline && m_seedPipeline && m_diagnosticsPipeline &&
                  m_copyPipeline && m_surfaceCopyPipeline && m_seedAlphaPipeline &&
                  m_alphaAdvectPipeline && m_volumeCorrectPipeline &&                   m_spherePipeline && m_objectVelocityPipeline && m_clearPipeline;
  for (size_t i = 0; i < kVelocityCount; ++i) complete = complete && m_velocity[i];
  for (size_t i = 0; i < kPressureCount; ++i) complete = complete && m_pressure[i];
  complete = complete && m_divergence;
  complete = complete && m_alpha[0] && m_alpha[1];
  if (!complete) {
    T8_LOG_ERROR("[Fluid] Solver resources are incomplete on %s: adv=%d for=%d div=%d pre=%d grad=%d surf=%d seed=%d diag=%d copy=%d surfCopy=%d seedAlpha=%d advAlpha=%d volCorrect=%d velA=%d velB=%d preA=%d preB=%d divVol=%d alphaA=%d alphaB=%d",
                driver->ApiTag(),
                m_advect ? 1 : 0, m_forces ? 1 : 0, m_divergencePipeline ? 1 : 0,
                m_pressurePipeline ? 1 : 0, m_gradientPipeline ? 1 : 0, m_surfacePipeline ? 1 : 0,
                m_seedPipeline ? 1 : 0, m_diagnosticsPipeline ? 1 : 0, m_copyPipeline ? 1 : 0,
                m_surfaceCopyPipeline ? 1 : 0, m_seedAlphaPipeline ? 1 : 0,
                m_alphaAdvectPipeline ? 1 : 0, m_volumeCorrectPipeline ? 1 : 0,
                m_velocity[0] ? 1 : 0, m_velocity[1] ? 1 : 0,
                m_pressure[0] ? 1 : 0, m_pressure[1] ? 1 : 0,
                m_divergence ? 1 : 0, m_alpha[0] ? 1 : 0, m_alpha[1] ? 1 : 0);
    Shutdown(driver);
    return false;
  }
  if (!EnsureSurfaceTarget(driver)) {
    Shutdown(driver);
    return false;
  }

  m_ready = true;
  T8_LOG_INFO("[Fluid] Solver ready on %s: grid=%ux%ux%u iterations=%u",
              driver->ApiTag(), m_settings.width, m_settings.height, m_settings.depth,
              m_settings.pressureIterations);
  return true;
}

void FluidSimulation::Shutdown(BaseDriver* driver) {
  if (driver) driver->FlushGPUResources();
  m_advect.reset();
  m_forces.reset();
  m_divergencePipeline.reset();
  m_pressurePipeline.reset();
  m_gradientPipeline.reset();
  m_surfacePipeline.reset();
  m_seedPipeline.reset();
  m_seedDivergentPipeline.reset();
  m_diagnosticsPipeline.reset();
  m_copyPipeline.reset();
  m_surfaceCopyPipeline.reset();
  m_surfaceOutput = nullptr;
  m_wallMask.reset();
  m_alpha[0].reset();
  m_alpha[1].reset();
  m_seedAlphaPipeline.reset();
  m_alphaAdvectPipeline.reset();
  m_volumeCorrectPipeline.reset();
  m_spherePipeline.reset();
  m_objectVelocityPipeline.reset();
  m_clearPipeline.reset();
  m_removedBuffer.reset();
  m_removedColumns = 0;
  m_referenceVolumeFraction = 0.0f;
  m_interfaceAlphaSum = 0.0f;
  m_framesSinceVolumeCheck = 0;
  m_volumeReadbackBuffer.reset();
  m_volumeReadbackVoxels = 0;
  m_solidBoxes.clear();
  m_solidVoxelCount = 0;
  for (auto& volume : m_velocity) volume.reset();
  for (auto& volume : m_pressure) volume.reset();
  m_divergence.reset();
  if (driver) {
    for (int index = 0; index < 2; ++index) {
      if (m_surfaceRT[index] >= 0) driver->DestroyRT(m_surfaceRT[index]);
      m_surfaceRT[index] = -1;
      m_surfaceTexture[index] = nullptr;
    }
    driver->FlushGPUResources();
  }
  m_driver = nullptr;
  m_ready = false;
  m_current = 0;
  m_pressureCurrent = 0;
  m_surfaceCurrent = 0;
  m_time = 0.0f;
}

bool FluidSimulation::SeedVolumeFraction(BaseDriver* driver) {
  if (!m_ready || !driver || !m_seedAlphaPipeline) return false;
  const std::vector<uint32_t> constants = BuildConstants();
  const std::vector<ComputeBindingDesc> bindings = {
    {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
    {ComputeBindingType::ReadOnlyBuffer, 8, m_wallMask.get(), nullptr, 0},
    {ComputeBindingType::ReadWriteTexture, 0, nullptr, nullptr, 0, nullptr, m_alpha[m_alphaCurrent].get()}
  };
  const uint32_t groupsX = (m_settings.width + m_seedAlphaPipeline->threadGroupSize[0] - 1) / m_seedAlphaPipeline->threadGroupSize[0];
  const uint32_t groupsY = (m_settings.height + m_seedAlphaPipeline->threadGroupSize[1] - 1) / m_seedAlphaPipeline->threadGroupSize[1];
  const uint32_t groupsZ = (m_settings.depth + m_seedAlphaPipeline->threadGroupSize[2] - 1) / m_seedAlphaPipeline->threadGroupSize[2];
  if (!driver->DispatchCompute(*m_seedAlphaPipeline, bindings, groupsX, groupsY, groupsZ))
    return false;
  float total = 0.0f;
  float interfaceSum = 0.0f;
  MeasureVolumeFraction(driver, total, interfaceSum);
  m_referenceVolumeFraction = total;
  T8_LOG_INFO("[Fluid] Volume fraction seeded at %.1f%% fill: total alpha = %.0f cells (%.1f%% of the %u fluid voxels), reference locked",
              m_settings.surfaceLevel * 100.0f, total,
              100.0 * static_cast<double>(total) / static_cast<double>((std::max)(1u, FluidVoxelCount())),
              FluidVoxelCount());
  return true;
}

bool FluidSimulation::CorrectVolumeFraction(BaseDriver* driver, float& appliedScale) {
  appliedScale = 1.0f;
  if (!m_ready || !driver || !m_volumeCorrectPipeline) return false;
  if (m_referenceVolumeFraction <= 0.0f) return false;

  float total = 0.0f;
  float interfaceSum = 0.0f;
  if (!MeasureVolumeFraction(driver, total, interfaceSum)) return false;

  // The reference must account for the space the body occupies. Chasing the
  // seed-time total while a ball sits in the water makes the correction pump water
  // straight back into the cavity the body opened: measured column elevations
  // formed an 18 cm dome centred on the ball, and the cavity never survived long
  // enough to launch a wave. The physically correct target is
  // (seed water) - (volume the submerged part of the body displaces).
  float reference = m_referenceVolumeFraction;
  if (m_sphere.enabled && m_sphere.radius > 0.0f && m_sphere.submerged > 0.0f) {
    const double radius = static_cast<double>(m_sphere.radius);
    const double cell = static_cast<double>((std::max)(m_settings.cellSize, 1e-4f));
    const double ballCells = (4.0 / 3.0) * 3.14159265358979 * radius * radius * radius /
                             (cell * cell * cell);
    reference -= static_cast<float>(ballCells * m_sphere.submerged);
  }
  if (reference <= 1.0f) reference = m_referenceVolumeFraction;

  // The correction only touches partially filled cells, so the achievable delta
  // is bounded by their total. If the loss exceeds that there is nothing sane to
  // rescale and the honest answer is to leave the field alone than to fabricate
  // water out of saturated cells.
  const float deficit = reference - total;
  if (std::abs(deficit) <= 0.5f) return true;             // within noise
  if (interfaceSum <= 1.0f) return false;                 // no interface to scale

  const float targetInterface = interfaceSum + deficit;
  if (targetInterface <= 0.0f) return false;
  appliedScale = targetInterface / interfaceSum;

  // Additive, headroom-weighted distribution. A multiplicative rescale is eaten
  // by the clamp: interface cells sit just under 1, so alpha * 1.03 lands above
  // 1 and saturate() discards the whole gain.
  const uint32_t alphaNext = (m_alphaCurrent + 1) % 2;
  const std::vector<uint32_t> constants = BuildConstants();
  const std::vector<uint32_t> correctionConstants = BuildCorrectionConstants(deficit);
  const std::vector<ComputeBindingDesc> bindings = {
    {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
    {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, m_alpha[m_alphaCurrent].get()},
    {ComputeBindingType::Constants32, 1, nullptr, correctionConstants.data(), static_cast<uint32_t>(correctionConstants.size())},
    {ComputeBindingType::ReadWriteTexture, 0, nullptr, nullptr, 0, nullptr, m_alpha[alphaNext].get()}
  };
  const uint32_t gx = (m_settings.width + m_volumeCorrectPipeline->threadGroupSize[0] - 1) / m_volumeCorrectPipeline->threadGroupSize[0];
  const uint32_t gy = (m_settings.height + m_volumeCorrectPipeline->threadGroupSize[1] - 1) / m_volumeCorrectPipeline->threadGroupSize[1];
  const uint32_t gz = (m_settings.depth + m_volumeCorrectPipeline->threadGroupSize[2] - 1) / m_volumeCorrectPipeline->threadGroupSize[2];
  if (!driver->DispatchCompute(*m_volumeCorrectPipeline, bindings, gx, gy, gz))
    return false;
  m_alphaCurrent = alphaNext;

  // Verify the correction actually landed. A correction that is computed but
  // then clamped back on the GPU is the difference between "mass is conserved"
  // and "mass is conserved on paper", so measure the result instead of assuming.
  float totalAfter = 0.0f;
  float interfaceAfter = 0.0f;
  if (MeasureVolumeFraction(driver, totalAfter, interfaceAfter)) {
    T8_LOG_DEBUG("[Fluid] VolumeCorrection t=%.2fs was=%.0f deficit=%.1f headroom=%.0f -> now=%.0f (%+.1f)",
                 m_time, total, deficit, m_lastInterfaceHeadroom, totalAfter, totalAfter - total);
  }
  return true;
}

bool FluidSimulation::MeasureVolumeFraction(BaseDriver* driver, float& totalAlpha) {
  totalAlpha = 0.0f;
  float unused = 0.0f;
  return MeasureVolumeFraction(driver, totalAlpha, unused);
}

bool FluidSimulation::ReadVolumeFraction(BaseDriver* driver, std::vector<float>& out) {
  out.clear();
  if (!m_ready || !driver || !m_alpha[m_alphaCurrent]) return false;
  const uint32_t width = m_settings.width;
  const uint32_t height = m_settings.height;
  const uint32_t depth = m_settings.depth;
  const uint32_t voxelCount = width * height * depth;

  ComputeBufferDesc bufferDesc;
  bufferDesc.byteWidth = voxelCount * 4 * sizeof(uint32_t);
  bufferDesc.structureStride = 4 * sizeof(uint32_t);
  bufferDesc.access = ComputeBufferAccess::ReadWrite;
  bufferDesc.debugName = "Fluid Alpha Readback";
  if (!m_volumeReadbackBuffer || m_volumeReadbackVoxels != voxelCount) {
    std::vector<uint32_t> zeroSeed(static_cast<size_t>(voxelCount) * 4, 0u);
    m_volumeReadbackBuffer = driver->CreateComputeBuffer(bufferDesc, zeroSeed.data());
    if (!m_volumeReadbackBuffer) return false;
    m_volumeReadbackVoxels = voxelCount;
  }

  const std::vector<uint32_t> constants = BuildConstants();
  const std::vector<ComputeBindingDesc> bindings = {
    {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
    {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, m_alpha[m_alphaCurrent].get()},
    {ComputeBindingType::ReadWriteBuffer, 0, m_volumeReadbackBuffer.get(), nullptr, 0}
  };
  const uint32_t gx = (width + m_copyPipeline->threadGroupSize[0] - 1) / m_copyPipeline->threadGroupSize[0];
  const uint32_t gy = (height + m_copyPipeline->threadGroupSize[1] - 1) / m_copyPipeline->threadGroupSize[1];
  const uint32_t gz = (depth + m_copyPipeline->threadGroupSize[2] - 1) / m_copyPipeline->threadGroupSize[2];
  if (!driver->DispatchCompute(*m_copyPipeline, bindings, gx, gy, gz)) return false;

  std::vector<uint32_t> raw(static_cast<size_t>(voxelCount) * 4, 0u);
  if (!driver->ReadComputeBuffer(*m_volumeReadbackBuffer, raw.data(), raw.size() * sizeof(uint32_t)))
    return false;
  out.resize(voxelCount);
  for (uint32_t index = 0; index < voxelCount; ++index)
    std::memcpy(&out[index], &raw[index * 4 + 0], sizeof(float));
  return true;
}

bool FluidSimulation::MeasureVolumeFraction(BaseDriver* driver, float& totalAlpha,
                                            float& interfaceSum) {
  totalAlpha = 0.0f;
  interfaceSum = 0.0f;
  float interfaceHeadroom = 0.0f;
  if (!driver || !m_copyPipeline || !m_alpha[0]) return false;
  const uint32_t width = m_settings.width;
  const uint32_t height = m_settings.height;
  const uint32_t depth = m_settings.depth;
  const uint32_t voxelCount = width * height * depth;

  ComputeBufferDesc bufferDesc;
  bufferDesc.byteWidth = voxelCount * 4 * sizeof(uint32_t);
  bufferDesc.structureStride = 4 * sizeof(uint32_t);
  bufferDesc.access = ComputeBufferAccess::ReadWrite;
  bufferDesc.debugName = "Fluid Alpha Volume Readback";
  // Persist the readback buffer instead of allocating one per call: a fresh
  // buffer every frame churns Vulkan descriptor sets and eventually exhausts the
  // pool, which showed up as a late Step failure in the selftest.
  if (!m_volumeReadbackBuffer || m_volumeReadbackVoxels != voxelCount) {
    std::vector<uint32_t> zeroSeed(static_cast<size_t>(voxelCount) * 4, 0u);
    m_volumeReadbackBuffer = driver->CreateComputeBuffer(bufferDesc, zeroSeed.data());
    if (!m_volumeReadbackBuffer) return false;
    m_volumeReadbackVoxels = voxelCount;
  }
  ComputeBuffer* buffer = m_volumeReadbackBuffer.get();

  const std::vector<uint32_t> constants = BuildConstants();
  const std::vector<ComputeBindingDesc> bindings = {
    {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
    {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, m_alpha[m_alphaCurrent].get()},
    {ComputeBindingType::ReadWriteBuffer, 0, buffer, nullptr, 0}
  };
  const uint32_t groupsX = (width + m_copyPipeline->threadGroupSize[0] - 1) / m_copyPipeline->threadGroupSize[0];
  const uint32_t groupsY = (height + m_copyPipeline->threadGroupSize[1] - 1) / m_copyPipeline->threadGroupSize[1];
  const uint32_t groupsZ = (depth + m_copyPipeline->threadGroupSize[2] - 1) / m_copyPipeline->threadGroupSize[2];
  if (!driver->DispatchCompute(*m_copyPipeline, bindings, groupsX, groupsY, groupsZ))
    return false;

  std::vector<uint32_t> raw(static_cast<size_t>(voxelCount) * 4, 0u);
  if (!driver->ReadComputeBuffer(*buffer, raw.data(), raw.size() * sizeof(uint32_t)))
    return false;
  for (uint32_t index = 0; index < voxelCount; ++index) {
    float value = 0.0f;
    std::memcpy(&value, &raw[index * 4 + 0], sizeof(float));
    totalAlpha += value;
    if (value > 0.001f && value < 0.999f) {
      interfaceSum += value;
      interfaceHeadroom += 1.0f - value;
    }
  }
  m_lastInterfaceHeadroom = interfaceHeadroom;
  return true;
}

void FluidSimulation::AdvanceSphere(float dt) {
  FluidSphere& s = m_sphere;
  if (!s.enabled || dt <= 0.0f) return;

  const float floorY = m_settings.basinMinY;
  const float radius = (std::max)(s.radius, 0.001f);

  // Submerged FRACTION of the sphere by the water plane, in [0,1].
  const float bottom = s.y - radius;
  const float submergedDepth = m_settings.waterPlaneY - bottom;
  s.submerged = (std::min)((std::max)(submergedDepth / (2.0f * radius), 0.0f), 1.0f);

  // Archimedes, net of gravity: a_net = g * (submerged / densityRatio - 1).
  // Equilibrium is submerged == densityRatio, so density ALONE decides whether the
  // body floats and how deep it sits; mass does not enter here, and should not.
  const float ratio = (std::max)(s.densityRatio, 0.02f);
  const float accel = m_settings.gravity * (s.submerged / ratio - 1.0f);

  // Quadratic drag on a sphere, with added mass:
  //   F_d      = 0.5 * rho_w * Cd * A * v^2          (A = pi r^2)
  //   m_eff    = rho_w * V * (densityRatio + 1/3)    (V = 4/3 pi r^3, plus the
  //                                                   half-volume of water the body
  //                                                   drags along with it)
  //   a_drag   = 9 * Cd * v^2 / (16 * r * (densityRatio + 1/3))
  // Dividing by the EFFECTIVE inertia is what makes a heavy ball punch through the
  // surface while a light one is stopped dead. The previous fixed damping
  // coefficient was mass-independent and about 5x too strong at entry speed.
  const float speed = std::sqrt(s.vx * s.vx + s.vy * s.vy + s.vz * s.vz);
  float dragScale = 0.0f;
  if (s.submerged > 0.0f && speed > 1e-5f) {
    const float dragAccel = s.submerged * (9.0f * s.dragCoefficient * speed * speed) /
                            (16.0f * radius * (ratio + 1.0f / 3.0f));
    dragScale = dragAccel / speed;
  }

  s.vy += accel * dt;
  // Explicit drag integrated per component; clamped so a fast entry cannot reverse
  // the velocity in a single frame.
  const float dragStep = (std::min)(dragScale * dt, 0.5f);
  s.vx -= s.vx * dragStep;
  s.vy -= s.vy * dragStep;
  s.vz -= s.vz * dragStep;

  s.x += s.vx * dt;
  s.y += s.vy * dt;
  s.z += s.vz * dt;

  // Keep the body inside the basin: resting on the floor, and against the walls.
  const float bounce = (std::min)((std::max)(s.restitution, 0.0f), 0.99f);
  if (s.y - radius < floorY) {
    s.y = floorY + radius;
    if (s.vy < 0.0f) s.vy = -s.vy * bounce;
    s.vx *= bounce;
    s.vz *= bounce;
  }
  const float minX = m_settings.basinMinX + radius;
  const float maxX = m_settings.basinMaxX - radius;
  const float minZ = m_settings.basinMinZ + radius;
  const float maxZ = m_settings.basinMaxZ - radius;
  if (s.x < minX) { s.x = minX; s.vx = std::abs(s.vx) * bounce; }
  if (s.x > maxX) { s.x = maxX; s.vx = -std::abs(s.vx) * bounce; }
  if (s.z < minZ) { s.z = minZ; s.vz = std::abs(s.vz) * bounce; }
  if (s.z > maxZ) { s.z = maxZ; s.vz = -std::abs(s.vz) * bounce; }
}

bool FluidSimulation::ResetToRest(BaseDriver* driver) {
  if (!m_ready || !driver || !m_clearPipeline) return false;

  const std::vector<uint32_t> constants = BuildConstants();
  const uint32_t px = (m_settings.width + m_clearPipeline->threadGroupSize[0] - 1) / m_clearPipeline->threadGroupSize[0];
  const uint32_t py = (m_settings.height + m_clearPipeline->threadGroupSize[1] - 1) / m_clearPipeline->threadGroupSize[1];
  const uint32_t pz = (m_settings.depth + m_clearPipeline->threadGroupSize[2] - 1) / m_clearPipeline->threadGroupSize[2];

  auto clear = [&](ComputeTexture3D* volume) {
    if (!volume) return true;
    const std::vector<ComputeBindingDesc> bindings = {
      {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
      {ComputeBindingType::ReadWriteTexture, 0, nullptr, nullptr, 0, nullptr, volume}
    };
    return driver->DispatchCompute(*m_clearPipeline, bindings, px, py, pz);
  };

  // Clearing momentum is the point of a reset: without it the next drop lands in
  // water the previous one set moving, and two runs at different heights are not
  // comparable.
  if (!clear(m_velocity[0].get()) || !clear(m_velocity[1].get())) return false;
  if (!clear(m_pressure[0].get()) || !clear(m_pressure[1].get())) return false;
  if (!clear(m_divergence.get())) return false;
  if (!clear(m_alpha[0].get()) || !clear(m_alpha[1].get())) return false;

  m_current = 0;
  m_pressureCurrent = 0;
  m_time = 0.0f;
  m_framesSinceVolumeCheck = 0;
  // Force the next frame to re-stamp (or clear) the body footprint.
  m_maskBodyValid = false;

  // Re-seed the volume fraction and lock the reference mass to the resting pool.
  return SeedVolumeFraction(driver);
}

bool FluidSimulation::InjectSphere(BaseDriver* driver) {
  if (!m_ready || !driver || !m_spherePipeline || !m_alpha[m_alphaCurrent]) return false;

  const uint32_t columns = m_settings.width * m_settings.depth;
  if (!m_removedBuffer || m_removedColumns != columns) {
    ComputeBufferDesc desc;
    desc.byteWidth = columns * 2 * sizeof(float);
    desc.structureStride = 2 * sizeof(float);
    desc.access = ComputeBufferAccess::ReadWrite;
    desc.debugName = "Fluid Sphere Removed";
    const std::vector<uint32_t> seed(columns * 2, 0u);
    m_removedBuffer = driver->CreateComputeBuffer(desc, seed.data());
    if (!m_removedBuffer) return false;
    m_removedColumns = columns;
  }

  const uint32_t alphaMid = (m_alphaCurrent + 1) % 2;
  const std::vector<uint32_t> constants = BuildConstants();
  const std::vector<uint32_t> objectConstants = BuildObjectConstants();
  // The obstacle parameters go in as CONSTANTS, not as a structured buffer.
  // ComputeBuffer has no update path in this engine, so a buffer created once
  // would be read stale forever and the obstacle would never move.
  const std::vector<ComputeBindingDesc> openBindings = {
    {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
    {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, m_alpha[m_alphaCurrent].get()},
    {ComputeBindingType::Constants32, 1, nullptr, objectConstants.data(), static_cast<uint32_t>(objectConstants.size())},
    {ComputeBindingType::ReadOnlyBuffer, 8, m_wallMask.get(), nullptr, 0},
    {ComputeBindingType::ReadWriteTexture, 0, nullptr, nullptr, 0, nullptr, m_alpha[alphaMid].get()},
    {ComputeBindingType::ReadWriteBuffer, 1, m_removedBuffer.get(), nullptr, 0}
  };
  const uint32_t gx = (m_settings.width + m_spherePipeline->threadGroupSize[0] - 1) / m_spherePipeline->threadGroupSize[0];
  const uint32_t gy = (m_settings.depth + m_spherePipeline->threadGroupSize[1] - 1) / m_spherePipeline->threadGroupSize[1];
  if (!driver->DispatchCompute(*m_spherePipeline, openBindings, gx, gy, 1))
    return false;
  m_alphaCurrent = alphaMid;
  // NOTE: there is deliberately no redistribution pass here. Returning the removed
  // volume as a UNIFORM elevation rise over every column low-pass-filtered the
  // surface: it replaced any local crest or trough with a flat DC offset, which is
  // exactly what removed the waves. Displacement now enters through the momentum
  // equation instead (CS_FluidObstacleVelocity3D, see StepObjectVelocity), and the
  // conservative alpha advection carries the surface shape from there.
  return true;
}

bool FluidSimulation::StepObjectVelocity(BaseDriver* driver) {
  if (!m_ready || !driver || !m_objectVelocityPipeline || !m_velocity[m_current]) return false;

  // No-penetration at the moving body. Must run before divergence so the pressure
  // solve sees the body's motion; there is nothing else in the solver that tells
  // the fluid a solid object is sweeping through it.
  const uint32_t target = (m_current + 1) % kVelocityCount;
  const std::vector<uint32_t> constants = BuildConstants();
  const std::vector<uint32_t> objectConstants = BuildObjectConstants();
  const std::vector<ComputeBindingDesc> bindings = {
    {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
    {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, m_velocity[m_current].get()},
    {ComputeBindingType::ReadOnlyTexture, 1, nullptr, nullptr, 0, nullptr,
     (m_alpha[m_alphaCurrent] ? m_alpha[m_alphaCurrent].get() : m_velocity[m_current].get())},
    {ComputeBindingType::Constants32, 1, nullptr, objectConstants.data(), static_cast<uint32_t>(objectConstants.size())},
    {ComputeBindingType::ReadOnlyBuffer, 8, m_wallMask.get(), nullptr, 0},
    {ComputeBindingType::ReadWriteTexture, 0, nullptr, nullptr, 0, nullptr, m_velocity[target].get()}
  };
  const uint32_t gx = (m_settings.width + m_objectVelocityPipeline->threadGroupSize[0] - 1) / m_objectVelocityPipeline->threadGroupSize[0];
  const uint32_t gy = (m_settings.height + m_objectVelocityPipeline->threadGroupSize[1] - 1) / m_objectVelocityPipeline->threadGroupSize[1];
  const uint32_t gz = (m_settings.depth + m_objectVelocityPipeline->threadGroupSize[2] - 1) / m_objectVelocityPipeline->threadGroupSize[2];
  if (!driver->DispatchCompute(*m_objectVelocityPipeline, bindings, gx, gy, gz))
    return false;
  m_current = target;
  return true;
}

bool FluidSimulation::Reset(BaseDriver* driver) {
  if (!m_ready || !driver) return false;
  (void)driver;
  m_current = 0;
  m_pressureCurrent = 0;
  return true;
}

bool FluidSimulation::DispatchVolume(BaseDriver* driver, ComputePipeline& pipeline,
                                     ComputeTexture3D* sampledA, ComputeTexture3D* sampledB,
                                     ComputeTexture3D* target, uint32_t iterationPacked,
                                     bool withAlpha, float paddingOverride) {
  const std::vector<uint32_t> constants = BuildConstants(iterationPacked, paddingOverride);
  std::vector<ComputeBindingDesc> bindings;
  bindings.push_back({ComputeBindingType::Constants32, 0, nullptr, constants.data(),
                      static_cast<uint32_t>(constants.size())});
  bindings.push_back({ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, sampledA});
  if (sampledB)
    bindings.push_back({ComputeBindingType::ReadOnlyTexture, 1, nullptr, nullptr, 0, nullptr, sampledB});
  bindings.push_back({ComputeBindingType::ReadWriteTexture, 0, nullptr, nullptr, 0, nullptr, target});
  if (m_wallMask)
    bindings.push_back({ComputeBindingType::ReadOnlyBuffer, 8, m_wallMask.get(), nullptr, 0});
  // Free-surface-aware kernels also read the volume fraction at t2. Appended last
  // so the binding indices of every existing kernel are unchanged.
  if (withAlpha && m_alpha[m_alphaCurrent])
    bindings.push_back({ComputeBindingType::ReadOnlyTexture, 2, nullptr, nullptr, 0, nullptr,
                        m_alpha[m_alphaCurrent].get()});

  const uint32_t groupsX = (m_settings.width + pipeline.threadGroupSize[0] - 1) / pipeline.threadGroupSize[0];
  const uint32_t groupsY = (m_settings.height + pipeline.threadGroupSize[1] - 1) / pipeline.threadGroupSize[1];
  const uint32_t groupsZ = (m_settings.depth + pipeline.threadGroupSize[2] - 1) / pipeline.threadGroupSize[2];
  const bool dispatched = driver->DispatchCompute(pipeline, bindings, groupsX, groupsY, groupsZ);
  if (!dispatched) {
    std::string layout;
    for (const ComputeBindingLayoutDesc& desc : pipeline.bindingLayout) {
      layout += " [" + std::to_string(static_cast<int>(desc.type)) + ":" +
                std::to_string(desc.shaderRegister) + "]";
    }
    T8_LOG_ERROR("[Fluid] Dispatch failed: passed %zu bindings, pipeline expects %zu; layout:%s",
                 bindings.size(), pipeline.bindingLayout.size(), layout.c_str());
  }
  return dispatched;
}

bool FluidSimulation::PublishSurface(BaseDriver* driver) {
  if (!m_surfaceOutput) return true;
  if (m_surfaceOutput->x != static_cast<int>(m_settings.width) ||
      m_surfaceOutput->y != static_cast<int>(m_settings.depth)) {
    T8_LOG_ERROR("[Fluid] Surface output is %dx%d but the solver is %ux%u",
                 m_surfaceOutput->x, m_surfaceOutput->y, m_settings.width, m_settings.depth);
    return false;
  }
  const std::vector<ComputeBindingDesc> bindings = {
    {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, m_surfaceTexture[m_surfaceCurrent]},
    {ComputeBindingType::ReadWriteTexture, 0, nullptr, nullptr, 0, m_surfaceOutput}
  };
  const uint32_t groupsX = (m_settings.width + m_surfaceCopyPipeline->threadGroupSize[0] - 1) /
                           m_surfaceCopyPipeline->threadGroupSize[0];
  const uint32_t groupsY = (m_settings.depth + m_surfaceCopyPipeline->threadGroupSize[1] - 1) /
                           m_surfaceCopyPipeline->threadGroupSize[1];
  return driver->DispatchCompute(*m_surfaceCopyPipeline, bindings, groupsX, groupsY, 1);
}

bool FluidSimulation::Step(BaseDriver* driver) {
  if (!m_ready || !driver) return false;

  const float frameDt = m_settings.deltaTime;
  const float cell = (std::max)(m_settings.cellSize, 1e-4f);

  // Substep count from the body's own speed. The solid stamp and the velocity
  // coupling only sample the body at discrete instants, so a fast body jumps over
  // water it never actually touches: at entry speed above cellSize/dt (5.4 m/s here
  // at 60 fps and 0.09 m cells) it can cross more than a cell per frame and pass
  // clean through the surface, which is exactly the "went through like nothing"
  // failure. Keeping its displacement under half a cell per substep means every
  // stage - stamp, coupling, divergence, pressure, advection - sees a consistent
  // configuration and a consistent dt.
  uint32_t substeps = 1;
  float bodySpeed = 0.0f;
  if (m_sphere.enabled) {
    bodySpeed = std::sqrt(m_sphere.vx * m_sphere.vx +
                          m_sphere.vy * m_sphere.vy +
                          m_sphere.vz * m_sphere.vz);
    const float travel = bodySpeed * frameDt;
    if (travel > 0.5f * cell)
      substeps = static_cast<uint32_t>(std::ceil(travel / (0.5f * cell)));
    substeps = (std::min)((std::max)(substeps, 1u), 12u);
  }
  T8_LOG_DEBUG("[Fluid] substeps=%u bodySpeed=%.2f m/s travel=%.3f m cell=%.3f m",
               substeps, bodySpeed, bodySpeed * frameDt, cell);

  const float subDt = frameDt / static_cast<float>(substeps);
  m_settings.deltaTime = subDt;
  bool ok = true;
  for (uint32_t substep = 0; substep < substeps && ok; ++substep)
    ok = StepInternal(driver);
  // Restore the frame delta so the caller and the GUI keep seeing the real value.
  m_settings.deltaTime = frameDt;
  return ok;
}

bool FluidSimulation::StepInternal(BaseDriver* driver) {
  if (!m_ready || !driver) return false;
  m_time += m_settings.deltaTime;
  const uint32_t next = (m_current + 1) % kVelocityCount;

  // The body moves and becomes part of the solid boundary BEFORE anything else in
  // the frame, so divergence, the pressure solve, the gradient and the alpha
  // advection all see it as a wall. Without that the pressure projection simply
  // undid the no-penetration velocity the body imposed, and the impact produced no
  // divergence for the solver to react to. Measured column elevations stayed
  // within 1 cm of the seed profile across the whole plunge: no wave, ever.
  AdvanceSphere(m_settings.deltaTime);
  UpdateWallMaskForBody(driver);

  // 1. Advect velocity through itself.
  if (!DispatchVolume(driver, *m_advect, m_velocity[m_current].get(), nullptr, m_velocity[next].get()))
    return false;
  m_current = next;

  // 2. External forces and viscosity. padding0 is read here as the sustained
  // forcing gain, so it is overridden rather than defaulting to the SOR factor.
  const uint32_t forced = (m_current + 1) % kVelocityCount;
  if (!DispatchVolume(driver, *m_forces, m_velocity[m_current].get(), nullptr,
                      m_velocity[forced].get(), 0, false, m_settings.forcingAmplitude))
    return false;
  m_current = forced;

  // 2.5 Obstacle velocity coupling. The body imposes its normal velocity on the
  // fluid (no-penetration at a moving solid boundary), which is what puts the
  // impact into the momentum equation. It has to happen BEFORE divergence: the
  // pressure solve needs to see the divergence the body creates, otherwise there
  // is nothing to react to and no wave can radiate outwards.
  if (m_sphere.enabled && m_objectVelocityPipeline)
    StepObjectVelocity(driver);

  // 3. Divergence, restricted to wet cells.
  if (!DispatchVolume(driver, *m_divergencePipeline, m_velocity[m_current].get(), nullptr,
                      m_divergence.get(), 0, true))
    return false;

  // 4. Red-black Gauss-Seidel pressure sweeps with over-relaxation. Each sweep
  // is two dispatches: one relaxes the even cells, the next relaxes the odd
  // cells against those freshly updated neighbours. Storage textures are
  // write-only here, so an in-place Gauss-Seidel is impossible and the ping-pong
  // pair carries the intermediate field; cells of the other colour are copied
  // through unchanged.
  for (uint32_t iteration = 0; iteration < m_settings.pressureIterations; ++iteration) {
    for (uint32_t parity = 0; parity < 2; ++parity) {
      const uint32_t source = m_pressureCurrent;
      const uint32_t target = (source + 1) % kPressureCount;
      if (!DispatchVolume(driver, *m_pressurePipeline, m_pressure[source].get(), m_divergence.get(),
                          m_pressure[target].get(), parity, true))
        return false;
      m_pressureCurrent = target;
    }
  }

  // 5. Project velocity, then transport the free surface by conservative flux.
  const uint32_t projected = (m_current + 1) % kVelocityCount;
  if (!DispatchVolume(driver, *m_gradientPipeline, m_velocity[m_current].get(),
                      m_pressure[m_pressureCurrent].get(), m_velocity[projected].get(), 0, true))
    return false;
  m_current = projected;

  // Transport alpha AFTER projection so the flux field is the one the fluid
  // actually moves with. Flux-form advection conserves volume by construction.
  //
  // This is what carries the wave now. The body's momentum went into the velocity
  // field before divergence, the pressure solve turned that into motion, and this
  // advection transports the free surface with it, which is exactly how a real
  // impact builds a rim and radiates gravity waves. Cells the body occupies are
  // already solid in the mask, so the advection clears its footprint by itself and
  // no separate removal pass is needed.
  if (m_alphaAdvectPipeline && m_alpha[0] && m_alpha[1]) {
    const uint32_t alphaNext = (m_alphaCurrent + 1) % 2;
    const std::vector<uint32_t> alphaConstants = BuildConstants(0, m_settings.alphaCompression);
    const std::vector<ComputeBindingDesc> alphaBindings = {
      {ComputeBindingType::Constants32, 0, nullptr, alphaConstants.data(),
       static_cast<uint32_t>(alphaConstants.size())},
      {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, m_velocity[m_current].get()},
      {ComputeBindingType::ReadOnlyTexture, 1, nullptr, nullptr, 0, nullptr, m_alpha[m_alphaCurrent].get()},
      {ComputeBindingType::ReadWriteTexture, 0, nullptr, nullptr, 0, nullptr, m_alpha[alphaNext].get()},
      {ComputeBindingType::ReadOnlyBuffer, 8, m_wallMask.get(), nullptr, 0}
    };
    const uint32_t ax = (m_settings.width + m_alphaAdvectPipeline->threadGroupSize[0] - 1) / m_alphaAdvectPipeline->threadGroupSize[0];
    const uint32_t ay = (m_settings.height + m_alphaAdvectPipeline->threadGroupSize[1] - 1) / m_alphaAdvectPipeline->threadGroupSize[1];
    const uint32_t az = (m_settings.depth + m_alphaAdvectPipeline->threadGroupSize[2] - 1) / m_alphaAdvectPipeline->threadGroupSize[2];
    if (!driver->DispatchCompute(*m_alphaAdvectPipeline, alphaBindings, ax, ay, az))
      return false;
    m_alphaCurrent = alphaNext;
  }

  // The body's own footprint is handled by the wall mask (see UpdateWallMaskForBody
  // at the top of the frame), so there is no alpha surgery here: the mask makes its
  // cells solid, the advection writes them dry, and the volume correction restores
  // exactly the amount the body's submerged volume displaced.

  if (m_alphaAdvectPipeline && m_alpha[0] && m_alpha[1]) {
    // Volume correction reads the field back to the CPU, which is a hard sync.
    // Doing that every frame would stall the pipeline and churn Vulkan
    // descriptors, and mass drifts slowly, so it runs periodically instead.
    if (++m_framesSinceVolumeCheck >= m_volumeCheckInterval) {
      m_framesSinceVolumeCheck = 0;
      float appliedScale = 1.0f;
      CorrectVolumeFraction(driver, appliedScale);
    }
  }

  // Derive the renderable 2D surface from the volume. The surface pass samples
  // the previous surface and writes a separate target (ping-pong): a resource
  // cannot be both read and written in one dispatch.
  const uint32_t surfaceNext = (m_surfaceCurrent + 1) % 2;
  const std::vector<uint32_t> constants = BuildConstants();
  const std::vector<uint32_t> geometryConstants = BuildGeometryConstants();
  const std::vector<ComputeBindingDesc> surfaceBindings = {
    {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
    {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, m_velocity[m_current].get()},
    // The surface is now an integral of the simulated volume fraction, so alpha is
    // the input that defines the waterline. The old code sampled the previous 2D
    // surface texture here, which kept the visible water independent of alpha.
    {ComputeBindingType::ReadOnlyTexture, 1, nullptr, nullptr, 0, nullptr,
     (m_alpha[m_alphaCurrent] ? m_alpha[m_alphaCurrent].get() : m_velocity[m_current].get())},
    // Basin floor and authored water plane, from the pool layout JSON. These used
    // to be literals in the kernel.
    {ComputeBindingType::Constants32, 1, nullptr, geometryConstants.data(), static_cast<uint32_t>(geometryConstants.size())},
    {ComputeBindingType::ReadWriteTexture, 0, nullptr, nullptr, 0, m_surfaceTexture[surfaceNext]}
  };
  const uint32_t groupsX = (m_settings.width + m_surfacePipeline->threadGroupSize[0] - 1) / m_surfacePipeline->threadGroupSize[0];
  const uint32_t groupsY = (m_settings.depth + m_surfacePipeline->threadGroupSize[1] - 1) / m_surfacePipeline->threadGroupSize[1];
  if (!driver->DispatchCompute(*m_surfacePipeline, surfaceBindings, groupsX, groupsY, 1))
    return false;
  m_surfaceCurrent = surfaceNext;

  // Copy the freshly written surface into the fixed output texel set. Keeping
  // the output stable lets a mesh bind one texture for the whole frame instead
  // of chasing the ping-pong pair.
  if (m_surfaceOutput) {
    const std::vector<ComputeBindingDesc> copyBindings = {
      {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, m_surfaceTexture[m_surfaceCurrent]},
      {ComputeBindingType::ReadWriteTexture, 0, nullptr, nullptr, 0, m_surfaceOutput}
    };
    if (!driver->DispatchCompute(*m_surfaceCopyPipeline, copyBindings, groupsX, groupsY, 1))
      return false;
  }
  return true;
}

bool FluidSimulation::Seed(BaseDriver* driver, bool divergent) {
  if (!m_ready || !driver) return false;
  const std::vector<uint32_t> constants = BuildConstants();
  const std::vector<ComputeBindingDesc> bindings = {
    {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
    {ComputeBindingType::ReadWriteTexture, 0, nullptr, nullptr, 0, nullptr, m_velocity[m_current].get()}
  };
  const uint32_t groupsX = (m_settings.width + m_seedPipeline->threadGroupSize[0] - 1) / m_seedPipeline->threadGroupSize[0];
  const uint32_t groupsY = (m_settings.height + m_seedPipeline->threadGroupSize[1] - 1) / m_seedPipeline->threadGroupSize[1];
  const uint32_t groupsZ = (m_settings.depth + m_seedPipeline->threadGroupSize[2] - 1) / m_seedPipeline->threadGroupSize[2];

  if (!divergent) return driver->DispatchCompute(*m_seedPipeline, bindings, groupsX, groupsY, groupsZ);

  // The base pipeline was compiled without SEED_DIVERGENT; build the
  // permutation lazily and cache it.
  if (!m_seedDivergentPipeline) {
    const ComputeKernelDefinition* kernel = FindKernel("CS_FluidSeed3D.hlsl");
    if (!kernel) return false;
    ComputePipelineDesc desc;
    desc.debugName = kernel->sourceName + std::string("#divergent");
    desc.entryPoint = kernel->entryPoint;
    desc.permutationName = "divergent";
    desc.defines = {"SEED_DIVERGENT"};
    desc.bindings = kernel->bindings;
    const std::string sourcePath = "Shaders/" + kernel->sourceName;
    if (!ResourceLocator::Instance().ReadText(sourcePath, desc.source)) return false;
    m_seedDivergentPipeline = driver->CreateComputePipeline(desc);
    if (!m_seedDivergentPipeline) {
      T8_LOG_ERROR("[Fluid] Cannot build the divergent seed permutation");
      return false;
    }
  }
  return driver->DispatchCompute(*m_seedDivergentPipeline, bindings, groupsX, groupsY, groupsZ);
}

bool FluidSimulation::StepStage(BaseDriver* driver, const char* stage) {
  if (!m_ready || !driver || !stage) return false;
  const uint32_t next = (m_current + 1) % kVelocityCount;
  const std::string name = stage;
  if (name == "advect") {
    if (!DispatchVolume(driver, *m_advect, m_velocity[m_current].get(), nullptr, m_velocity[next].get()))
      return false;
    m_current = next;
    return true;
  }
  if (name == "forces") {
    if (!DispatchVolume(driver, *m_forces, m_velocity[m_current].get(), nullptr, m_velocity[next].get()))
      return false;
    m_current = next;
    return true;
  }
  if (name == "divergence")
    return DispatchVolume(driver, *m_divergencePipeline, m_velocity[m_current].get(), nullptr,
                          m_divergence.get(), 0, true);
  if (name == "pressure") {
    // One full red-black sweep: relax the even cells, then the odd cells against
    // the freshly updated even ones.
    for (uint32_t parity = 0; parity < 2; ++parity) {
      const uint32_t source = m_pressureCurrent;
      const uint32_t target = (source + 1) % kPressureCount;
      if (!DispatchVolume(driver, *m_pressurePipeline, m_pressure[source].get(), m_divergence.get(),
                          m_pressure[target].get(), parity, true))
        return false;
      m_pressureCurrent = target;
    }
    return true;
  }
  if (name == "gradient") {
    if (!DispatchVolume(driver, *m_gradientPipeline, m_velocity[m_current].get(),
                        m_pressure[m_pressureCurrent].get(), m_velocity[next].get(), 0, true))
      return false;
    m_current = next;
    return true;
  }
  if (name == "surface") {
    const uint32_t surfaceNext = (m_surfaceCurrent + 1) % 2;
    const std::vector<uint32_t> constants = BuildConstants();
    const std::vector<uint32_t> geometryConstants = BuildGeometryConstants();
    const std::vector<ComputeBindingDesc> bindings = {
      {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
      {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, m_velocity[m_current].get()},
      {ComputeBindingType::ReadOnlyTexture, 1, nullptr, nullptr, 0, nullptr,
       (m_alpha[m_alphaCurrent] ? m_alpha[m_alphaCurrent].get() : m_velocity[m_current].get())},
      {ComputeBindingType::Constants32, 1, nullptr, geometryConstants.data(), static_cast<uint32_t>(geometryConstants.size())},
      {ComputeBindingType::ReadWriteTexture, 0, nullptr, nullptr, 0, m_surfaceTexture[surfaceNext]}
    };
    const uint32_t groupsX = (m_settings.width + m_surfacePipeline->threadGroupSize[0] - 1) / m_surfacePipeline->threadGroupSize[0];
    const uint32_t groupsY = (m_settings.depth + m_surfacePipeline->threadGroupSize[1] - 1) / m_surfacePipeline->threadGroupSize[1];
    if (!driver->DispatchCompute(*m_surfacePipeline, bindings, groupsX, groupsY, 1)) return false;
    m_surfaceCurrent = surfaceNext;
    return PublishSurface(driver);
  }
  T8_LOG_ERROR("[Fluid] Unknown stage '%s'", stage);
  return false;
}

bool FluidSimulation::DumpVolume(BaseDriver* driver, const std::string& directory, VolumeDump& summary) {
  return AnalyzeVolume(driver, &directory, summary);
}

bool FluidSimulation::MeasureVolume(BaseDriver* driver, VolumeDump& summary) {
  return AnalyzeVolume(driver, nullptr, summary);
}

bool FluidSimulation::RefreshMaskSnapshot(BaseDriver* driver) {
  if (!m_wallMask) return false;
  const size_t voxelCount = static_cast<size_t>(m_settings.width) * m_settings.height * m_settings.depth;
  m_maskSnapshot.resize(voxelCount);
  return driver && driver->ReadComputeBuffer(*m_wallMask, m_maskSnapshot.data(),
                                             voxelCount * sizeof(uint32_t));
}

bool FluidSimulation::AnalyzeVolume(BaseDriver* driver, const std::string* directory, VolumeDump& summary) {
  summary = VolumeDump{};
  if (!m_ready || !driver || !m_copyPipeline) return false;
  RefreshMaskSnapshot(driver);
  const uint32_t width = m_settings.width;
  const uint32_t height = m_settings.height;
  const uint32_t depth = m_settings.depth;
  const uint32_t voxelCount = width * height * depth;

  ComputeBufferDesc bufferDesc;
  bufferDesc.byteWidth = voxelCount * 4 * sizeof(uint32_t);
  bufferDesc.structureStride = 4 * sizeof(uint32_t);
  bufferDesc.access = ComputeBufferAccess::ReadWrite;
  bufferDesc.debugName = "Fluid Volume Copy";
  std::vector<uint32_t> zeroSeed(static_cast<size_t>(voxelCount) * 4, 0u);
  std::unique_ptr<ComputeBuffer> buffer = driver->CreateComputeBuffer(bufferDesc, zeroSeed.data());
  if (!buffer) {
    T8_LOG_ERROR("[Fluid] Cannot allocate %u-voxel copy buffer", voxelCount);
    return false;
  }

  const std::vector<uint32_t> constants = BuildConstants();
  const std::vector<ComputeBindingDesc> bindings = {
    {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
    {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, m_velocity[m_current].get()},
    {ComputeBindingType::ReadWriteBuffer, 0, buffer.get(), nullptr, 0}
  };
  const uint32_t groupsX = (width + m_copyPipeline->threadGroupSize[0] - 1) / m_copyPipeline->threadGroupSize[0];
  const uint32_t groupsY = (height + m_copyPipeline->threadGroupSize[1] - 1) / m_copyPipeline->threadGroupSize[1];
  const uint32_t groupsZ = (depth + m_copyPipeline->threadGroupSize[2] - 1) / m_copyPipeline->threadGroupSize[2];
  if (!driver->DispatchCompute(*m_copyPipeline, bindings, groupsX, groupsY, groupsZ)) {
    T8_LOG_ERROR("[Fluid] Volume copy dispatch failed");
    return false;
  }

  std::vector<uint32_t> raw(static_cast<size_t>(voxelCount) * 4, 0u);
  if (!driver->ReadComputeBuffer(*buffer, raw.data(), raw.size() * sizeof(uint32_t))) {
    T8_LOG_ERROR("[Fluid] Volume readback failed (%zu bytes)", raw.size() * sizeof(uint32_t));
    return false;
  }
  driver->FlushGPUResources();

  std::vector<float> vx(voxelCount), vy(voxelCount), vz(voxelCount);
  double sumSpeed = 0.0, sumVertical = 0.0;
  float maxSpeed = 0.0f, maxDivergence = 0.0f;
  double sumDivergence = 0.0;
  uint32_t divergenceSamples = 0;
  float interiorMaxDivergence = 0.0f;
  double interiorSumDivergence = 0.0;
  uint32_t interiorSamples = 0;
  double solidSpeedSum = 0.0;
  uint32_t solidSamples = 0;
  float maxWallPenetration = 0.0f;
  for (uint32_t z = 0; z < depth; ++z) {
    for (uint32_t y = 0; y < height; ++y) {
      for (uint32_t x = 0; x < width; ++x) {
        const uint32_t index = (z * height + y) * width + x;
        float fx, fy, fz;
        std::memcpy(&fx, &raw[index * 4 + 0], sizeof(float));
        std::memcpy(&fy, &raw[index * 4 + 1], sizeof(float));
        std::memcpy(&fz, &raw[index * 4 + 2], sizeof(float));
        vx[index] = fx; vy[index] = fy; vz[index] = fz;
        const float speed = std::sqrt(fx * fx + fy * fy + fz * fz);
        sumSpeed += speed;
        sumVertical += fy;
        maxSpeed = (std::max)(maxSpeed, speed);

        // Wall-mask acceptance probe. A solid voxel carrying velocity means water
        // leaked into the pool geometry; a fluid voxel pointing into a solid face
        // means the no-penetration condition is not being enforced.
        const bool voxelSolid = IsMaskSolid(x, y, z);
        if (voxelSolid) {
          solidSpeedSum += speed;
          ++solidSamples;
          continue;
        }
        const float ax = std::abs(fx), ay = std::abs(fy), az = std::abs(fz);
        if (IsMaskSolid(x + 1, y, z) && fx > ax && fx > 0.0f) maxWallPenetration = (std::max)(maxWallPenetration, fx);
        if (IsMaskSolid(x - 1, y, z) && -fx > ax && fx < 0.0f) maxWallPenetration = (std::max)(maxWallPenetration, -fx);
        if (IsMaskSolid(x, y + 1, z) && fy > ay && fy > 0.0f) maxWallPenetration = (std::max)(maxWallPenetration, fy);
        if (IsMaskSolid(x, y - 1, z) && -fy > ay && fy < 0.0f) maxWallPenetration = (std::max)(maxWallPenetration, -fy);
        if (IsMaskSolid(x, y, z + 1) && fz > az && fz > 0.0f) maxWallPenetration = (std::max)(maxWallPenetration, fz);
        if (IsMaskSolid(x, y, z - 1) && -fz > az && fz < 0.0f) maxWallPenetration = (std::max)(maxWallPenetration, -fz);

        // Interior cells only: walls are zeroed and would bias the residual.
        if (x > 0 && y > 0 && z > 0 && x + 1 < width && y + 1 < height && z + 1 < depth) {
          const float scale = 0.5f / m_settings.cellSize;
          const float divergence =
            (vx[(z * height + y) * width + (x + 1)] - vx[(z * height + y) * width + (x - 1)]) * scale +
            (vy[(z * height + (y + 1)) * width + x] - vy[(z * height + (y - 1)) * width + x]) * scale +
            (vz[((z + 1) * height + y) * width + x] - vz[((z - 1) * height + y) * width + x]) * scale;
          maxDivergence = (std::max)(maxDivergence, std::abs(divergence));
          sumDivergence += std::abs(divergence);
          ++divergenceSamples;
        }
        // Deep interior: skip the boundary band, where the wall stencil and the
        // Neumann pressure clamp leave a residual that projection cannot remove.
        if (x >= kInteriorMargin && y >= kInteriorMargin && z >= kInteriorMargin &&
            x + kInteriorMargin < width && y + kInteriorMargin < height && z + kInteriorMargin < depth) {
          const float scale = 0.5f / m_settings.cellSize;
          const float divergence =
            (vx[(z * height + y) * width + (x + 1)] - vx[(z * height + y) * width + (x - 1)]) * scale +
            (vy[(z * height + (y + 1)) * width + x] - vy[(z * height + (y - 1)) * width + x]) * scale +
            (vz[((z + 1) * height + y) * width + x] - vz[((z - 1) * height + y) * width + x]) * scale;
          interiorMaxDivergence = (std::max)(interiorMaxDivergence, std::abs(divergence));
          interiorSumDivergence += std::abs(divergence);
          ++interiorSamples;
        }
      }
    }
  }

  summary.width = width;
  summary.height = height;
  summary.depth = depth;
  summary.maxSpeed = maxSpeed;
  summary.maxDivergence = maxDivergence;
  summary.meanDivergence = divergenceSamples
    ? static_cast<float>(sumDivergence / divergenceSamples) : 0.0f;
  summary.interiorMaxDivergence = interiorMaxDivergence;
  summary.interiorMeanDivergence = interiorSamples
    ? static_cast<float>(interiorSumDivergence / interiorSamples) : 0.0f;
  summary.meanVertical = voxelCount ? static_cast<float>(sumVertical / voxelCount) : 0.0f;
  summary.solidSpeedSum = static_cast<float>(solidSpeedSum);
  summary.solidSamples = solidSamples;
  summary.maxWallPenetration = maxWallPenetration;

  if (!directory) return true;

  std::error_code ec;
  std::filesystem::create_directories(*directory, ec);
  if (ec) {
    T8_LOG_ERROR("[Fluid] Cannot create dump directory '%s': %s", directory->c_str(), ec.message().c_str());
    return false;
  }

  float dumpMaxSpeed = maxSpeed > 1e-6f ? maxSpeed : 1.0f;
  for (uint32_t z = 0; z < depth; ++z) {
    std::vector<uint8_t> magnitude(static_cast<size_t>(width) * height * 3, 0);
    std::vector<uint8_t> vertical(static_cast<size_t>(width) * height * 3, 0);
    for (uint32_t y = 0; y < height; ++y) {
      for (uint32_t x = 0; x < width; ++x) {
        const uint32_t index = (z * height + y) * width + x;
        const uint32_t pixel = (y * width + x) * 3;
        const float speed = std::sqrt(vx[index] * vx[index] + vy[index] * vy[index] + vz[index] * vz[index]);
        const uint8_t speedByte = static_cast<uint8_t>((std::min)(255.0f, speed / dumpMaxSpeed * 255.0f));
        magnitude[pixel + 0] = speedByte;
        magnitude[pixel + 1] = speedByte;
        magnitude[pixel + 2] = speedByte;
        // Vertical component: red = up, blue = down, mid-grey = still.
        const float normalised = (std::max)(-1.0f, (std::min)(1.0f, vy[index] / dumpMaxSpeed));
        const uint8_t up = static_cast<uint8_t>((std::max)(0.0f, normalised) * 255.0f);
        const uint8_t down = static_cast<uint8_t>((std::max)(0.0f, -normalised) * 255.0f);
        vertical[pixel + 0] = up;
        vertical[pixel + 1] = static_cast<uint8_t>(64);
        vertical[pixel + 2] = down;
      }
    }
    const auto writePpm = [&](const char* prefix, const std::vector<uint8_t>& pixels) {
      char name[64];
      std::snprintf(name, sizeof(name), "/%s_z%02u.ppm", prefix, z);
      const std::string path = *directory + name;
      std::ofstream out(path, std::ios::binary);
      if (!out) return;
      out << "P6\n" << width << ' ' << height << "\n255\n";
      out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    };
    writePpm("fluid_speed", magnitude);
    writePpm("fluid_vertical", vertical);
    ++summary.slices;
  }

  std::ofstream json(*directory + "/fluid_volume.json");
  if (json) {
    json << "{\n"
         << "  \"width\": " << width << ",\n"
         << "  \"height\": " << height << ",\n"
         << "  \"depth\": " << depth << ",\n"
         << "  \"cellSize\": " << m_settings.cellSize << ",\n"
         << "  \"sliceCount\": " << summary.slices << ",\n"
         << "  \"maxSpeed\": " << summary.maxSpeed << ",\n"
         << "  \"meanSpeed\": " << (voxelCount ? sumSpeed / voxelCount : 0.0) << ",\n"
         << "  \"meanVertical\": " << summary.meanVertical << ",\n"
         << "  \"maxDivergence\": " << summary.maxDivergence << ",\n"
         << "  \"meanDivergence\": " << summary.meanDivergence << ",\n"
         << "  \"interiorMaxDivergence\": " << summary.interiorMaxDivergence << ",\n"
         << "  \"interiorMeanDivergence\": " << summary.interiorMeanDivergence << "\n"
         << "}\n";
  }

  T8_LOG_INFO("[Fluid] Volume dumped to '%s': %ux%ux%u slices=%u maxSpeed=%.4f meanSpeed=%.4f wallDiv=%.4f interiorMaxDiv=%.4f interiorMeanDiv=%.4f",
              directory->c_str(), width, height, depth, summary.slices, summary.maxSpeed,
              voxelCount ? static_cast<float>(sumSpeed / voxelCount) : 0.0f,
              summary.maxDivergence, summary.interiorMaxDivergence, summary.interiorMeanDivergence);
  return true;
}

bool FluidSimulation::ReadDiagnostics(BaseDriver* driver, Diagnostics& out) {
  out = Diagnostics{};
  if (!m_ready || !driver || !m_diagnosticsPipeline) return false;

  ComputeBufferDesc bufferDesc;
  bufferDesc.byteWidth = 8 * sizeof(uint32_t);
  bufferDesc.structureStride = 4 * sizeof(uint32_t);
  bufferDesc.access = ComputeBufferAccess::ReadWrite;
  bufferDesc.debugName = "Fluid Diagnostics";
  const std::array<uint32_t, 8> seed = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
  std::unique_ptr<ComputeBuffer> buffer = driver->CreateComputeBuffer(bufferDesc, seed.data());
  if (!buffer) return false;

  const std::vector<uint32_t> constants = BuildConstants();
  const std::vector<ComputeBindingDesc> bindings = {
    {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
    {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, m_velocity[m_current].get()},
    {ComputeBindingType::ReadWriteBuffer, 0, buffer.get(), nullptr, 0}
  };
  const uint32_t groupsX = (m_settings.width + m_diagnosticsPipeline->threadGroupSize[0] - 1) / m_diagnosticsPipeline->threadGroupSize[0];
  const uint32_t groupsY = (m_settings.height + m_diagnosticsPipeline->threadGroupSize[1] - 1) / m_diagnosticsPipeline->threadGroupSize[1];
  const uint32_t groupsZ = (m_settings.depth + m_diagnosticsPipeline->threadGroupSize[2] - 1) / m_diagnosticsPipeline->threadGroupSize[2];
  if (!driver->DispatchCompute(*m_diagnosticsPipeline, bindings, groupsX, groupsY, groupsZ)) return false;

  std::array<uint32_t, 8> raw = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
  if (!driver->ReadComputeBuffer(*buffer, raw.data(), sizeof(raw))) return false;
  out.samples = raw[0];
  out.nonFinite = raw[3];
  out.maxSpeed = static_cast<float>(raw[2]) / 1000.0f;
  out.meanSpeed = raw[0] ? (static_cast<float>(raw[1]) / 1000.0f) / static_cast<float>(raw[0]) : 0.0f;
  // raw[4] accumulates the biased +X component; the voxel count lives in raw[0].
  out.meanX = raw[0] ? ((static_cast<float>(raw[4]) / 1000.0f) / static_cast<float>(raw[0])) - 8.0f : 0.0f;
  return true;
}

} // namespace t850
