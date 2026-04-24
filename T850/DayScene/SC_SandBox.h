#pragma once
#include <core/Core.h>
#include <scene/PrimitiveManager.h>
#include <scene/PrimitiveInstance.h>
#include <utils/xMaths.h>
#include <utils/Camera.h>
#include <utils/Timer.h>
#include <scene/SceneSetup.h>
#include <scene/RenderGraph.h>
#include <scene/WireframeSphere.h>
#include <scene/T8_TextRenderer.h>
#include <debug/FrameDumper.h>
#include <gui/T8_GUI.h>
#include <Config.h>

class SC_SandBox : public t800::SceneBase
{
  enum {
    CHANGE_EXPOSURE = 0,
    CHANGE_BLOOM_FACTOR,
    CHANGE_BLOOM_THRESHOLD,
    CHANGE_TM_WHITE_LEVEL,
    CHANGE_TM_ADAPT_TAU,
    CHANGE_PCF_RADIUS,
    CHANGE_PCF_SAMPLES,
    CHANGE_SSAO_KERNEL_SIZE,
    CHANGE_SSAO_RADIUS,
    CHANGE_DOF_APERTURE,
    CHANGE_DOF_FOCAL_LENGHT,
    CHANGE_DOF_MAX_COC,
    CHANGE_DOF_FAR_SAMPLE,
    CHANGE_DOF_NEAR_SAMPLE,
    CHANGE_LIGHT_VOLUME_STEPS,
    CHANGE_GODRAYS_FACTOR,
    CHANGE_GAUSS_KERNEL_RADIUS,
    CHANGE_GAUSS_KERNEL_DEVIATION,
    CHANGE_FOV,
    CHANGE_LIGHT_INTENSITY,
    CHANGE_SHADOW_BIAS,
    CHANGE_SHADOW_MIN,
    CHANGE_ENV_FACTOR,
    CHANGE_IBL_FACTOR,
    CHANGE_PCF_TOOGLE,
    CHANGLE_SSAO_TOOGLE,
    CHANGE_DEBUG_RT,
    CHANGE_CUBEMAP,
    CHANGE_GAUSS_KERNEL_SAMPLE_COUNT,
    CHANGE_ACTIVE_GAUSS_KERNEL,
    // Animation controls (only active for skinned meshes)
    CHANGE_ANIM_SPEED,
    CHANGE_ANIM_SELECT,
    CHANGE_ANIM_MODE,     // selector: "Interpolation" / "Keyframe"
    CHANGE_SHOW_WIREFRAME,
    CHANGE_SHOW_SKELETON,
    CHANGE_MAX_NUM_OPTIONS
  };
public:
  SC_SandBox() {}
  void OnUpdate(float _DtSecs);
  void OnDraw();
  void OnInput(InputManager* IManager);
  void OnLoadScene();
  void OnDestoryScene();
  void InitVars();
  void CreateAssets();
  void DestroyAssets();

  void PopulateGUI(t800::GUIManager& gui) override;
  void SyncToGUI(t800::GUIManager& gui) override;
  void SyncFromGUI(t800::GUIManager& gui) override;
  void RequestDump() override { m_dumper.RequestDump(); }

  float DtSecs = 0.0f;
  t800::PrimitiveManager PrimitiveMgr;
  t800::PrimitiveInst Meshes[10];
  t800::PrimitiveInst Quads[10];

  t800::RenderGraph m_renderGraph;
  t800::SceneSetup m_guiSetup; // loaded from SC_SandBox.json for GUI descriptors
  t800::FrameDumper m_dumper;
  int ChangeActiveGaussSelection = 1; // 0=Shadow, 1=Bloom, 2=DOF
  int m_debugRTSelection = 0;

  Camera Cam;
  Camera LightCam;
  Camera* ActiveCam = nullptr;

  XMATRIX44 VP;
  XMATRIX44 m;

  GaussFilter ShadowFilter;
  GaussFilter BloomFilter;
  GaussFilter NearDOFFilter;

  int EnvMapTexIndex = -1;
  int GBufferPass = -1;
  int DeferredPass = -1;
  int Extra16FPass = -1;
  int DepthPass = -1;
  int ShadowAccumPass = -1;
  int ExtraHelperPass = -1;
  int BloomAccumPass = -1;
  int LuminanceMapPass = -1;
  int AdaptedLumCurrentPass = -1;
  int AdaptedLumPrevPass = -1;

  int m_currentCubemapIndex = 0;
  std::string m_pendingCubemap; // deferred load — set in SyncFromGUI, applied in OnUpdate

  t800::TextRenderer m_debugText;
  t800::WireframeSphere m_debugSphere;
  bool m_showCullStats = false;
  bool m_showAABBs = false;
  bool m_showWireframe = true;
  bool m_showSkeleton = true;

  // Orbit camera state
  XVECTOR3 m_orbitTarget;    // center of the model (world space)
  XVECTOR3 m_panOffset;      // accumulated pan offset
  float m_orbitYaw   = 0.0f;
  float m_orbitPitch = 0.3f;
  float m_orbitDist  = 5.0f;
  float m_modelRadius = 1.0f;

  void ComputeOrbitCamera();
  void FitModelToView();
};
