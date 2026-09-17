#include <pch.h>
#include <debug/ComputeSelfTest.h>

#include <utils/Log.h>
#include <utils/ResourceLocator.h>
#include <video/BaseDriver.h>

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace t850 {

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
    if (!m_pipeline) {
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

    const uint32_t groupCount = (kElementCount + kThreadsPerGroup - 1) / kThreadsPerGroup;
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
    T8_LOG_INFO("[ComputeSelfTest] PASS");
    return 0;
  }

} // namespace t850
