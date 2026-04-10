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

// ── Init ──

void FrameDumper::Init(const FrameDumperConfig& config) {
  config_ = config;
  hasFeedData_ = false;
  feedState_ = 0;
  feedWarmup_ = 0;
  dumpTimer_ = 0.0f;
  dumpFrameCounter_ = 0;
  dumped_ = false;
  debugDumpRequested_ = false;
  shouldExit_ = false;
}

// ── Feed matrices ──

bool FrameDumper::HasPendingFeed() const {
  return !config_.feedMatricesPath.empty() && feedState_ == 0;
}

bool FrameDumper::LoadFeedMatrices() {
  if (config_.feedMatricesPath.empty()) return false;

  if (LoadFeedFile(config_.feedMatricesPath, feedData_)) {
    hasFeedData_ = true;
    feedState_ = 1; // enter warmup
    return true;
  } else {
    feedState_ = 2;
    config_.feedMatricesPath.clear();
    printf("[FrameDumper] Failed to load '%s', continuing normally.\n",
           config_.feedMatricesPath.c_str());
    return false;
  }
}

void FrameDumper::ApplyFeedState(Camera& cam, Camera& lightCam, SceneProps& props) {
  if (!hasFeedData_) return;

  // Disable spline agent control
  cam.m_externalControl = false;

  cam.Eye = XVECTOR3(feedData_.cam.eye[0], feedData_.cam.eye[1], feedData_.cam.eye[2]);
  cam.Pitch = feedData_.cam.pitch;
  cam.Roll = feedData_.cam.roll;
  cam.Yaw = feedData_.cam.yaw;
  cam.Velocity = XVECTOR3(0, 0, 0);
  cam.Update(0.0f);

  if (feedData_.matrices.has_value()) {
    cam.View       = FromJson(feedData_.matrices->camView);
    cam.Projection = FromJson(feedData_.matrices->camProjection);
    cam.VP         = FromJson(feedData_.matrices->camVP);
  }

  lightCam.Eye = XVECTOR3(feedData_.lightCam.eye[0], feedData_.lightCam.eye[1], feedData_.lightCam.eye[2]);
  lightCam.Pitch = feedData_.lightCam.pitch;
  lightCam.Roll = feedData_.lightCam.roll;
  lightCam.Yaw = feedData_.lightCam.yaw;
  lightCam.Velocity = XVECTOR3(0, 0, 0);
  lightCam.Update(0.0f);

  if (feedData_.matrices.has_value()) {
    lightCam.View       = FromJson(feedData_.matrices->lightCamView);
    lightCam.Projection = FromJson(feedData_.matrices->lightCamProjection);
    lightCam.VP         = FromJson(feedData_.matrices->lightCamVP);
  }

  // Apply light data from feed
  if (!props.Lights.empty())
    props.Lights[0].Position = lightCam.Eye;

  for (size_t i = 0; i < feedData_.lights.size() && i < props.Lights.size(); i++) {
    auto& fl = feedData_.lights[i];
    props.Lights[i].Position = XVECTOR3(fl.position[0], fl.position[1], fl.position[2]);
    props.Lights[i].Color    = XVECTOR3(fl.color[0], fl.color[1], fl.color[2]);
    props.Lights[i].radius   = fl.radius;
  }

  printf("[FrameDumper] Camera state applied, warming up %d frames before dump...\n", WARMUP_FRAMES);
  fflush(stdout);
}

void FrameDumper::UpdateFeedState() {
  if (feedState_ != 1) return;

  feedWarmup_++;
  if (feedWarmup_ >= WARMUP_FRAMES) {
    feedState_ = 2;
    debugDumpRequested_ = true;
    printf("[FrameDumper] Warm-up done, triggering dump\n");
    fflush(stdout);
  }
}

bool FrameDumper::IsFeedActive() const {
  return hasFeedData_ || !config_.feedMatricesPath.empty();
}

bool FrameDumper::SkipCameraUpdates() const {
  if (feedState_ != 0) return true;
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

  // Explicit dump request (spacebar or feed warmup) always fires
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
                            float dt) {
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

  // Log + write matrices.json
  LogCameraState(cam, lightCam, props, dumpFrameCounter_, apiName, dt);
  WriteMatricesJson(dumpDir + "/matrices.json", cam, lightCam, props,
                    dumpFrameCounter_, apiName, dt);

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

void FrameDumper::WriteMatricesJson(const std::string& path,
                                    Camera& cam, Camera& lightCam,
                                    const SceneProps& props,
                                    int frame, const std::string& apiName, float dt) {
  float camEffPitch = asinf(-cam.Look.y);
  float camEffYaw   = atan2f(cam.Look.x, cam.Look.z);
  float lcEffPitch  = asinf(-lightCam.Look.y);
  float lcEffYaw    = atan2f(lightCam.Look.x, lightCam.Look.z);

  FeedFileJson out;
  out.frame = frame;
  out.api = apiName;
  out.dt = dt;

  out.cam.eye = { cam.Eye.x, cam.Eye.y, cam.Eye.z };
  out.cam.pitch = camEffPitch;
  out.cam.roll = cam.Roll;
  out.cam.yaw = camEffYaw;

  out.lightCam.eye = { lightCam.Eye.x, lightCam.Eye.y, lightCam.Eye.z };
  out.lightCam.pitch = lcEffPitch;
  out.lightCam.roll = lightCam.Roll;
  out.lightCam.yaw = lcEffYaw;

  for (size_t i = 0; i < props.Lights.size(); i++) {
    const Light& lt = props.Lights[i];
    FeedLightJson flt;
    flt.position = { lt.Position.x, lt.Position.y, lt.Position.z };
    flt.color = { lt.Color.x, lt.Color.y, lt.Color.z };
    flt.radius = lt.radius;
    out.lights.push_back(flt);
  }

  FeedMatricesJson mats;
  mats.camView       = ToJson(cam.View);
  mats.camProjection = ToJson(cam.Projection);
  mats.camVP         = ToJson(cam.VP);
  mats.lightCamView       = ToJson(lightCam.View);
  mats.lightCamProjection = ToJson(lightCam.Projection);
  mats.lightCamVP         = ToJson(lightCam.VP);
  out.matrices = mats;

  SaveFeedFile(path, out);
}

} // namespace t800
