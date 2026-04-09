#include "SC_Day.h"
#include <chrono>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>
#ifdef OS_WINDOWS
#include <windows.h>
#else
#include <sys/stat.h>
#endif
using namespace t800;

// ── Minimal JSON value extraction (no library needed) ──

static size_t findJsonSection(const std::string& json, const std::string& key, size_t start = 0) {
  std::string needle = "\"" + key + "\"";
  return json.find(needle, start);
}

static float extractFloat(const std::string& json, size_t regionStart, const std::string& key) {
  size_t pos = findJsonSection(json, key, regionStart);
  if (pos == std::string::npos) return 0.0f;
  pos = json.find(':', pos);
  if (pos == std::string::npos) return 0.0f;
  return std::stof(json.substr(pos + 1));
}

static void extractFloat3(const std::string& json, size_t regionStart, const std::string& key, float out[3]) {
  size_t pos = findJsonSection(json, key, regionStart);
  if (pos == std::string::npos) return;
  pos = json.find('[', pos);
  if (pos == std::string::npos) return;
  if (sscanf(json.c_str() + pos, "[%f , %f , %f]", &out[0], &out[1], &out[2]) != 3)
    sscanf(json.c_str() + pos, "[%f,%f,%f]", &out[0], &out[1], &out[2]);
}

struct FeedLightState {
  float position[3];
  float color[3];
  float radius;
  bool  valid;
};

struct FeedCameraState {
  float eye[3];
  float pitch, roll, yaw;
  XMATRIX44 View, Projection, VP;
  bool hasMatrices;
};

static const int MAX_FEED_LIGHTS = 8;

static bool extractMatrix(const std::string& json, size_t regionStart, const std::string& key, XMATRIX44& mat) {
  size_t pos = findJsonSection(json, key, regionStart);
  if (pos == std::string::npos) return false;
  pos = json.find('[', pos);
  if (pos == std::string::npos) return false;
  for (int r = 0; r < 4; r++) {
    pos = json.find('[', pos + 1);
    if (pos == std::string::npos) return false;
    if (sscanf(json.c_str() + pos, "[%f , %f , %f , %f]",
               &mat.m[r][0], &mat.m[r][1], &mat.m[r][2], &mat.m[r][3]) != 4)
      return false;
    pos = json.find(']', pos);
  }
  return true;
}

static bool loadFeedMatrices(const std::string& path, FeedCameraState& cam, FeedCameraState& lightCam,
                             FeedLightState* lights, int& numLights) {
  numLights = 0;
  std::ifstream f(path);
  if (!f.is_open()) {
    printf("[feedMatrices] ERROR: cannot open '%s'\n", path.c_str());
    return false;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  std::string json = ss.str();

  // If this is the old .txt format, try to parse that instead
  if (json.find('{') == std::string::npos) {
    // Old key=value format
    auto getVal = [&](const char* key) -> float {
      std::string k = std::string(key) + "=";
      size_t p = json.find(k);
      if (p == std::string::npos) return 0.0f;
      return std::stof(json.substr(p + k.size()));
    };
    auto getVec = [&](const char* key, float out[3]) {
      std::string k = std::string(key) + "=";
      size_t p = json.find(k);
      if (p == std::string::npos) return;
      sscanf(json.c_str() + p + k.size(), "%f,%f,%f", &out[0], &out[1], &out[2]);
    };
    getVec("Cam.Eye", cam.eye);
    cam.pitch = getVal("Cam.Pitch");
    cam.roll = getVal("Cam.Roll");
    cam.yaw = getVal("Cam.Yaw");
    getVec("LightCam.Eye", lightCam.eye);
    lightCam.pitch = getVal("LightCam.Pitch");
    lightCam.roll = getVal("LightCam.Roll");
    lightCam.yaw = getVal("LightCam.Yaw");
    printf("[feedMatrices] Loaded (txt format) from '%s'\n", path.c_str());
    return true;
  }

  // JSON format
  size_t camSec = findJsonSection(json, "cam");
  size_t lightSec = findJsonSection(json, "lightCam");
  if (camSec == std::string::npos || lightSec == std::string::npos) {
    printf("[feedMatrices] ERROR: missing 'cam' or 'lightCam' section\n");
    return false;
  }

  extractFloat3(json, camSec, "eye", cam.eye);
  cam.pitch = extractFloat(json, camSec, "pitch");
  cam.roll = extractFloat(json, camSec, "roll");
  cam.yaw = extractFloat(json, camSec, "yaw");

  extractFloat3(json, lightSec, "eye", lightCam.eye);
  lightCam.pitch = extractFloat(json, lightSec, "pitch");
  lightCam.roll = extractFloat(json, lightSec, "roll");
  lightCam.yaw = extractFloat(json, lightSec, "yaw");

  // Parse lights array if available
  size_t lightsSec = findJsonSection(json, "lights");
  if (lightsSec != std::string::npos) {
    // Find the opening '[' of the lights array
    size_t arrStart = json.find('[', lightsSec);
    if (arrStart != std::string::npos) {
      // Find the matching ']' for the lights array (skip nested brackets)
      size_t arrEnd = std::string::npos;
      int depth = 0;
      for (size_t i = arrStart; i < json.size(); i++) {
        if (json[i] == '[') depth++;
        else if (json[i] == ']') { depth--; if (depth == 0) { arrEnd = i; break; } }
      }
      if (arrEnd == std::string::npos) arrEnd = json.size();
      size_t pos = arrStart;
      while (numLights < MAX_FEED_LIGHTS) {
        size_t objStart = json.find('{', pos);
        if (objStart == std::string::npos || objStart >= arrEnd) break;
        // Find matching '}'
        size_t objEnd = json.find('}', objStart);
        if (objEnd == std::string::npos) break;
        FeedLightState& lt = lights[numLights];
        lt.valid = false;
        extractFloat3(json, objStart, "position", lt.position);
        extractFloat3(json, objStart, "color", lt.color);
        lt.radius = extractFloat(json, objStart, "radius");
        lt.valid = true;
        numLights++;
        pos = objEnd + 1;
      }
    }
  }

  // Parse precomputed matrices if available
  cam.hasMatrices = false;
  lightCam.hasMatrices = false;
  size_t matSec = findJsonSection(json, "matrices");
  if (matSec != std::string::npos) {
    cam.hasMatrices = extractMatrix(json, matSec, "camView", cam.View) &&
                      extractMatrix(json, matSec, "camProjection", cam.Projection) &&
                      extractMatrix(json, matSec, "camVP", cam.VP);
    lightCam.hasMatrices = extractMatrix(json, matSec, "lightCamView", lightCam.View) &&
                           extractMatrix(json, matSec, "lightCamProjection", lightCam.Projection) &&
                           extractMatrix(json, matSec, "lightCamVP", lightCam.VP);
  }

  printf("[feedMatrices] Loaded from '%s'\n", path.c_str());
  printf("  Cam: eye=[%.6f, %.6f, %.6f] pitch=%.6f roll=%.6f yaw=%.6f matrices=%s\n",
    cam.eye[0], cam.eye[1], cam.eye[2], cam.pitch, cam.roll, cam.yaw,
    cam.hasMatrices ? "yes" : "no");
  printf("  Light: eye=[%.6f, %.6f, %.6f] pitch=%.6f roll=%.6f yaw=%.6f matrices=%s\n",
    lightCam.eye[0], lightCam.eye[1], lightCam.eye[2], lightCam.pitch, lightCam.roll, lightCam.yaw,
    lightCam.hasMatrices ? "yes" : "no");
  if (numLights > 0)
    printf("  Lights: %d loaded\n", numLights);
  return true;
}

#define NUM_LIGHTS 1
#define RADI 170.0f

#define HIGHQ 1
#define MEDIUMQ 2
#define LOWQ 3

#define QUALITY_SELECTED HIGHQ

#if   QUALITY_SELECTED == HIGHQ
#define MAX_QUALITY
#elif QUALITY_SELECTED == MEDIUMQ
#define MEDIUM_QUALITY
#elif QUALITY_SELECTED == LOWQ
#define LOW_QUALITY 
#endif

void SC_Day::InitVars() {
  Position = XVECTOR3(0.0f, 0.0f, 0.0f);
  Orientation = XVECTOR3(0.0f, 0.0f, 0.0f);
  Scaling = XVECTOR3(1.0f, 1.0f, 1.0f);
  SelectedMesh = 0;

  CamSelection = NORMAL_CAM1;
  SceneSettingSelection = CHANGE_EXPOSURE;

  Cam.InitPerspective(XVECTOR3(0.0f, 1.0f, 10.0f), Deg2Rad(46.8f), 1280.0f / 720.0f, 2.0f, 12000.0f);
  Cam.Speed = 10.0f;
  Cam.Eye = XVECTOR3(0.0f, 9.75f, -31.0f);
  Cam.Pitch = 0.14f;
  Cam.Roll = 0.0f;
  Cam.Yaw = 0.020f;
  Cam.Update(0.0f);

  LightCam.InitOrtho(XVECTOR3(0.0f, 100.0f, 10.0f), 130.0f, 130.0f, 0.1f, 600.0f);
  LightCam.Speed = 10.0f;
  LightCam.Eye = XVECTOR3(25.0f, 100.0f, 0.0f);
  LightCam.Pitch = 1.12f;
  LightCam.Roll = 0.0f;
  LightCam.Yaw = -0.9f;
  LightCam.Update(0.0f);

  ActiveCam = &Cam;

  SceneProp.AddCamera(ActiveCam);
  SceneProp.AddLightCamera(&LightCam);

  SceneProp.AddLight(LightCam.Eye, XVECTOR3(1, 1, 1), 30000, true);
  SceneProp.AddLight(XVECTOR3(-55, 10, 0), XVECTOR3(1.0, 0.57, 0.16), 60, true);
  SceneProp.AddLight(XVECTOR3(55, 10, 0), XVECTOR3(1.0, 0.57, 0.16), 60, true);
  SceneProp.AddLight(XVECTOR3(60, 10, 30), XVECTOR3(1.0, 0.57, 0.16), 60, true);
  SceneProp.AddLight(XVECTOR3(60, 10, -30), XVECTOR3(1.0, 0.57, 0.16), 60, true);
  SceneProp.ActiveLights = 5;
  SceneProp.AmbientColor = XVECTOR3(0.8f, 0.8f, 0.8f);


  ShadowFilter.kernelSize = 4;
  ShadowFilter.radius = 1.f;
  ShadowFilter.sigma = 1.0f;
  ShadowFilter.Update();

  BloomFilter.kernelSize = 11;
  BloomFilter.radius = 2.5f;
  BloomFilter.sigma = 4.5f;
  BloomFilter.Update();


  NearDOFFilter.kernelSize = 8;
  NearDOFFilter.radius = 2.5f;
  NearDOFFilter.sigma = 4.5f;
  NearDOFFilter.Update();

  SceneProp.Aperture = 120;
  SceneProp.FocalLength = 50;
  SceneProp.MaxCoc = 2.5;
#ifdef  MAX_QUALITY
  SceneProp.DOF_Near_Samples_squared = 1.0f;
  SceneProp.DOF_Far_Samples_squared = 3.0f;
  SceneProp.ShadowMapResolution = 2048.0f;
  SceneProp.GoodRaysResolution = 0.0f;
  SceneProp.PCFScale = 1.5f;
  SceneProp.PCFSamples = 3.0f;
  SceneProp.ParallaxLowSamples = 20.0f;
  SceneProp.ParallaxHighSamples = 30.0f;
  SceneProp.ParallaxHeight = 0.02f;
  SceneProp.LightVolumeSteps = 256.0f;
  SceneProp.SSAOKernel.Radius = 1.5f;
  SceneProp.SSAOKernel.KernelSize = 32;
#elif defined(MEDIUM_QUALITY)
  SceneProp.DOF_Near_Samples_squared = 1.0f;
  SceneProp.DOF_Far_Samples_squared = 3.0f;
  SceneProp.ShadowMapResolution = 2048.0f;
  SceneProp.GoodRaysResolution = 0.0f;
  SceneProp.PCFScale = 2.1f;
  SceneProp.PCFSamples = 3.0f;
  SceneProp.ParallaxLowSamples = 10.0f;
  SceneProp.ParallaxHighSamples = 18.0f;
  SceneProp.ParallaxHeight = 0.02f;
  SceneProp.LightVolumeSteps = 248.0f;
  SceneProp.SSAOKernel.Radius = 1.5f;
  SceneProp.SSAOKernel.KernelSize = 32;
#elif defined(LOW_QUALITY)
  SceneProp.DOF_Near_Samples_squared = 1.0f;
  SceneProp.DOF_Far_Samples_squared = 2.0f;
  SceneProp.ShadowMapResolution = 1024.0f;
  SceneProp.GoodRaysResolution = 512.0f;
  SceneProp.PCFScale = 1.7f;
  SceneProp.PCFSamples = 1.0f;
  SceneProp.ParallaxLowSamples = 2.0f;
  SceneProp.ParallaxHighSamples = 8.0f;
  SceneProp.ParallaxHeight = 0.01f;
  SceneProp.LightVolumeSteps = 64.0f;
  SceneProp.SSAOKernel.Radius = 1.5f;
  SceneProp.SSAOKernel.KernelSize = 8;
#endif

  SceneProp.SSAOKernel.Update();


  SceneProp.ToogleShadow = true;
  SceneProp.ToogleSSAO = false;
  SceneProp.AutoFocus = true;



  SceneProp.AddGaussKernel(&ShadowFilter);
  SceneProp.AddGaussKernel(&BloomFilter);
  SceneProp.AddGaussKernel(&NearDOFFilter);
  SceneProp.ActiveGaussKernel = SHADOW_KERNEL;
  ChangeActiveGaussSelection = SHADOW_KERNEL;

  RTIndex = -1;

  m_spline.m_points.push_back(SplinePoint(80, 90, -80));
  m_spline.m_points.back().m_velocity = 3.0f;
  m_spline.m_points.push_back(SplinePoint(80, 90, -20));
  m_spline.m_points.back().m_velocity = 6.f;
  m_spline.m_points.push_back(SplinePoint(20, 110, 0));
  m_spline.m_points.back().m_velocity = 20.f;
  m_spline.m_points.push_back(SplinePoint(30, 20, 0));
  m_spline.m_points.back().m_velocity = 7;
  m_spline.m_points.push_back(SplinePoint(40, 6, 0));
  m_spline.m_points.back().m_velocity = 7;
  m_spline.m_points.push_back(SplinePoint(50, 4, 20));
  m_spline.m_points.back().m_velocity = 7;
  m_spline.m_points.push_back(SplinePoint(40, 4, 20));
  m_spline.m_points.back().m_velocity = 7;
  m_spline.m_points.push_back(SplinePoint(0, 4, 20));
  m_spline.m_points.back().m_velocity = 7;
  m_spline.m_points.push_back(SplinePoint(-50, 3, 20));
  m_spline.m_points.back().m_velocity = 7;
  m_spline.m_points.push_back(SplinePoint(-50, 6, 0));
  m_spline.m_points.back().m_velocity = 7;
  m_spline.m_points.push_back(SplinePoint(-20, 15, 0));
  m_spline.m_points.back().m_velocity = 7;
  m_spline.m_points.push_back(SplinePoint(0, 10, 0)); //
  m_spline.m_points.back().m_velocity = 7;
  m_spline.m_points.push_back(SplinePoint(-40, 25, 3));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(-55, 30, 5));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(-55, 30, -25));
  m_spline.m_points.back().m_velocity = 3;
  m_spline.m_points.push_back(SplinePoint(-65, 25, 0));
  m_spline.m_points.back().m_velocity = 3;
  m_spline.m_points.push_back(SplinePoint(-60, 25, 25));
  m_spline.m_points.back().m_velocity = 3;
  m_spline.m_points.push_back(SplinePoint(-35, 30, -20));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(15, 30, -20));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(25, 30, 20));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(50, 30, 20));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(50, 30, -20));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(-50, 30, -20));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(-50, 30, 0));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(-30, 30, 0));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(0, 5, 0));
  m_spline.m_points.back().m_velocity = 10;

  m_spline.m_looped = false;
  m_spline.Init();

  m_agent.SetOffset(0);
  m_agent.m_pSpline = &m_spline;
  m_agent.m_moving = true;
  m_agent.m_velocity = 15.0f;
}
void SC_Day::CreateAssets() {
  //Create RT's
  GBufferPass = pFramework->pVideoDriver->CreateRT(5, BaseRT::RGBA16F, BaseRT::F32, 0, 0, true);
  DeferredPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::RGBA16F, BaseRT::NOTHING, 0, 0, true);
  Extra16FPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::RGBA16F, BaseRT::NOTHING, 0, 0, true);
  DepthPass = pFramework->pVideoDriver->CreateRT(0, BaseRT::NOTHING, BaseRT::F32, (int)SceneProp.ShadowMapResolution, (int)SceneProp.ShadowMapResolution, false);
  ShadowAccumPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::R8, BaseRT::NOTHING, 0, 0, true);
  ExtraHelperPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::RGBA8, BaseRT::NOTHING, 0, 0, true);
  BloomAccumPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::RGBA8, BaseRT::NOTHING, 512, 512, true);
  GodRaysCalcPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::RGBA8, BaseRT::NOTHING, SceneProp.GoodRaysResolution, SceneProp.GoodRaysResolution, true);
  GodRaysCalcExtraPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::RGBA8, BaseRT::NOTHING, SceneProp.GoodRaysResolution, SceneProp.GoodRaysResolution, true);
  CoCPass = pFramework->pVideoDriver->CreateRT(2, BaseRT::F16, BaseRT::NOTHING, 512, 512, true);
  CombineCoCPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::F16, BaseRT::NOTHING, 512, 512, true);
  CoCHelperPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::F16, BaseRT::NOTHING, 512, 512, false);
  CoCHelperPass2 = pFramework->pVideoDriver->CreateRT(1, BaseRT::F16, BaseRT::NOTHING, 512, 512, false);

  //
  PrimitiveMgr.Init();
  PrimitiveMgr.SetVP(&VP);
  m_flare.Init(PrimitiveMgr);

  SceneProp.SSAOKernel.InitTexture();

  EnvMapTexIndex = g_pBaseDriver->CreateTexture(string("CubeMap_Mountains.dds"));

  int index = PrimitiveMgr.CreateMesh("Models/SkyBox.X");
  Meshes[1].CreateInstance(PrimitiveMgr.GetPrimitive(index), &VP);
  Meshes[1].TranslateAbsolute(0.0, -10.0f, 0.0f);
  Meshes[1].Update();

  index = PrimitiveMgr.CreateMesh("Models/SponzaEsc.X");
  Meshes[0].CreateInstance(PrimitiveMgr.GetPrimitive(index), &VP);

  index = PrimitiveMgr.CreateSpline(m_spline);
  splineWire = (SplineWireframe*)PrimitiveMgr.GetPrimitive(index);
  splineInst.CreateInstance(splineWire, &VP);
  m.Identity();
  Quads[0].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[0], 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[1], 1);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[2], 2);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[3], 3);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->pDepthTexture, 4);
  Quads[0].SetEnvironmentMap(g_pBaseDriver->GetTexture(EnvMapTexIndex));

  Quads[1].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[2].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[3].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[4].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[5].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[6].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[7].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);

  PrimitiveMgr.SetSceneProps(&SceneProp);

  m_agent.m_actualPoint = m_spline.GetPoint(m_spline.GetNormalizedOffset(0));
  ActiveCam->AttachAgent(m_agent);
  ActiveCam->m_lookAtCenter = false;

  Quads[0].TranslateAbsolute(0.0f, 0.0f, 0.0f);
  Quads[0].Update();

  Quads[1].ScaleAbsolute(0.25);
  Quads[1].TranslateAbsolute(-0.75f, +0.75f, 0.0f);
  Quads[1].Update();

  Quads[2].ScaleAbsolute(0.25f);
  Quads[2].TranslateAbsolute(0.75f, +0.75f, 0.0f);
  Quads[2].Update();

  Quads[3].ScaleAbsolute(0.25f);
  Quads[3].TranslateAbsolute(-0.75f, -0.75f, 0.0f);
  Quads[3].Update();

  Quads[4].ScaleAbsolute(0.25f);
  Quads[4].TranslateAbsolute(0.75f, -0.75f, 0.0f);
  Quads[4].Update();

  Quads[5].ScaleAbsolute(0.25f);
  Quads[5].TranslateAbsolute(0.75f, 0.0f, 0.0f);
  Quads[5].Update();

  Quads[6].ScaleAbsolute(0.25f);
  Quads[6].TranslateAbsolute(-0.75f, 0.0f, 0.0f);
  Quads[6].Update();

  Quads[7].ScaleAbsolute(1.0f);
  Quads[7].TranslateAbsolute(0.0f, 0.0f, 0.1f);
  Quads[7].Update();
}

void SC_Day::OnLoadScene() {
  InitVars();
  CreateAssets();
}

void SC_Day::OnDestoryScene() {
  DestroyAssets();
}

void SC_Day::DestroyAssets() {
  PrimitiveMgr.DestroyPrimitives();
  pFramework->pVideoDriver->DestroyRTs();
  //pFramework->pVideoDriver->DestroyShaders();
  //pFramework->pVideoDriver->DestroyTextures();
  //pFramework->pVideoDriver->DestroyTechniques();
}

void SC_Day::OnUpdate(float _DtSecs) {
  static float totalTime = 0.0f;
  static int frameCounter = 0;
  totalTime += _DtSecs;
  frameCounter++;
  DtSecs = _DtSecs;
  Meshes[0].SetParallaxSettings(SceneProp.ParallaxLowSamples, SceneProp.ParallaxHighSamples, SceneProp.ParallaxHeight);

  // Check if debugFrames spacebar dump was requested
  extern bool g_debugDumpRequested;
  extern std::string g_feedMatricesPath;
  extern bool g_dumpEnabled;

  // --feedMatrices: apply camera state and schedule dump after a few warm-up frames
  static int feedState = 0; // 0=pending, 1=applied (warming up), 2=dump requested
  static const int FEED_WARMUP_FRAMES = 3;
  static int feedWarmup = 0;
  if (!g_feedMatricesPath.empty() && feedState == 0) {
    feedState = 1;
    FeedCameraState camState = {}, lightState = {};
    FeedLightState feedLights[MAX_FEED_LIGHTS] = {};
    int feedNumLights = 0;
    if (loadFeedMatrices(g_feedMatricesPath, camState, lightState, feedLights, feedNumLights)) {
      // Detach spline agent so Update() uses our manual Eye/Pitch/Yaw
      Cam.m_externalControl = false;

      Cam.Eye = XVECTOR3(camState.eye[0], camState.eye[1], camState.eye[2]);
      Cam.Pitch = camState.pitch;
      Cam.Roll = camState.roll;
      Cam.Yaw = camState.yaw;
      Cam.Velocity = XVECTOR3(0, 0, 0);
      Cam.Update(0.0f);
      if (camState.hasMatrices) {
        Cam.View = camState.View;
        Cam.Projection = camState.Projection;
        Cam.VP = camState.VP;
      }

      LightCam.Eye = XVECTOR3(lightState.eye[0], lightState.eye[1], lightState.eye[2]);
      LightCam.Pitch = lightState.pitch;
      LightCam.Roll = lightState.roll;
      LightCam.Yaw = lightState.yaw;
      LightCam.Velocity = XVECTOR3(0, 0, 0);
      LightCam.Update(0.0f);
      if (lightState.hasMatrices) {
        LightCam.View = lightState.View;
        LightCam.Projection = lightState.Projection;
        LightCam.VP = lightState.VP;
      }

      VP = ActiveCam->VP;
      SceneProp.Lights[0].Position = LightCam.Eye;

      // Apply light positions from JSON
      for (int i = 0; i < feedNumLights && i < (int)SceneProp.Lights.size(); i++) {
        if (feedLights[i].valid) {
          SceneProp.Lights[i].Position = XVECTOR3(feedLights[i].position[0], feedLights[i].position[1], feedLights[i].position[2]);
          SceneProp.Lights[i].Color    = XVECTOR3(feedLights[i].color[0], feedLights[i].color[1], feedLights[i].color[2]);
          SceneProp.Lights[i].radius   = feedLights[i].radius;
        }
      }

      printf("[feedMatrices] Camera applied, warming up %d frames before dump...\n", FEED_WARMUP_FRAMES);
      fflush(stdout);
    } else {
      printf("[feedMatrices] Failed to load matrices, continuing normally.\n");
      fflush(stdout);
      feedState = 2; // skip warmup, continue normal
      g_feedMatricesPath.clear();
    }
  }
  // During warmup: keep camera frozen but let render pipeline stabilize
  if (!g_feedMatricesPath.empty() && feedState == 1) {
    feedWarmup++;
    if (feedWarmup >= FEED_WARMUP_FRAMES) {
      feedState = 2;
      g_dumpEnabled = true;
      g_debugDumpRequested = true;
      printf("[feedMatrices] Warm-up done, triggering dump on frame %d\n", frameCounter);
      fflush(stdout);
    }
  }

  // Skip all camera/light updates when feedMatrices is active (fully static scene)
  if (!g_debugDumpRequested && feedState == 0) {
    m_agent.Update(DtSecs);
    ActiveCam->Update(DtSecs);
    VP = ActiveCam->VP;
    SceneProp.Lights[0].Position = LightCam.Eye;
    SceneProp.pLightCameras[0]->Yaw -= 0.008f *DtSecs;
    SceneProp.pLightCameras[0]->Update(DtSecs);
  }


  if (totalTime > 150.0f) {
    totalTime = 0.0;
#ifdef T850_HEADLESS
    exit(0);
#else
    pFramework->pBaseApp->LoadScene(1);
#endif
  }
}

void SC_Day::OnInput(InputManager* IManager) {
  bool changed = false;
  const float speedFactor = 10.0f;
  if (IManager->PressedKey(T800K_UP)) {
    Position.y += 1.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_DOWN)) {
    Position.y -= 1.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_LEFT)) {
    Position.x -= 1.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_RIGHT)) {
    Position.x += 1.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_z)) {
    Position.z -= 1.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_x)) {
    Position.z += 1.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedOnceKey(T800K_KP_PLUS)) {
    ChangeSettingsOnPlus();
  }

  if (IManager->PressedOnceKey(T800K_KP_MINUS)) {

    ChangeSettingsOnMinus();
  }

  if (IManager->PressedOnceKey(T800K_b)) {
    SceneSettingSelection--;
    if (SceneSettingSelection < 0) {
      SceneSettingSelection = CHANGE_MAX_NUM_OPTIONS - 1;
    }

    printCurrSelection();
  }

  if (IManager->PressedOnceKey(T800K_n)) {
    SceneSettingSelection++;
    if (SceneSettingSelection == CHANGE_MAX_NUM_OPTIONS) {
      SceneSettingSelection = CHANGE_EXPOSURE;
    }

    printCurrSelection();
  }

  if (IManager->PressedKey(T800K_KP5)) {
    Orientation.x -= 60.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_KP6)) {
    Orientation.x += 60.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_KP2)) {
    Orientation.y -= 60.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_KP3)) {
    Orientation.y += 60.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_KP0)) {
    Orientation.z -= 60.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_KP_PERIOD)) {
    Orientation.z += 60.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_KP_PERIOD)) {
    Orientation.z += 60.0f*speedFactor*DtSecs;
    changed = true;
  }

  bool displayInfo = false;
  if (changed && displayInfo) {
    printf("Position[%f,%f,%f] Rot[%f,%f,%f] Sc[%f]\n", Position.x, Position.y, Position.z, Orientation.x, Orientation.y, Orientation.z, Scaling.x);
  }

  if (IManager->PressedOnceKey(T800K_k)) {
    printf("Position[%f, %f, %f]\n\n", ActiveCam->Eye.x, ActiveCam->Eye.y, ActiveCam->Eye.z);
    printf("Orientation[%f, %f, %f]\n\n", ActiveCam->Pitch, ActiveCam->Roll, ActiveCam->Yaw);
  }


  if (IManager->PressedOnceKey(T800K_c)) {
    if (ActiveCam == (&Cam)) {
      ActiveCam = &LightCam;
    }
    else {
      ActiveCam = &Cam;
    }
    SceneProp.pCameras[0] = ActiveCam;
  }

  if (IManager->PressedKey(T800K_w)) {
    ActiveCam->MoveForward(DtSecs);
  }

  if (IManager->PressedKey(T800K_s)) {
    ActiveCam->MoveBackward(DtSecs);
  }

  if (IManager->PressedKey(T800K_a)) {
    ActiveCam->StrafeLeft(DtSecs);
  }

  if (IManager->PressedKey(T800K_d)) {
    ActiveCam->StrafeRight(DtSecs);
  }

  if (IManager->PressedKey(T800K_q)) {
	  ActiveCam->MoveUp(DtSecs);
  }

  if (IManager->PressedKey(T800K_e)) {
	  ActiveCam->MoveDown(DtSecs);
  }

  if (IManager->PressedKey(T800K_KP3)) {
	  ActiveCam->MoveRoll(DtSecs);
  }

  if (IManager->PressedKey(T800K_KP1)) {
	  ActiveCam->MoveRoll(-DtSecs);
  }

  if (IManager->PressedOnceKey(T800K_SPACE)) {
    extern bool g_debugFrames;
    extern bool g_debugDumpRequested;
    extern bool g_dumpEnabled;
    if (g_debugFrames) {
      g_debugDumpRequested = true;
      g_dumpEnabled = true;
    }
  }

  if (IManager->PressedOnceKey(T800K_1)) {
    pFramework->ChangeAPI(GRAPHICS_API::D3D11);
  }

  if (IManager->PressedOnceKey(T800K_2)) {
    pFramework->ChangeAPI(GRAPHICS_API::OPENGL);
  }
  if (IManager->PressedOnceKey(T800K_3)) {
    pFramework->pVideoDriver->ModifyRT(DepthPass,0, BaseRT::NOTHING, BaseRT::F32, 128, 128, false);
  }
  // Skip mouse-driven camera movement when feedMatrices is active
  extern std::string g_feedMatricesPath;
  if (g_feedMatricesPath.empty()) {
    float yaw = 0.005f*static_cast<float>(IManager->xDelta);
    ActiveCam->MoveYaw(yaw);
    float pitch = 0.005f*static_cast<float>(IManager->yDelta);
    ActiveCam->MovePitch(pitch);
  }
}

void SC_Day::OnDraw() {
  pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::READ_WRITE);

  // Shadow Map Depth Pass
  pFramework->pVideoDriver->PushRT(DepthPass);
  SceneProp.pCameras[0] = &LightCam;
  pFramework->pVideoDriver->SetCullFace(BaseDriver::FACE_CULLING::BACK_FACES);
  for (int i = 0; i < 2; i++) {
    Meshes[i].SetGlobalSignature(Signature::SHADOW_MAP_PASS);
    Meshes[i].Draw();
    Meshes[i].SetGlobalSignature(Signature::FORWARD_PASS);
  }
  pFramework->pVideoDriver->PopRT();

  pFramework->pVideoDriver->SetCullFace(BaseDriver::FACE_CULLING::FRONT_FACES);

  // G Buffer Pass
  pFramework->pVideoDriver->PushRT(GBufferPass);
  SceneProp.pCameras[0] = &Cam;
  for (int i = 0; i < 2; i++) {
    Meshes[i].SetGlobalSignature(Signature::GBUFF_PASS);
    Meshes[i].Draw();
    Meshes[i].SetGlobalSignature(Signature::FORWARD_PASS);
  }
  pFramework->pVideoDriver->PopRT();

  pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::READ);
  
  // Shadow Map Buffer Accumulation + Occlusion 
  pFramework->pVideoDriver->PushRT(ShadowAccumPass);
  pFramework->pVideoDriver->Clear();
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::COLOR4_ATTACHMENT), 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(DepthPass, BaseDriver::DEPTH_ATTACHMENT), 1);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::COLOR1_ATTACHMENT), 2);
  Quads[0].SetTexture(SceneProp.SSAOKernel.NoiseTex, 3);
  Quads[0].SetGlobalSignature(Signature::SHADOW_COMP_PASS);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  
  // Shadow Map Blur Pass
  pFramework->pVideoDriver->PushRT(ExtraHelperPass);
  SceneProp.ActiveGaussKernel = SHADOW_KERNEL;
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(ShadowAccumPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetGlobalSignature(Signature::VERTICAL_BLUR_PASS);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  pFramework->pVideoDriver->PushRT(ShadowAccumPass);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(ExtraHelperPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetGlobalSignature(Signature::HORIZONTAL_BLUR_PASS);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();
  

  // Deferred Pass
  pFramework->pVideoDriver->PushRT(DeferredPass);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::COLOR1_ATTACHMENT), 1);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::COLOR2_ATTACHMENT), 2);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::COLOR3_ATTACHMENT), 3);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::COLOR4_ATTACHMENT), 4);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(ShadowAccumPass, BaseDriver::COLOR0_ATTACHMENT), 5);
  Quads[0].SetEnvironmentMap(g_pBaseDriver->GetTexture(EnvMapTexIndex));
  Quads[0].SetGlobalSignature(Signature::DEFERRED_PASS);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  
  // God Rays and Volumetric Pass
  pFramework->pVideoDriver->PushRT(GodRaysCalcPass);
  Quads[0].SetGlobalSignature(Signature::LIGHT_RAY_MARCHING);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::COLOR4_ATTACHMENT), 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(DepthPass, BaseDriver::DEPTH_ATTACHMENT), 1);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  //God Rays blur
  pFramework->pVideoDriver->PushRT(GodRaysCalcExtraPass);
  SceneProp.ActiveGaussKernel = DOF_KERNEL;
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GodRaysCalcPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetGlobalSignature(Signature::VERTICAL_BLUR_PASS);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  pFramework->pVideoDriver->PushRT(GodRaysCalcPass);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GodRaysCalcExtraPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetGlobalSignature(Signature::HORIZONTAL_BLUR_PASS);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  pFramework->pVideoDriver->PushRT(Extra16FPass);
  Quads[0].SetGlobalSignature(Signature::LIGHT_ADD);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GodRaysCalcPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(DeferredPass, BaseDriver::COLOR0_ATTACHMENT), 1);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();



  // Bright Pass
  pFramework->pVideoDriver->PushRT(BloomAccumPass);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(Extra16FPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetGlobalSignature(Signature::BRIGHT_PASS);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  SceneProp.ActiveGaussKernel = BLOOM_KERNEL;
  pFramework->pVideoDriver->PushRT(ExtraHelperPass);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(BloomAccumPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetGlobalSignature(Signature::HORIZONTAL_BLUR_PASS);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  pFramework->pVideoDriver->PushRT(BloomAccumPass);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(ExtraHelperPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetGlobalSignature(Signature::VERTICAL_BLUR_PASS);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();


  SceneProp.ActiveGaussKernel = DOF_KERNEL;
  //DOF PASS
  pFramework->pVideoDriver->PushRT(CoCPass);
  Quads[0].SetGlobalSignature(Signature::COC_PASS);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::COLOR4_ATTACHMENT), 0);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  //COMBINE COC
  pFramework->pVideoDriver->PushRT(CombineCoCPass);
  Quads[0].SetGlobalSignature(Signature::COMBINE_COC_PASS);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(CoCPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(CoCPass, BaseDriver::COLOR1_ATTACHMENT), 1);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();
  ////DOF_BLUR
  pFramework->pVideoDriver->PushRT(DeferredPass);
  Quads[0].SetGlobalSignature(Signature::DOF_PASS);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(Extra16FPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(CombineCoCPass, BaseDriver::COLOR0_ATTACHMENT), 1);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  pFramework->pVideoDriver->PushRT(Extra16FPass);
  Quads[0].SetGlobalSignature(Signature::DOF_PASS_2);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(DeferredPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(CombineCoCPass, BaseDriver::COLOR0_ATTACHMENT), 1);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  // HDR Composition Pass
  pFramework->pVideoDriver->PushRT(ExtraHelperPass);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(Extra16FPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(BloomAccumPass, BaseDriver::COLOR0_ATTACHMENT), 1);
  Quads[0].SetGlobalSignature(Signature::HDR_COMP_PASS);
  Quads[0].Draw();
  
 
#ifdef T850_HEADLESS
  //Save file
  const float timeToScreenshot = 5.0;
  static float screenshotTime = 0.0;
  static int screenshotNum = 0;
  screenshotTime += DtSecs;
  if (screenshotTime >= timeToScreenshot) {
    screenshotTime = 0;
    pFramework->pVideoDriver->SaveScreenshot("Test_" + std::to_string(screenshotNum));
    screenshotNum++;
  }
#else
  pFramework->pVideoDriver->PopRT();

  // Final Draw
  Quads[7].SetTexture(pFramework->pVideoDriver->GetRTTexture(ExtraHelperPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[7].SetGlobalSignature(Signature::VIGNETTE_PASS);
  Quads[7].Draw();

  // RT Dump: save key render targets (configurable via command-line)
  {
    static float dumpTimer = 0.0f;
    static int   dumpFrameCounter = 0;
    static bool  dumped = false;
    dumpTimer += DtSecs;
    dumpFrameCounter++;

    extern bool  g_dumpEnabled;
    extern bool  g_dumpByFrame;
    extern int   g_dumpFrame;
    extern float g_dumpSeconds;

    extern bool  g_debugDumpRequested;

    bool shouldDump = false;
    if (g_dumpEnabled && !dumped) {
      if (g_debugDumpRequested)
        shouldDump = true;
      else if (g_dumpByFrame && dumpFrameCounter >= g_dumpFrame)
        shouldDump = true;
      else if (!g_dumpByFrame && dumpTimer >= g_dumpSeconds)
        shouldDump = true;
    }

    if (shouldDump) {
      dumped = true;

      // Build timestamped output directory
      auto now = std::chrono::system_clock::now();
      auto tt = std::chrono::system_clock::to_time_t(now);
      struct tm lt;
#ifdef OS_WINDOWS
      localtime_s(&lt, &tt);
#else
      localtime_r(&tt, &lt);
#endif
      char tsBuf[64];
      std::snprintf(tsBuf, sizeof(tsBuf), "%04d%02d%02d_%02d%02d%02d",
        lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
        lt.tm_hour, lt.tm_min, lt.tm_sec);

      std::string apiName = (pFramework->pVideoDriver->m_currentAPI == GRAPHICS_API::D3D11) ? "d3d11" : "gl";
      std::string dumpDir = "dumps_" + apiName + "_f" + std::to_string(dumpFrameCounter) + "_" + tsBuf;
#ifdef OS_WINDOWS
      CreateDirectoryA(dumpDir.c_str(), NULL);
#else
      mkdir(dumpDir.c_str(), 0755);
#endif
      std::string prefix = dumpDir + "/RT_Dump_";
      pFramework->pVideoDriver->SaveScreenshot(prefix + "BackBuffer");
      pFramework->pVideoDriver->SaveRTToFile(GBufferPass,      BaseDriver::COLOR0_ATTACHMENT, prefix + "GBuffer_Color0");
      pFramework->pVideoDriver->SaveRTToFile(GBufferPass,      BaseDriver::COLOR1_ATTACHMENT, prefix + "GBuffer_Normals");
      pFramework->pVideoDriver->SaveRTToFile(GBufferPass,      BaseDriver::COLOR4_ATTACHMENT, prefix + "GBuffer_Depth");
      pFramework->pVideoDriver->SaveRTToFile(DepthPass,        BaseDriver::DEPTH_ATTACHMENT,  prefix + "ShadowMap_Depth");
      pFramework->pVideoDriver->SaveRTToFile(ShadowAccumPass,  BaseDriver::COLOR0_ATTACHMENT, prefix + "ShadowAccum");
      pFramework->pVideoDriver->SaveRTToFile(DeferredPass,     BaseDriver::COLOR0_ATTACHMENT, prefix + "Deferred");
      pFramework->pVideoDriver->SaveRTToFile(Extra16FPass,     BaseDriver::COLOR0_ATTACHMENT, prefix + "Extra16F");
      pFramework->pVideoDriver->SaveRTToFile(ExtraHelperPass,  BaseDriver::COLOR0_ATTACHMENT, prefix + "HDR_Final");
      pFramework->pVideoDriver->SaveRTToFile(BloomAccumPass,   BaseDriver::COLOR0_ATTACHMENT, prefix + "Bloom");
      pFramework->pVideoDriver->SaveRTToFile(GodRaysCalcPass,  BaseDriver::COLOR0_ATTACHMENT, prefix + "GodRays");

      // Log all camera/matrix state for cross-API verification
      auto dumpMatrix = [](std::ostream& os, const char* name, const XMATRIX44& mat) {
        os << name << ":\n";
        for (int r = 0; r < 4; r++)
          os << "  [" << r << "]: " << mat.m[r][0] << ", " << mat.m[r][1] << ", " << mat.m[r][2] << ", " << mat.m[r][3] << "\n";
      };

      // Compute effective pitch/yaw from the Look vector (these are
      // the values actually used by the View matrix, which may differ
      // from Cam.Pitch/Cam.Yaw when the spline agent controls the camera).
      float camEffPitch = asinf(-Cam.Look.y);
      float camEffYaw   = atan2f(Cam.Look.x, Cam.Look.z);
      float lcEffPitch  = asinf(-LightCam.Look.y);
      float lcEffYaw    = atan2f(LightCam.Look.x, LightCam.Look.z);

      std::cout << "=== DUMP STATE (frame " << dumpFrameCounter << ", " << apiName << ", dt=" << DtSecs << "s) ===" << std::endl;
      std::cout << "Cam Eye: " << Cam.Eye.x << ", " << Cam.Eye.y << ", " << Cam.Eye.z << std::endl;
      std::cout << "Cam Pitch: " << camEffPitch << " Roll: " << Cam.Roll << " Yaw: " << camEffYaw << std::endl;
      std::cout << "LightCam Eye: " << LightCam.Eye.x << ", " << LightCam.Eye.y << ", " << LightCam.Eye.z << std::endl;
      std::cout << "LightCam Pitch: " << lcEffPitch << " Roll: " << LightCam.Roll << " Yaw: " << lcEffYaw << std::endl;
      dumpMatrix(std::cout, "Cam.View", Cam.View);
      dumpMatrix(std::cout, "Cam.Projection", Cam.Projection);
      dumpMatrix(std::cout, "Cam.VP", Cam.VP);
      dumpMatrix(std::cout, "LightCam.View", LightCam.View);
      dumpMatrix(std::cout, "LightCam.Projection", LightCam.Projection);
      dumpMatrix(std::cout, "LightCam.VP", LightCam.VP);
      dumpMatrix(std::cout, "VP (active)", VP);

      // Dump scene lights
      std::cout << "Lights (" << SceneProp.Lights.size() << "):" << std::endl;
      for (size_t i = 0; i < SceneProp.Lights.size(); i++) {
        const Light& lt = SceneProp.Lights[i];
        std::cout << "  [" << i << "] Pos=(" << lt.Position.x << ", " << lt.Position.y << ", " << lt.Position.z
                  << ") Col=(" << lt.Color.x << ", " << lt.Color.y << ", " << lt.Color.z
                  << ") R=" << lt.radius << std::endl;
      }

      // Write matrices as JSON for easy reuse with --feedMatrices
      auto jsonMat = [](std::ostream& os, const char* name, const XMATRIX44& mat, bool last = false) {
        os << "    \"" << name << "\": [\n";
        for (int r = 0; r < 4; r++) {
          os << "      [" << mat.m[r][0] << ", " << mat.m[r][1] << ", " << mat.m[r][2] << ", " << mat.m[r][3] << "]";
          os << (r < 3 ? ",\n" : "\n");
        }
        os << "    ]" << (last ? "\n" : ",\n");
      };

      std::ofstream matFile(dumpDir + "/matrices.json");
      if (matFile.is_open()) {
        matFile << std::setprecision(10);
        matFile << "{\n";
        matFile << "  \"frame\": " << dumpFrameCounter << ",\n";
        matFile << "  \"api\": \"" << apiName << "\",\n";
        matFile << "  \"dt\": " << DtSecs << ",\n";
        matFile << "  \"cam\": {\n";
        matFile << "    \"eye\": [" << Cam.Eye.x << ", " << Cam.Eye.y << ", " << Cam.Eye.z << "],\n";
        matFile << "    \"pitch\": " << camEffPitch << ",\n";
        matFile << "    \"roll\": " << Cam.Roll << ",\n";
        matFile << "    \"yaw\": " << camEffYaw << "\n";
        matFile << "  },\n";
        matFile << "  \"lightCam\": {\n";
        matFile << "    \"eye\": [" << LightCam.Eye.x << ", " << LightCam.Eye.y << ", " << LightCam.Eye.z << "],\n";
        matFile << "    \"pitch\": " << lcEffPitch << ",\n";
        matFile << "    \"roll\": " << LightCam.Roll << ",\n";
        matFile << "    \"yaw\": " << lcEffYaw << "\n";
        matFile << "  },\n";

        // Dump all scene lights
        matFile << "  \"lights\": [\n";
        for (size_t i = 0; i < SceneProp.Lights.size(); i++) {
          const Light& lt = SceneProp.Lights[i];
          matFile << "    { \"position\": [" << lt.Position.x << ", " << lt.Position.y << ", " << lt.Position.z << "]";
          matFile << ", \"color\": [" << lt.Color.x << ", " << lt.Color.y << ", " << lt.Color.z << "]";
          matFile << ", \"radius\": " << lt.radius << " }";
          matFile << (i + 1 < SceneProp.Lights.size() ? ",\n" : "\n");
        }
        matFile << "  ],\n";

        matFile << "  \"matrices\": {\n";
        jsonMat(matFile, "camView", Cam.View);
        jsonMat(matFile, "camProjection", Cam.Projection);
        jsonMat(matFile, "camVP", Cam.VP);
        jsonMat(matFile, "lightCamView", LightCam.View);
        jsonMat(matFile, "lightCamProjection", LightCam.Projection);
        jsonMat(matFile, "lightCamVP", LightCam.VP, true);
        matFile << "  }\n";
        matFile << "}\n";
      }

      std::cout << "RT dump complete -> " << dumpDir << "/ (11 files)" << std::endl;

      // Exit the application after dumping (unless --keepRunning)
      extern bool g_keepRunning;
      if (!g_keepRunning) {
        exit(0);
      } else {
        g_dumpEnabled = false;
        g_debugDumpRequested = false;
        dumped = false;
        printf("[keepRunning] Dump done, continuing...\n");
        fflush(stdout);
      }
    }
  }
  /*
  Quads[1].SetTexture(pFramework->pVideoDriver->GetRTTexture(DepthPass, BaseDriver::DEPTH_ATTACHMENT), 0);
  Quads[1].SetGlobalSignature(Signature::FSQUAD_3_TEX);
  Quads[1].Draw();
 
  Quads[2].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::COLOR1_ATTACHMENT), 0);
  Quads[2].SetGlobalSignature(Signature::FSQUAD_3_TEX);
  Quads[2].Draw();

  Quads[3].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::COLOR2_ATTACHMENT), 0);
  Quads[3].SetGlobalSignature(Signature::FSQUAD_3_TEX);
  Quads[3].Draw();

  Quads[4].SetTexture(pFramework->pVideoDriver->GetRTTexture(ShadowAccumPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[4].SetGlobalSignature(Signature::FSQUAD_3_TEX);
  Quads[4].Draw();
  */
  if (SceneProp.pCameras[0]->Eye.y > 80) {
    m_flare.Draw();
  }
#endif
}

void  SC_Day::ChangeSettingsOnPlus() {
  switch (SceneSettingSelection) {
  case CHANGE_EXPOSURE: {
    float prevVal = SceneProp.Exposure;
    SceneProp.Exposure += 0.1f;
    cout << "[CHANGE_EXPOSURE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.Exposure << "]" << endl;
  }break;
  case CHANGE_BLOOM_FACTOR: {
    float prevVal = SceneProp.BloomFactor;
    SceneProp.BloomFactor += 0.1f;
    cout << "[CHANGE_BLOOM_FACTOR] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.BloomFactor << "]" << endl;
  }break;
  case CHANGE_NUM_LIGHTS: {
    int prevVal = SceneProp.ActiveLights;
    SceneProp.ActiveLights *= 2;
    if (SceneProp.ActiveLights >= 127) {
      SceneProp.ActiveLights = 127;
    }
    cout << "[CHANGE_NUM_LIGHTS] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ActiveLights << "]" << endl;
  }break;
  case CHANGE_ACTIVE_GAUSS_KERNEL: {
    int prevVal = ChangeActiveGaussSelection;
    ChangeActiveGaussSelection++;
    if (ChangeActiveGaussSelection >= (int)SceneProp.pGaussKernels.size()) {
      ChangeActiveGaussSelection = SceneProp.pGaussKernels.size() - 1;
    }
    cout << "[CHANGE_ACTIVE_GAUSS_KERNEL] Previous Value[" << prevVal << "] Actual Value[" << ChangeActiveGaussSelection << "]" << endl;
  }break;
  case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
    int prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize += 2;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    cout << "[CHANGE_GAUSS_KERNEL_SAMPLE_COUNT] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize << "]" << endl;
  }break;
  case CHANGE_GAUSS_KERNEL_RADIUS: {
    float prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius += 0.5;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    cout << "[CHANGE_GAUSS_KERNEL_RADIUS] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius << "]" << endl;
  }break;
  case CHANGE_GAUSS_KERNEL_DEVIATION: {
    float prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma += 0.5;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    cout << "[CHANGE_GAUSS_KERNEL_DEVIATION] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma << "]" << endl;
  }break;
  case CHANGE_PCF_RADIUS: {
    float prevVal = SceneProp.PCFScale;
    SceneProp.PCFScale += 0.1f;
    cout << "[CHANGE_PCF_RADIUS] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.PCFScale << "]" << endl;
  }break;
  case CHANGE_PCF_SAMPLES: {
    float prevVal = SceneProp.PCFSamples;
    SceneProp.PCFSamples++;
    cout << "[CHANGE_PCF_SAMPLES] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.PCFSamples << "]" << endl;
  }break;
  case CHANGE_SSAO_KERNEL_SIZE: {
    float prevVal = (float)SceneProp.SSAOKernel.KernelSize;
    SceneProp.SSAOKernel.KernelSize += 2;
    SceneProp.SSAOKernel.Update();
    cout << "[CHANGE_SSAO_KERNEL_SIZE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.SSAOKernel.KernelSize << "]" << endl;
  }break;
  case CHANGE_SSAO_RADIUS: {
    float prevVal = SceneProp.SSAOKernel.Radius;
    SceneProp.SSAOKernel.Radius += 0.5f;
    cout << "[CHANGE_SSAO_RADIUS] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.SSAOKernel.Radius << "]" << endl;
  }break;
  case CHANGE_DOF_APERTURE: {
    float prevVal = SceneProp.Aperture;
    SceneProp.Aperture += 10.0f;
    cout << "[CHANGE_DOF_APERTURE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.Aperture << "]" << endl;
  }break;
  case CHANGE_DOF_FOCAL_LENGHT: {
    float prevVal = SceneProp.FocalLength;
    SceneProp.FocalLength += 10.0f;
    cout << "[CHANGE_DOF_FOCAL_LENGHT] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.FocalLength << "]" << endl;
  }break;
  case CHANGE_DOF_MAX_COC: {
    float prevVal = SceneProp.MaxCoc;
    SceneProp.MaxCoc += 0.5f;
    cout << "[CHANGE_DOF_MAX_COC] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.MaxCoc << "]" << endl;
  }break;
  case CHANGE_DOF_FAR_SAMPLE: {
    float prevVal = SceneProp.DOF_Far_Samples_squared;
    SceneProp.DOF_Far_Samples_squared += 1.0f;
    cout << "[CHANGE_DOF_FAR_SAMPLE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.DOF_Far_Samples_squared << "]" << endl;
  }break;
  case CHANGE_DOF_NEAR_SAMPLE: {
    float prevVal = SceneProp.DOF_Near_Samples_squared;
    SceneProp.DOF_Near_Samples_squared += 1.0f;
    cout << "[CHANGE_DOF_NEAR_SAMPLE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.DOF_Near_Samples_squared << "]" << endl;
  }break;
  case CHANGE_DOF_AUTO_FOCUS: {
    bool prevVal = SceneProp.AutoFocus;
    SceneProp.AutoFocus = true;
    cout << "[CHANGE_DOF_AUTO_FOCUS] Previous Value[" << prevVal << "] Actual Value[" << (int)SceneProp.AutoFocus << "]" << endl;
  }break;
  case CHANGE_PARALLAX_LOW_SAMPLES: {
    float prevVal = SceneProp.ParallaxLowSamples;
    SceneProp.ParallaxLowSamples += 5.0f;
    cout << "[CHANGE_PARALLAX_LOW_SAMPLES] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ParallaxLowSamples << "]" << endl;
  }break;
  case CHANGE_PARALLAX_HIGH_SAMPLES: {
    float prevVal = SceneProp.ParallaxHighSamples;
    SceneProp.ParallaxHighSamples += 10.0f;
    cout << "[CHANGE_PARALLAX_HIGH_SAMPLES] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ParallaxHighSamples << "]" << endl;
  }break;
  case CHANGE_PARALLAX_HEIGHT: {
    float prevVal = SceneProp.ParallaxHeight;
    SceneProp.ParallaxHeight += 0.01f;
    cout << "[CHANGE_PARALLAX_HEIGHT] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ParallaxHeight << "]" << endl;
  }break;
  case CHANGE_LIGHT_VOLUME_STEPS: {
    float prevVal = SceneProp.LightVolumeSteps;
    SceneProp.LightVolumeSteps += 16.0f;
    cout << "[CHANGE_LIGHT_VOLUME_STEPS] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.LightVolumeSteps << "]" << endl;
  }break;
  case CHANGE_PCF_TOOGLE: {
	  int prevVal = SceneProp.ToogleShadow;
	  SceneProp.ToogleShadow = 1;
	  cout << "[CHANGE_PCF_TOOGLE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ToogleShadow << "]" << endl;
  }break;
  case CHANGLE_SSAO_TOOGLE: {
	  int prevVal = SceneProp.ToogleSSAO;
	  SceneProp.ToogleSSAO = 1;
	  cout << "[CHANGLE_SSAO_TOOGLE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ToogleSSAO << "]" << endl;
  }break;
  case CHANGE_LIGHT_NEAR_PLANE: {
    float prevVal = LightCam.NPlane;
    LightCam.NPlane += 1.0f;
    LightCam.CreatePojection();
    LightCam.Update(0);
    cout << "[CHANGE_LIGHT_NEAR_PLANE] Previous Value[" << prevVal << "] Actual Value[" << LightCam.NPlane << "]" << endl;
  }break;
  case CHANGE_LIGHT_FAR_PLANE: {
    float prevVal = LightCam.FPlane;
    LightCam.FPlane += 10.0f;
    LightCam.CreatePojection();
    LightCam.Update(0);
    cout << "[CHANGE_LIGHT_FAR_PLANE] Previous Value[" << prevVal << "] Actual Value[" << LightCam.FPlane << "]" << endl;
  }break;
  }
}

void  SC_Day::ChangeSettingsOnMinus() {
  switch (SceneSettingSelection) {
  case CHANGE_EXPOSURE: {
    float prevVal = SceneProp.Exposure;
    SceneProp.Exposure -= 0.1f;
    cout << "[CHANGE_EXPOSURE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.Exposure << "]" << endl;
  }break;
  case CHANGE_BLOOM_FACTOR: {
    float prevVal = SceneProp.BloomFactor;
    SceneProp.BloomFactor -= 0.1f;
    cout << "[CHANGE_BLOOM_FACTOR] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.BloomFactor << "]" << endl;
  }break;
  case CHANGE_NUM_LIGHTS: {
    int prevVal = SceneProp.ActiveLights;
    SceneProp.ActiveLights /= 2;
    if (SceneProp.ActiveLights <= 0) {
      SceneProp.ActiveLights = 1;
    }
    cout << "[CHANGE_NUM_LIGHTS] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ActiveLights << "]" << endl;
  }break;
  case CHANGE_ACTIVE_GAUSS_KERNEL: {
    int prevVal = ChangeActiveGaussSelection;
    ChangeActiveGaussSelection--;
    if (ChangeActiveGaussSelection < 0) {
      ChangeActiveGaussSelection = 0;
    }
    cout << "[CHANGE_ACTIVE_GAUSS_KERNEL] Previous Value[" << prevVal << "] Actual Value[" << ChangeActiveGaussSelection << "]" << endl;
  }break;
  case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
    int prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize -= 2;
    if (SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize <= 2) {
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize = 3;
    }
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    cout << "[CHANGE_GAUSS_KERNEL_SAMPLE_COUNT] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize << "]" << endl;
  }break;
  case CHANGE_GAUSS_KERNEL_RADIUS: {
    float prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius -= 0.5f;
    if (SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius <= 0.5f) {
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius = 0.5f;
    }
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    cout << "[CHANGE_GAUSS_KERNEL_RADIUS] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius << "]" << endl;
  }break;
  case CHANGE_GAUSS_KERNEL_DEVIATION: {
    float prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma -= 0.5;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    cout << "[CHANGE_GAUSS_KERNEL_DEVIATION] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma << "]" << endl;
  }break;
  case CHANGE_PCF_RADIUS: {
    float prevVal = SceneProp.PCFScale;
    SceneProp.PCFScale -= 0.1f;
    cout << "[CHANGE_PCF_RADIUS] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.PCFScale << "]" << endl;
  }break;
  case CHANGE_PCF_SAMPLES: {
    float prevVal = SceneProp.PCFSamples;
    SceneProp.PCFSamples--;
    cout << "[CHANGE_PCF_SAMPLES] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.PCFSamples << "]" << endl;
  }break;
  case CHANGE_SSAO_KERNEL_SIZE: {
    float prevVal = (float)SceneProp.SSAOKernel.KernelSize;
    SceneProp.SSAOKernel.KernelSize -= 2;
    SceneProp.SSAOKernel.Update();
    cout << "[CHANGE_SSAO_KERNEL_SIZE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.SSAOKernel.KernelSize << "]" << endl;
  }break;
  case CHANGE_SSAO_RADIUS: {
    float prevVal = SceneProp.SSAOKernel.Radius;
    SceneProp.SSAOKernel.Radius -= 0.5f;
    cout << "[CHANGE_SSAO_RADIUS] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.SSAOKernel.Radius << "]" << endl;
  }break;
  case CHANGE_DOF_APERTURE: {
    float prevVal = SceneProp.Aperture;
    SceneProp.Aperture -= 10.0f;
    cout << "[CHANGE_DOF_APERTURE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.Aperture << "]" << endl;
  }break;
  case CHANGE_DOF_FOCAL_LENGHT: {
    float prevVal = SceneProp.FocalLength;
    SceneProp.FocalLength -= 10.0f;
    cout << "[CHANGE_DOF_FOCAL_LENGHT] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.FocalLength << "]" << endl;
  }break;
  case CHANGE_DOF_MAX_COC: {
    float prevVal = SceneProp.MaxCoc;
    SceneProp.MaxCoc -= 0.5f;
    cout << "[CHANGE_DOF_MAX_COC] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.MaxCoc << "]" << endl;
  }break;
  case CHANGE_DOF_FAR_SAMPLE: {
    float prevVal = SceneProp.DOF_Far_Samples_squared;
    SceneProp.DOF_Far_Samples_squared -= 1.0f;
    cout << "[CHANGE_DOF_FAR_SAMPLE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.DOF_Far_Samples_squared << "]" << endl;
  }break;
  case CHANGE_DOF_NEAR_SAMPLE: {
    float prevVal = SceneProp.DOF_Near_Samples_squared;
    SceneProp.DOF_Near_Samples_squared -= 1.0f;
    cout << "[CHANGE_DOF_NEAR_SAMPLE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.DOF_Near_Samples_squared << "]" << endl;
  }break;
  case CHANGE_DOF_AUTO_FOCUS: {
    bool prevVal = SceneProp.AutoFocus;
    SceneProp.AutoFocus = false;
    cout << "[CHANGE_DOF_AUTO_FOCUS] Previous Value[" << prevVal << "] Actual Value[" << (int)SceneProp.AutoFocus << "]" << endl;
  }break;
  case CHANGE_PARALLAX_LOW_SAMPLES: {
    float prevVal = SceneProp.ParallaxLowSamples;
    SceneProp.ParallaxLowSamples -= 5.0f;
    cout << "[CHANGE_PARALLAX_LOW_SAMPLES] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ParallaxLowSamples << "]" << endl;
  }break;
  case CHANGE_PARALLAX_HIGH_SAMPLES: {
    float prevVal = SceneProp.ParallaxHighSamples;
    SceneProp.ParallaxHighSamples -= 10.0f;
    cout << "[CHANGE_PARALLAX_HIGH_SAMPLES] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ParallaxHighSamples << "]" << endl;
  }break;
  case CHANGE_PARALLAX_HEIGHT: {
    float prevVal = SceneProp.ParallaxHeight;
    SceneProp.ParallaxHeight -= 0.01f;
    cout << "[CHANGE_PARALLAX_HEIGHT] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ParallaxHeight << "]" << endl;
  }break;
  case CHANGE_LIGHT_VOLUME_STEPS: {
    float prevVal = SceneProp.LightVolumeSteps;
    SceneProp.LightVolumeSteps -= 16.0f;
    cout << "[CHANGE_LIGHT_VOLUME_STEPS] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.LightVolumeSteps << "]" << endl;
  }break;
  case CHANGE_PCF_TOOGLE: {
	  int prevVal = SceneProp.ToogleShadow;
	  SceneProp.ToogleShadow = 0;
	  cout << "[CHANGE_PCF_TOOGLE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ToogleShadow << "]" << endl;
  }break;
  case CHANGLE_SSAO_TOOGLE: {
	  int prevVal = SceneProp.ToogleSSAO;
	  SceneProp.ToogleSSAO = 0;
	  cout << "[CHANGLE_SSAO_TOOGLE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ToogleSSAO << "]" << endl;
  }break;
  case CHANGE_LIGHT_NEAR_PLANE: {
    float prevVal = LightCam.NPlane;
    LightCam.NPlane -= 1.0f;
    if (LightCam.NPlane < 0.1f) LightCam.NPlane = 0.1f;
    LightCam.CreatePojection();
    LightCam.Update(0);
    cout << "[CHANGE_LIGHT_NEAR_PLANE] Previous Value[" << prevVal << "] Actual Value[" << LightCam.NPlane << "]" << endl;
  }break;
  case CHANGE_LIGHT_FAR_PLANE: {
    float prevVal = LightCam.FPlane;
    LightCam.FPlane -= 10.0f;
    if (LightCam.FPlane < 1.0f) LightCam.FPlane = 1.0f;
    LightCam.CreatePojection();
    LightCam.Update(0);
    cout << "[CHANGE_LIGHT_FAR_PLANE] Previous Value[" << prevVal << "] Actual Value[" << LightCam.FPlane << "]" << endl;
  }break;
  }
}

void SC_Day::printCurrSelection() {
  switch (SceneSettingSelection) {
  case CHANGE_EXPOSURE: {
    cout << "Option[CHANGE_EXPOSURE] Value[" << SceneProp.Exposure << "]" << endl;
  }break;
  case CHANGE_BLOOM_FACTOR: {
    cout << "Option[CHANGE_BLOOM_FACTOR] Value[" << SceneProp.BloomFactor << "]" << endl;
  }break;
  case CHANGE_NUM_LIGHTS: {
    cout << "Option[CHANGE_NUM_LIGHTS] Value[" << SceneProp.ActiveLights << "]" << endl;
  }break;
  case CHANGE_ACTIVE_GAUSS_KERNEL: {
    cout << "Option[CHANGE_ACTIVE_GAUSS_KERNEL] Value[" << ChangeActiveGaussSelection << "]" << endl;
  }break;
  case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
    cout << "Option[CHANGE_GAUSS_KERNEL_SAMPLE_COUNT] Value[" << SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize << "]" << endl;
  }break;
  case CHANGE_GAUSS_KERNEL_RADIUS: {
    cout << "Option[CHANGE_GAUSS_KERNEL_RADIUS] Value[" << SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius << "]" << endl;
  }break;
  case CHANGE_GAUSS_KERNEL_DEVIATION: {
    cout << "Option[CHANGE_GAUSS_KERNEL_DEVIATION] Value[" << SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma << "]" << endl;
  }break;
  case CHANGE_PCF_RADIUS: {
    cout << "Option[CHANGE_PCF_RADIUS] Value[" << SceneProp.PCFScale << "]" << endl;
  }break;
  case CHANGE_PCF_SAMPLES: {
    cout << "Option[CHANGE_PCF_SAMPLES] Value[" << SceneProp.PCFSamples << "]" << endl;
  }break;
  case CHANGE_SSAO_KERNEL_SIZE: {
    cout << "Option[CHANGE_SSAO_KERNEL_SIZE] Value[" << SceneProp.SSAOKernel.KernelSize << "]" << endl;
  }break;
  case CHANGE_SSAO_RADIUS: {
    cout << "Option[CHANGE_SSAO_RADIUS] Value[" << SceneProp.SSAOKernel.Radius << "]" << endl;
  }break;
  case CHANGE_DOF_APERTURE: {
    cout << "Option[CHANGE_DOF_APERTURE] Value[" << SceneProp.Aperture << "]" << endl;
  }break;
  case CHANGE_DOF_FOCAL_LENGHT: {
    cout << "Option[CHANGE_DOF_FOCAL_LENGHT] Value[" << SceneProp.FocalLength << "]" << endl;
  }break;
  case CHANGE_DOF_MAX_COC: {
    cout << "Option[CHANGE_DOF_MAX_COC] Value[" << SceneProp.MaxCoc << "]" << endl;
  }break;
  case CHANGE_DOF_FAR_SAMPLE: {
    cout << "Option[CHANGE_DOF_FAR_SAMPLE] Value[" << SceneProp.DOF_Far_Samples_squared << "]" << endl;
  }break;
  case CHANGE_DOF_NEAR_SAMPLE: {
    cout << "Option[CHANGE_DOF_NEAR_SAMPLE] Value[" << SceneProp.DOF_Near_Samples_squared << "]" << endl;
  }break;
  case CHANGE_DOF_AUTO_FOCUS: {
    cout << "Option[CHANGE_DOF_AUTO_FOCUS] Value[" << (int)SceneProp.AutoFocus << "]" << endl;
  }break;
  case CHANGE_PARALLAX_LOW_SAMPLES: {
    cout << "Option[CHANGE_PARALLAX_LOW_SAMPLES] Value[" << SceneProp.ParallaxLowSamples << "]" << endl;
  }break;
  case CHANGE_PARALLAX_HIGH_SAMPLES: {
    cout << "Option[CHANGE_PARALLAX_HIGH_SAMPLES] Value[" << SceneProp.ParallaxHighSamples << "]" << endl;
  }break;
  case CHANGE_PARALLAX_HEIGHT: {
    cout << "Option[CHANGE_PARALLAX_HEIGHT] Value[" << SceneProp.ParallaxHeight << "]" << endl;
  }break;
  case CHANGE_LIGHT_VOLUME_STEPS: {
    cout << "Option[CHANGE_LIGHT_VOLUME_STEPS] Value[" << SceneProp.LightVolumeSteps << "]" << endl;
  }break;
  case CHANGE_PCF_TOOGLE: {
	  cout << "Option[CHANGE_PCF_TOOGLE] Value[" << SceneProp.ToogleShadow << "]" << endl;
  }break;
  case CHANGLE_SSAO_TOOGLE: {
	  cout << "Option[CHANGLE_SSAO_TOOGLE] Value[" << SceneProp.ToogleSSAO << "]" << endl;
  }break;
  case CHANGE_LIGHT_NEAR_PLANE: {
    cout << "Option[CHANGE_LIGHT_NEAR_PLANE] Value[" << LightCam.NPlane << "]" << endl;
  }break;
  case CHANGE_LIGHT_FAR_PLANE: {
    cout << "Option[CHANGE_LIGHT_FAR_PLANE] Value[" << LightCam.FPlane << "]" << endl;
  }break;
  }
}
