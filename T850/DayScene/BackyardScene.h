#pragma once

#include <core/Core.h>
#include <debug/FrameDumper.h>
#include <scene/RenderContainer.h>
#include <scene/SceneSetup.h>
#include <simulation/FluidDebugRenderer.h>
#include <simulation/FluidSimulation.h>
#include <utils/CameraProfiles.h>

#include <vector>

// Backyard: authored residential backyard with a house and a detached pool
// mesh. The pool GLB carries named wall/floor/step nodes so the fluid solver
// can derive its boundary mask from geometry instead of authored numbers.
class BackyardScene final : public t850::SceneBase {
public:
  BackyardScene();

  void InitVars() override;
  void CreateAssets() override;
  void DestroyAssets() override;
  void OnLoadScene() override;
  void OnDestoryScene() override;
  void OnUpdate(float deltaSeconds) override;
  void OnDraw() override;
  void OnInput(InputManager* input) override;
  void DrawDevGui(t850::DevGuiContext& gui) override;
  // The GUI can change the solver grid, which means new 3D textures.
  bool RebuildFluid();
  // Clears the solver to a flat, motionless pool and re-arms the dropped body at the
  // configured height, so a changed drop parameter can be retested from a comparable
  // initial state.
  void ResetSimulation();
  void RequestDump() override { m_dumper.RequestDump(); }
  void ResetViewInput() override { m_cameraController.ClearInput(); }
  // Test hook: spawn this many balls as soon as the solver is ready. Used by
  // --backyard-spawn so the multi-body path can be exercised without the GUI.
  static void SetSpawnOnStart(int count) { s_spawnOnStart = count; }

private:
  struct AssetRequest {
    const char* path;
    const char* label;
  };

  static constexpr int kSceneIndex = 7;

  void EnsureWaterShaderPermutations(t850::BaseDriver* driver);
  // Enables/clears the FLUID_SURFACE permutation on the water mesh. Called only
  // from RebuildFluid so the flag always matches solver availability.
  void SetWaterFluidFeature(bool enabled);
  // Builds and binds the static per-column floor height texture at t24, so the
  // water shader can use the real depth over the submerged steps.
  bool BindFloorHeightTexture(t850::BaseDriver* driver, t850::PrimitiveInst* water);
  // The solver's surface texture has row 0 at basinMinZ, so the water mesh's UV v must
  // grow with world Z. Returns false (and logs) for a mirrored mesh.
  bool WaterUVMatchesSolver(t850::PrimitiveInst* water) const;
  int m_waterFloorTexture = -1;
  uint32_t m_waterFloorWidth = 0;
  uint32_t m_waterFloorDepth = 0;
  t850::FluidSettings MakeFluidSettings() const;
  void DrawFluidDebug();

  t850::RenderContainer m_renderContainer;
  t850::PrimitiveManager PrimitiveMgr;
  t850::SceneSetup m_sceneSetup;
  t850::CameraController m_cameraController;
  t850::FrameDumper m_dumper;

  Camera m_camera;
  Camera m_lightCamera;

  std::vector<unsigned int> m_primitiveIndices;
  std::vector<t850::RenderInstanceHandle> m_instances;
  t850::RenderInstanceHandle m_waterHandle;
  t850::RenderInstanceHandle m_ballHandle;  t850::FluidSimulation m_fluid;
  bool m_fluidReady = false;
  // ── Spawned balls (W-22) ──
  // m_ballPrimitive is the primitive loaded for Backyard_Ball.glb; every spawned ball
  // shares it and only differs by transform and scale.
  int m_ballPrimitive = -1;
  std::vector<t850::RenderInstanceHandle> m_extraBallHandles;
  // Deterministic LCG state for spawn placement: spawnSeed != 0 reproduces a run.
  uint32_t m_spawnRng = 1u;
  // Drops one ball at a random place inside the basin, at a random radius and density.
  // Returns false at the cap or when no free spot was found.
  bool SpawnBall();
  void ClearSpawnedBalls();
  // Welds every spawned ball's instance to its simulated body.
  void UpdateExtraBallTransforms();
  float NextSpawnRandom();
  t850::FluidDebugRenderer m_fluidDebug;
  t850::FluidDebugView m_fluidDebugView;
  bool m_fluidDebugMeshBound = false;
  bool m_hideWater = false;
  // Stable 2D target the solver copies its surface into every Step, so the
  // water mesh binds one texture for the whole frame instead of chasing the
  // solver's ping-pong pair.
  int m_waterSurfaceRT = -1;
  // Grid the solver is actually running, so the GUI can detect a change.
  int m_appliedGridWidth = 0;
  int m_appliedGridHeight = 0;
  int m_appliedGridDepth = 0;
  float m_appliedCellSize = 0.0f;
  // Cell size being dragged in the GUI; committed (and the grid rebuilt) on release.
  float m_gridEditCell = -1.0f;
  // Seconds to wait before releasing the dropped sphere.
  float m_sphereReleaseDelay = 1.0f;
  float m_elapsedSeconds = 0.0f;
  t850::EnvironmentMapSet m_envMaps;
  int m_envMapTexture = -1;
  int m_diffuseIBLTexture = -1;
  int m_specularIBLTexture = -1;
  int m_brdfLUTTexture = -1;
  int m_sheenIBLTexture = -1;
  int m_charlieLUTTexture = -1;
  int m_sheenELUTTexture = -1;

  float m_deltaSeconds = 0.0f;
  bool m_assetsCreated = false;
  static int s_spawnOnStart;
};
