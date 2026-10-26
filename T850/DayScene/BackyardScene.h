#pragma once

#include <core/Core.h>
#include <debug/FrameDumper.h>
#include <scene/RenderContainer.h>
#include <scene/SceneSetup.h>
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
  void RequestDump() override { m_dumper.RequestDump(); }
  void ResetViewInput() override { m_cameraController.ClearInput(); }

private:
  struct AssetRequest {
    const char* path;
    const char* label;
  };

  static constexpr int kSceneIndex = 7;

  void EnsureWaterShaderPermutations(t850::BaseDriver* driver);
  t850::FluidSettings MakeFluidSettings() const;

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
  t850::FluidSimulation m_fluid;
  bool m_fluidReady = false;
  // Stable 2D target the solver copies its surface into every Step, so the
  // water mesh binds one texture for the whole frame instead of chasing the
  // solver's ping-pong pair.
  int m_waterSurfaceRT = -1;
  // Grid the solver is actually running, so the GUI can detect a change.
  int m_appliedGridWidth = 0;
  int m_appliedGridHeight = 0;
  int m_appliedGridDepth = 0;
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
};
