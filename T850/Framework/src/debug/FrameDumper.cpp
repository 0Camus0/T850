#include <debug/FrameDumper.h>
#include <utils/Camera.h>
#include <scene/SceneProp.h>
#include <video/BaseDriver.h>
#include <T8_descriptors.h>

#include <iostream>
#include <cstdio>
#include <cmath>
#include <chrono>
#include <ctime>
#ifdef OS_WINDOWS
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace t800 {

// ── Mat4Json <-> XMATRIX44 conversion ──

static XMATRIX44 FromJson(const Mat4Json& j) {
  XMATRIX44 mat;
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      mat.m[r][c] = j[r][c];
  return mat;
}

static Mat4Json ToJson(const XMATRIX44& mat) {
  Mat4Json j;
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      j[r][c] = mat.m[r][c];
  return j;
}

// ── Camera <-> SnapshotCamJson helpers ──

static SnapshotCamJson CamToJson(Camera& cam) {
  SnapshotCamJson j;
  j.eye = { cam.Eye.x, cam.Eye.y, cam.Eye.z };
  j.pitch = asinf(-cam.Look.y);
  j.roll = cam.Roll;
  j.yaw = atan2f(cam.Look.x, cam.Look.z);
  j.speed = cam.Speed;
  j.fov = cam.Fov;
  j.aspect_ratio = cam.AspectRatio;
  j.near_plane = cam.NPlane;
  j.far_plane = cam.FPlane;
  return j;
}

static void JsonToCam(const SnapshotCamJson& j, Camera& cam) {
  cam.Eye = XVECTOR3(j.eye[0], j.eye[1], j.eye[2]);
  cam.Pitch = j.pitch;
  cam.Roll = j.roll;
  cam.Yaw = j.yaw;
  cam.Speed = j.speed;
  cam.Velocity = XVECTOR3(0, 0, 0);
  cam.Update(0.0f);
}

// ── Init ──

void FrameDumper::Init(const FrameDumperConfig& config) {
  config_ = config;
  hasReplayData_ = false;
  replayState_ = 0;
  replayWarmup_ = 0;
  dumpTimer_ = 0.0f;
  dumpFrameCounter_ = 0;
  dumped_ = false;
  debugDumpRequested_ = false;
  shouldExit_ = false;
}

// ── Replay snapshot ──

bool FrameDumper::HasPendingReplay() const {
  return !config_.replaySnapshotPath.empty() && replayState_ == 0;
}

bool FrameDumper::LoadReplaySnapshot() {
  if (config_.replaySnapshotPath.empty()) return false;

  if (LoadSnapshot(config_.replaySnapshotPath, replayData_)) {
    hasReplayData_ = true;
    replayState_ = 1; // enter warmup
    return true;
  } else {
    replayState_ = 2;
    printf("[FrameDumper] Failed to load '%s', continuing normally.\n",
           config_.replaySnapshotPath.c_str());
    config_.replaySnapshotPath.clear();
    return false;
  }
}

void FrameDumper::ApplySnapshot(Camera& cam, Camera& lightCam, SceneProps& props,
                                Camera* omniCams, XVECTOR3* omniLightPos) {
  if (!hasReplayData_) return;

  // Disable spline agent control
  cam.m_externalControl = false;

  // Restore main camera
  JsonToCam(replayData_.cam, cam);
  if (replayData_.matrices.has_value()) {
    cam.View       = FromJson(replayData_.matrices->camView);
    cam.Projection = FromJson(replayData_.matrices->camProjection);
    cam.VP         = FromJson(replayData_.matrices->camVP);
  }

  // Restore light camera
  JsonToCam(replayData_.light_cam, lightCam);
  if (replayData_.matrices.has_value()) {
    lightCam.View       = FromJson(replayData_.matrices->lightCamView);
    lightCam.Projection = FromJson(replayData_.matrices->lightCamProjection);
    lightCam.VP         = FromJson(replayData_.matrices->lightCamVP);
  }

  // Restore lights
  if (!props.Lights.empty())
    props.Lights[0].Position = lightCam.Eye;

  for (size_t i = 0; i < replayData_.lights.size() && i < props.Lights.size(); i++) {
    auto& fl = replayData_.lights[i];
    props.Lights[i].Position = XVECTOR3(fl.position[0], fl.position[1], fl.position[2]);
    props.Lights[i].Color    = XVECTOR3(fl.color[0], fl.color[1], fl.color[2]);
    props.Lights[i].radius   = fl.radius;
  }

  // Restore scene properties
  auto& sp = replayData_.scene_props;
  props.Exposure = sp.exposure;
  props.BloomFactor = sp.bloom_factor;
  props.ShadowMapResolution = sp.shadow_map_resolution;
  props.PCFScale = sp.pcf_scale;
  props.PCFSamples = sp.pcf_samples;
  props.ParallaxLowSamples = sp.parallax_low;
  props.ParallaxHighSamples = sp.parallax_high;
  props.ParallaxHeight = sp.parallax_height;
  props.LightVolumeSteps = sp.light_volume_steps;
  props.Aperture = sp.aperture;
  props.FocalLength = sp.focal_length;
  props.FocusDepth = sp.focus_depth;
  props.MaxCoc = sp.max_coc;
  props.ActiveLights = sp.active_lights;
  props.ActiveLightCamera = sp.active_light_camera;
  props.ToogleShadow = sp.toggle_shadow;
  props.ToogleSSAO = sp.toggle_ssao;

  // Restore Night scene omni state
  if (replayData_.omni.has_value() && omniLightPos) {
    auto& omni = *replayData_.omni;
    *omniLightPos = XVECTOR3(omni.omni_light_pos[0], omni.omni_light_pos[1], omni.omni_light_pos[2]);

    if (omniCams) {
      for (size_t i = 0; i < omni.omni_cameras.size() && i < 6; i++) {
        JsonToCam(omni.omni_cameras[i], omniCams[i]);
      }
    }
  }

  printf("[FrameDumper] Snapshot applied (scene=%d, %zu lights), warming up %d frames before dump...\n",
         replayData_.scene, replayData_.lights.size(), WARMUP_FRAMES);
  fflush(stdout);
}

void FrameDumper::UpdateReplayState() {
  if (replayState_ != 1) return;

  replayWarmup_++;
  if (replayWarmup_ >= WARMUP_FRAMES) {
    replayState_ = 2;
    debugDumpRequested_ = true;
    printf("[FrameDumper] Warm-up done, triggering dump\n");
    fflush(stdout);
  }
}

bool FrameDumper::IsReplayActive() const {
  return hasReplayData_ || !config_.replaySnapshotPath.empty();
}

bool FrameDumper::SkipCameraUpdates() const {
  if (replayState_ != 0) return true;
  if (debugDumpRequested_) return true;
  return false;
}

// ── Dump control ──

void FrameDumper::RequestDump() {
  if (config_.debugFrames) {
    debugDumpRequested_ = true;
  }
}

bool FrameDumper::ShouldDump(float dt) {
  dumpTimer_ += dt;
  dumpFrameCounter_++;

  if (dumped_) return false;

  // Explicit dump request (spacebar or replay warmup) always fires
  if (debugDumpRequested_) return true;

  // Timed/frame-based dumps require dumpEnabled from command line
  if (!config_.dumpEnabled) return false;

  if (config_.dumpByFrame && dumpFrameCounter_ >= config_.dumpFrame) return true;
  if (!config_.dumpByFrame && config_.dumpSeconds >= 0 && dumpTimer_ >= config_.dumpSeconds) return true;

  return false;
}

bool FrameDumper::ShouldExit() const {
  return shouldExit_;
}

// ── DumpFrame ──

void FrameDumper::DumpFrame(BaseDriver* driver,
                            Camera& cam, Camera& lightCam,
                            const SceneProps& props,
                            const std::vector<RTDumpEntry>& rts,
                            float dt,
                            Camera* omniCams,
                            const XVECTOR3* omniLightPos) {
  dumped_ = true;

  std::string apiName = (driver->m_currentAPI == GRAPHICS_API::D3D11) ? "d3d11" : "gl";
  std::string dumpDir = BuildDumpDir(apiName);

#ifdef OS_WINDOWS
  CreateDirectoryA(dumpDir.c_str(), NULL);
#else
  mkdir(dumpDir.c_str(), 0755);
#endif

  std::string prefix = dumpDir + "/RT_Dump_";

  // Save screenshot + RTs
  driver->SaveScreenshot(prefix + "BackBuffer");
  for (auto& rt : rts) {
    driver->SaveRTToFile(rt.rtID, rt.attachment, prefix + rt.name);
  }

  // Log + write snapshot.json
  LogCameraState(cam, lightCam, props, dumpFrameCounter_, apiName, dt);
  WriteSnapshot(dumpDir + "/snapshot.json", cam, lightCam, props,
                dumpFrameCounter_, apiName, dt, omniCams, omniLightPos);

  std::cout << "RT dump complete -> " << dumpDir << "/ (" << (rts.size() + 1) << " files)" << std::endl;

  // Post-dump behavior
  if (!config_.keepRunning) {
    shouldExit_ = true;
  } else {
    dumped_ = false;
    debugDumpRequested_ = false;
    printf("[keepRunning] Dump done, continuing...\n");
    fflush(stdout);
  }
}

// ── Helpers ──

std::string FrameDumper::BuildDumpDir(const std::string& apiName) {
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

  return "dumps_" + apiName + "_f" + std::to_string(dumpFrameCounter_) + "_" + tsBuf;
}

void FrameDumper::LogCameraState(Camera& cam, Camera& lightCam,
                                 const SceneProps& props,
                                 int frame, const std::string& apiName, float dt) {
  float camEffPitch = asinf(-cam.Look.y);
  float camEffYaw   = atan2f(cam.Look.x, cam.Look.z);
  float lcEffPitch  = asinf(-lightCam.Look.y);
  float lcEffYaw    = atan2f(lightCam.Look.x, lightCam.Look.z);

  auto dumpMatrix = [](std::ostream& os, const char* name, const XMATRIX44& mat) {
    os << name << ":\n";
    for (int r = 0; r < 4; r++)
      os << "  [" << r << "]: " << mat.m[r][0] << ", "
         << mat.m[r][1] << ", " << mat.m[r][2] << ", " << mat.m[r][3] << "\n";
  };

  std::cout << "=== DUMP STATE (frame " << frame << ", " << apiName
            << ", dt=" << dt << "s) ===" << std::endl;
  std::cout << "Cam Eye: " << cam.Eye.x << ", " << cam.Eye.y << ", " << cam.Eye.z << std::endl;
  std::cout << "Cam Pitch: " << camEffPitch << " Roll: " << cam.Roll << " Yaw: " << camEffYaw << std::endl;
  std::cout << "LightCam Eye: " << lightCam.Eye.x << ", " << lightCam.Eye.y
            << ", " << lightCam.Eye.z << std::endl;
  std::cout << "LightCam Pitch: " << lcEffPitch << " Roll: " << lightCam.Roll
            << " Yaw: " << lcEffYaw << std::endl;
  dumpMatrix(std::cout, "Cam.View", cam.View);
  dumpMatrix(std::cout, "Cam.Projection", cam.Projection);
  dumpMatrix(std::cout, "Cam.VP", cam.VP);
  dumpMatrix(std::cout, "LightCam.View", lightCam.View);
  dumpMatrix(std::cout, "LightCam.Projection", lightCam.Projection);
  dumpMatrix(std::cout, "LightCam.VP", lightCam.VP);

  std::cout << "Lights (" << props.Lights.size() << "):" << std::endl;
  for (size_t i = 0; i < props.Lights.size(); i++) {
    const Light& lt = props.Lights[i];
    std::cout << "  [" << i << "] Pos=(" << lt.Position.x << ", "
              << lt.Position.y << ", " << lt.Position.z
              << ") Col=(" << lt.Color.x << ", " << lt.Color.y << ", " << lt.Color.z
              << ") R=" << lt.radius << std::endl;
  }
}

void FrameDumper::WriteSnapshot(const std::string& path,
                                Camera& cam, Camera& lightCam,
                                const SceneProps& props,
                                int frame, const std::string& apiName, float dt,
                                Camera* omniCams, const XVECTOR3* omniLightPos) {
  SnapshotJson out;
  out.frame = frame;
  out.scene = config_.sceneIndex;
  out.api = apiName;
  out.dt = dt;

  out.cam = CamToJson(cam);
  out.light_cam = CamToJson(lightCam);

  // Lights
  for (size_t i = 0; i < props.Lights.size(); i++) {
    const Light& lt = props.Lights[i];
    SnapshotLightJson flt;
    flt.position = { lt.Position.x, lt.Position.y, lt.Position.z };
    flt.color = { lt.Color.x, lt.Color.y, lt.Color.z };
    flt.radius = lt.radius;
    out.lights.push_back(flt);
  }

  // Scene properties
  auto& sp = out.scene_props;
  sp.exposure = props.Exposure;
  sp.bloom_factor = props.BloomFactor;
  sp.shadow_map_resolution = props.ShadowMapResolution;
  sp.pcf_scale = props.PCFScale;
  sp.pcf_samples = props.PCFSamples;
  sp.parallax_low = props.ParallaxLowSamples;
  sp.parallax_high = props.ParallaxHighSamples;
  sp.parallax_height = props.ParallaxHeight;
  sp.light_volume_steps = props.LightVolumeSteps;
  sp.aperture = props.Aperture;
  sp.focal_length = props.FocalLength;
  sp.focus_depth = props.FocusDepth;
  sp.max_coc = props.MaxCoc;
  sp.active_lights = props.ActiveLights;
  sp.active_light_camera = props.ActiveLightCamera;
  sp.toggle_shadow = props.ToogleShadow;
  sp.toggle_ssao = props.ToogleSSAO;

  // Matrices
  SnapshotMatricesJson mats;
  mats.camView       = ToJson(cam.View);
  mats.camProjection = ToJson(cam.Projection);
  mats.camVP         = ToJson(cam.VP);
  mats.lightCamView       = ToJson(lightCam.View);
  mats.lightCamProjection = ToJson(lightCam.Projection);
  mats.lightCamVP         = ToJson(lightCam.VP);
  out.matrices = mats;

  // Night scene omni data
  if (omniCams && omniLightPos) {
    SnapshotOmniJson omni;
    omni.omni_light_pos = { omniLightPos->x, omniLightPos->y, omniLightPos->z };
    for (int i = 0; i < 6; i++) {
      omni.omni_cameras.push_back(CamToJson(omniCams[i]));
    }
    out.omni = omni;
  }

  SaveSnapshot(path, out);
}

} // namespace t800
