#pragma once

// Position Based Fluids (Macklin & Muller, ACM TOG 32(4), 2013) as a GPU solver.
//
// This is the Lagrangian counterpart of FluidSimulation (the Eulerian VOF solver used by
// Backyard scene 7). Backyard8 uses it so water can actually fall into the pool: a
// column of particles above the rim has nothing holding it up, so it collapses, splashes
// and settles. The VOF solver cannot represent that state (its domain tops out at the
// water plane's freeboard, well under the coping, and a full column there is a closed box).
//
// Everything runs in compute shaders; the host only orchestrates dispatches. The spatial
// hash is a neighbour-acceleration structure rebuilt from predicted positions each
// substep, not an Eulerian fluid grid.
//
// Pipeline per substep (paper Algorithm 1):
//   1. CS_PbfIntegrate       v += g dt; x* = x + v dt
//   2. CS_PbfClearGrid       zero the fixed-capacity neighbour grid
//   3. CS_PbfBuildGrid       scatter x* into cells (atomicAdd counts)
//   4. CS_PbfDensity         rho_i, C_i, lambda_i   (eqs 1, 2, 8, 11)
//   5. CS_PbfSolve   x N     Jacobi position projection (eqs 13, 14) + collision
//   6. CS_PbfUpdateVelocity  v = (x* - x)/dt, XSPH viscosity (eq 17), commit x = x*
//   7. CS_PbfSurface         particle cloud -> the width x depth RGBA16F heightfield the
//                            existing water mesh renderer consumes, unchanged

#include <core/Core.h>
#include <video/BaseDriver.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace t850 {

  struct PbfSettings {
    // Pool domain; LoadPoolLayout overwrites these from Models/Backyard/backyard_layout.json.
    float basinMinX = -4.5f;
    float basinMinY = -1.6f;
    float basinMinZ = -9.0f;
    float basinMaxX = 4.5f;
    float basinMaxY = 0.38f;
    float basinMaxZ = -4.5f;
    float waterPlaneY = -0.04f;
    float copingTopY = 0.04f;
    // Top of the solid pool wall (the rim the water can splash over). Below this the walls are
    // solid; above it the container is open. LoadPoolLayout derives it from the authored
    // freeboard. Walls used to stop at waterPlaneY, which left a 0.4 m hole through which a
    // splash could pass straight through the wall and be snapped onto the deck.
    float wallTopY = 0.38f;

    // Particle fluid parameters.
    float particleRadius = 0.13f;    // lattice SPACING (the collision margin is 0.5 * this)
    float smoothingRadius = 0.26f;   // h = 2 x spacing (the PBF paper does not need SPH's 30-40 neighbours)
    // Dimensionless rest density: the kernels are mass-normalised so a full lattice
    // measures exactly 1 (see restDensityScale).
    float restDensity = 1.0f;
    // Multiplier on the normalised rest density: 1.0 is the neutral lattice, >1 contracts
    // the fluid, <1 lets it expand.
    float restDensityScale = 1.0f;
    float stiffnessEps = 0.05f;      // CFM relaxation on the normalised denominator (eq 11)
    float artPressureK = 0.0f;       // eq 13; 0 with the unilateral constraint (see Backyard8Scene.cpp)
    float artPressureN = 4.0f;
    float viscosityC = 0.15f;        // eq 17, normalised-XSPH equivalent (see Backyard8Scene.cpp)
    float gravity = 9.81f;
    // Surface wind: direction is (windX, windZ), speed in m/s. Applied as a tangential
    // drag on particles in the top layer of the free surface, so it shows on the water
    // without pushing the bulk around.
    float windX = 1.0f;
    float windZ = 0.0f;
    // Multiplier on the physical air-drag term in CS_PbfIntegrate. 1 is real air drag and
    // leaves the pool essentially still; raise it for visible ripples.
    float windStrength = 1.0f;
    // Multiplier on the no-slip boundary viscosity in CS_PbfUpdateVelocity. 1 uses the
    // fluid's XSPH coefficient against a stationary wall; higher values damp the wall
    // layer faster.
    float wallFriction = 8.0f;
    float maxVelocity = 12.0f;

    uint32_t solverIterations = 3;
    uint32_t substeps = 2;
    float deltaTime = 1.0f / 60.0f;

    // Initial condition: a sphere of water dropped into the pool.
    // The sphere's CENTRE is `sphereHeight` metres above the resting water plane, i.e. the
    // bottom of the sphere is (sphereHeight - sphereRadius) above it. With the shipped
    // values the sphere hangs ~1 m above the pool rim and ~1 m above that again.
    float sphereRadius = 2.4f;
    float sphereHeight = 3.48f;
    bool dropSphere = true;
    // Resting water seeded under the drop: its surface in metres relative to the water plane
    // (0 = full to the plane). At or below the basin floor the pool starts empty.
    float prefillLevel = -10.0f;
    // Particles that fall below this Y are removed (they missed the pool and hit the deck).
    // 0 disables culling.
    float cullBelowY = 0.0f;
    // Particles that drift outside this XZ margin around the pool footprint are culled
    // (they splashed onto the deck). 0 disables.
    float cullMargin = 0.35f;

    uint32_t surfaceWidth = 100;
    uint32_t surfaceDepth = 50;
    uint32_t maxParticles = 40000;
  };

  class PbfFluid {
  public:
    PbfFluid();
    ~PbfFluid();

    PbfFluid(const PbfFluid&) = delete;
    PbfFluid& operator=(const PbfFluid&) = delete;

    bool TryInitialize(BaseDriver* driver, const PbfSettings& settings, const char* layoutPath);
    void Shutdown(BaseDriver* driver);

    // Re-seeds the water sphere (reset / GUI re-drop).
    bool ResetToRest(BaseDriver* driver);
    // Publishes the live particle buffers into ParticleBufferRegistry under these names so
    // the generic particle renderer can draw them. Call after seeding and after any reset.
    void PublishParticleBuffers(const std::string& positionName, const std::string& velocityName);
    // Number of particles removed by the deck/handle culling, cumulative.
    uint32_t CulledParticles() const { return m_culledTotal; }
    bool Step(BaseDriver* driver);
    // Runs `seconds` of simulation with speeds capped at `maxSpeed` and no wind, then restores the
    // settings and the clock: a seeded resting pool compacts before it is shown.
    bool Settle(BaseDriver* driver, float seconds, float maxSpeed);
    // The water start keeps the relaxed sphere pending while the pool settles; call this
    // after Settle() to append it (positions + zero velocities) and rebuild the buffers.
    bool HasPendingSphere() const { return !m_pendingSphere.empty(); }
    bool AppendPendingSphere(BaseDriver* driver);

    bool IsReady() const { return m_ready; }
    uint32_t ParticleCount() const { return m_particleCount; }
    const PbfSettings& Settings() const { return m_settings; }
    float SimulationTime() const { return m_time; }

    // The driver-owned RGBA16F target the surface pass writes (FluidSimulation contract).
    void SetSurfaceOutput(Texture* texture) { m_surfaceOutput = texture; }
    Texture* SurfaceTarget() const { return m_surfaceOutput; }

    struct Diagnostics {
      float meanSpeed = 0.0f;
      float maxSpeed = 0.0f;
      uint32_t aboveRim = 0;
      uint32_t nonFinite = 0;
      float maxY = 0.0f;
      float meanY = 0.0f;
      uint32_t dead = 0;      // retired particles parked out of the simulation
      float meanDensity = 0.0f;   // Lambda.y, 1.0 == rest
      float maxDensity = 0.0f;
      float maxSpeedX = 0.0f, maxSpeedY = 0.0f, maxSpeedZ = 0.0f;
    };
    bool ReadDiagnostics(BaseDriver* driver, Diagnostics& out);
    bool ReadParticles(BaseDriver* driver, std::vector<float>& outXyz);
    // Full field readback for validation: positions (xyzw, w = alive), velocities (xyzw) and
    // lambda (x = lambda, y = density, z = |sum gradW|^2, w = neighbour count), all
    // particleCount * 4 floats. Any of the outputs may be null to skip that buffer.
    bool ReadParticleFields(BaseDriver* driver, std::vector<float>* positions,
                            std::vector<float>* velocities, std::vector<float>* lambda);
    bool ReadColumnHeights(BaseDriver* driver, std::vector<float>& outElevation);

    // Live tuning, mirroring the setters the Backyard GUI already drives.
    void SetSolverIterations(uint32_t n) { m_settings.solverIterations = (std::max)(n, 1u); }
    void SetSubsteps(uint32_t n) { m_settings.substeps = (std::max)(n, 1u); }
    void SetViscosity(float c) { m_settings.viscosityC = c; }
    void SetStiffness(float eps) { m_settings.stiffnessEps = (std::max)(eps, 0.0f); }
    void SetArtificialPressure(float k, float n) {
      m_settings.artPressureK = k;
      m_settings.artPressureN = n;
    }
    void SetGravity(float g) { m_settings.gravity = g; }
    void SetDeltaTime(float dt) { m_settings.deltaTime = (std::max)(dt, 1e-4f); }
    void SetWind(float x, float z) { m_settings.windX = x; m_settings.windZ = z; }
    void SetWindStrength(float s) { m_settings.windStrength = s; }
    void SetWallFriction(float f) { m_settings.wallFriction = (std::max)(f, 0.0f); }
    void SetMaxVelocity(float v) { m_settings.maxVelocity = (std::max)(v, 0.0f); }
    void SetSphere(float radius, float height) {
      m_settings.sphereRadius = (std::max)(radius, 0.1f);
      m_settings.sphereHeight = height;
    }

  private:
    bool CreatePipelines(BaseDriver* driver);
    bool CreateFieldBuffers(BaseDriver* driver);
    // Creates the six particle buffers. An empty (or wrong-sized) velocity vector seeds
    // zero velocities; otherwise the pool's settled velocities are preserved.
    bool CreateParticleBuffers(BaseDriver* driver, const std::vector<float>& positions,
                               const std::vector<float>& velocities);
    bool SeedParticles(BaseDriver* driver);
    std::vector<uint32_t> BuildConstants(uint32_t iteration) const;
    bool LoadPoolLayout(const char* path);
    std::unique_ptr<ComputePipeline> CreatePipeline(BaseDriver* driver, const char* source);

    PbfSettings m_settings;
    BaseDriver* m_driver = nullptr;
    bool m_ready = false;
    float m_time = 0.0f;
    uint32_t m_particleCount = 0;
    uint32_t m_culledTotal = 0;
    std::string m_publishedPositionName;
    std::string m_publishedVelocityName;

    // rho_lattice * spacing^3: the kernel mass normalisation passed to the shaders.
    float m_restScale = 1.0f;
    float m_restDenomReference = 1.0f;
    float m_minNeighbours = 8.0f;
    uint32_t m_gridX = 1, m_gridY = 1, m_gridZ = 1;
    float m_gridOriginX = 0.0f, m_gridOriginY = 0.0f, m_gridOriginZ = 0.0f;

    // Pool step intrusions, 8 floats per box: min xyz then max xyz.
    std::vector<float> m_solidBoxes;
    // Relaxed sphere held back until the resting pool has settled (water start).
    std::vector<float> m_pendingSphere;

    // Sized to the live particle count, so they are rebuilt whenever the seed changes.
    std::unique_ptr<ComputeBuffer> m_positions;
    std::unique_ptr<ComputeBuffer> m_predicted;
    // Jacobi ping-pong partner for m_predicted: the solve reads one and writes the other.
    std::unique_ptr<ComputeBuffer> m_predictedNext;
    std::unique_ptr<ComputeBuffer> m_velocities;
    // XSPH ping-pong partner: the update reads one and writes the other.
    std::unique_ptr<ComputeBuffer> m_velocitiesNext;
    std::unique_ptr<ComputeBuffer> m_lambda;

    // Fixed by the domain; created once.
    std::unique_ptr<ComputeBuffer> m_grid;
    std::unique_ptr<ComputeBuffer> m_solidBoxBuffer;
    std::unique_ptr<ComputeBuffer> m_columnHeights;

    std::unique_ptr<ComputePipeline> m_integrate;
    std::unique_ptr<ComputePipeline> m_clearGrid;
    std::unique_ptr<ComputePipeline> m_buildGrid;
    std::unique_ptr<ComputePipeline> m_density;
    std::unique_ptr<ComputePipeline> m_solve;
    std::unique_ptr<ComputePipeline> m_updateVelocity;
    std::unique_ptr<ComputePipeline> m_surface;
    std::unique_ptr<ComputePipeline> m_clearHeights;
    std::unique_ptr<ComputePipeline> m_cull;
    std::unique_ptr<ComputePipeline> m_copyFields;
    // Compaction scratch + bookkeeping for particle culling.

    Texture* m_surfaceOutput = nullptr;
  };

}  // namespace t850
