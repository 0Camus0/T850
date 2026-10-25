#include <pch.h>

#include <simulation/FluidSimulation.h>

#include <utils/ComputeKernelRegistry.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>

#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace t850 {
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

std::vector<uint32_t> FluidSimulation::BuildConstants() const {
  FluidConstants constants{};
  constants.width = m_settings.width;
  constants.height = m_settings.height;
  constants.depth = m_settings.depth;
  constants.iteration = 0;
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
  constants.padding0 = 0.0f;
  std::vector<uint32_t> words(sizeof(constants) / sizeof(uint32_t));
  std::memcpy(words.data(), &constants, sizeof(constants));
  return words;
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

  for (size_t i = 0; i < kVelocityCount; ++i)
    m_velocity[i] = CreateVolume(driver, i == 0 ? "Fluid Velocity A" : "Fluid Velocity B");
  m_divergence = CreateVolume(driver, "Fluid Divergence");
  for (size_t i = 0; i < kPressureCount; ++i)
    m_pressure[i] = CreateVolume(driver, i == 0 ? "Fluid Pressure A" : "Fluid Pressure B");

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

  bool complete = m_advect && m_forces && m_divergencePipeline && m_pressurePipeline &&
                  m_gradientPipeline && m_surfacePipeline && m_seedPipeline && m_diagnosticsPipeline &&
                  m_copyPipeline && m_surfaceCopyPipeline;
  for (size_t i = 0; i < kVelocityCount; ++i) complete = complete && m_velocity[i];
  for (size_t i = 0; i < kPressureCount; ++i) complete = complete && m_pressure[i];
  complete = complete && m_divergence;
  if (!complete) {
    T8_LOG_ERROR("[Fluid] Solver resources are incomplete on %s", driver->ApiTag());
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

bool FluidSimulation::Reset(BaseDriver* driver) {
  if (!m_ready || !driver) return false;
  (void)driver;
  m_current = 0;
  m_pressureCurrent = 0;
  return true;
}

bool FluidSimulation::DispatchVolume(BaseDriver* driver, ComputePipeline& pipeline,
                                     ComputeTexture3D* sampledA, ComputeTexture3D* sampledB,
                                     ComputeTexture3D* target) {
  const std::vector<uint32_t> constants = BuildConstants();
  std::vector<ComputeBindingDesc> bindings;
  bindings.push_back({ComputeBindingType::Constants32, 0, nullptr, constants.data(),
                      static_cast<uint32_t>(constants.size())});
  bindings.push_back({ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, sampledA});
  if (sampledB)
    bindings.push_back({ComputeBindingType::ReadOnlyTexture, 1, nullptr, nullptr, 0, nullptr, sampledB});
  bindings.push_back({ComputeBindingType::ReadWriteTexture, 0, nullptr, nullptr, 0, nullptr, target});

  const uint32_t groupsX = (m_settings.width + pipeline.threadGroupSize[0] - 1) / pipeline.threadGroupSize[0];
  const uint32_t groupsY = (m_settings.height + pipeline.threadGroupSize[1] - 1) / pipeline.threadGroupSize[1];
  const uint32_t groupsZ = (m_settings.depth + pipeline.threadGroupSize[2] - 1) / pipeline.threadGroupSize[2];
  return driver->DispatchCompute(pipeline, bindings, groupsX, groupsY, groupsZ);
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
  m_time += m_settings.deltaTime;
  const uint32_t next = (m_current + 1) % kVelocityCount;

  // 1. Advect velocity through itself.
  if (!DispatchVolume(driver, *m_advect, m_velocity[m_current].get(), nullptr, m_velocity[next].get()))
    return false;
  m_current = next;

  // 2. External forces and viscosity.
  const uint32_t forced = (m_current + 1) % kVelocityCount;
  if (!DispatchVolume(driver, *m_forces, m_velocity[m_current].get(), nullptr, m_velocity[forced].get()))
    return false;
  m_current = forced;

  // 3. Divergence.
  if (!DispatchVolume(driver, *m_divergencePipeline, m_velocity[m_current].get(), nullptr, m_divergence.get()))
    return false;

  // 4. Jacobi pressure solve.
  const uint32_t pressureNext = (m_pressureCurrent + 1) % kPressureCount;
  for (uint32_t iteration = 0; iteration < m_settings.pressureIterations; ++iteration) {
    const uint32_t source = m_pressureCurrent;
    const uint32_t target = (source + 1) % kPressureCount;
    if (!DispatchVolume(driver, *m_pressurePipeline, m_pressure[source].get(), m_divergence.get(),
                        m_pressure[target].get()))
      return false;
    m_pressureCurrent = target;
  }
  (void)pressureNext;

  // 5. Project velocity.
  const uint32_t projected = (m_current + 1) % kVelocityCount;
  if (!DispatchVolume(driver, *m_gradientPipeline, m_velocity[m_current].get(), m_pressure[m_pressureCurrent].get(),
                      m_velocity[projected].get()))
    return false;
  m_current = projected;

  // Derive the renderable 2D surface from the volume. The surface pass samples
  // the previous surface and writes a separate target (ping-pong): a resource
  // cannot be both read and written in one dispatch.
  const uint32_t surfaceNext = (m_surfaceCurrent + 1) % 2;
  const std::vector<uint32_t> constants = BuildConstants();
  const std::vector<ComputeBindingDesc> surfaceBindings = {
    {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
    {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, m_velocity[m_current].get()},
    {ComputeBindingType::ReadOnlyTexture, 1, nullptr, nullptr, 0, m_surfaceTexture[m_surfaceCurrent]},
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
    return DispatchVolume(driver, *m_divergencePipeline, m_velocity[m_current].get(), nullptr, m_divergence.get());
  if (name == "pressure") {
    const uint32_t source = m_pressureCurrent;
    const uint32_t target = (source + 1) % kPressureCount;
    if (!DispatchVolume(driver, *m_pressurePipeline, m_pressure[source].get(), m_divergence.get(),
                        m_pressure[target].get()))
      return false;
    m_pressureCurrent = target;
    return true;
  }
  if (name == "gradient") {
    if (!DispatchVolume(driver, *m_gradientPipeline, m_velocity[m_current].get(),
                        m_pressure[m_pressureCurrent].get(), m_velocity[next].get()))
      return false;
    m_current = next;
    return true;
  }
  if (name == "surface") {
    const uint32_t surfaceNext = (m_surfaceCurrent + 1) % 2;
    const std::vector<uint32_t> constants = BuildConstants();
    const std::vector<ComputeBindingDesc> bindings = {
      {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
      {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, nullptr, m_velocity[m_current].get()},
      {ComputeBindingType::ReadOnlyTexture, 1, nullptr, nullptr, 0, m_surfaceTexture[m_surfaceCurrent]},
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

bool FluidSimulation::AnalyzeVolume(BaseDriver* driver, const std::string* directory, VolumeDump& summary) {
  summary = VolumeDump{};
  if (!m_ready || !driver || !m_copyPipeline) return false;
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
