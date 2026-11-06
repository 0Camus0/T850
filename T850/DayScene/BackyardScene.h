#pragma once

#include <BackyardWaterJson.h>
#include <core/Core.h>
#include <debug/FrameDumper.h>
#include <scene/RenderContainer.h>
#include <scene/SceneSetup.h>
#include <scene/WireframeArrow.h>
#include <simulation/FluidDebugRenderer.h>
#include <simulation/FluidSimulation.h>
#include <utils/CameraProfiles.h>
#include <utils/InputManager.h>
#include <utils/Picking.h>

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
  // The runtime GUI is the scene's control panel, so the camera and ball dragging stay
  // usable while it is open; ImGui's own mouse capture is checked in OnInput.
  bool AllowsInputWhenRuntimeGuiVisible() const override { return true; }
  // Holding Ctrl, or dragging a ball, releases the camera's mouse capture so the cursor
  // is free to pick balls; the capture returns when both end.
  bool AllowsMouseCapture() const override { return SceneProp.MouseCaptureAllowed && !m_cursorFreed; }
  // Test hook: spawn this many balls as soon as the solver is ready. Used by
  // --backyard-spawn so the multi-body path can be exercised without the GUI.
  static void SetSpawnOnStart(int count) { s_spawnOnStart = count; }
  // Test hook (--backyard-spawn-seed): fixes the spawn RNG so a run is reproducible.
  static void SetSpawnSeed(uint32_t seed) { s_spawnSeed = seed; }
  // Test hook (--backyard-mass-audit N): log the W-03 mass audit every N frames.
  static void SetMassAuditFrames(int frames) { s_massAuditFrames = frames > 0 ? frames : 0; }
  // Test hook (--backyard-ball-probe N): log the ball's vertical state and the radial
  // surface profile around it every N frames. This is the instrument W-06 and W-07 need:
  // the mass audit averages over the whole pool and cannot see a 2 cm moat.
  static void SetBallProbeFrames(int frames) { s_ballProbeFrames = frames > 0 ? frames : 0; }
  // Test hook (--backyard-pick-test): drives the real pick/drag input path with a
  // synthetic InputManager, so no OS mouse or keyboard input is ever generated.
  static void SetPickTest(bool enabled) { s_pickTest = enabled; }

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
  // W-30 follow-up: an arrow over the pool whose direction is the wind and whose length is its
  // speed. Uses the shared wireframe-arrow primitive, so it is the same geometry on every API.
  void DrawWindGizmo();

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
  // ── Per-ball PBR materials (W-25) ──
  // Textures are created on a material's first use and kept until DestroyAssets, so
  // the resident set is bounded by the palette, not by the ball count.
  struct BallMaterialMaps {
    int colorIndex = -1, normalIndex = -1, ormIndex = -1;
    t850::Texture* color = nullptr;
    t850::Texture* normal = nullptr;
    t850::Texture* orm = nullptr;
    bool attempted = false;
  };
  t850::BackyardBallPalette m_ballPalette;
  std::vector<BallMaterialMaps> m_ballMaterialMaps;   // parallel to m_ballPalette.materials
  // Shuffle bag: every material is used once before any repeats. Seeded from the spawn
  // seed through a separate LCG, so material picks never shift the spawn positions.
  t850::BackyardBallMaterialBag m_ballMaterialBag;
  void LoadBallPalette();
  void DestroyBallMaterials();
  // Gives the instance a random palette material; returns its label, or nullptr when
  // no material is available (the GLB's own placeholder look stays).
  const char* AssignBallMaterial(t850::PrimitiveInst& inst);
  // ── Mouse drag: Ctrl + left button on a ball holds it under the cursor ──
  int m_dragBody = -1;                // held body index, -1 when nothing is held
  bool m_cursorFreed = false;         // Ctrl held or a ball held: no mouse capture
  float m_dragDistance = 0.0f;        // along the cursor ray, eye to the ball centre
  XVECTOR3 m_dragOffset;              // ball centre minus its projection on the ray
  XVECTOR3 m_dragTarget;              // where the cursor wants the centre this frame
  t850::Ray CursorRay(float mouseX, float mouseY) const;
  bool ProjectToScreen(const XVECTOR3& world, float& x, float& y) const;
  // Camera and ball input; OnInput feeds the real InputManager, the pick test a fake one.
  void ProcessSceneInput(InputManager* input, bool imguiWantsMouse);
  // Returns true while a ball is held, so the camera ignores that mouse gesture.
  bool HandleBallDrag(InputManager* input, bool imguiWantsMouse, bool ctrlDown);
  // Steers the held body toward the cursor target over the next solver step.
  void ApplyBallDrag(float dt);
  // Releases the held body; it keeps its last velocity, so a fast drag throws it.
  void EndBallDrag();
  // ── --backyard-pick-test state ──
  static bool s_pickTest;
  InputManager m_pickTestInput;
  int m_pickTestFrame = 0;
  int m_pickTestTarget = -1;
  int m_pickTestVictim = -1;
  int m_pickTestFailures = 0;
  bool m_pickTestDone = false;
  XVECTOR3 m_pickTestVictimStart;
  float m_pickTestFrom[2] = {0.0f, 0.0f};
  float m_pickTestTo[2] = {0.0f, 0.0f};
  void RunPickTestStep();
  t850::FluidDebugRenderer m_fluidDebug;
  t850::WireframeArrow m_windArrow;
  bool m_windArrowReady = false;
  bool m_showWind = false;
  // Metres of arrow per m/s of wind. 2 local units per call, so Draw gets length/2.
  float m_windArrowScale = 0.5f;
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
  // Last wind heading the GUI showed, in degrees counter-clockwise from +X. Sticky so that
  // dragging the air speed to 0 and back does not silently rotate the wind.
  float m_windHeadingDeg = 0.0f;
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
  static uint32_t s_spawnSeed;
  static int s_massAuditFrames;
  int m_massAuditCounter = 0;
  static int s_ballProbeFrames;
  int m_ballProbeCounter = 0;
  float m_probeRimPeak = 0.0f;
  float m_probeMoatMin = 0.0f;
};
