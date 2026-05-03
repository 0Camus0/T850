#pragma once
#include <utils/xMaths.h>
#include <utils/Camera.h>
#include <utils/Timer.h>
#include <scene/SplineWireframe.h>
#include <scene/WireframeSphere.h>
#include <scene/WireframeArrow.h>
#include <utils/Spline.h>
#include <scene/SceneSetup.h>
#include <scene/RenderGraph.h>
#include <debug/FrameDumper.h>
#include <scene/LensFlare.h>
#include <scene/TextRenderer.h>
#include <gui/GUIManager.h>
#include <Config.h>


#include <core/Core.h>
class DayScene : public t850::SceneBase
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
    CHANGE_IBL_FACTOR,
    CHANGE_MATERIAL_EMISSIVE_INTENSITY,
    CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER,
    CHANGE_MATERIAL_REFRACTION_STRENGTH,
    CHANGE_CUBEMAP,
    CHANGE_PARALLAX_SHADOW_MIN_LAYERS,
    CHANGE_PARALLAX_SHADOW_MAX_LAYERS,
    CHANGE_PARALLAX_SHADOW_SOFTNESS,
    CHANGE_PARALLAX_SHADOW_STRENGTH,
    CHANGE_PARALLAX_SHADOW_TOGGLE,
    CHANGE_MAX_NUM_OPTIONS
  };
  public:
  DayScene() {}
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
  void PopulateGUI(t850::GUIManager& gui) override;
  void SyncToGUI(t850::GUIManager& gui) override;
  void SyncFromGUI(t850::GUIManager& gui) override;

  // Dump scene state to JSON
  void SaveSceneState() override;
  void RequestDump() override { m_dumper.RequestDump(); }

  // Helper: find selector index for a light count value
  int FindLightOption(int activeLights);

  float DtSecs;
  t850::PrimitiveManager PrimitiveMgr;
  t850::PrimitiveInst	Cubes[10];
  t850::PrimitiveInst	Triangles[10];
  t850::PrimitiveInst   Meshes[10];
  t850::PrimitiveInst	QuadInst;

  t850::PrimitiveInst	Quads[10];

  t850::SceneSetup  m_sceneSetup;
  t850::RenderGraph  m_renderGraph;
  t850::FrameDumper  m_dumper;

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
  int       DiffuseIBLTexIndex = -1;
  int       SpecularIBLTexIndex = -1;
  int       BrdfLUTTexIndex = -1;
  int       SheenIBLTexIndex = -1;
  int       CharlieLUTTexIndex = -1;
  int       SheenELUTTexIndex = -1;
  t850::EnvironmentMapSet EnvMaps;
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
  std::string m_pendingCubemap; // deferred load for D3D12 safety

  t850::SplineWireframe* splineWire;
  t850::PrimitiveInst splineInst;
  t850::WireframeSphere m_wireframeSphere;
  t850::WireframeArrow m_wireframeArrow;
  t850::TextRenderer m_debugText;
  bool m_showCullStats = false;
  t850::LensFlare m_flare;
  XMATRIX44 m;
};

