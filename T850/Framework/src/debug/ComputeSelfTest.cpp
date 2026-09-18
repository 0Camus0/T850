#include <pch.h>
#include <debug/ComputeSelfTest.h>

#include <utils/Log.h>
#include <utils/ResourceLocator.h>
#include <utils/ComputeKernelRegistry.h>
#include <utils/Camera.h>
#include <scene/SceneProp.h>
#include <video/BaseDriver.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace t850 {
extern Device* T8Device;
namespace {

  std::unique_ptr<ComputePipeline> CreateTestPipeline(
      BaseDriver* driver,
      const char* shaderPath,
      std::vector<ComputeBindingLayoutDesc> bindings) {
    std::string shaderSource;
    if (!ResourceLocator::Instance().ReadText(shaderPath, shaderSource)) {
      T8_LOG_ERROR("[ComputeImage] Could not load %s", shaderPath);
      return {};
    }
    ComputePipelineDesc pipelineDesc;
    pipelineDesc.source = std::move(shaderSource);
    pipelineDesc.entryPoint = "CS";
    pipelineDesc.debugName = shaderPath;
    pipelineDesc.permutationName = "base";
    pipelineDesc.bindings = std::move(bindings);
    for (auto& binding : pipelineDesc.bindings)
      if (binding.type == ComputeBindingType::ReadWriteTexture && binding.storageFormat == ComputeStorageFormat::Unspecified)
        binding.storageFormat = ComputeStorageFormat::Rgba8Unorm;
    std::unique_ptr<ComputePipeline> pipeline = driver->CreateComputePipeline(pipelineDesc);
    if (!pipeline || !pipeline->threadGroupSize[0] ||
        !pipeline->threadGroupSize[1] || !pipeline->threadGroupSize[2]) {
      T8_LOG_ERROR("[ComputeImage] Pipeline '%s' has no reflected workgroup size",
                   shaderPath);
      return {};
    }
    return pipeline;
  }

  uint32_t ExpectedPattern(uint32_t x, uint32_t y,
                           uint32_t width, uint32_t seed) {
    const uint32_t linear = y * width + x;
    const uint32_t red = (linear * 17u + seed) & 255u;
    const uint32_t green = (x * 29u + y * 11u + seed * 3u) & 255u;
    const uint32_t blue = (x * 7u + y * 31u + seed * 5u) & 255u;
    return red | (green << 8u) | (blue << 16u) | 0xff000000u;
  }

  bool ValidateImageExtent(BaseDriver* driver,
                           ComputePipeline& writer,
                           ComputePipeline& reader,
                           uint32_t width,
                           uint32_t height,
                           uint32_t seed) {
    const int renderTarget = driver->CreateRT(
      1, BaseRT::RGBA8, BaseRT::NOTHING,
      static_cast<int>(width), static_cast<int>(height), false, true);
    if (renderTarget < 0 || renderTarget >= static_cast<int>(driver->RTs.size()) ||
        !driver->RTs[renderTarget]) {
      T8_LOG_ERROR("[ComputeImage] Could not create %ux%u storage texture", width, height);
      return false;
    }

    std::unique_ptr<ComputeBuffer> output;
    const auto cleanup = [&]() {
      driver->FlushGPUResources();
      output.reset();
      driver->DestroyRT(renderTarget);
    };
    Texture* texture = driver->GetRTTexture(renderTarget, BaseDriver::COLOR0_ATTACHMENT);
    if (!texture) {
      cleanup();
      return false;
    }

    ComputeBufferDesc bufferDesc;
    bufferDesc.byteWidth = width * height * sizeof(uint32_t);
    bufferDesc.structureStride = sizeof(uint32_t);
    bufferDesc.access = ComputeBufferAccess::ReadWrite;
    bufferDesc.debugName = "T850 Odd Image Compute Readback";
    output = driver->CreateComputeBuffer(bufferDesc);
    if (!output) {
      cleanup();
      return false;
    }

    const std::array<uint32_t, 4> constants = {width, height, seed, 0u};
    const std::vector<ComputeBindingDesc> writeBindings = {
      ComputeBindingDesc{
        ComputeBindingType::Constants32, 0, nullptr,
        constants.data(), static_cast<uint32_t>(constants.size())},
      ComputeBindingDesc{
        ComputeBindingType::ReadWriteTexture, 0, nullptr,
        nullptr, 0, texture}
    };
    const uint32_t writeGroupsX =
      (width + writer.threadGroupSize[0] - 1u) / writer.threadGroupSize[0];
    const uint32_t writeGroupsY =
      (height + writer.threadGroupSize[1] - 1u) / writer.threadGroupSize[1];
    if (!driver->DispatchCompute(writer, writeBindings,
                                 writeGroupsX, writeGroupsY, 1)) {
      T8_LOG_ERROR("[ComputeImage] %ux%u image write dispatch failed", width, height);
      cleanup();
      return false;
    }

    const std::vector<ComputeBindingDesc> readBindings = {
      ComputeBindingDesc{
        ComputeBindingType::Constants32, 0, nullptr,
        constants.data(), static_cast<uint32_t>(constants.size())},
      ComputeBindingDesc{
        ComputeBindingType::ReadOnlyTexture, 0, nullptr,
        nullptr, 0, texture},
      ComputeBindingDesc{
        ComputeBindingType::ReadWriteBuffer, 0, output.get(), nullptr, 0}
    };
    const uint32_t readGroupsX =
      (width + reader.threadGroupSize[0] - 1u) / reader.threadGroupSize[0];
    const uint32_t readGroupsY =
      (height + reader.threadGroupSize[1] - 1u) / reader.threadGroupSize[1];
    if (!driver->DispatchCompute(reader, readBindings,
                                 readGroupsX, readGroupsY, 1)) {
      T8_LOG_ERROR("[ComputeImage] %ux%u image read dispatch failed", width, height);
      cleanup();
      return false;
    }

    std::vector<uint32_t> actual(width * height, 0xdeadbeefu);
    if (!driver->ReadComputeBuffer(
          *output, actual.data(), actual.size() * sizeof(uint32_t))) {
      T8_LOG_ERROR("[ComputeImage] %ux%u image readback failed", width, height);
      cleanup();
      return false;
    }

    int mismatches = 0;
    for (uint32_t y = 0; y < height; ++y) {
      for (uint32_t x = 0; x < width; ++x) {
        const uint32_t index = y * width + x;
        const uint32_t expected = ExpectedPattern(x, y, width, seed);
        if (actual[index] == expected)
          continue;
        if (mismatches < 8) {
          T8_LOG_ERROR("[ComputeImage] %ux%u pixel(%u,%u)=0x%08X expected=0x%08X",
                       width, height, x, y, actual[index], expected);
        }
        ++mismatches;
      }
    }
    cleanup();
    if (mismatches) {
      T8_LOG_ERROR("[ComputeImage] %ux%u failed with %d mismatched pixels",
                   width, height, mismatches);
      return false;
    }
    T8_LOG_INFO("[ComputeImage] PASS: API=%s extent=%ux%u dispatch=%ux%u",
                driver->ApiTag(), width, height, writeGroupsX, writeGroupsY);
    return true;
  }

  bool ValidateParticleDepth(BaseDriver* driver, ComputePipeline& reader) {
    const auto* kernel = FindComputeKernel("CS_TorchParticles.hlsl");
    if (!kernel) return false;
    auto particles = CreateTestPipeline(driver, "Shaders/CS_TorchParticles.hlsl",
      {kernel->bindings, kernel->bindings + kernel->bindingCount});
    if (!particles) return false;

    constexpr uint32_t width = 17;
    constexpr uint32_t height = 13;
    const int target = driver->CreateRT(1, BaseRT::RGBA16F, BaseRT::NOTHING,
      width, height, false, true);
    if (target < 0) return false;
    auto* texture = driver->GetRTTexture(target, BaseDriver::COLOR0_ATTACHMENT);
    ComputeBufferDesc bufferDesc;
    bufferDesc.byteWidth = width * height * sizeof(uint32_t);
    bufferDesc.structureStride = sizeof(uint32_t);
    bufferDesc.access = ComputeBufferAccess::ReadWrite;
    bufferDesc.debugName = "T850 Particle Depth Readback";
    auto output = driver->CreateComputeBuffer(bufferDesc);

    Camera camera;
    camera.VP.Identity();
    SceneProps props;
    props.AddCamera(&camera);
    props.ParticleEmitterEnabled = 1;
    props.ParticleCount = 1;
    props.ParticleLifetime = 1.0f;
    props.ParticleSize = 2.0f;
    props.ParticleColor0 = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
    props.ParticleColor1 = props.ParticleColor2 = props.ParticleColor0;
    props.ParticleWobble.w = 1.0f;
    props.ParticleFade = XVECTOR3(1.0f, 1.0f, 0.1f, 0.001f);
    props.ParticleFadeOutStart = 0.999f;
    props.ParticleIntensity = 1.0f;
    const std::array<uint32_t, 4> readConstants = {width, height, 0, 0};
    bool passed = texture && output;
    for (unsigned scenario = 0; scenario < 10 && passed; ++scenario) {
      std::vector<unsigned char> depthPixels(width * height * 4, 0);
      for (uint32_t pixel = 0; pixel < width * height; ++pixel)
        depthPixels[pixel * 4] = scenario == 1 || (scenario == 2 && pixel % width < width / 2) ? 255 : 0;
      const int depthId = driver->CreateTextureFromMemory(
        "particle-test-depth-" + std::to_string(scenario), depthPixels.data(), width, height, 4);
      auto* depth = driver->GetTexture(depthId);
      props.ParticleEmitterPosition = XVECTOR3(0.0f, 0.0f,
        scenario == 3 ? -0.1f : scenario == 4 ? 1.1f : 0.5f, 1.0f);
      props.ParticleEmitterEnabled = scenario == 5 ? 3 : scenario == 8 ? 0 : 1;
      props.ParticleCount = scenario == 5 ? 33 : scenario == 9 ? 0 : 1;
      props.ParticleSize = scenario == 6 ? 0.18f : 2.0f;
      if (scenario == 5) {
        props.ParticleEmitterPosition.x = -20.0f;
        props.ParticleEmitterPosition1 = XVECTOR3(20.0f, 0.0f, 0.5f, 1.0f);
        props.ParticleEmitterPosition2 = XVECTOR3(0.0f, 0.0f, 0.5f, 1.0f);
      }
      if (scenario == 7) props.ParticleEmitterPosition.x = 20.0f;
      std::vector<uint32_t> constants;
      std::string error;
      passed = depth && BuildComputeConstants(*kernel, {&props, width, height, "base"}, constants, error);
      if (passed) {
        const std::vector<ComputeBindingDesc> bindings = {
          {ComputeBindingType::Constants32, 0, nullptr, constants.data(), static_cast<uint32_t>(constants.size())},
          {ComputeBindingType::ReadWriteTexture, 0, nullptr, nullptr, 0, texture},
          {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, depth}
        };
        const std::vector<ComputeBindingDesc> readBindings = {
          {ComputeBindingType::Constants32, 0, nullptr, readConstants.data(), 4},
          {ComputeBindingType::ReadOnlyTexture, 0, nullptr, nullptr, 0, texture},
          {ComputeBindingType::ReadWriteBuffer, 0, output.get(), nullptr, 0}
        };
        std::array<uint32_t, width * height> actual{};
        passed = driver->DispatchCompute(*particles, bindings, (width + 7) / 8, (height + 7) / 8, 1) &&
          driver->DispatchCompute(reader, readBindings, (width + 7) / 8, (height + 7) / 8, 1) &&
          driver->ReadComputeBuffer(*output, actual.data(), sizeof(actual));
        for (uint32_t pixel = 0; pixel < actual.size() && passed; ++pixel) {
          const bool visible = scenario == 0 || scenario == 5 || (scenario == 2 && pixel % width >= width / 2);
          uint32_t expected = visible ? 0xff0000ffu : 0u;
          if (scenario == 6) {
            const float deltaX = std::abs((static_cast<float>(pixel % width) + 0.5f) / width - 0.5f) * width / height;
            const float deltaY = std::abs((static_cast<float>(pixel / width) + 0.5f) / height - 0.5f);
            const float softness = (std::min)(1.0f / height, props.ParticleSize * props.ParticleFade.z);
            const float edge = std::clamp(((std::max)(deltaX, deltaY) - props.ParticleSize + softness) / softness, 0.0f, 1.0f);
            const auto intensity = static_cast<uint32_t>(std::round((1.0f - edge * edge * (3.0f - 2.0f * edge)) * 255.0f));
            expected = intensity | (intensity << 24u);
          }
          bool matches = true;
          for (unsigned channel = 0; channel < 4; ++channel) {
            const int difference = static_cast<int>((actual[pixel] >> (channel * 8u)) & 255u) - static_cast<int>((expected >> (channel * 8u)) & 255u);
            if (std::abs(difference) > (scenario == 6 ? 1 : 0)) matches = false;
          }
          if (!matches) {
            T8_LOG_ERROR("[ComputeParticleDepth] scenario=%u pixel=%u actual=0x%08X expected=0x%08X",
              scenario, pixel, actual[pixel], expected);
            passed = false;
          }
        }
      }
      driver->FlushGPUResources();
      if (depthId >= 0) driver->DestroyTexture(depthId);
    }
    driver->FlushGPUResources();
    output.reset();
    driver->DestroyRT(target);
    if (passed) T8_LOG_INFO("[ComputeParticleDepth] PASS: API=%s depth, clip, tile edges, partial groups, 3 emitters, 99 particles and empty output", driver->ApiTag());
    else T8_LOG_ERROR("[ComputeParticleDepth] FAIL: API=%s", driver->ApiTag());
    return passed;
  }

  bool ValidateBufferChain(BaseDriver* driver) {
    ComputePipelineDesc desc;
    desc.debugName = "Shaders/CS_Arithmetic.hlsl";
    if (!ResourceLocator::Instance().ReadText(desc.debugName, desc.source)) return false;
    desc.bindings = {{ComputeBindingType::Constants32, 0, 0, 4},
                    {ComputeBindingType::ReadWriteBuffer, 0, 1, 0}};
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
      auto invalid = desc;
      if (scenario == 0) invalid.bindings[0].constantCount = 3;
      if (scenario == 1) invalid.bindings.pop_back();
      if (scenario == 2) invalid.bindings[1].type = ComputeBindingType::ReadOnlyBuffer;
      if (scenario == 3) invalid.bindings[1].bindingIndex = 0;
      if (driver->CreateComputePipeline(invalid)) return false;
    }
    auto producer = driver->CreateComputePipeline(desc);
    desc.defines = {"COMPUTE_READ_INPUT 1"};
    desc.permutationName = "read-input";
    desc.bindings.push_back({ComputeBindingType::ReadOnlyBuffer, 0, 2, 0});
    auto consumer = driver->CreateComputePipeline(desc);
    if (!producer || !consumer) return false;
    constexpr uint32_t count = 64;
    std::array<uint32_t, count> initial{};
    initial.fill(9);
    ComputeBufferDesc bufferDesc;
    bufferDesc.byteWidth = sizeof(initial);
    bufferDesc.structureStride = sizeof(uint32_t);
    auto intermediate = driver->CreateComputeBuffer(bufferDesc, initial.data());
    auto output = driver->CreateComputeBuffer(bufferDesc);
    bufferDesc.access = ComputeBufferAccess::ReadOnly;
    auto readOnly = driver->CreateComputeBuffer(bufferDesc, initial.data());
    if (!intermediate || !output || !readOnly) return false;
    std::array<uint32_t, count> initialRead{};
    if (driver->ReadComputeBuffer(*readOnly, initialRead.data(), 3)) return false;
    if (!driver->ReadComputeBuffer(*readOnly, initialRead.data(), sizeof(initialRead)) || initialRead != initial)
      return false;
    std::array<uint32_t, 4> constants = {count, 3, 2, 17};
    std::vector<ComputeBindingDesc> writeBindings = {
      {ComputeBindingType::Constants32, 0, nullptr, constants.data(), 4},
      {ComputeBindingType::ReadWriteBuffer, 0, readOnly.get()}
    };
    bool passed = !driver->DispatchCompute(*producer, writeBindings, 1, 1, 1);
    std::vector<ComputeBindingDesc> readBindings = {
      {ComputeBindingType::Constants32, 0, nullptr, constants.data(), 4},
      {ComputeBindingType::ReadWriteBuffer, 0, output.get()},
      {ComputeBindingType::ReadOnlyBuffer, 0, intermediate.get()}
    };
    auto aliased = readBindings;
    aliased[2].buffer = output.get();
    passed = passed && !driver->DispatchCompute(*consumer, aliased, 1, 1, 1);
    auto incomplete = readBindings;
    incomplete.pop_back();
    passed = passed && !driver->DispatchCompute(*consumer, incomplete, 1, 1, 1);
    for (uint32_t iteration = 0; iteration < 4 && passed; ++iteration) {
      if (iteration != 0) {
        writeBindings[1].buffer = intermediate.get();
        passed = driver->DispatchCompute(*producer, writeBindings, 1, 1, 1);
      }
      std::array<uint32_t, count> actual{};
      passed = passed && driver->DispatchCompute(*consumer, readBindings, 1, 1, 1);
      if (iteration == 2) driver->CompleteFrame(BaseDriver::FrameCompletionMode::SubmitNoPresent);
      passed = passed && driver->ReadComputeBuffer(*output, actual.data(), sizeof(actual));
      for (uint32_t index = 0; index < count && passed; ++index) {
        const uint32_t input = iteration == 0 ? 9 : ((index + 3) * 2) ^ 17;
        passed = actual[index] == (((input + 3) * 2) ^ 17);
      }
    }
    if (passed) {
      driver->BeginFrame(BaseDriver::FrameTargetMode::Offscreen);
      passed = driver->DispatchCompute(*consumer, readBindings, 1, 1, 1);
      producer.reset();
      consumer.reset();
      intermediate.reset();
      driver->CompleteFrame(BaseDriver::FrameCompletionMode::SubmitNoPresent);
      std::array<uint32_t, count> actual{};
      passed = passed && driver->ReadComputeBuffer(*output, actual.data(), sizeof(actual));
      for (uint32_t index = 0; index < count && passed; ++index)
        passed = actual[index] == ((((((index + 3) * 2) ^ 17) + 3) * 2) ^ 17);
    }
    auto stateProbe = driver->CreateComputePipeline(desc);
    const int target = driver->CreateRT(1, BaseRT::RGBA8, BaseRT::NOTHING, 7, 5);
    if (target < 0 || !stateProbe) passed = false;
    if (passed) {
      driver->BeginFrame(BaseDriver::FrameTargetMode::Offscreen);
      driver->PushRT(target);
      driver->ClearWithColor(1, 0, 0, 1);
      passed = !driver->DispatchCompute(*stateProbe, {}, 1, 1, 1) && driver->CurrentRT == target;
      passed = passed && driver->ReadComputeBuffer(*readOnly, initialRead.data(), sizeof(initialRead)) &&
        initialRead == initial && driver->CurrentRT == target;
      driver->ClearWithColor(0, 1, 0, 1);
      driver->PopRT();
      driver->CompleteFrame(BaseDriver::FrameCompletionMode::SubmitNoPresent);
      float color[4] = {};
      passed = passed && driver->ReadRTColorFloat(target, BaseDriver::COLOR0_ATTACHMENT, color) &&
        color[0] == 0 && color[1] == 1;
    }
    driver->FlushGPUResources();
    if (target >= 0) driver->DestroyRT(target);
    T8_LOG_INFO("[ComputeBufferChain] %s: API=%s upload, read-only consumption and repeated writes",
      passed ? "PASS" : "FAIL", driver->ApiTag());
    return passed;
  }

} // namespace

  ComputeArithmeticWorkload::~ComputeArithmeticWorkload() = default;

  bool ComputeArithmeticWorkload::Initialize(BaseDriver* driver) {
    Reset();
    if (!driver) {
      T8_LOG_ERROR("[ComputeArithmetic] Initialization failed: no graphics driver");
      return false;
    }
    if (!driver->SupportsComputeShaders()) {
      T8_LOG_ERROR("[ComputeArithmetic] API '%s' does not support compute shaders",
                   driver->ApiTag());
      return false;
    }

    std::string shaderSource;
    constexpr const char* shaderPath = "Shaders/CS_Arithmetic.hlsl";
    if (!ResourceLocator::Instance().ReadText(shaderPath, shaderSource)) {
      T8_LOG_ERROR("[ComputeArithmetic] Could not load %s", shaderPath);
      return false;
    }

    ComputePipelineDesc pipelineDesc;
    pipelineDesc.source = std::move(shaderSource);
    pipelineDesc.entryPoint = "CS";
    pipelineDesc.debugName = shaderPath;
    pipelineDesc.permutationName = "base";
    pipelineDesc.bindings = {
      {ComputeBindingType::Constants32, 0, 0, 4},
      {ComputeBindingType::ReadWriteBuffer, 0, 1, 0}
    };
    m_pipeline = driver->CreateComputePipeline(pipelineDesc);
    if (!m_pipeline || !m_pipeline->threadGroupSize[0] ||
      !m_pipeline->threadGroupSize[1] || !m_pipeline->threadGroupSize[2]) {
      T8_LOG_ERROR("[ComputeArithmetic] Pipeline creation failed");
      return false;
    }

    ComputeBufferDesc bufferDesc;
    bufferDesc.byteWidth = kElementCount * sizeof(uint32_t);
    bufferDesc.structureStride = sizeof(uint32_t);
    bufferDesc.access = ComputeBufferAccess::ReadWrite;
    bufferDesc.debugName = "T850 Arithmetic Compute Output";
    m_output = driver->CreateComputeBuffer(bufferDesc);
    if (!m_output) {
      T8_LOG_ERROR("[ComputeArithmetic] Output buffer creation failed");
      Reset();
      return false;
    }

    m_driver = driver;
    T8_LOG_INFO("[ComputeArithmetic] Workload ready on API=%s", driver->ApiTag());
    return true;
  }

  bool ComputeArithmeticWorkload::Dispatch(uint32_t sequence) {
    if (!IsReady())
      return false;

    // Vary one constant every frame so PIX captures prove live command
    // recording rather than replaying one immutable initialization result.
    m_lastAddend = 7u + (sequence & 0xFFu);
    m_constants = {
      kElementCount,
      m_lastAddend,
      kMultiplier,
      kXorMask
    };

    const std::vector<ComputeBindingDesc> bindings = {
      ComputeBindingDesc{
        ComputeBindingType::Constants32,
        0,
        nullptr,
        m_constants.data(),
        static_cast<uint32_t>(m_constants.size())
      },
      ComputeBindingDesc{
        ComputeBindingType::ReadWriteBuffer,
        0,
        m_output.get(),
        nullptr,
        0
      }
    };

    const uint32_t groupCount =
      (kElementCount + m_pipeline->threadGroupSize[0] - 1) /
      m_pipeline->threadGroupSize[0];
    const bool dispatched = m_driver->DispatchCompute(*m_pipeline, bindings, groupCount, 1, 1);
    if (dispatched && m_logNextDispatch) {
      T8_LOG_INFO("[ComputeArithmetic] Dispatch(%u,1,1): b0={ElementCount=%u Addend=%u Multiplier=%u XorMask=0x%08X} u0='T850 Arithmetic Compute Output'",
                  groupCount, kElementCount, m_lastAddend, kMultiplier, kXorMask);
      m_logNextDispatch = false;
    }
    return dispatched;
  }

  bool ComputeArithmeticWorkload::ValidateLastDispatch() {
    if (!IsReady())
      return false;

    std::vector<uint32_t> actual(kElementCount, 0xDEADBEEFu);
    if (!m_driver->ReadComputeBuffer(*m_output, actual.data(), actual.size() * sizeof(uint32_t))) {
      T8_LOG_ERROR("[ComputeArithmetic] Readback failed");
      return false;
    }

    int mismatches = 0;
    for (uint32_t index = 0; index < kElementCount; ++index) {
      const uint32_t expected = ((index + m_lastAddend) * kMultiplier) ^ kXorMask;
      if (actual[index] == expected)
        continue;
      if (mismatches < 8) {
        T8_LOG_ERROR("[ComputeArithmetic] output[%u]=0x%08X expected=0x%08X",
                     index, actual[index], expected);
      }
      ++mismatches;
    }

    if (mismatches != 0) {
      T8_LOG_ERROR("[ComputeArithmetic] %d/%u results mismatched",
                   mismatches, kElementCount);
      return false;
    }

    T8_LOG_INFO("[ComputeArithmetic] PASS: API=%s elements=%u expression=((index+%u)*%u)^0x%08X",
                m_driver->ApiTag(), kElementCount, m_lastAddend, kMultiplier, kXorMask);
    return true;
  }

  void ComputeArithmeticWorkload::Reset() {
    m_output.reset();
    m_pipeline.reset();
    m_driver = nullptr;
    m_logNextDispatch = true;
  }

  int RunComputeArithmeticSelfTest(BaseDriver* driver) {
    ComputeArithmeticWorkload workload;
    if (!workload.Initialize(driver)) {
      T8_LOG_ERROR("[ComputeSelfTest] FAIL: workload initialization failed");
      return 1;
    }
    if (!workload.Dispatch(0)) {
      T8_LOG_ERROR("[ComputeSelfTest] FAIL: dispatch failed");
      return 1;
    }
    if (!workload.ValidateLastDispatch()) {
      T8_LOG_ERROR("[ComputeSelfTest] FAIL: validation failed");
      return 1;
    }
    T8_LOG_INFO("[ComputeArithmetic] PASS");
    return 0;
  }

  int RunComputeImageSelfTest(BaseDriver* driver) {
    if (!driver || !driver->SupportsComputeTextures()) {
      T8_LOG_ERROR("[ComputeImage] API '%s' does not support compute textures",
                   driver ? driver->ApiTag() : "none");
      return 1;
    }
    std::unique_ptr<ComputePipeline> writer = CreateTestPipeline(
      driver, "Shaders/CS_ImagePatternWrite.hlsl",
      {
        {ComputeBindingType::Constants32, 0, 0, 4},
        {ComputeBindingType::ReadWriteTexture, 0, 1, 0}
      });
    std::unique_ptr<ComputePipeline> reader = CreateTestPipeline(
      driver, "Shaders/CS_ImagePatternRead.hlsl",
      {
        {ComputeBindingType::Constants32, 0, 0, 4},
        {ComputeBindingType::ReadOnlyTexture, 0, 1, 0},
        {ComputeBindingType::ReadWriteBuffer, 0, 2, 0}
      });
    if (!writer || !reader)
      return 1;

    const int wrongFormat = driver->CreateRT(1, BaseRT::RGBA16F, BaseRT::NOTHING, 7, 5, false, true);
    if (wrongFormat < 0) return 1;
    const std::array<uint32_t, 4> constants = {7, 5, 1, 0};
    const std::vector<ComputeBindingDesc> invalidBindings = {
      {ComputeBindingType::Constants32, 0, nullptr, constants.data(), 4},
      {ComputeBindingType::ReadWriteTexture, 0, nullptr, nullptr, 0,
        driver->GetRTTexture(wrongFormat, BaseDriver::COLOR0_ATTACHMENT)}
    };
    const bool rejected = !driver->DispatchCompute(*writer, invalidBindings, 1, 1, 1);
    driver->FlushGPUResources();
    driver->DestroyRT(wrongFormat);
    if (!rejected) return 1;

    constexpr std::array<std::array<uint32_t, 2>, 3> extents = {{
      {1, 1}, {7, 5}, {257, 129}
    }};
    for (size_t index = 0; index < extents.size(); ++index) {
      if (!ValidateImageExtent(driver, *writer, *reader,
                               extents[index][0], extents[index][1],
                               37u + static_cast<uint32_t>(index)))
        return 1;
    }
    return ValidateParticleDepth(driver, *reader) ? 0 : 1;
  }

  int RunComputeSelfTests(BaseDriver* driver) {
    const std::array<float, 16> texels{};
    for (unsigned cycle = 0; cycle < 4; ++cycle) {
      Texture* texture = T8Device->CreateFloatTexture(2, 2, texels.data());
      if (!texture) {
        T8_LOG_ERROR("[SamplerLifetime] Float texture creation failed");
        return 1;
      }
      for (const auto filter : {NEAREST_FILTER, LINEAR_FILTER, NEAREST_FILTER}) {
        texture->params = CLAMP_TO_EDGE | filter;
        texture->SetTextureParams();
      }
      driver->FlushGPUResources();
      texture->release();
    }
    T8_LOG_INFO("[SamplerLifetime] PASS: API=%s repeated float texture sampler variants", driver->ApiTag());
    if (!ValidateBufferChain(driver) || RunComputeArithmeticSelfTest(driver) != 0 ||
        RunComputeImageSelfTest(driver) != 0) {
      T8_LOG_ERROR("[ComputeSelfTest] FAIL");
      return 1;
    }
    T8_LOG_INFO("[ComputeSelfTest] PASS: arithmetic and odd-sized image kernels");
    return 0;
  }

} // namespace t850
