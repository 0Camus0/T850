#include <pch.h>
#include <debug/ComputeSelfTest.h>

#include <utils/Log.h>
#include <utils/ResourceLocator.h>
#include <video/BaseDriver.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace t850 {
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

    constexpr std::array<std::array<uint32_t, 2>, 3> extents = {{
      {1, 1}, {7, 5}, {257, 129}
    }};
    for (size_t index = 0; index < extents.size(); ++index) {
      if (!ValidateImageExtent(driver, *writer, *reader,
                               extents[index][0], extents[index][1],
                               37u + static_cast<uint32_t>(index)))
        return 1;
    }
    return 0;
  }

  int RunComputeSelfTests(BaseDriver* driver) {
    if (RunComputeArithmeticSelfTest(driver) != 0 ||
        RunComputeImageSelfTest(driver) != 0) {
      T8_LOG_ERROR("[ComputeSelfTest] FAIL");
      return 1;
    }
    T8_LOG_INFO("[ComputeSelfTest] PASS: arithmetic and odd-sized image kernels");
    return 0;
  }

} // namespace t850
