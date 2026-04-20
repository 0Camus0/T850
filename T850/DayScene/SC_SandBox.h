#pragma once
#include <core/Core.h>
#include <scene/PrimitiveManager.h>
#include <scene/PrimitiveInstance.h>
#include <utils/xMaths.h>
#include <utils/Camera.h>
#include <utils/Timer.h>
#include <scene/SceneSetup.h>
#include <scene/RenderGraph.h>
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
    CHANGE_PCF_TOOGLE,
    CHANGLE_SSAO_TOOGLE,
    CHANGE_DEBUG_RT,
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

  float DtSecs = 0.0f;
  t800::PrimitiveManager PrimitiveMgr;
  t800::PrimitiveInst Meshes[10];
  t800::PrimitiveInst Quads[10];

  t800::RenderGraph m_renderGraph;
  t800::SceneSetup m_guiSetup; // loaded from SC_Day.json for GUI descriptors

  int ChangeActiveGaussSelection = 0;
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
  int GodRaysCalcPass = -1;
  int CoCPass = -1;
  int CombineCoCPass = -1;
  int CoCHelperPass = -1;
  int CoCHelperPass2 = -1;
};
