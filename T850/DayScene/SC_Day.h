#pragma once
#include <utils/xMaths.h>
#include <utils/Camera.h>
#include <utils/Timer.h>
#include <scene/SplineWireframe.h>
#include <scene/WireframeSphere.h>
#include <scene/WireframeArrow.h>
#include <utils/T8_Spline.h>
#include <scene/SceneSetup.h>
#include <scene/RenderGraph.h>
#include <debug/FrameDumper.h>
#include <scene/LensFlare.h>
#include <scene/T8_TextRenderer.h>
#include <gui/T8_GUI.h>
#include <Config.h>


#include <core/Core.h>
class SC_Day : public t800::SceneBase
{
  enum {
    DRAW_CUBE_SPINNING = 0,
    DRAW_CUBE_BIG,
    DRAW_MESH,
    DRAW_ALL,
  };

  enum {
    SHADOW_KERNEL = 0,
    BLOOM_KERNEL = 1,
    DOF_KERNEL = 2
  };

  enum {
    CHANGE_EXPOSURE = 0,
    CHANGE_BLOOM_FACTOR,
    CHANGE_BLOOM_THRESHOLD,
    CHANGE_TM_WHITE_LEVEL,
    CHANGE_TM_ADAPT_TAU,
    CHANGE_NUM_LIGHTS,
    CHANGE_ACTIVE_GAUSS_KERNEL,
    CHANGE_GAUSS_KERNEL_SAMPLE_COUNT,
    CHANGE_GAUSS_KERNEL_RADIUS,
    CHANGE_GAUSS_KERNEL_DEVIATION,    
	CHANGE_PCF_RADIUS,
    CHANGE_PCF_SAMPLES,
    CHANGE_SSAO_KERNEL_SIZE,
    CHANGE_SSAO_RADIUS,
    CHANGE_DOF_APERTURE,
    CHANGE_DOF_FOCAL_LENGHT,
    CHANGE_DOF_MAX_COC,
    CHANGE_DOF_FAR_SAMPLE,
    CHANGE_DOF_NEAR_SAMPLE,
    CHANGE_DOF_AUTO_FOCUS,
    CHANGE_PARALLAX_LOW_SAMPLES,
    CHANGE_PARALLAX_HIGH_SAMPLES,
    CHANGE_PARALLAX_HEIGHT,
    CHANGE_LIGHT_VOLUME_STEPS,
  	CHANGE_GODRAYS_FACTOR,
	CHANGE_PCF_TOOGLE,
	CHANGLE_SSAO_TOOGLE,
    CHANGE_LIGHT_NEAR_PLANE,
    CHANGE_LIGHT_FAR_PLANE,
    CHANGE_DEBUG_RT,
    CHANGE_ACTIVE_CAMERA,
    CHANGE_FOV,
    CHANGE_SHOW_SPLINE,
    CHANGE_SHOW_LIGHTS,
    CHANGE_LIGHT_INTENSITY,
    CHANGE_DOF_TOGGLE,
    CHANGE_PARALLAX_TOGGLE,
    CHANGE_GODRAYS_TOGGLE,
    CHANGE_SHADOW_BIAS,
    CHANGE_SHADOW_MIN,
    CHANGE_ENV_FACTOR,
    CHANGE_CUBEMAP,
    CHANGE_PARALLAX_SHADOW_MIN_LAYERS,
    CHANGE_PARALLAX_SHADOW_MAX_LAYERS,
    CHANGE_PARALLAX_SHADOW_SOFTNESS,
    CHANGE_PARALLAX_SHADOW_STRENGTH,
    CHANGE_PARALLAX_SHADOW_TOGGLE,
    CHANGE_MAX_NUM_OPTIONS
  };
  public:
  SC_Day() {}
  void OnUpdate(float _DtSecs);
  void OnDraw();
  void OnInput(InputManager* IManager);
  void OnLoadScene();
  void OnDestoryScene();
  void InitVars();
  void CreateAssets();
  void DestroyAssets();

  void ChangeSettingsOnPlus();
  void ChangeSettingsOnMinus();
  void printCurrSelection();

  // GUI hooks (called by DevLayer)
  void PopulateGUI(t800::GUIManager& gui) override;
  void SyncToGUI(t800::GUIManager& gui) override;
  void SyncFromGUI(t800::GUIManager& gui) override;

  // Dump scene state to JSON
  void SaveSceneState() override;
  void RequestDump() override { m_dumper.RequestDump(); }

  // Helper: find selector index for a light count value
  int FindLightOption(int activeLights);

  float DtSecs;
  t800::PrimitiveManager PrimitiveMgr;
  t800::PrimitiveInst	Cubes[10];
  t800::PrimitiveInst	Triangles[10];
  t800::PrimitiveInst   Meshes[10];
  t800::PrimitiveInst	QuadInst;

  t800::PrimitiveInst	Quads[10];

  t800::SceneSetup  m_sceneSetup;
  t800::RenderGraph  m_renderGraph;
  t800::FrameDumper  m_dumper;

  Camera			*ActiveCam;

  XVECTOR3		Position;
  XVECTOR3		Orientation;
  XVECTOR3		Scaling;

  XMATRIX44		View;
  XMATRIX44		Projection;
  XMATRIX44		VP;

  int				SelectedMesh;
  int				RTIndex;
  int				GBufferPass;
  int				DeferredPass;
  int				DepthPass;
  int				ShadowAccumPass;
  int				BloomAccumPass;
  int       BrightPassPass;
  int				ExtraHelperPass;
  int Extra16FPass;
  int GodRaysCalcPass;
  int GodRaysCalcExtraPass;
  int LuminanceMapPass;
  int AdaptedLumCurrentPass;
  int AdaptedLumPrevPass;

  int CoCPass;
  int CoCHelperPass;
  int CoCHelperPass2;
  int DOFPass;
  int CombineCoCPass;
  int Extra16FPass5x5;
  //int				

  int				EnvMapTexIndex;
  int       fireTextureIndx;
  int       noiseTexture;

  enum {
    NORMAL_CAM1 = 0,
    LIGHT_CAM1,
    MAX_CAMS
  };
  int				CamSelection;
  int				SceneSettingSelection;
  int				ChangeActiveGaussSelection;
  int       m_debugRTSelection = 0;
  bool      m_showSpline = false;
  bool      m_showLights = false;
  int       m_activeCameraIndex = 0;
  std::vector<std::string> m_cubemapNames;
  int       m_currentCubemapIndex = 0;

  t800::SplineWireframe* splineWire;
  t800::PrimitiveInst splineInst;
  t800::WireframeSphere m_wireframeSphere;
  t800::WireframeArrow m_wireframeArrow;
  t800::LensFlare m_flare;
  XMATRIX44 m;
};

