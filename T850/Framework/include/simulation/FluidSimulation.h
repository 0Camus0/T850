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

    // Sustained surface forcing gain. Without it the one-shot seed decays and the
    // pool becomes perfectly flat, so the simulation has nothing to show.
    float forcingAmplitude = 1.0f;

    // Basin bounds in engine space. The solver domain is exactly this box, so a
    // voxel is outside the fluid only where the pool geometry intrudes (the
    // steps) or on the outer shell. Cells outside are solid.
    float basinMinX = -4.5f;
    float basinMinY = -1.6f;
    float basinMinZ = -9.0f;
    float basinMaxX = 4.5f;
    float basinMaxY = 0.02f;
    float basinMaxZ = -4.5f;

    // Absolute world Y of the still-water surface (the plane the water mesh is
    // authored at). Overridden from the pool layout JSON by LoadPoolLayout().
    float waterPlaneY = -0.04f;
    // Metres of air kept above the water plane so a free surface can rise.
    float freeboard = 0.35f;
    // When true, LoadPoolLayout() derives the row count from the water plane plus
    // the freeboard, and derives the fill level so the still surface lands exactly
    // on the authored plane. Off by default so synthetic tests keep the grid they
    // asked for.
    bool deriveGridFromLayout = false;

    // Fraction of the basin height that starts filled with water. Cells below
    // this are alpha = 1, cells above are air (alpha = 0), and the single row
    // straddling the initial waterline holds the fractional value.
    float initialFill = 0.94f;
    // Alpha below this counts as air: no pressure solve, no projection.
    float airThreshold = 0.05f;
    // Alpha above this counts as fully wet.
    float wetThreshold = 0.95f;
    // Russo-Smereka compression strength for the flux-form alpha advection.
    // Higher keeps the interface sharper at the cost of some surface noise.
    float alphaCompression = 0.0f;
  };

  // One solid intrusion into the basin, in engine space. Derived from the pool
  // geometry's named nodes (emitted by scripts/blender/build_backyard.py into
  // backyard_layout.json), never hard-coded here.
  struct FluidSolidBox {
    float minX = 0.0f, minY = 0.0f, minZ = 0.0f;
    float maxX = 0.0f, maxY = 0.0f, maxZ = 0.0f;
  };

  // A sphere dropped into the water. Simulated as a ballistic body until it enters
  // the pool, then integrated through the fluid as a displaced-volume obstacle.
  // All values come from the scene JSON, never from literals here.
  struct FluidSphere {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
    float radius = 0.1f;
    // Density relative to water. Below 1 floats, above 1 sinks. This is the only
    // term that decides the floating depth (equilibrium submerged fraction equals
    // this value), which is why it is the primary control rather than mass.
    float densityRatio = 0.6f;
    // Drag coefficient of a smooth sphere, and the bounce at the floor and walls.
    float dragCoefficient = 0.47f;
    float restitution = 0.30f;
    bool enabled = false;
    // Submerged depth below the water plane, used to damp the fall and to decide
    // when it has settled on the floor.
    float submerged = 0.0f;
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

    // Volume fraction in [0,1] per voxel: 1 = water, 0 = air. This is the real
    // free-surface field; the 2D surface texture the shader consumes is derived
    // from it.
    bool SeedVolumeFraction(BaseDriver* driver);
    // Restores total alpha to the value captured at seed time by rescaling the
    // interface cells. Returns how much it had to correct, as a fraction.
    bool CorrectVolumeFraction(BaseDriver* driver, float& appliedScale);
    // Summed alpha over the fluid domain, read back to the CPU. Used to prove
    // mass conservation.
    bool MeasureVolumeFraction(BaseDriver* driver, float& totalAlpha);
    // Raw alpha of every voxel, for diagnostics that need the actual surface shape
    // rather than a rendered frame (a still image cannot tell a dimple that tracks
    // the body apart from waves that actually propagate).
    bool ReadVolumeFraction(BaseDriver* driver, std::vector<float>& out);

    // Loads the basin bounds and solid intrusions (pool steps) from the
    // Blender-emitted layout, so wall conditions come from the real geometry.
    // Returns false if the layout is missing; the solver then falls back to the
    // closed-box shell only.
    bool LoadPoolLayout(const char* layoutPath = "Models/Backyard/backyard_layout.json");

    // Number of voxels the mask marks solid (outside the fluid domain).
    uint32_t SolidVoxelCount() const { return m_solidVoxelCount; }
    uint32_t FluidVoxelCount() const { return m_settings.width * m_settings.height * m_settings.depth - m_solidVoxelCount; }

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
    void SetForcingAmplitude(float amplitude) { m_settings.forcingAmplitude = amplitude; }

    // Sphere dropping into the pool. Advance() integrates it and pushes the result
    // into the solver each frame, so the water responds to a real event.
    FluidSphere& DroppedSphere() { return m_sphere; }
    void AdvanceSphere(float dt);
    bool InjectSphere(BaseDriver* driver);
    // Imposes no-penetration between the fluid and the moving body. Must be called
    // before the divergence/pressure step.
    bool StepObjectVelocity(BaseDriver* driver);
    // Stamps the moving body into the wall mask so it becomes a solid boundary for
    // every kernel (divergence, pressure, gradient, alpha advection).
    bool UpdateWallMaskForBody(BaseDriver* driver);
    // Returns the solver to a flat, motionless pool and re-locks the reference mass.
    // Used by the scene's reset button so a new drop height can be tested from a
    // comparable initial state.
    bool ResetToRest(BaseDriver* driver);

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
      // Summed |velocity| over every voxel the wall mask marks solid. Non-zero
      // means water leaked into the pool geometry.
      float solidSpeedSum = 0.0f;
      uint32_t solidSamples = 0;
      // Largest normal velocity component pointing into a solid face, over fluid
      // voxels adjacent to a wall.
      float maxWallPenetration = 0.0f;
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
    std::vector<uint32_t> BuildConstants(uint32_t iterationPacked = 0,
                                         float paddingOverride = -1.0f) const;
    // Obstacle parameters for CS_FluidSphere3D, packed as live constants because
    // ComputeBuffer has no update path: a buffer seeded once would be read stale
    // forever and the obstacle would never move.
    std::vector<uint32_t> BuildObjectConstants() const;
    // Basin floor, authored water plane and grid origin. Also live constants, for
    // the same reason: the surface kernel must not carry the pool placement as a
    // literal.
    std::vector<uint32_t> BuildGeometryConstants() const;
    // Headroom-weighted volume delta for CS_FluidVolumeCorrect3D.
    std::vector<uint32_t> BuildCorrectionConstants(float deficit) const;
    // (displaced volume, total surface free capacity) for CS_FluidSphereSpread3D.
    std::vector<uint32_t> BuildSpreadConstants(float displaced, float totalFree) const;
    bool BuildWallMask(BaseDriver* driver);
    bool MeasureVolumeFraction(BaseDriver* driver, float& totalAlpha, float& interfaceSum);
    bool EnsureSurfaceTarget(BaseDriver* driver);
    bool AnalyzeVolume(BaseDriver* driver, const std::string* directory, VolumeDump& summary);
  // Reads the mask buffer once so the CPU-side acceptance probe can classify voxels.
  bool RefreshMaskSnapshot(BaseDriver* driver);
  bool IsMaskSolid(uint32_t x, uint32_t y, uint32_t z) const {
    if (x >= m_settings.width || y >= m_settings.height || z >= m_settings.depth) return true;
    return m_maskSnapshot[static_cast<size_t>(x) + m_settings.width *
                           (static_cast<size_t>(y) + m_settings.height * z)] != 0u;
  }
  std::vector<uint32_t> m_maskSnapshot;
    bool PublishSurface(BaseDriver* driver);
    bool DispatchVolume(BaseDriver* driver, ComputePipeline& pipeline,
                        ComputeTexture3D* sampledA, ComputeTexture3D* sampledB,
                        ComputeTexture3D* target, uint32_t iterationPacked = 0,
                        bool withAlpha = false, float paddingOverride = -1.0f);

    static constexpr size_t kVelocityCount = 2;
    static constexpr size_t kPressureCount = 2;

    BaseDriver* m_driver = nullptr;
    FluidSettings m_settings;
    bool m_ready = false;

    std::unique_ptr<ComputeTexture3D> m_velocity[kVelocityCount];
    std::unique_ptr<ComputeTexture3D> m_divergence;
    std::unique_ptr<ComputeTexture3D> m_pressure[kPressureCount];
    // Free-surface volume fraction, ping-ponged. RGBA16F holds alpha in .x.
    std::unique_ptr<ComputeTexture3D> m_alpha[2];
    uint32_t m_alphaCurrent = 0;

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
    std::unique_ptr<ComputePipeline> m_seedAlphaPipeline;
    std::unique_ptr<ComputePipeline> m_alphaAdvectPipeline;
    std::unique_ptr<ComputePipeline> m_volumeCorrectPipeline;
    std::unique_ptr<ComputePipeline> m_spherePipeline;
    // Couples the moving body to the velocity field (no-penetration at a moving
    // solid boundary). Without this there is no divergence from the impact, so the
    // pressure solve has nothing to react to and no wave can radiate.
    std::unique_ptr<ComputePipeline> m_objectVelocityPipeline;
    // Zeroing kernel used by the reset path.
    std::unique_ptr<ComputePipeline> m_clearPipeline;
    // Per-column (removed, free capacity), written by the obstacle pass.
    std::unique_ptr<ComputeBuffer> m_removedBuffer;
    uint32_t m_removedColumns = 0;
    // Authored wall mask on the CPU, used to re-stamp the moving body each frame.
    std::vector<uint32_t> m_baseMask;
    uint32_t m_maskBodyCellCount = 0;
    FluidSphere m_sphere;
    // Total alpha at seed time, the reference the volume correction restores to.
    float m_referenceVolumeFraction = 0.0f;
    // Interface-cell alpha sum, needed to compute the correction scale.
    float m_interfaceAlphaSum = 0.0f;
    // Volume correction runs periodically: it needs a GPU readback, which is a
    // hard sync, and mass drifts slowly enough that per-frame correction is not
    // needed.
    uint32_t m_framesSinceVolumeCheck = 0;
    uint32_t m_volumeCheckInterval = 15;
    // Free capacity of the interface cells from the most recent measurement,
    // used to bound the additive volume correction.
    float m_lastInterfaceHeadroom = 0.0f;
    std::unique_ptr<ComputeBuffer> m_volumeReadbackBuffer;
    uint32_t m_volumeReadbackVoxels = 0;
    // Structured buffer of one uint per voxel: 1 = solid, 0 = fluid. Built from
    // the pool layout and bound read-only to every kernel that needs to know
    // where the walls are.
    std::unique_ptr<ComputeBuffer> m_wallMask;
    std::vector<FluidSolidBox> m_solidBoxes;
    uint32_t m_solidVoxelCount = 0;
    Texture* m_surfaceOutput = nullptr;

    int m_surfaceRT[2] = {-1, -1};
    Texture* m_surfaceTexture[2] = {nullptr, nullptr};
    uint32_t m_surfaceCurrent = 0;
    uint32_t m_current = 0;
    uint32_t m_pressureCurrent = 0;
    float m_time = 0.0f;
  };

} // namespace t850
