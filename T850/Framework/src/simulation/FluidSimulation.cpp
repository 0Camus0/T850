#include <pch.h>

#include <simulation/FluidSimulation.h>

#include <utils/ComputeKernelRegistry.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>

#include <glaze/glaze.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unordered_map>

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
    // 1 when the rows above the domain are air rather than a solid ceiling. Padded
    // to 4 DWORDs so the cbuffer stays 16-byte aligned; the whole blob is 80 bytes
    // and every kernel declares the same five fields in the same order.
    float openCeiling;
    float openCeilingPad0;
    float openCeilingPad1;
    float openCeilingPad2;
  };
  static_assert(sizeof(FluidConstants) == 80);

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

std::unique_ptr<ComputeTexture3D> FluidSimulation::CreateVolume(BaseDriver* driver, ComputeStorageFormat format,
                                                                const char* debugName) {
  ComputeTexture3DDesc desc;
  desc.width = m_settings.width;
  desc.height = m_settings.height;
  desc.depth = m_settings.depth;
  desc.format = format;
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
  // Lap(p) = div/dt and u -= dt*grad(p): pressure stays in physical units, so the
  // previous substep's field remains a valid warm start when dt changes.
  constants.pressureScale = m_settings.cellSize * m_settings.cellSize /
                            (std::max)(m_settings.deltaTime, 1e-5f);
  // padding0 carries the SOR factor to the pressure kernel, or the compression
  // strength to the alpha advection kernel. The forces kernel reads the same
  // field as its sustained forcing gain.
  constants.padding0 = paddingOverride >= 0.0f ? paddingOverride : m_settings.sorOmega;
  (void)constants.padding0;
  constants.openCeiling = m_settings.openCeiling ? 1.0f : 0.0f;
  (void)constants.openCeiling;
  std::vector<uint32_t> words(sizeof(constants) / sizeof(uint32_t));
  std::memcpy(words.data(), &constants, sizeof(constants));
  return words;
}

namespace {
  // Mirrors `cbuffer ObjectConstants : register(b1)` in CS_FluidGradient3D and
  // CS_FluidObstacleVelocity3D: just the body count. The wall mask encodes which body
  // (2u + k) owns each cell, and the velocities come from BodyVelocity (t9), so the
  // registry's constant_count for b1 in those two kernels is 4.
  struct ObjectConstantsBlob {
    uint32_t bodyCount;
    uint32_t pad0;
    uint32_t pad1;
    uint32_t pad2;
  };
  static_assert(sizeof(ObjectConstantsBlob) == 16u);
}

std::vector<uint32_t> FluidSimulation::BuildObjectConstants() const {
  ObjectConstantsBlob blob{};
  blob.bodyCount = static_cast<uint32_t>(m_bodies.size());
  std::vector<uint32_t> words(sizeof(blob) / sizeof(uint32_t));
  std::memcpy(words.data(), &blob, sizeof(blob));
  return words;
}

std::vector<float> FluidSimulation::BuildBodyVelocities() const {
  std::vector<float> velocities(m_bodies.size() * 4u, 0.0f);
  for (size_t i = 0; i < m_bodies.size(); ++i) {
    const FluidSphere& body = m_bodies[i];
    if (!body.enabled) continue;
    velocities[i * 4u + 0u] = body.vx;
    velocities[i * 4u + 1u] = body.vy;
    velocities[i * 4u + 2u] = body.vz;
  }
  return velocities;
}

int FluidSimulation::AddBody(const FluidSphere& body) {
  m_bodies.push_back(body);
  m_maskBodyValid = false;   // the new footprint has to be stamped
  ++m_bodyListGeneration;
  return static_cast<int>(m_bodies.size() - 1);
}

void FluidSimulation::RemoveBodiesFrom(size_t first) {
  // Body 0 is the scripted ball: the scene, the self-tests and surface gates 7 and 8
  // all assume it exists and keeps its state.
  if (first < 1) first = 1;
  if (first >= m_bodies.size()) return;
  m_bodies.erase(m_bodies.begin() + static_cast<std::ptrdiff_t>(first), m_bodies.end());
  m_maskBodyValid = false;
  ++m_bodyListGeneration;
}

// Mirrors `cbuffer CorrectionConstants : register(b1)` in
// CS_FluidVolumeCorrect3D.hlsl.
namespace {
  // W-30: the two densities the body drag terms need. AIR_DENSITY matches the shader constant in
  // CS_FluidForces3D.hlsl so the wind's effect on the water and on the bodies agree; WATER_DENSITY
  // matches the solver's reference density, which is what the integrator's mass units assume.
  constexpr float kAirDensity = 1.2f;
  constexpr float kWaterDensity = 1000.0f;

  struct CorrectionConstantsBlob {
    float referenceVolume;
    uint32_t sumSlot;
    float pad0;
    float pad1;
  };
}

std::vector<uint32_t> FluidSimulation::BuildCorrectionConstants(float reference, uint32_t sumSlot) const {
  CorrectionConstantsBlob blob{};
  blob.referenceVolume = reference;
  blob.sumSlot = sumSlot;
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

FluidSimulation::DerivedGrid FluidSimulation::DeriveGrid(const LayoutExtent& extent, float requestedCellSize) {
  auto cells = [](double length, double cell, long minimum, long maximum) {
    return static_cast<uint32_t>((std::min)((std::max)(std::lround(length / cell), minimum), maximum));
  };
  DerivedGrid grid;
  const double requested = (std::max)(requestedCellSize, 0.001f);
  const long nominal = static_cast<long>(cells(extent.sizeX, requested, 8, static_cast<long>(kMaxGridCells)));
  // Prefer a nearby width that also makes Z a whole number of cells, so neither wall sits a
  // fraction of a cell off (a 2:1 pool snaps to even widths).
  long width = nominal;
  double bestError = 1e30;
  for (long candidate = nominal - 2; candidate <= nominal + 2; ++candidate) {
    if (candidate < 8 || candidate > static_cast<long>(kMaxGridCells)) continue;
    const double h = extent.sizeX / static_cast<double>(candidate);
    const double rows = extent.sizeZ / h;
    const double error = std::fabs(rows - std::round(rows)) * h + 1e-9 * std::labs(candidate - nominal);
    if (error < bestError) {
      bestError = error;
      width = candidate;
    }
  }
  grid.width = static_cast<uint32_t>(width);
  // Snap so the pool length is a whole number of cubic cells.
  grid.cellSize = static_cast<float>(extent.sizeX / static_cast<double>(grid.width));
  grid.depth = cells(extent.sizeZ, grid.cellSize, 8, static_cast<long>(kMaxGridCells));
  grid.height = cells(extent.domainHeight, grid.cellSize, 4, static_cast<long>(kMaxGridRows));
  return grid;
}

bool FluidSimulation::LoadPoolLayout(const char* layoutPath) {
  m_solidBoxes.clear();
  m_layoutExtent = LayoutExtent{};
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
  m_layoutExtent.sizeX = static_cast<float>(layout.pool.maxX - layout.pool.minX);
  m_layoutExtent.sizeZ = static_cast<float>(layout.pool.maxY - layout.pool.minY);
  m_layoutExtent.domainHeight = static_cast<float>(
      (std::max)(layout.waterPlaneY + (std::max)(layout.pool.freeboard, 0.0) - floorY, 0.0));

  if (m_settings.deriveGridFromLayout) {
    // The whole grid follows from one cubic cell size: X spans the pool exactly, Z and
    // the air above the water to within half a cell. Width, depth and height are outputs.
    const DerivedGrid grid = DeriveGrid(m_layoutExtent, m_settings.cellSize);
    m_settings.width = grid.width;
    m_settings.depth = grid.depth;
    m_settings.height = grid.height;
    m_settings.cellSize = grid.cellSize;
    const float h = grid.cellSize;
    // Exact multiples of the cell size, so the mask stamp (size / count) and the kernels
    // (origin + i * cellSize) place every cell in the same spot.
    m_settings.basinMaxX = m_settings.basinMinX + static_cast<float>(grid.width) * h;
    m_settings.basinMaxZ = m_settings.basinMinZ + static_cast<float>(grid.depth) * h;
    m_settings.basinMaxY = m_settings.basinMinY + static_cast<float>(grid.height) * h;
    // Fill level that places the still-water surface exactly on the authored plane.
    m_settings.surfaceLevel = (std::min)(
        (std::max)(static_cast<float>((layout.waterPlaneY - floorY) /
                                      (static_cast<double>(m_settings.height) * h)),
                   0.02f),
        0.99f);
    T8_LOG_INFO("[Fluid] Grid %ux%ux%u @ %.4f m (requested %.4f m), domain Y %.2f..%.2f m, water plane %.3f m, fill %.3f",
                grid.width, grid.height, grid.depth, h, cellSize,
                m_settings.basinMinY, m_settings.basinMaxY,
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

        // The domain is the inner basin and every kernel already treats out-of-range
        // neighbours as solid, so only authored intrusions are marked here.
        bool solid = false;

        // Then every authored intrusion (the pool steps). A voxel is solid when
        // its centre lies inside the box, which is the same convention the
        // kernels use for stencil neighbours.
        {
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
  bufferDesc.debugName = "Fluid Base Mask";
  m_baseMaskBuffer = driver->CreateComputeBuffer(bufferDesc, mask.data());
  // Read-write so the bodies are restamped in place on the GPU (UpdateWallMaskForBody).
  bufferDesc.access = ComputeBufferAccess::ReadWrite;
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
  m_maskBodyStampCount = 0;
  m_maskBodyFootprints.clear();

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

  auto indexOf = [](float value, float origin, float step, uint32_t count) {
    const int index = static_cast<int>(std::floor((value - origin) / step));
    return (std::min)((std::max)(index, 0), static_cast<int>(count) - 1);
  };

  // W-22: every enabled body gets a cell AABB, and only those cells are re-stamped.
  // The authored mask is untouched everywhere else: the solver substeps up to a dozen
  // times per frame, so re-scanning the whole volume would cost 12x what it used to.
  std::vector<CellBox> footprint(m_bodies.size());
  uint32_t activeBodies = 0;
  for (size_t b = 0; b < m_bodies.size(); ++b) {
    const FluidSphere& body = m_bodies[b];
    if (!body.enabled || body.radius <= 0.0f) continue;
    const float reach = body.radius + cell;   // one cell of margin
    footprint[b].lo[0] = indexOf(body.x - reach, m_settings.basinMinX, stepX, width);
    footprint[b].hi[0] = indexOf(body.x + reach, m_settings.basinMinX, stepX, width);
    footprint[b].lo[1] = indexOf(body.y - reach, m_settings.basinMinY, stepY, height);
    footprint[b].hi[1] = indexOf(body.y + reach, m_settings.basinMinY, stepY, height);
    footprint[b].lo[2] = indexOf(body.z - reach, m_settings.basinMinZ, stepZ, depth);
    footprint[b].hi[2] = indexOf(body.z + reach, m_settings.basinMinZ, stepZ, depth);
    footprint[b].valid = true;
    ++activeBodies;
  }

  // Identical footprints and body count to the previous substep: the buffer already
  // matches. Comparing every body, not just a union AABB: two bodies moving in
  // opposite directions can leave the union unchanged while their footprints moved.
  bool sameFootprints = m_maskBodyValid && m_maskBodyStampCount == activeBodies &&
                        m_maskBodyFootprints.size() == footprint.size();
  for (size_t b = 0; b < footprint.size() && sameFootprints; ++b) {
    const CellBox& now = footprint[b];
    const CellBox& before = m_maskBodyFootprints[b];
    if (now.valid != before.valid) { sameFootprints = false; break; }
    if (!now.valid) continue;
    for (int axis = 0; axis < 3; ++axis) {
      if (now.lo[axis] != before.lo[axis] || now.hi[axis] != before.hi[axis]) {
        sameFootprints = false;
        break;
      }
    }
  }
  if (sameFootprints) return true;

  // GPU path: rewrite the persistent mask from the base mask in one dispatch. The CPU
  // path below copies every voxel and re-creates the buffer through a blocking upload.
  if (m_stampBodiesPipeline && m_baseMaskBuffer && m_wallMask &&
      m_wallMask->descriptor.access == ComputeBufferAccess::ReadWrite) {
    std::vector<float> spheres((std::max)(m_bodies.size(), size_t(1)) * 4u, 0.0f);
    for (size_t b = 0; b < footprint.size(); ++b) {
      if (!footprint[b].valid) continue;
      spheres[b * 4u + 0u] = m_bodies[b].x;
      spheres[b * 4u + 1u] = m_bodies[b].y;
      spheres[b * 4u + 2u] = m_bodies[b].z;
      spheres[b * 4u + 3u] = m_bodies[b].radius;
    }
    struct StampConstants {
      float basinMinX, basinMinY, basinMinZ;
      uint32_t bodyCount;
      float stepX, stepY, stepZ, pad;
    } stamp{m_settings.basinMinX, m_settings.basinMinY, m_settings.basinMinZ,
            static_cast<uint32_t>(m_bodies.size()), stepX, stepY, stepZ, 0.0f};
    static_assert(sizeof(StampConstants) == 8u * sizeof(uint32_t));
    uint32_t stampWords[8];
    std::memcpy(stampWords, &stamp, sizeof(stamp));
    const std::vector<uint32_t> constants = BuildConstants();
    ComputeBindingDesc bodies;
    bodies.type = ComputeBindingType::ReadOnlyBuffer;
    bodies.shaderRegister = 1;
    bodies.inlineData = spheres.data();
    bodies.inlineBytes = static_cast<uint32_t>(spheres.size() * sizeof(float));
    bodies.inlineStride = 4u * sizeof(float);
    const std::vector<ComputeBindingDesc> bindings = {
      {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
      {ComputeBindingType::Constants32, 1, nullptr, stampWords, 8u},
      {ComputeBindingType::ReadOnlyBuffer, 0, m_baseMaskBuffer.get(), nullptr, 0},
      bodies,
      {ComputeBindingType::ReadWriteBuffer, 0, m_wallMask.get(), nullptr, 0}
    };
    const uint32_t* group = m_stampBodiesPipeline->threadGroupSize.data();
    if (!driver->DispatchCompute(*m_stampBodiesPipeline, bindings,
                                 (width + group[0] - 1) / group[0],
                                 (height + group[1] - 1) / group[1],
                                 (depth + group[2] - 1) / group[2])) {
      T8_LOG_ERROR("[Fluid] Body stamp dispatch failed");
      return false;
    }
    m_maskBodyFootprints = std::move(footprint);
    m_maskBodyStampCount = activeBodies;
    m_maskBodyValid = true;
    return true;
  }

  std::vector<uint32_t> mask = m_baseMask;
  uint32_t stamped = 0;
  for (size_t b = 0; b < footprint.size(); ++b) {
    if (!footprint[b].valid) continue;
    const FluidSphere& body = m_bodies[b];
    // Body k is stamped as 2u + k. Static solids stay 1u, and every kernel keeps
    // treating "non-zero" as solid; the two body kernels recover the index with
    // mask - 2u to look up that body's velocity. Cell centres, matching the convention
    // BuildWallMask() uses for the authored intrusions and the kernels use for their
    // stencil neighbours.
    const uint32_t stamp = 2u + static_cast<uint32_t>(b);
    const float radius2 = body.radius * body.radius;
    for (int k = footprint[b].lo[2]; k <= footprint[b].hi[2]; ++k) {
      const float pz = m_settings.basinMinZ + (k + 0.5f) * stepZ;
      const float dz = pz - body.z;
      for (int j = footprint[b].lo[1]; j <= footprint[b].hi[1]; ++j) {
        const float py = m_settings.basinMinY + (j + 0.5f) * stepY;
        const float dy = py - body.y;
        for (int i = footprint[b].lo[0]; i <= footprint[b].hi[0]; ++i) {
          const float px = m_settings.basinMinX + (i + 0.5f) * stepX;
          const float dx = px - body.x;
          if (dx * dx + dy * dy + dz * dz > radius2) continue;
          const size_t index = static_cast<size_t>(i + width * (j + height * k));
          if (mask[index] == 0u) {
            mask[index] = stamp;
            ++stamped;
          } else if (mask[index] >= 2u) {
            // Overlapping bodies: the later index wins, which is the same rule as
            // before and keeps the stamp deterministic from the body order.
            mask[index] = stamp;
            ++stamped;
          }
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
    T8_LOG_ERROR("[Fluid] Cannot reallocate the wall mask for the moving bodies");
    return false;
  }
  // Remember this substep's footprints; disabled or removed bodies are stored as
  // invalid so the next comparison cannot match by accident.
  m_maskBodyFootprints = std::move(footprint);
  m_maskBodyStampCount = activeBodies;
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
    m_velocity[i] = CreateVolume(driver, ComputeStorageFormat::Rgba16Float,
                                 i == 0 ? "Fluid Velocity A" : "Fluid Velocity B");
  // The scalar fields are R32F (W-02): fp16 alpha lost 0.69 % of the pool in 2 s.
  m_divergence = CreateVolume(driver, ComputeStorageFormat::R32Float, "Fluid Divergence");
  for (size_t i = 0; i < kPressureCount; ++i)
    m_pressure[i] = CreateVolume(driver, ComputeStorageFormat::R32Float,
                                 i == 0 ? "Fluid Pressure A" : "Fluid Pressure B");
  for (int i = 0; i < 2; ++i)
    m_alpha[i] = CreateVolume(driver, ComputeStorageFormat::R32Float,
                              i == 0 ? "Fluid Alpha A" : "Fluid Alpha B");

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
  m_airRisePipeline = CreatePipeline(driver, "CS_FluidAirRise3D.hlsl");
  m_bodyLevelPipeline = CreatePipeline(driver, "CS_FluidBodyLevel.hlsl");
  m_volumeCorrectPipeline = CreatePipeline(driver, "CS_FluidVolumeCorrect3D.hlsl");
  m_volumeReducePipeline = CreatePipeline(driver, "CS_FluidVolumeReduce3D.hlsl");
  m_stampBodiesPipeline = CreatePipeline(driver, "CS_FluidStampBodies3D.hlsl");
  m_objectVelocityPipeline = CreatePipeline(driver, "CS_FluidObstacleVelocity3D.hlsl");
  m_clearPipeline = CreatePipeline(driver, "CS_FluidClear3D.hlsl");
  m_clearFloatPipeline = CreatePipeline(driver, "CS_FluidClearFloat3D.hlsl");
  m_surfaceToBufferPipeline = CreatePipeline(driver, "CS_FluidSurfaceToBuffer.hlsl");
  m_extrapolatePipeline = CreatePipeline(driver, "CS_FluidExtrapolate3D.hlsl");
  m_maxVelocityPipeline = CreatePipeline(driver, "CS_FluidMaxVelocity3D.hlsl");

  bool complete = m_advect && m_forces && m_divergencePipeline && m_pressurePipeline &&
                  m_gradientPipeline && m_extrapolatePipeline && m_surfacePipeline && m_seedPipeline && m_diagnosticsPipeline &&
                  m_copyPipeline && m_surfaceCopyPipeline && m_seedAlphaPipeline &&
                  m_alphaAdvectPipeline && m_airRisePipeline && m_bodyLevelPipeline && m_volumeCorrectPipeline && m_volumeReducePipeline &&
                  m_stampBodiesPipeline && m_baseMaskBuffer &&
                  m_objectVelocityPipeline && m_clearPipeline && m_clearFloatPipeline &&
                  m_surfaceToBufferPipeline && m_maxVelocityPipeline;
  for (size_t i = 0; i < kVelocityCount; ++i) complete = complete && m_velocity[i];
  for (size_t i = 0; i < kPressureCount; ++i) complete = complete && m_pressure[i];
  complete = complete && m_divergence;
  complete = complete && m_alpha[0] && m_alpha[1];
  if (!complete) {
    T8_LOG_ERROR("[Fluid] Solver resources are incomplete on %s: adv=%d for=%d div=%d pre=%d grad=%d surf=%d seed=%d diag=%d copy=%d surfCopy=%d seedAlpha=%d advAlpha=%d airRise=%d volCorrect=%d velA=%d velB=%d preA=%d preB=%d divVol=%d alphaA=%d alphaB=%d",
                driver->ApiTag(),
                m_advect ? 1 : 0, m_forces ? 1 : 0, m_divergencePipeline ? 1 : 0,
                m_pressurePipeline ? 1 : 0, m_gradientPipeline ? 1 : 0, m_surfacePipeline ? 1 : 0,
                m_seedPipeline ? 1 : 0, m_diagnosticsPipeline ? 1 : 0, m_copyPipeline ? 1 : 0,
                m_surfaceCopyPipeline ? 1 : 0, m_seedAlphaPipeline ? 1 : 0,
                m_alphaAdvectPipeline ? 1 : 0, m_airRisePipeline ? 1 : 0, m_volumeCorrectPipeline ? 1 : 0,
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
  // A fresh solver must start from a DEFINED state. New storage volumes hold whatever
  // was in GPU memory: under a scene that has already run its own solver, the allocator
  // hands back its old volume contents, so a caller that only seeds alpha and steps
  // starts from garbage momentum. That is why --fluid-selftest failed only under
  // --scene 7 on Vulkan (divergence ×540, mass -18%), and passed under scenes 0/1/6
  // where the memory happened to be zero. --backyard-selftest had already worked around
  // it by calling ResetToRest(); this fixes it for every caller instead.
  if (!ResetToRest(driver)) {
    T8_LOG_ERROR("[Fluid] Could not clear the initial volume state on %s", driver->ApiTag());
    Shutdown(driver);
    return false;
  }
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
  m_extrapolatePipeline.reset();
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
  m_airRisePipeline.reset();
  m_bodyLevelPipeline.reset();
  m_bodyLevelBuffer.reset();
  m_diagnosticsBuffer.reset();
  m_bodyLevelTicket = -1;
  m_bodyLevelWaitFrames = 0;
  m_bodyLevelCapacity = 0;
  m_maxVelocityPipeline.reset();
  m_maxVelocityBuffer.reset();
  m_maxVelocityTicket = -1;
  m_maxVelocityWaitFrames = 0;
  m_maxVelocityAsyncDisabled = false;
  m_measuredVelocitySum = 0.0f;
  m_lastFrameSubsteps = 1;
  m_lastFrameBudgetLimited = false;
  m_bodyLevelCount = 0;
  m_volumeCorrectPipeline.reset();
  m_volumeReducePipeline.reset();
  m_volumeSums.reset();
  m_volumeSumSlot = 0;
  m_stampBodiesPipeline.reset();
  m_baseMaskBuffer.reset();
  m_objectVelocityPipeline.reset();
  m_clearPipeline.reset();
  m_clearFloatPipeline.reset();
  m_surfaceToBufferPipeline.reset();
  m_surfaceReadbackBuffer.reset();
  m_surfaceReadbackTexels = 0;
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
  if (!m_ready || !driver || !m_volumeCorrectPipeline || !m_volumeReducePipeline) return false;
  if (m_referenceVolumeFraction <= 0.0f) return false;

  // The reference must account for the space the body occupies. Chasing the
  // seed-time total while a ball sits in the water makes the correction pump water
  // straight back into the cavity the body opened: measured column elevations
  // formed an 18 cm dome centred on the ball, and the cavity never survived long
  // enough to launch a wave. The physically correct target is
  // (seed water) - (volume the submerged part of the body displaces).
  float reference = m_referenceVolumeFraction;
  // W-22: sum over every body. Each body's submerged spherical cap displaces its own
  // volume, so the reference target is (seed water) minus all of them.
  for (const FluidSphere& body : m_bodies) {
    if (!body.enabled || body.radius <= 0.0f || body.submerged <= 0.0f) continue;
    const double radius = static_cast<double>(body.radius);
    const double cell = static_cast<double>((std::max)(m_settings.cellSize, 1e-4f));
    const double ballCells = (4.0 / 3.0) * 3.14159265358979 * radius * radius * radius /
                             (cell * cell * cell);
    reference -= static_cast<float>(ballCells * body.submerged);
  }
  if (reference <= 1.0f) reference = m_referenceVolumeFraction;

  // Both slots start at zero; each correction clears the slot the next round uses.
  if (!m_volumeSums) {
    ComputeBufferDesc sumsDesc;
    sumsDesc.byteWidth = 4 * 4 * sizeof(uint32_t);
    sumsDesc.structureStride = 4 * sizeof(uint32_t);
    sumsDesc.access = ComputeBufferAccess::ReadWrite;
    sumsDesc.debugName = "Fluid Volume Sums";
    const std::vector<uint32_t> zero(16, 0u);
    m_volumeSums = driver->CreateComputeBuffer(sumsDesc, zero.data());
    m_volumeSumSlot = 0;
    if (!m_volumeSums) return false;
  }
  const uint32_t slot = m_volumeSumSlot;

  // 1. Total alpha, interface headroom and interface alpha, summed on the GPU. The
  // correction kernel derives the deficit and the same guards from them, so nothing
  // is read back: the old full-volume readback stalled a frame every interval.
  const std::vector<uint32_t> reduceConstants = BuildConstants(slot);
  const std::vector<ComputeBindingDesc> reduceBindings = {
    {ComputeBindingType::Constants32, 0, nullptr, reduceConstants.data(), static_cast<uint32_t>(reduceConstants.size())},
    {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, m_alpha[m_alphaCurrent].get()},
    {ComputeBindingType::ReadWriteBuffer, 0, m_volumeSums.get(), nullptr, 0}
  };
  const uint32_t gx = (m_settings.width + m_volumeCorrectPipeline->threadGroupSize[0] - 1) / m_volumeCorrectPipeline->threadGroupSize[0];
  const uint32_t gy = (m_settings.height + m_volumeCorrectPipeline->threadGroupSize[1] - 1) / m_volumeCorrectPipeline->threadGroupSize[1];
  const uint32_t gz = (m_settings.depth + m_volumeCorrectPipeline->threadGroupSize[2] - 1) / m_volumeCorrectPipeline->threadGroupSize[2];
  if (!driver->DispatchCompute(*m_volumeReducePipeline, reduceBindings, gx, gy, gz)) {
    m_volumeSums.reset();   // a half-written slot must not be reused
    return false;
  }

  // 2. Additive, headroom-weighted distribution. A multiplicative rescale is eaten
  // by the clamp: interface cells sit just under 1, so alpha * 1.03 lands above
  // 1 and saturate() discards the whole gain.
  const uint32_t alphaNext = (m_alphaCurrent + 1) % 2;
  const std::vector<uint32_t> constants = BuildConstants();
  const std::vector<uint32_t> correctionConstants = BuildCorrectionConstants(reference, slot);
  const std::vector<ComputeBindingDesc> bindings = {
    {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
    {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, m_alpha[m_alphaCurrent].get()},
    {ComputeBindingType::Constants32, 1, nullptr, correctionConstants.data(), static_cast<uint32_t>(correctionConstants.size())},
    {ComputeBindingType::ReadWriteTexture, 0, nullptr, nullptr, 0, nullptr, m_alpha[alphaNext].get()},
    {ComputeBindingType::ReadWriteBuffer, 1, m_volumeSums.get(), nullptr, 0}
  };
  if (!driver->DispatchCompute(*m_volumeCorrectPipeline, bindings, gx, gy, gz)) {
    m_volumeSums.reset();
    return false;
  }
  m_alphaCurrent = alphaNext;
  m_volumeSumSlot = slot ^ 1u;

  // The readback is a full GPU stall, so the numbers are only fetched for a debug line.
  if (Log::GetMaxLevel() < Log::LVL_DEBUG) return true;
  uint32_t sums[16] = {};
  if (driver->ReadComputeBuffer(*m_volumeSums, sums, sizeof(sums))) {
    const uint32_t* s = sums + slot * 8u;
    const float total = static_cast<float>(s[0]) + static_cast<float>(s[1]) / 65536.0f;
    const float headroom = static_cast<float>(s[2]) + static_cast<float>(s[3]) / 65536.0f;
    appliedScale = 1.0f + (reference - total) / (std::max)(total, 1.0f);
    T8_LOG_DEBUG("[Fluid] VolumeCorrection t=%.2fs was=%.0f deficit=%.1f headroom=%.0f",
                 m_time, total, reference - total, headroom);
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

bool FluidSimulation::AuditMass(BaseDriver* driver, MassAudit& out) {
  out = MassAudit{};
  std::vector<float> alpha;
  if (!ReadVolumeFraction(driver, alpha)) return false;
  if (!RefreshMaskSnapshot(driver)) return false;

  const uint32_t width = m_settings.width;
  const uint32_t height = m_settings.height;
  const uint32_t depth = m_settings.depth;
  if (alpha.size() != static_cast<size_t>(width) * height * depth) return false;
  const float threshold = m_settings.airThreshold;

  auto solid = [&](int x, int y, int z) {
    if (x < 0 || y < 0 || z < 0 || x >= static_cast<int>(width) || y >= static_cast<int>(height) ||
        z >= static_cast<int>(depth))
      return true;
    return IsMaskSolid(static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(z));
  };
  auto alphaAt = [&](int x, int y, int z) {
    return alpha[static_cast<size_t>(x) + static_cast<size_t>(width) *
                 (static_cast<size_t>(y) + static_cast<size_t>(height) * static_cast<size_t>(z))];
  };
  auto airAt = [&](int x, int y, int z) {
    if (x < 0 || z < 0 || x >= static_cast<int>(width) || z >= static_cast<int>(depth)) return false;
    if (y >= static_cast<int>(height)) return true;   // open air above the domain
    if (y < 0) return false;
    if (solid(x, y, z)) return false;
    return alphaAt(x, y, z) < threshold;
  };

  double bulkSum = 0.0;
  double total = 0.0;
  float bulkMin = 1.0f;
  for (uint32_t z = 0; z < depth; ++z) {
    for (uint32_t x = 0; x < width; ++x) {
      float widthSum = 0.0f;
      for (uint32_t y = 0; y < height; ++y) {
        if (IsMaskSolid(x, y, z)) continue;
        const float a = alphaAt(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z));
        total += a;
        if (a > threshold && a < 0.999f) widthSum += 1.0f;
        if (a >= 0.9999f) ++out.saturatedCells;
        if (a >= threshold) {
          const int xi = static_cast<int>(x), yi = static_cast<int>(y), zi = static_cast<int>(z);
          const bool touchesAir = airAt(xi - 1, yi, zi) || airAt(xi + 1, yi, zi) || airAt(xi, yi - 1, zi) ||
                                  airAt(xi, yi + 1, zi) || airAt(xi, yi, zi - 1) || airAt(xi, yi, zi + 1);
          if (!touchesAir) {
            bulkSum += a;
            bulkMin = (std::min)(bulkMin, a);
            ++out.bulkCells;
            if (a < 0.98f) ++out.bulkBelow98;
          }
        }
      }
      // A column with no partially filled cell is fully wet or fully dry, not a width sample.
      if (widthSum > 0.0f) {
        out.interfaceWidthMean += widthSum;
        out.interfaceWidthMax = (std::max)(out.interfaceWidthMax, widthSum);
        ++out.interfaceColumns;
      }
    }
  }

  out.totalAlpha = static_cast<float>(total);
  // The correction aims at this number, so a disagreement here is a silent offset in
  // every deficit measurement (and in what the correction repays).
  float gpuTotal = 0.0f;
  float gpuInterface = 0.0f;
  if (MeasureVolumeFraction(driver, gpuTotal, gpuInterface)) out.totalAlphaGpu = gpuTotal;

  // Residual divergence of the MAC field, from the same kind of readback. The staggered
  // layout stores the +X/+Y/+Z face of each cell, so the divergence of cell (i,j,k) uses
  // u(i,j,k) - u(i-1,j,k) and so on, with solid faces counting as zero.
  std::vector<float> velocity;
  if (ReadVelocity(driver, velocity) &&
      velocity.size() == static_cast<size_t>(width) * height * depth * 4u) {
    const float invH = 1.0f / (std::max)(m_settings.cellSize, 1e-4f);
    auto faceU = [&](int x, int y, int z, int axis) {
      if (x < 0 || y < 0 || z < 0 || x >= static_cast<int>(width) || y >= static_cast<int>(height) ||
          z >= static_cast<int>(depth))
        return 0.0f;
      const size_t base = (static_cast<size_t>(x) + static_cast<size_t>(width) *
                           (static_cast<size_t>(y) + static_cast<size_t>(height) * static_cast<size_t>(z))) * 4u;
      return velocity[base + static_cast<size_t>(axis)];
    };
    double sumSquares = 0.0;
    float worst = 0.0f;
    uint32_t counted = 0;
    for (uint32_t z = 0; z < depth; ++z) {
      for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
          if (IsMaskSolid(x, y, z)) continue;
          const int xi = static_cast<int>(x), yi = static_cast<int>(y), zi = static_cast<int>(z);
          const float divergence =
            ((faceU(xi, yi, zi, 0) - faceU(xi - 1, yi, zi, 0)) +
             (faceU(xi, yi, zi, 1) - faceU(xi, yi - 1, zi, 1)) +
             (faceU(xi, yi, zi, 2) - faceU(xi, yi, zi - 1, 2))) * invH;
          sumSquares += static_cast<double>(divergence) * divergence;
          worst = (std::max)(worst, std::fabs(divergence));
          ++counted;
        }
      }
    }
    if (counted > 0) {
      out.divergenceRms = static_cast<float>(std::sqrt(sumSquares / counted));
      out.divergenceMax = worst;
    }
  }
  out.referenceAlpha = m_referenceVolumeFraction;
  out.deficit = m_referenceVolumeFraction - static_cast<float>(total);
  if (out.bulkCells > 0) {
    out.bulkMeanAlpha = static_cast<float>(bulkSum / out.bulkCells);
    out.bulkMinAlpha = bulkMin;
  }
  if (out.interfaceColumns > 0)
    out.interfaceWidthMean /= static_cast<float>(out.interfaceColumns);
  return true;
}

bool FluidSimulation::ReadVelocity(BaseDriver* driver, std::vector<float>& out) {
  out.clear();
  if (!m_ready || !driver || !m_velocity[m_current]) return false;
  const uint32_t width = m_settings.width;
  const uint32_t height = m_settings.height;
  const uint32_t depth = m_settings.depth;
  const uint32_t voxelCount = width * height * depth;

  ComputeBufferDesc bufferDesc;
  bufferDesc.byteWidth = voxelCount * 4 * sizeof(uint32_t);
  bufferDesc.structureStride = 4 * sizeof(uint32_t);
  bufferDesc.access = ComputeBufferAccess::ReadWrite;
  bufferDesc.debugName = "Fluid Velocity Readback";
  if (!m_volumeReadbackBuffer || m_volumeReadbackVoxels != voxelCount) {
    std::vector<uint32_t> zeroSeed(static_cast<size_t>(voxelCount) * 4, 0u);
    m_volumeReadbackBuffer = driver->CreateComputeBuffer(bufferDesc, zeroSeed.data());
    if (!m_volumeReadbackBuffer) return false;
    m_volumeReadbackVoxels = voxelCount;
  }

  const std::vector<uint32_t> constants = BuildConstants();
  const std::vector<ComputeBindingDesc> bindings = {
    {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
    {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, m_velocity[m_current].get()},
    {ComputeBindingType::ReadWriteBuffer, 0, m_volumeReadbackBuffer.get(), nullptr, 0}
  };
  const uint32_t gx = (width + m_copyPipeline->threadGroupSize[0] - 1) / m_copyPipeline->threadGroupSize[0];
  const uint32_t gy = (height + m_copyPipeline->threadGroupSize[1] - 1) / m_copyPipeline->threadGroupSize[1];
  const uint32_t gz = (depth + m_copyPipeline->threadGroupSize[2] - 1) / m_copyPipeline->threadGroupSize[2];
  if (!driver->DispatchCompute(*m_copyPipeline, bindings, gx, gy, gz)) return false;

  std::vector<uint32_t> raw(static_cast<size_t>(voxelCount) * 4, 0u);
  if (!driver->ReadComputeBuffer(*m_volumeReadbackBuffer, raw.data(), raw.size() * sizeof(uint32_t)))
    return false;
  out.resize(static_cast<size_t>(voxelCount) * 4);
  std::memcpy(out.data(), raw.data(), static_cast<size_t>(voxelCount) * 4 * sizeof(float));
  return true;
}

bool FluidSimulation::ReadWallMask(BaseDriver* driver, std::vector<uint32_t>& out) {
  out.clear();
  if (!m_ready || !RefreshMaskSnapshot(driver)) return false;
  out = m_maskSnapshot;
  return true;
}

bool FluidSimulation::ReadSurface(BaseDriver* driver, std::vector<float>& out) {
  out.clear();
  if (!m_ready || !driver || !m_surfaceToBufferPipeline) return false;
  Texture* surface = SurfaceTexture();
  if (!surface) return false;

  const uint32_t texels = m_settings.width * m_settings.depth;
  ComputeBufferDesc bufferDesc;
  bufferDesc.byteWidth = texels * 4 * sizeof(uint32_t);
  bufferDesc.structureStride = 4 * sizeof(uint32_t);
  bufferDesc.access = ComputeBufferAccess::ReadWrite;
  bufferDesc.debugName = "Fluid Surface Readback";
  if (!m_surfaceReadbackBuffer || m_surfaceReadbackTexels != texels) {
    std::vector<uint32_t> zeroSeed(static_cast<size_t>(texels) * 4, 0u);
    m_surfaceReadbackBuffer = driver->CreateComputeBuffer(bufferDesc, zeroSeed.data());
    if (!m_surfaceReadbackBuffer) return false;
    m_surfaceReadbackTexels = texels;
  }

  const std::vector<uint32_t> constants = BuildConstants();
  const std::vector<ComputeBindingDesc> bindings = {
    {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
    {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, surface},
    {ComputeBindingType::ReadWriteBuffer, 0, m_surfaceReadbackBuffer.get(), nullptr, 0}
  };
  const uint32_t gx = (m_settings.width + m_surfaceToBufferPipeline->threadGroupSize[0] - 1) / m_surfaceToBufferPipeline->threadGroupSize[0];
  const uint32_t gy = (m_settings.depth + m_surfaceToBufferPipeline->threadGroupSize[1] - 1) / m_surfaceToBufferPipeline->threadGroupSize[1];
  if (!driver->DispatchCompute(*m_surfaceToBufferPipeline, bindings, gx, gy, 1)) return false;

  std::vector<uint32_t> raw(static_cast<size_t>(texels) * 4, 0u);
  if (!driver->ReadComputeBuffer(*m_surfaceReadbackBuffer, raw.data(), raw.size() * sizeof(uint32_t)))
    return false;
  out.resize(static_cast<size_t>(texels) * 4);
  std::memcpy(out.data(), raw.data(), static_cast<size_t>(texels) * 4 * sizeof(float));
  return true;
}

bool FluidSimulation::MeasureVolumeFraction(BaseDriver* driver, float& totalAlpha,
                                            float& interfaceSum) {
  totalAlpha = 0.0f;
  interfaceSum = 0.0f;
  float interfaceHeadroom = 0.0f;
  float interfaceAlpha = 0.0f;
  double totalSum = 0.0;
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
    // Summed in double: 106k float32 additions of values near 1 lose ~25 cells by the time
    // the total reaches 83k (measured: 82 964 in float against 82 941 in double). That
    // inflated the seed reference by the same amount, so the correction was aiming at a
    // number the field could never reach and every deficit reading was 25-30 cells high.
    totalSum += static_cast<double>(value);
    // Same band the correction kernel touches: liquid cells with room, [airThreshold, 0.999).
    if (value >= m_settings.airThreshold && value < 0.999f) {
      interfaceSum += value;
      interfaceHeadroom += 1.0f - value;
      interfaceAlpha += value;
    }
  }
  totalAlpha = static_cast<float>(totalSum);
  m_lastInterfaceHeadroom = interfaceHeadroom;
  m_lastInterfaceAlpha = interfaceAlpha;
  return true;
}

void FluidSimulation::ApplyBodyLevels(const std::vector<float>& results, uint32_t count) {
  const size_t bodies = (std::min)(static_cast<size_t>(count), m_bodies.size());
  for (size_t i = 0; i < bodies; ++i) {
    if (results[i * 4u + 3u] <= 0.0f) continue;   // no open water in the annulus
    FluidSphere& body = m_bodies[i];
    // The surface texture holds a delta from the authored plane.
    body.simulatedLevel = m_settings.waterPlaneY + results[i * 4u + 0u];
    body.flowX = results[i * 4u + 1u];
    body.flowZ = results[i * 4u + 2u];
    body.hasSimulatedLevel = true;
  }
}

void FluidSimulation::QueryBodyWaterLevel(BaseDriver* driver) {
  if (!m_ready || !driver || !m_bodyLevelPipeline || m_bodies.empty()) return;
  // Headless runs keep the authored plane: gate 7 with simulated buoyancy is open work (W-06 step 4).
  Texture* surface = m_surfaceOutput;
  if (!surface) return;

  // Consume the answer in flight first; its copy retires one to three frames after it was
  // queued. Each body keeps its last answer until the next one arrives.
  if (m_bodyLevelTicket > 0) {
    std::vector<float> results(static_cast<size_t>(m_bodyLevelCount) * 4u, 0.0f);
    if (!driver->ConsumeComputeReadback(m_bodyLevelTicket, results.data(),
                                        results.size() * sizeof(float))) {
      if (++m_bodyLevelWaitFrames == 60)
        T8_LOG_ERROR("[Fluid] Body water level readback still pending after 60 frames on %s; "
                     "every body is floating on a stale level", driver->ApiTag());
      return;   // one query in flight: re-requesting would orphan the ticket
    }
    m_bodyLevelTicket = -1;
    m_bodyLevelWaitFrames = 0;
    // Bodies spawned or cleared since the request would take another body's answer.
    if (m_bodyLevelGeneration == m_bodyListGeneration)
      ApplyBodyLevels(results, m_bodyLevelCount);
  }

  const uint32_t bodyCount = static_cast<uint32_t>(m_bodies.size());
  const size_t readBytes = static_cast<size_t>(bodyCount) * 4u * sizeof(float);
  if (!m_bodyLevelBuffer || m_bodyLevelCapacity < bodyCount) {
    const uint32_t capacity = (std::max)(bodyCount, 2u * m_bodyLevelCapacity);
    ComputeBufferDesc desc;
    desc.byteWidth = capacity * 4u * static_cast<uint32_t>(sizeof(float));
    desc.structureStride = 4u * static_cast<uint32_t>(sizeof(float));
    desc.access = ComputeBufferAccess::ReadWrite;
    desc.debugName = "Fluid Body Water Level";
    m_bodyLevelBuffer = driver->CreateComputeBuffer(desc);
    if (!m_bodyLevelBuffer) { m_bodyLevelCapacity = 0; return; }
    m_bodyLevelCapacity = capacity;
  }

  std::vector<float> bodyList(static_cast<size_t>(bodyCount) * 4u, 0.0f);
  for (uint32_t i = 0; i < bodyCount; ++i) {
    const FluidSphere& body = m_bodies[i];
    bodyList[i * 4u + 0u] = body.x;
    bodyList[i * 4u + 1u] = body.y;
    bodyList[i * 4u + 2u] = body.z;
    bodyList[i * 4u + 3u] = body.enabled ? body.radius : 0.0f;
  }
  struct LevelConstants {
    float basinMinX, basinMinY, basinMinZ, waterPlaneY;
    float stepX, stepY, stepZ;
    uint32_t bodyCount;
  } level{m_settings.basinMinX, m_settings.basinMinY, m_settings.basinMinZ, m_settings.waterPlaneY,
          (m_settings.basinMaxX - m_settings.basinMinX) / static_cast<float>(m_settings.width),
          (m_settings.basinMaxY - m_settings.basinMinY) / static_cast<float>(m_settings.height),
          (m_settings.basinMaxZ - m_settings.basinMinZ) / static_cast<float>(m_settings.depth),
          bodyCount};
  std::array<uint32_t, 8> levelWords{};
  static_assert(sizeof(LevelConstants) == sizeof(levelWords));
  std::memcpy(levelWords.data(), &level, sizeof(level));

  const std::vector<uint32_t> constants = BuildConstants();
  ComputeBindingDesc bodyBinding;
  bodyBinding.type = ComputeBindingType::ReadOnlyBuffer;
  bodyBinding.shaderRegister = 9;
  bodyBinding.inlineData = bodyList.data();
  bodyBinding.inlineBytes = static_cast<uint32_t>(bodyList.size() * sizeof(float));
  bodyBinding.inlineStride = 4u * sizeof(float);
  const std::vector<ComputeBindingDesc> bindings = {
    {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
    {ComputeBindingType::Constants32, 1, nullptr, levelWords.data(), static_cast<uint32_t>(levelWords.size())},
    {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, surface},
    {ComputeBindingType::ReadOnlyTexture, 1, nullptr, nullptr, 0, nullptr, m_velocity[m_current].get()},
    {ComputeBindingType::ReadOnlyBuffer, 8, m_wallMask.get(), nullptr, 0},
    bodyBinding,
    {ComputeBindingType::ReadWriteBuffer, 0, m_bodyLevelBuffer.get(), nullptr, 0}
  };
  const uint32_t groups = (bodyCount + m_bodyLevelPipeline->threadGroupSize[0] - 1) /
                          m_bodyLevelPipeline->threadGroupSize[0];
  if (!driver->DispatchCompute(*m_bodyLevelPipeline, bindings, groups, 1, 1)) return;

  m_bodyLevelCount = bodyCount;
  m_bodyLevelGeneration = m_bodyListGeneration;
  m_bodyLevelTicket = driver->RequestComputeReadback(*m_bodyLevelBuffer, readBytes);
  if (m_bodyLevelTicket > 0) return;

  // Backend without an asynchronous path: pay one stall rather than lose the feature.
  std::vector<float> results(static_cast<size_t>(bodyCount) * 4u, 0.0f);
  if (driver->ReadComputeBuffer(*m_bodyLevelBuffer, results.data(), readBytes))
    ApplyBodyLevels(results, bodyCount);
}

void FluidSimulation::QueryMaxVelocity(BaseDriver* driver) {
  if (!m_ready || !driver || !m_maxVelocityPipeline) return;

  // The readback is a full 256-byte cache line even though only word 0 carries the answer. A 4-byte
  // copy removed the D3D12 device (DXGI_ERROR_DEVICE_HUNG) on the first frame that issued one, while
  // the same dispatches without the copy ran clean on all five APIs.
  constexpr uint32_t kMaxVelocityBytes = 256;
  constexpr size_t kMaxVelocityWords = kMaxVelocityBytes / sizeof(uint32_t);

  // Consume the answer already in flight. One request at a time, exactly like the body-level query.
  if (m_maxVelocityTicket > 0) {
    std::array<uint32_t, kMaxVelocityWords> words{};
    if (!driver->ConsumeComputeReadback(m_maxVelocityTicket, words.data(), kMaxVelocityBytes)) {
      ++m_maxVelocityWaitFrames;
      // A headless or fixed-step run never completes a frame, so the per-frame async slot can never
      // deliver. Switch to the synchronous read once instead of erroring every frame.
      if (m_maxVelocityWaitFrames >= 3 && !m_maxVelocityAsyncDisabled) {
        m_maxVelocityAsyncDisabled = true;
        T8_LOG_DEBUG("[Fluid] Max-velocity readback has not completed in %u frames on %s; using the "
                     "synchronous path", m_maxVelocityWaitFrames, driver->ApiTag());
      }
      if (!m_maxVelocityAsyncDisabled) return;
      m_maxVelocityTicket = -1;
      m_maxVelocityWaitFrames = 0;
    } else {
      m_maxVelocityTicket = -1;
      m_maxVelocityWaitFrames = 0;
      float measured = 0.0f;
      std::memcpy(&measured, &words[0], sizeof(measured));
      // A zero means the kernel cleared the accumulator and nothing was written, which happens on a
      // perfectly still start; keep the previous value rather than sizing substeps from nothing.
      if (measured > 0.0f && std::isfinite(measured)) m_measuredVelocitySum = measured;
    }
  }

  if (!m_maxVelocityBuffer) {
    ComputeBufferDesc desc;
    desc.byteWidth = kMaxVelocityBytes;
    desc.structureStride = static_cast<uint32_t>(sizeof(uint32_t));
    desc.access = ComputeBufferAccess::ReadWrite;
    desc.debugName = "Fluid Max Velocity";
    m_maxVelocityBuffer = driver->CreateComputeBuffer(desc);
    if (!m_maxVelocityBuffer) return;
  }

  // Positional aggregate: type, register, buffer, constants, constantCount, texture, texture3D.
  // A volume goes in the LAST field; one slot early lands in `inlineData` and the dispatch is
  // rejected as "invalid pipeline or zero dispatch extent".
  const ComputeBindingDesc volume = {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0,
                                     nullptr, m_velocity[m_current].get()};
  const ComputeBindingDesc accumulator = {ComputeBindingType::ReadWriteBuffer, 0, m_maxVelocityBuffer.get(),
                                          nullptr, 0};

  // Clear the accumulator first: the kernel uses the Iteration slot as a reset sentinel, because the
  // abstraction deliberately has no buffer-write API.
  std::vector<uint32_t> clearConstants = BuildConstants();
  if (clearConstants.size() > 3) clearConstants[3] = 0xFFFFFFFFu;
  const std::vector<ComputeBindingDesc> clearBindings = {
    {ComputeBindingType::Constants32, 0, nullptr, clearConstants.data(),
     static_cast<uint32_t>(clearConstants.size())},
    volume, accumulator
  };
  if (!driver->DispatchCompute(*m_maxVelocityPipeline, clearBindings, 1, 1, 1)) return;

  const std::vector<uint32_t> constants = BuildConstants();
  const std::vector<ComputeBindingDesc> bindings = {
    {ComputeBindingType::Constants32, 0, nullptr, constants.data(),
     static_cast<uint32_t>(constants.size())},
    volume, accumulator
  };
  const uint32_t gx = (m_settings.width + m_maxVelocityPipeline->threadGroupSize[0] - 1u) /
                      m_maxVelocityPipeline->threadGroupSize[0];
  const uint32_t gy = (m_settings.height + m_maxVelocityPipeline->threadGroupSize[1] - 1u) /
                      m_maxVelocityPipeline->threadGroupSize[1];
  const uint32_t gz = (m_settings.depth + m_maxVelocityPipeline->threadGroupSize[2] - 1u) /
                      m_maxVelocityPipeline->threadGroupSize[2];
  if (!driver->DispatchCompute(*m_maxVelocityPipeline, bindings, gx, gy, gz)) return;

  if (!m_maxVelocityAsyncDisabled) {
    m_maxVelocityTicket = driver->RequestComputeReadback(*m_maxVelocityBuffer, kMaxVelocityBytes);
    if (m_maxVelocityTicket > 0) return;
  }
  std::array<uint32_t, kMaxVelocityWords> words{};
  if (driver->ReadComputeBuffer(*m_maxVelocityBuffer, words.data(), kMaxVelocityBytes)) {
    float measured = 0.0f;
    std::memcpy(&measured, &words[0], sizeof(measured));
    if (measured > 0.0f && std::isfinite(measured)) m_measuredVelocitySum = measured;
  }
}

void FluidSimulation::AdvanceSphere(float dt) {
  if (dt <= 0.0f) return;
  for (FluidSphere& body : m_bodies) AdvanceOneSphere(body, dt);
  // W-22: without contact resolution two overlapping bodies stamp the same cells and
  // the later index simply wins, which looks like one ball swallowing the other.
  // Two passes settle a short chain (a ball resting on another ball).
  for (int pass = 0; pass < 2; ++pass) ResolveBodyContacts();
}

void FluidSimulation::ResolveBodyContacts() {
  // Fixed i < j order and inverse-mass weighting: deterministic, so a spawnSeed gives
  // the same run every time.
  auto resolvePair = [this](size_t i, size_t j) {
      FluidSphere& a = m_bodies[i];
      FluidSphere& b = m_bodies[j];
      const float dx = b.x - a.x;
      const float dy = b.y - a.y;
      const float dz = b.z - a.z;
      const float sum = a.radius + b.radius;
      const float distSq = dx * dx + dy * dy + dz * dz;
      if (distSq >= sum * sum || distSq <= 1e-12f) return;

      const float dist = std::sqrt(distSq);
      const float nx = dx / dist, ny = dy / dist, nz = dz / dist;
      const float overlap = sum - dist;
      // Density ratio stands in for mass: both bodies are spheres of unit volume
      // factor, so a denser ball moves less. Clamped like the integrator's terms.
      // A held body behaves as infinitely heavy.
      const float invA = a.held ? 0.0f : 1.0f / (std::max)(a.densityRatio, 0.02f);
      const float invB = b.held ? 0.0f : 1.0f / (std::max)(b.densityRatio, 0.02f);
      const float invSum = invA + invB;
      if (invSum <= 0.0f) return;

      const float shareA = invA / invSum;
      const float shareB = invB / invSum;
      a.x -= nx * overlap * shareA;
      a.y -= ny * overlap * shareA;
      a.z -= nz * overlap * shareA;
      b.x += nx * overlap * shareB;
      b.y += ny * overlap * shareB;
      b.z += nz * overlap * shareB;

      // Normal impulse, only when they are closing.
      const float rel = (b.vx - a.vx) * nx + (b.vy - a.vy) * ny + (b.vz - a.vz) * nz;
      if (rel >= 0.0f) return;
      const float restitution =
          (std::min)((std::max)(0.5f * (a.restitution + b.restitution), 0.0f), 0.99f);
      const float impulse = -(1.0f + restitution) * rel / invSum;
      a.vx -= impulse * invA * nx;
      a.vy -= impulse * invA * ny;
      a.vz -= impulse * invA * nz;
      b.vx += impulse * invB * nx;
      b.vy += impulse * invB * ny;
      b.vz += impulse * invB * nz;
  };

  // Small lists test every pair, exactly as before, so existing runs are unchanged.
  constexpr size_t kBroadPhaseThreshold = 32;
  if (m_bodies.size() <= kBroadPhaseThreshold) {
    for (size_t i = 0; i < m_bodies.size(); ++i) {
      if (!m_bodies[i].enabled) continue;
      for (size_t j = i + 1; j < m_bodies.size(); ++j) {
        if (m_bodies[j].enabled) resolvePair(i, j);
      }
    }
    return;
  }

  // Any number of bodies: a uniform grid as wide as the largest diameter puts every
  // overlapping pair in the same or a neighbouring cell. Each body's candidates are
  // resolved in ascending index, so the order stays deterministic.
  float maxRadius = 0.0f;
  for (const FluidSphere& body : m_bodies)
    if (body.enabled) maxRadius = (std::max)(maxRadius, body.radius);
  if (maxRadius <= 0.0f) return;
  const float gridCell = 2.0f * maxRadius;
  auto cellKey = [](int x, int y, int z) {
    return (static_cast<int64_t>(x & 0x1FFFFF) << 42) |
           (static_cast<int64_t>(y & 0x1FFFFF) << 21) |
           static_cast<int64_t>(z & 0x1FFFFF);
  };
  std::vector<std::array<int, 3>> cellOf(m_bodies.size());
  std::unordered_map<int64_t, std::vector<uint32_t>> grid;
  grid.reserve(m_bodies.size());
  for (size_t i = 0; i < m_bodies.size(); ++i) {
    const FluidSphere& body = m_bodies[i];
    if (!body.enabled) continue;
    cellOf[i] = {static_cast<int>(std::floor(body.x / gridCell)),
                 static_cast<int>(std::floor(body.y / gridCell)),
                 static_cast<int>(std::floor(body.z / gridCell))};
    grid[cellKey(cellOf[i][0], cellOf[i][1], cellOf[i][2])].push_back(static_cast<uint32_t>(i));
  }
  std::vector<uint32_t> candidates;
  for (size_t i = 0; i < m_bodies.size(); ++i) {
    if (!m_bodies[i].enabled) continue;
    candidates.clear();
    for (int oz = -1; oz <= 1; ++oz)
      for (int oy = -1; oy <= 1; ++oy)
        for (int ox = -1; ox <= 1; ++ox) {
          const auto found = grid.find(cellKey(cellOf[i][0] + ox, cellOf[i][1] + oy, cellOf[i][2] + oz));
          if (found == grid.end()) continue;
          for (uint32_t j : found->second)
            if (j > i) candidates.push_back(j);
        }
    std::sort(candidates.begin(), candidates.end());
    for (uint32_t j : candidates) resolvePair(i, j);
  }
}

float FluidSimulation::ExposedFrontalFraction(float submergedDepth, float radius) {
  // Circular segment above the waterline over the full disc. `exposed` is the dry cap height,
  // `offset` its centre distance from the sphere's centre, so the segment subtends
  // 2·acos(offset/r) and its area is r²·acos(offset/r) − offset·sqrt(r² − offset²).
  const float r = (std::max)(radius, 1e-6f);
  const float h = (std::min)((std::max)(submergedDepth, 0.0f), 2.0f * r);
  const float exposed = 2.0f * r - h;
  if (exposed <= 0.0f) return 0.0f;
  if (exposed >= 2.0f * r) return 1.0f;
  const float offset = r - exposed;
  const float ratio = (std::min)((std::max)(offset / r, -1.0f), 1.0f);
  const float segment = r * r * std::acos(ratio) - offset * std::sqrt((std::max)(r * r - offset * offset, 0.0f));
  const float fraction = segment / (3.14159265358979323846f * r * r);
  return (std::min)((std::max)(fraction, 0.0f), 1.0f);
}

void FluidSimulation::AdvanceOneSphere(FluidSphere& s, float dt) {
  if (!s.enabled || dt <= 0.0f) return;

  const float floorY = m_settings.basinMinY;
  const float radius = (std::max)(s.radius, 0.001f);

  // W-06: buoyancy follows the simulated surface under the body, not the authored plane, so
  // the body rides waves and rises in the trough they leave. The level arrives from the
  // asynchronous ring query two or three frames late; until the first result the authored
  // plane stands in.
  const float waterLevel = s.hasSimulatedLevel ? s.simulatedLevel : m_settings.waterPlaneY;

  // Spherical-cap volume fraction: f = h^2(3r-h)/(4r^3)
  const float h = (std::min)((std::max)(waterLevel - (s.y - radius), 0.0f), 2.0f * radius);
  s.submerged = (h * h * (3.0f * radius - h)) / (4.0f * radius * radius * radius);

  const float ratio = (std::max)(s.densityRatio, 0.02f);
  const float addedMass = 0.5f;
  const float effectiveMass = ratio + addedMass * s.submerged;

  // Net vertical acceleration: a_y = g * (f - rho) / (rho + Ca*f)
  const float accel = m_settings.gravity * (s.submerged - ratio) / effectiveMass;

  // W-06 step 3: drag acts on the velocity RELATIVE to the surrounding water, so a current
  // carries the body instead of the water acting as an absolute brake. The current is the
  // annulus mean from the same query as the level, so it lags by the same two or three frames.
  const float relVx = s.vx - s.flowX;
  const float relVz = s.vz - s.flowZ;
  const float speed = std::sqrt(relVx * relVx + s.vy * s.vy + relVz * relVz);
  float dragScale = 0.0f;
  if (s.submerged > 0.0f && speed > 1e-5f) {
    const float dragAccel = (3.0f * s.dragCoefficient * s.submerged * speed * speed) /
                            (8.0f * radius * effectiveMass);
    dragScale = dragAccel / speed;
  }

  // W-30: wind on the exposed cap. Quadratic air drag, horizontal only: the vertical term is
  // under 2 % of g for a falling ball and would shift the drop timing gates 7 and 8 rely on.
  // The reference velocity is the wind relative to the body, and the density ratio converts the
  // water-density mass the integrator uses into the air-density force. With `airDrag` at 0 (or a
  // fully submerged body, where the exposed fraction is 0) this is exactly zero and the update
  // below collapses to the pre-W-30 formula.
  float airScale = 0.0f;
  if (s.airDragCoefficient > 0.0f && s.submerged < 1.0f) {
    const float exposed = ExposedFrontalFraction(h, radius);
    if (exposed > 0.0f) {
      const float windRelX = m_settings.windX - s.vx;
      const float windRelZ = m_settings.windZ - s.vz;
      const float windSpeed = std::sqrt(windRelX * windRelX + windRelZ * windRelZ);
      if (windSpeed > 1e-5f) {
        const float airAccel = (kAirDensity / kWaterDensity) *
                               (3.0f * s.airDragCoefficient * exposed * windSpeed * windSpeed) /
                               (8.0f * radius * effectiveMass);
        airScale = airAccel / windSpeed;
      }
    }
  }

  if (!s.held) {
    s.vy += accel * dt;
    // Wave-radiation damping of heave, as a damping ratio of the hydrostatic natural
    // frequency; the solver applies no pressure force back on the body.
    if (h > 0.0f && h < 2.0f * radius) {
      const float stiffness = 3.0f * h * (2.0f * radius - h) / (4.0f * radius * radius * radius);
      const float omega = std::sqrt(m_settings.gravity * stiffness / effectiveMass);
      s.vy /= 1.0f + 2.0f * (std::max)(s.heaveDamping, 0.0f) * omega * dt;
    }
    // Implicit drag integration over BOTH media: (v - flow) /= (1 + (a_drag/|v_rel|) * dt) for
    // the water, plus the same treatment toward the wind for the air. Solving the two together
    // keeps the body asymptotically matching a steady current *and* drifting at the leeway the
    // wind implies, instead of one pulling the other to rest. With airScale = 0 the x/z lines are
    // algebraically identical to the previous single-medium version.
    const float dragFactor = 1.0f + dragScale * dt;
    const float combined = 1.0f + (dragScale + airScale) * dt;
    s.vx = (s.vx + dt * (dragScale * s.flowX + airScale * m_settings.windX)) / combined;
    s.vy /= dragFactor;
    s.vz = (s.vz + dt * (dragScale * s.flowZ + airScale * m_settings.windZ)) / combined;
  }

  s.x += s.vx * dt;
  s.y += s.vy * dt;
  s.z += s.vz * dt;

  const float bounce = (std::min)((std::max)(s.restitution, 0.0f), 0.99f);

  // Collision with authored pool intrusions
  for (const FluidSolidBox& box : m_solidBoxes) {
    float cx = (std::min)((std::max)(s.x, box.minX), box.maxX);
    float cy = (std::min)((std::max)(s.y, box.minY), box.maxY);
    float cz = (std::min)((std::max)(s.z, box.minZ), box.maxZ);

    float dx = s.x - cx;
    float dy = s.y - cy;
    float dz = s.z - cz;
    float distSq = dx * dx + dy * dy + dz * dz;

    if (distSq < radius * radius) {
      float dist = std::sqrt(distSq);
      float nx, ny, nz;
      if (dist > 1e-6f) {
        nx = dx / dist;
        ny = dy / dist;
        nz = dz / dist;
        float pen = radius - dist;
        s.x += nx * pen;
        s.y += ny * pen;
        s.z += nz * pen;
      } else {
        // Centre inside box: push out along axis of least penetration
        float px = (std::min)(s.x - box.minX, box.maxX - s.x);
        float py = (std::min)(s.y - box.minY, box.maxY - s.y);
        float pz = (std::min)(s.z - box.minZ, box.maxZ - s.z);
        if (px < py && px < pz) {
          nx = (s.x < (box.minX + box.maxX) * 0.5f) ? -1.0f : 1.0f;
          ny = 0.0f;
          nz = 0.0f;
          s.x += nx * (px + radius);
        } else if (py < pz) {
          nx = 0.0f;
          ny = (s.y < (box.minY + box.maxY) * 0.5f) ? -1.0f : 1.0f;
          nz = 0.0f;
          s.y += ny * (py + radius);
        } else {
          nx = 0.0f;
          ny = 0.0f;
          nz = (s.z < (box.minZ + box.maxZ) * 0.5f) ? -1.0f : 1.0f;
          s.z += nz * (pz + radius);
        }
      }

      // Reflect velocity if pointing into the box
      float vDotN = s.vx * nx + s.vy * ny + s.vz * nz;
      if (vDotN < 0.0f) {
        s.vx -= (1.0f + bounce) * vDotN * nx;
        s.vy -= (1.0f + bounce) * vDotN * ny;
        s.vz -= (1.0f + bounce) * vDotN * nz;
        // Multiply tangential components by restitution
        float vn = s.vx * nx + s.vy * ny + s.vz * nz;
        s.vx = vn * nx + (s.vx - vn * nx) * bounce;
        s.vy = vn * ny + (s.vy - vn * ny) * bounce;
        s.vz = vn * nz + (s.vz - vn * nz) * bounce;
      }
    }
  }

  // Keep the body inside the basin: resting on the floor, and against the walls.
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
  if (!m_ready || !driver || !m_clearPipeline || !m_clearFloatPipeline) return false;

  const std::vector<uint32_t> constants = BuildConstants();
  const uint32_t px = (m_settings.width + m_clearPipeline->threadGroupSize[0] - 1) / m_clearPipeline->threadGroupSize[0];
  const uint32_t py = (m_settings.height + m_clearPipeline->threadGroupSize[1] - 1) / m_clearPipeline->threadGroupSize[1];
  const uint32_t pz = (m_settings.depth + m_clearPipeline->threadGroupSize[2] - 1) / m_clearPipeline->threadGroupSize[2];

  auto clearWith = [&](ComputePipeline& pipeline, ComputeTexture3D* volume) {
    if (!volume) return true;
    const std::vector<ComputeBindingDesc> bindings = {
      {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
      {ComputeBindingType::ReadWriteTexture, 0, nullptr, nullptr, 0, nullptr, volume}
    };
    return driver->DispatchCompute(pipeline, bindings, px, py, pz);
  };
  auto clear = [&](ComputeTexture3D* volume) { return clearWith(*m_clearPipeline, volume); };
  auto clearScalar = [&](ComputeTexture3D* volume) { return clearWith(*m_clearFloatPipeline, volume); };

  // Clearing momentum is the point of a reset: without it the next drop lands in
  // water the previous one set moving, and two runs at different heights are not
  // comparable.
  if (!clear(m_velocity[0].get()) || !clear(m_velocity[1].get())) return false;
  if (!clearScalar(m_pressure[0].get()) || !clearScalar(m_pressure[1].get())) return false;
  if (!clearScalar(m_divergence.get())) return false;
  if (!clearScalar(m_alpha[0].get()) || !clearScalar(m_alpha[1].get())) return false;

  m_current = 0;
  m_pressureCurrent = 0;
  m_time = 0.0f;
  m_framesSinceVolumeCheck = 0;
  // Force the next frame to re-stamp (or clear) the body footprint.
  m_maskBodyValid = false;

  // Re-seed the volume fraction and lock the reference mass to the resting pool.
  return SeedVolumeFraction(driver);
}

bool FluidSimulation::StepObjectVelocity(BaseDriver* driver) {
  if (!m_ready || !driver || !m_objectVelocityPipeline || !m_velocity[m_current]) return false;

  // Solid-face boundary values (static walls 0, body faces = body velocity). On the
  // staggered grid this is the whole moving-wall condition, so it runs every substep.
  const uint32_t target = (m_current + 1) % kVelocityCount;
  const std::vector<uint32_t> constants = BuildConstants();
  const std::vector<uint32_t> objectConstants = BuildObjectConstants();
  const std::vector<float> bodyVelocities = BuildBodyVelocities();
  std::vector<ComputeBindingDesc> bindings = {
    {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
    {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, m_velocity[m_current].get()},
    {ComputeBindingType::Constants32, 1, nullptr, objectConstants.data(), static_cast<uint32_t>(objectConstants.size())},
    {ComputeBindingType::ReadOnlyBuffer, 8, m_wallMask.get(), nullptr, 0},
    {ComputeBindingType::ReadWriteTexture, 0, nullptr, nullptr, 0, nullptr, m_velocity[target].get()}
  };
  ComputeBindingDesc velocityBinding;
  velocityBinding.type = ComputeBindingType::ReadOnlyBuffer;
  velocityBinding.shaderRegister = 9;
  velocityBinding.inlineData = bodyVelocities.data();
  velocityBinding.inlineBytes = static_cast<uint32_t>(bodyVelocities.size() * sizeof(float));
  velocityBinding.inlineStride = 4u * sizeof(float);
  bindings.push_back(velocityBinding);
  const uint32_t gx = (m_settings.width + m_objectVelocityPipeline->threadGroupSize[0] - 1) / m_objectVelocityPipeline->threadGroupSize[0];
  const uint32_t gy = (m_settings.height + m_objectVelocityPipeline->threadGroupSize[1] - 1) / m_objectVelocityPipeline->threadGroupSize[1];
  const uint32_t gz = (m_settings.depth + m_objectVelocityPipeline->threadGroupSize[2] - 1) / m_objectVelocityPipeline->threadGroupSize[2];
  if (!driver->DispatchCompute(*m_objectVelocityPipeline, bindings, gx, gy, gz))
    return false;
  m_current = target;
  return true;
}

bool FluidSimulation::DispatchVolume(BaseDriver* driver, ComputePipeline& pipeline,
                                     ComputeTexture3D* sampledA, ComputeTexture3D* sampledB,
                                     ComputeTexture3D* target, uint32_t iterationPacked,
                                     bool withAlpha, float paddingOverride,
                                     bool withBodies) {
  const std::vector<uint32_t> constants = BuildConstants(iterationPacked, paddingOverride);
  const std::vector<uint32_t> objectConstants = withBodies ? BuildObjectConstants() : std::vector<uint32_t>();
  const std::vector<float> bodyVelocities = withBodies ? BuildBodyVelocities() : std::vector<float>();
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
  // Only the body kernels (gradient) declare b1 and t9: the body count and one
  // velocity per body, the latter uploaded per dispatch so the count is unbounded.
  if (withBodies) {
    bindings.push_back({ComputeBindingType::Constants32, 1, nullptr, objectConstants.data(),
                        static_cast<uint32_t>(objectConstants.size())});
    ComputeBindingDesc velocityBinding;
    velocityBinding.type = ComputeBindingType::ReadOnlyBuffer;
    velocityBinding.shaderRegister = 9;
    velocityBinding.inlineData = bodyVelocities.data();
    velocityBinding.inlineBytes = static_cast<uint32_t>(bodyVelocities.size() * sizeof(float));
    velocityBinding.inlineStride = 4u * sizeof(float);
    bindings.push_back(velocityBinding);
  }

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
  uint32_t bodySubsteps = 1;
  float bodySpeed = 0.0f;
  // W-22: the fastest body sets the substep count. A slow body does not need its own
  // cadence: every body is stamped and coupled in every substep.
  for (const FluidSphere& body : m_bodies) {
    if (!body.enabled) continue;
    const float speed = std::sqrt(body.vx * body.vx +
                                  body.vy * body.vy +
                                  body.vz * body.vz);
    bodySpeed = (std::max)(bodySpeed, speed);
  }
  if (bodySpeed > 0.0f) {
    const float travel = bodySpeed * frameDt;
    if (travel > 0.5f * cell)
      bodySubsteps = static_cast<uint32_t>(std::ceil(travel / (0.5f * cell)));
    bodySubsteps = (std::min)((std::max)(bodySubsteps, 1u), 12u);
  }

  // W-31: the substep size follows the flow, not a fixed worst case.
  //
  // The transport's limit is (|u|+|v|+|w|) * dt / h <= 1, and the measured pool maximum is ~1.4 m/s
  // in a splash against the ~10.8 m/s a fixed 1/120 s implies. Sizing each substep from the measured
  // bound is what turns the device slow motion (4 substeps per 1/30 s frame whatever the water is
  // doing) into real time on calm water while still subdividing a splash.
  float subDtTarget = m_settings.maxSubstepDt > 0.0f ? m_settings.maxSubstepDt : frameDt;
  if (m_settings.adaptiveSubsteps) {
    // Lagged by one to three frames through the asynchronous readback, so add margin, and take the
    // body's own speed because a splash starts at the body before the field shows it. The floor keeps
    // a settling pool from asking for an absurd step every frame.
    const float uBound = (std::max)({1.25f * m_measuredVelocitySum,
                                     1.7321f * bodySpeed,
                                     (std::max)(m_settings.minCflSpeed, 0.05f)});
    const float cfl = (std::min)((std::max)(m_settings.cflNumber, 0.1f), 1.0f);
    const float cflDt = cfl * cell / uBound;
    const float ceiling = m_settings.maxSubstepDt > 0.0f ? m_settings.maxSubstepDt : frameDt;
    subDtTarget = (std::min)((std::max)(cflDt, 1.0f / 480.0f), ceiling);
  }

  uint32_t substeps = bodySubsteps;
  if (subDtTarget > 0.0f)
    substeps = (std::max)(substeps,
                          static_cast<uint32_t>(std::ceil(frameDt / subDtTarget)));
  // Clamp the REQUEST first, then size the substep from it: that is the step the flow needs. The
  // budget below limits how much time a frame simulates, never the step size, because stretching the
  // step back to frameDt would break the CFL bound this whole calculation exists to respect.
  const uint32_t requestedSubsteps = (std::min)((std::max)(substeps, 1u), 12u);
  const uint32_t budget = (std::max)(m_settings.maxSubstepsPerFrame, 1u);
  const uint32_t simulatedSubsteps = (std::min)(requestedSubsteps, budget);
  m_lastFrameBudgetLimited = requestedSubsteps > simulatedSubsteps;
  m_lastFrameSubsteps = simulatedSubsteps;

  T8_LOG_DEBUG("[Fluid] substeps=%u requested=%u bodySubsteps=%u bodySpeed=%.2f m/s "
               "uBound=%.2f m/s subDt=%.5f cell=%.3f m budgetLimited=%d",
               simulatedSubsteps, requestedSubsteps, bodySubsteps, bodySpeed,
               1.25f * m_measuredVelocitySum, subDtTarget, cell, m_lastFrameBudgetLimited ? 1 : 0);

  const float subDt = frameDt / static_cast<float>(requestedSubsteps);
  m_settings.deltaTime = subDt;
  bool ok = true;
  for (uint32_t substep = 0; substep < simulatedSubsteps && ok; ++substep)
    ok = StepInternal(driver, substep + 1 == simulatedSubsteps);
  // Restore the frame delta so the caller and the GUI keep seeing the real value.
  m_settings.deltaTime = frameDt;

  // Volume correction reads the field back to the CPU, which is a hard sync.
  // Doing that every substep would stall the pipeline and churn Vulkan
  // descriptors, and mass drifts slowly, so it runs periodically per frame.
  if (m_alphaAdvectPipeline && m_alpha[0] && m_alpha[1]) {
    if (++m_framesSinceVolumeCheck >= m_volumeCheckInterval) {
      m_framesSinceVolumeCheck = 0;
      float appliedScale = 1.0f;
      CorrectVolumeFraction(driver, appliedScale);
    }
  }

  // W-31: measure the bound for the NEXT frame here, after the last substep. Querying before the
  // substeps would size them from the previous frame's field.
  if (m_settings.adaptiveSubsteps && ok) QueryMaxVelocity(driver);
  return ok;
}

bool FluidSimulation::StepInternal(BaseDriver* driver, bool writeSurface) {
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
                      m_velocity[forced].get(), 0, true, m_settings.forcingAmplitude))
    return false;
  m_current = forced;

  // 2.5 Solid boundaries, including the moving body. Must precede divergence so
  // the pressure solve sees the body's motion.
  if (!StepObjectVelocity(driver))
    return false;

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
                      m_pressure[m_pressureCurrent].get(), m_velocity[projected].get(), 0, true,
                      -1.0f, true))
    return false;
  m_current = projected;

  // Air is never projected, so overwrite it with the neighbouring liquid velocity.
  if (m_extrapolatePipeline) {
    const uint32_t extrapolated = (m_current + 1) % kVelocityCount;
    if (!DispatchVolume(driver, *m_extrapolatePipeline, m_velocity[m_current].get(),
                        m_alpha[m_alphaCurrent].get(), m_velocity[extrapolated].get(), 0, false))
      return false;
    m_current = extrapolated;
  }

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

    // W-29: let the air the advection carried under the surface rise back out.
    if (m_settings.airRiseSpeed > 0.0f) {
      const uint32_t riseNext = (m_alphaCurrent + 1) % 2;
      const std::vector<uint32_t> riseConstants = BuildConstants(0, m_settings.airRiseSpeed);
      const std::vector<ComputeBindingDesc> riseBindings = {
        {ComputeBindingType::Constants32, 0, nullptr, riseConstants.data(),
         static_cast<uint32_t>(riseConstants.size())},
        {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, m_alpha[m_alphaCurrent].get()},
        {ComputeBindingType::ReadWriteTexture, 0, nullptr, nullptr, 0, nullptr, m_alpha[riseNext].get()},
        {ComputeBindingType::ReadOnlyBuffer, 8, m_wallMask.get(), nullptr, 0}
      };
      if (!driver->DispatchCompute(*m_airRisePipeline, riseBindings, ax, ay, az))
        return false;
      m_alphaCurrent = riseNext;
    }
  }

  // The body's own footprint is handled by the wall mask (see UpdateWallMaskForBody
  // at the top of the frame), so there is no alpha surgery here: the mask makes its
  // cells solid, the advection writes them dry, and the volume correction restores
  // exactly the amount the body's submerged volume displaced.

  if (!writeSurface) return true;
  // W-06: ask for the water level around the body now that this frame's surface exists. The
  // answer is consumed by the next query, so buoyancy lags the surface by one to three frames.
  QueryBodyWaterLevel(driver);
  // Derive the renderable 2D surface from the volume. The surface pass samples
  // the previous surface and writes a separate target (ping-pong): a resource
  // cannot be both read and written in one dispatch.
  const uint32_t surfaceNext = (m_surfaceCurrent + 1) % 2;
  const bool directOutput = m_surfaceOutput != nullptr;
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
    {ComputeBindingType::ReadWriteTexture, 0, nullptr, nullptr, 0,
     directOutput ? m_surfaceOutput : m_surfaceTexture[surfaceNext]},
    {ComputeBindingType::ReadOnlyBuffer, 8, m_wallMask.get(), nullptr, 0}
  };
  const uint32_t groupsX = (m_settings.width + m_surfacePipeline->threadGroupSize[0] - 1) / m_surfacePipeline->threadGroupSize[0];
  const uint32_t groupsY = (m_settings.depth + m_surfacePipeline->threadGroupSize[1] - 1) / m_surfacePipeline->threadGroupSize[1];
  if (!driver->DispatchCompute(*m_surfacePipeline, surfaceBindings, groupsX, groupsY, 1))
    return false;
  if (directOutput) return true;   // P7: written in place, nothing to copy
  m_surfaceCurrent = surfaceNext;

  // Headless: copy into the fixed texel set so the caller keeps one texture bound.
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
    if (!DispatchVolume(driver, *m_forces, m_velocity[m_current].get(), nullptr, m_velocity[next].get(),
                        0, true, m_settings.forcingAmplitude))
      return false;
    m_current = next;
    return true;
  }
  if (name == "divergence") {
    if (!StepObjectVelocity(driver))
      return false;
    return DispatchVolume(driver, *m_divergencePipeline, m_velocity[m_current].get(), nullptr,
                          m_divergence.get(), 0, true);
  }
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
                        m_pressure[m_pressureCurrent].get(), m_velocity[next].get(), 0, true,
                        -1.0f, true))
      return false;
    m_current = next;
    return true;
  }
  if (name == "surface") {
    const uint32_t surfaceNext = (m_surfaceCurrent + 1) % 2;
    const bool directOutput = m_surfaceOutput != nullptr;
    const std::vector<uint32_t> constants = BuildConstants();
    const std::vector<uint32_t> geometryConstants = BuildGeometryConstants();
    const std::vector<ComputeBindingDesc> bindings = {
      {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
      {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, m_velocity[m_current].get()},
      {ComputeBindingType::ReadOnlyTexture, 1, nullptr, nullptr, 0, nullptr,
       (m_alpha[m_alphaCurrent] ? m_alpha[m_alphaCurrent].get() : m_velocity[m_current].get())},
      {ComputeBindingType::Constants32, 1, nullptr, geometryConstants.data(), static_cast<uint32_t>(geometryConstants.size())},
      {ComputeBindingType::ReadWriteTexture, 0, nullptr, nullptr, 0,
       directOutput ? m_surfaceOutput : m_surfaceTexture[surfaceNext]},
      {ComputeBindingType::ReadOnlyBuffer, 8, m_wallMask.get(), nullptr, 0}
    };
    const uint32_t groupsX = (m_settings.width + m_surfacePipeline->threadGroupSize[0] - 1) / m_surfacePipeline->threadGroupSize[0];
    const uint32_t groupsY = (m_settings.depth + m_surfacePipeline->threadGroupSize[1] - 1) / m_surfacePipeline->threadGroupSize[1];
    if (!driver->DispatchCompute(*m_surfacePipeline, bindings, groupsX, groupsY, 1)) return false;
    if (directOutput) return true;   // P7: written in place, nothing to copy
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
      }
    }
  }
  // Staggered grid: texel (x,y,z) holds the +X/+Y/+Z faces of that cell; index -1 is a wall.
  const auto faceU = [&](int x, int y, int z) { return x < 0 ? 0.0f : vx[(z * height + y) * width + x]; };
  const auto faceV = [&](int x, int y, int z) { return y < 0 ? 0.0f : vy[(z * height + y) * width + x]; };
  const auto faceW = [&](int x, int y, int z) { return z < 0 ? 0.0f : vz[(z * height + y) * width + x]; };
  for (uint32_t z = 0; z < depth; ++z) {
    for (uint32_t y = 0; y < height; ++y) {
      for (uint32_t x = 0; x < width; ++x) {
        const int ix = static_cast<int>(x), iy = static_cast<int>(y), iz = static_cast<int>(z);
        // Wall-mask acceptance probe: every face touching a solid voxel must carry
        // zero velocity (no body is present in the self-test).
        if (IsMaskSolid(x, y, z)) {
          const uint32_t index = (z * height + y) * width + x;
          solidSpeedSum += std::abs(vx[index]) + std::abs(vy[index]) + std::abs(vz[index]);
          ++solidSamples;
          continue;
        }
        if (IsMaskSolid(x + 1, y, z)) maxWallPenetration = (std::max)(maxWallPenetration, std::abs(faceU(ix, iy, iz)));
        if (IsMaskSolid(x - 1, y, z)) maxWallPenetration = (std::max)(maxWallPenetration, std::abs(faceU(ix - 1, iy, iz)));
        if (IsMaskSolid(x, y + 1, z)) maxWallPenetration = (std::max)(maxWallPenetration, std::abs(faceV(ix, iy, iz)));
        if (IsMaskSolid(x, y - 1, z)) maxWallPenetration = (std::max)(maxWallPenetration, std::abs(faceV(ix, iy - 1, iz)));
        if (IsMaskSolid(x, y, z + 1)) maxWallPenetration = (std::max)(maxWallPenetration, std::abs(faceW(ix, iy, iz)));
        if (IsMaskSolid(x, y, z - 1)) maxWallPenetration = (std::max)(maxWallPenetration, std::abs(faceW(ix, iy, iz - 1)));

        const float divergence =
          ((faceU(ix, iy, iz) - faceU(ix - 1, iy, iz)) +
           (faceV(ix, iy, iz) - faceV(ix, iy - 1, iz)) +
           (faceW(ix, iy, iz) - faceW(ix, iy, iz - 1))) / m_settings.cellSize;
        maxDivergence = (std::max)(maxDivergence, std::abs(divergence));
        sumDivergence += std::abs(divergence);
        ++divergenceSamples;
        // Deep interior, away from the free surface and walls.
        if (x >= kInteriorMargin && y >= kInteriorMargin && z >= kInteriorMargin &&
            x + kInteriorMargin < width && y + kInteriorMargin < height && z + kInteriorMargin < depth) {
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

  // W-04 step 3: one persistent accumulator instead of a fresh buffer per call. The kernel
  // clears it itself (a sentinel in the Iteration slot), which costs one extra dispatch of a
  // kernel that only touches two uint4 slots.
  if (!m_diagnosticsBuffer) {
    ComputeBufferDesc bufferDesc;
    bufferDesc.byteWidth = 8 * sizeof(uint32_t);
    bufferDesc.structureStride = 4 * sizeof(uint32_t);
    bufferDesc.access = ComputeBufferAccess::ReadWrite;
    bufferDesc.debugName = "Fluid Diagnostics";
    const std::array<uint32_t, 8> seed = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    m_diagnosticsBuffer = driver->CreateComputeBuffer(bufferDesc, seed.data());
    if (!m_diagnosticsBuffer) return false;
  }
  ComputeBuffer* buffer = m_diagnosticsBuffer.get();

  const std::vector<uint32_t> resetConstants = BuildConstants(0xFFFFFFFFu);
  const std::vector<ComputeBindingDesc> resetBindings = {
    {ComputeBindingType::Constants32, 0, nullptr, resetConstants.data(), static_cast<uint32_t>(resetConstants.size())},
    {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, m_velocity[m_current].get()},
    {ComputeBindingType::ReadWriteBuffer, 0, buffer, nullptr, 0}
  };
  const std::vector<uint32_t> constants = BuildConstants();
  const std::vector<ComputeBindingDesc> bindings = {
    {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
    {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, m_velocity[m_current].get()},
    {ComputeBindingType::ReadWriteBuffer, 0, buffer, nullptr, 0}
  };
  const uint32_t groupsX = (m_settings.width + m_diagnosticsPipeline->threadGroupSize[0] - 1) / m_diagnosticsPipeline->threadGroupSize[0];
  const uint32_t groupsY = (m_settings.height + m_diagnosticsPipeline->threadGroupSize[1] - 1) / m_diagnosticsPipeline->threadGroupSize[1];
  const uint32_t groupsZ = (m_settings.depth + m_diagnosticsPipeline->threadGroupSize[2] - 1) / m_diagnosticsPipeline->threadGroupSize[2];
  if (!driver->DispatchCompute(*m_diagnosticsPipeline, resetBindings, groupsX, groupsY, groupsZ)) return false;
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
