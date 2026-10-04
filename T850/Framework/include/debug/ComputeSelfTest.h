#ifndef T850_COMPUTE_SELF_TEST_H
#define T850_COMPUTE_SELF_TEST_H

#include <array>
#include <cstdint>
#include <memory>

namespace t850 {
  class BaseDriver;
  class ComputeBuffer;
  class ComputePipeline;

  class ComputeArithmeticWorkload {
  public:
    ComputeArithmeticWorkload() = default;
    ~ComputeArithmeticWorkload();

    bool Initialize(BaseDriver* driver);
    bool Dispatch(uint32_t sequence);
    bool ValidateLastDispatch();
    void Reset();
    bool IsReady() const { return m_driver && m_pipeline && m_output; }

  private:
    static constexpr uint32_t kElementCount = 96;
    static constexpr uint32_t kMultiplier = 3;
    static constexpr uint32_t kXorMask = 0x55AA55AAu;

    BaseDriver* m_driver = nullptr;
    std::unique_ptr<ComputePipeline> m_pipeline;
    std::unique_ptr<ComputeBuffer> m_output;
    std::array<uint32_t, 4> m_constants = {};
    uint32_t m_lastAddend = 7;
    bool m_logNextDispatch = true;
  };

  // Runs a deterministic GPU arithmetic dispatch and validates its readback.
  // Returns zero on success and nonzero on unsupported/error/mismatch.
  int RunComputeArithmeticSelfTest(BaseDriver* driver);
  int RunComputeImageSelfTest(BaseDriver* driver);
  int RunComputeSelfTests(BaseDriver* driver);
}

#endif
