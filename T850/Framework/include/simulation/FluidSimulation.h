#pragma once

#include <video/BaseDriver.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace t850 {

  struct FluidSettings {
    uint32_t width = 96;
    uint32_t height = 32;
    uint32_t depth = 48;
    uint32_t pressureIterations = 8;
    float cellSize = 0.09f;
    float deltaTime = 1.0f / 30.0f;
    float viscosity = 0.02f;
    float gravity = 9.81f;
    float windX = 0.6f;
    float windY = 0.0f;
    float windZ = -0.35f;
    float damping = 0.6f;
    float surfaceLevel = 0.92f;
    float pressureScale = 1.0f;
    // Successive over-relaxation factor for the red-black Gauss-Seidel pressure
    // sweep. 1.0 is plain Gauss-Seidel; ~1.8 is near optimal for a 7-point
    // Laplacian on a long thin domain.
    float sorOmega = 1.8f;
  };

  // GPU volumetric Navier-Stokes solver for a bounded pool volume.
  // Requires SupportsComputeTextures3D(); callers must fall back to analytic
  // waves when TryInitialize returns false.
  class FluidSimulation {
  public:
    FluidSimulation() = default;
    ~FluidSimulation();

    bool TryInitialize(BaseDriver* driver, const FluidSettings& settings);
    void Shutdown(BaseDriver* driver);

    // Advances the solver one frame and refreshes the 2D surface target.
    bool Step(BaseDriver* driver);
    bool Reset(BaseDriver* driver);

    bool IsReady() const { return m_ready; }
    const FluidSettings& Settings() const { return m_settings; }
    // Solver clock in seconds, advanced by exactly deltaTime per Step. Driving
    // time-dependent shading from it keeps renders frame-deterministic instead
    // of depending on wall-clock frame pacing.
    float SimulationTime() const { return m_time; }
    // Live-tunable iteration count; changing the grid needs a full rebuild.
    void SetPressureIterations(uint32_t iterations) { m_settings.pressureIterations = iterations; }
    void SetSurfaceLevel(float level) { m_settings.surfaceLevel = level; }
    void SetSOROmega(float omega) { m_settings.sorOmega = omega; }
    void SetDeltaTime(float seconds) { m_settings.deltaTime = seconds; }

    // Render-graph target the solver copies its current surface into each Step.
    // Must match the simulation width/depth or the copy is skipped.
    void SetSurfaceOutput(Texture* texture) { m_surfaceOutput = texture; }

    // Sampled 2D surface (height, foam, slopeX, slopeZ) produced by Step().
    Texture* SurfaceTexture() const { return m_surfaceTexture[m_surfaceCurrent]; }
    int SurfaceRenderTarget() const { return m_surfaceRT[m_surfaceCurrent]; }

    struct Diagnostics {
      uint32_t samples = 0;
      float maxSpeed = 0.0f;
      float meanSpeed = 0.0f;
      uint32_t nonFinite = 0;
      float meanX = 0.0f;
    };
    bool ReadDiagnostics(BaseDriver* driver, Diagnostics& out);

    // Copies the whole velocity volume to the CPU and writes per-slice PPM
    // images plus a JSON summary (divergence residual, per-slice statistics).
    // Returns false if the readback fails.
    struct VolumeDump {
      uint32_t width = 0;
      uint32_t height = 0;
      uint32_t depth = 0;
      float maxDivergence = 0.0f;
      float meanDivergence = 0.0f;
      float interiorMeanDivergence = 0.0f;
      float interiorMaxDivergence = 0.0f;
      float maxSpeed = 0.0f;
      float meanVertical = 0.0f;
      uint32_t slices = 0;
    };
    bool DumpVolume(BaseDriver* driver, const std::string& directory, VolumeDump& summary);
    // Same statistics without writing images (cheap: one buffer copy).
    bool MeasureVolume(BaseDriver* driver, VolumeDump& summary);

    // Writes a known 1.0 m/s +X field so later stages have defined input.
    // With divergent=true it writes a field with a known constant divergence,
    // which is what proves the pressure solve actually projects.
    bool Seed(BaseDriver* driver, bool divergent = false);

    // Runs a single named stage and leaves the result in the current volume.
    // Stage names match the debug log; used by the staged self-test.
    bool StepStage(BaseDriver* driver, const char* stage);

  private:
    std::unique_ptr<ComputeTexture3D> CreateVolume(BaseDriver* driver, const char* debugName);
    std::unique_ptr<ComputePipeline> CreatePipeline(BaseDriver* driver, const char* source);
    std::vector<uint32_t> BuildConstants(uint32_t iterationPacked = 0) const;
    bool EnsureSurfaceTarget(BaseDriver* driver);
    bool AnalyzeVolume(BaseDriver* driver, const std::string* directory, VolumeDump& summary);
    bool PublishSurface(BaseDriver* driver);
    bool DispatchVolume(BaseDriver* driver, ComputePipeline& pipeline,
                        ComputeTexture3D* sampledA, ComputeTexture3D* sampledB,
                        ComputeTexture3D* target, uint32_t iterationPacked = 0);

    static constexpr size_t kVelocityCount = 2;
    static constexpr size_t kPressureCount = 2;

    BaseDriver* m_driver = nullptr;
    FluidSettings m_settings;
    bool m_ready = false;

    std::unique_ptr<ComputeTexture3D> m_velocity[kVelocityCount];
    std::unique_ptr<ComputeTexture3D> m_divergence;
    std::unique_ptr<ComputeTexture3D> m_pressure[kPressureCount];

    std::unique_ptr<ComputePipeline> m_advect;
    std::unique_ptr<ComputePipeline> m_forces;
    std::unique_ptr<ComputePipeline> m_divergencePipeline;
    std::unique_ptr<ComputePipeline> m_pressurePipeline;
    std::unique_ptr<ComputePipeline> m_gradientPipeline;
    std::unique_ptr<ComputePipeline> m_surfacePipeline;
    std::unique_ptr<ComputePipeline> m_seedPipeline;
    std::unique_ptr<ComputePipeline> m_seedDivergentPipeline;
    std::unique_ptr<ComputePipeline> m_diagnosticsPipeline;
    std::unique_ptr<ComputePipeline> m_copyPipeline;
    std::unique_ptr<ComputePipeline> m_surfaceCopyPipeline;
    Texture* m_surfaceOutput = nullptr;

    int m_surfaceRT[2] = {-1, -1};
    Texture* m_surfaceTexture[2] = {nullptr, nullptr};
    uint32_t m_surfaceCurrent = 0;
    uint32_t m_current = 0;
    uint32_t m_pressureCurrent = 0;
    float m_time = 0.0f;
  };

} // namespace t850
