#pragma once
#include <core/Core.h>
#include <utils/xMaths.h>
#include <utils/Camera.h>
#include <utils/Timer.h>
#include <scene/SplineWireframe.h>
#include <utils/T8_Spline.h>
#include <scene/LensFlare.h>
#include <scene/T8_TextRenderer.h>
#include <Config.h>


#include <core/Core.h>
class SC_Tech : public t800::SceneBase
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
    CHANGE_PCF_TOOGLE,
    CHANGLE_SSAO_TOOGLE,
    CHANGE_MAX_NUM_OPTIONS
  };
public:
  SC_Tech() {}
  void OnUpdate(float _DtSecs);
  void OnDraw();
  void OnInput(InputManager* IManager);
  void OnLoadScene();
  void OnDestoryScene();
  void InitVars();
  void CreateAssets();
  void DestroyAssets();

  float DtSecs;
  t800::PrimitiveManager PrimitiveMgr;
  t800::PrimitiveInst	Cubes[10];
  t800::PrimitiveInst	Triangles[10];
  t800::PrimitiveInst   Meshes[10];
  t800::PrimitiveInst	QuadInst;

  t800::PrimitiveInst	Quads[10];

  Camera			Cam;
  Camera			LightCam;

  GaussFilter		ShadowFilter;
  GaussFilter		BloomFilter;
  GaussFilter		NearDOFFilter;

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
  int				ExtraHelperPass;
  int Extra16FPass;
  int GodRaysCalcPass;
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

  t800::SplineWireframe* splineWire;
  t800::PrimitiveInst splineInst;
  t800::SplineAgent m_agent;
  t800::Spline m_spline;

  XMATRIX44 m;
  float totalTime;
};

