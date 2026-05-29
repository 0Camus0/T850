#include <pch.h>
#include <debug/FrameDumper.h>
#include <debug/RenderTrace.h>
#include <utils/Camera.h>
#include <scene/SceneProp.h>
#include <video/BaseDriver.h>
#include <Descriptors.h>

#include <iostream>
#include <cstdio>
#include <cmath>
#include <chrono>
#include <utils/Log.h>
#include <ctime>
#ifdef OS_WINDOWS
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace t850 {

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
    T8_LOG_ERROR("[FrameDumper] Failed to load '%s', continuing normally.",
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
  if (sp.bloom_threshold) props.BloomThreshold = *sp.bloom_threshold;
  props.ToneMapWhiteLevel = sp.tone_map_white_level;
  props.LuminanceTau = sp.luminance_tau;
  if (sp.luminance_mode) props.LuminanceMode = *sp.luminance_mode;
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
  props.DebugMode = sp.debug_mode;
  if (sp.ambient_color) props.AmbientColor = XVECTOR3((*sp.ambient_color)[0], (*sp.ambient_color)[1], (*sp.ambient_color)[2]);
  if (sp.toggle_dof) props.ToogleDOF = *sp.toggle_dof;
  if (sp.toggle_parallax) props.ToogleParallax = *sp.toggle_parallax;
  if (sp.toggle_parallax_shadow) props.ToogleParallaxShadow = *sp.toggle_parallax_shadow;
  if (sp.toggle_godrays) props.ToogleGodRays = *sp.toggle_godrays;
  if (sp.auto_focus) props.AutoFocus = *sp.auto_focus;
  if (sp.shadow_bias) props.ShadowBias = *sp.shadow_bias;
  if (sp.shadow_min) props.ShadowMin = *sp.shadow_min;
  if (sp.env_factor) props.EnvFactor = *sp.env_factor;
  if (sp.ibl_factor) props.IBLFactor = *sp.ibl_factor;
  if (sp.ibl_mip_count) props.IBLMipCount = *sp.ibl_mip_count;
  if (sp.ibl_diffuse_mip_level) props.IBLDiffuseMipLevel = *sp.ibl_diffuse_mip_level;
  if (sp.ibl_brdf_lut_enabled) props.IBLBRDFLUTEnabled = *sp.ibl_brdf_lut_enabled;
  if (sp.godrays_factor) props.GodRaysFactor = *sp.godrays_factor;
  if (sp.material_emissive_intensity) props.MaterialEmissiveIntensity = *sp.material_emissive_intensity;
  if (sp.material_transmission_multiplier) props.MaterialTransmissionMultiplier = *sp.material_transmission_multiplier;
  if (sp.material_refraction_strength) props.MaterialRefractionStrength = *sp.material_refraction_strength;
  if (sp.lightmap_intensity) props.LightmapIntensity = *sp.lightmap_intensity;
  if (sp.point_lights_enabled) props.PointLightsEnabled = *sp.point_lights_enabled;
  if (sp.parallax_shadow_min_layers) props.ParallaxShadowMinLayers = *sp.parallax_shadow_min_layers;
  if (sp.parallax_shadow_max_layers) props.ParallaxShadowMaxLayers = *sp.parallax_shadow_max_layers;
  if (sp.parallax_shadow_softness) props.ParallaxShadowSoftness = *sp.parallax_shadow_softness;
  if (sp.parallax_shadow_strength) props.ParallaxShadowStrength = *sp.parallax_shadow_strength;
  if (replayData_.omni.has_value() && omniLightPos) {
    auto& omni = *replayData_.omni;
    *omniLightPos = XVECTOR3(omni.omni_light_pos[0], omni.omni_light_pos[1], omni.omni_light_pos[2]);

    if (omniCams) {
      for (size_t i = 0; i < omni.omni_cameras.size() && i < 6; i++) {
        JsonToCam(omni.omni_cameras[i], omniCams[i]);
      }
    }
  }

  T8_LOG_INFO("[FrameDumper] Snapshot applied (scene=%d, %zu lights), warming up %d frames before dump...",
         replayData_.scene, replayData_.lights.size(), WARMUP_FRAMES);
  fflush(stdout);
}

void FrameDumper::UpdateReplayState() {
  if (replayState_ != 1) return;

  replayWarmup_++;
  if (replayWarmup_ >= WARMUP_FRAMES) {
    replayState_ = 2;
    debugDumpRequested_ = true;
    T8_LOG_INFO("[FrameDumper] Warm-up done, triggering dump");
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

const SnapshotSkinnedJson* FrameDumper::GetReplaySkinnedState() const {
  if (!hasReplayData_ || !replayData_.skinned.has_value()) return nullptr;
  return &*replayData_.skinned;
}

// ── Dump control ──

void FrameDumper::RequestDump() {
  debugDumpRequested_ = true;
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
                            const XVECTOR3* omniLightPos,
                            const SnapshotSkinnedJson* skinned) {
  dumped_ = true;

  std::string apiName = (driver->m_currentAPI == GraphicsApi::D3D12) ? "d3d12"
                       : (driver->m_currentAPI == GraphicsApi::D3D11) ? "d3d11"
                       : (driver->m_currentAPI == GraphicsApi::VULKAN) ? "vulkan"
                       : "gl";
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
                dumpFrameCounter_, apiName, dt, omniCams, omniLightPos, skinned);

#ifdef T850_RENDER_TRACE
  // Render trace: write trace.json next to RT_Dump_*.ppm and snapshot.json so
  // a side-by-side comparison of D3D12 vs Vulkan can use the same dumpDir.
  if (t850::g_renderTracer) {
    t850::g_renderTracer->Save(dumpDir);
  }
#endif

  T8_LOG_INFO("RT dump complete -> %s/ (%zu files)", dumpDir.c_str(), rts.size() + 1);

  // Post-dump behavior
  if (!config_.keepRunning) {
    shouldExit_ = true;
  } else {
    dumped_ = false;
    debugDumpRequested_ = false;
    T8_LOG_INFO("[keepRunning] Dump done, continuing...");
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

  auto dumpMatrix = [](const char* name, const XMATRIX44& mat) {
    for (int r = 0; r < 4; r++)
      T8_LOG_DEBUG("  %s[%d]: %f, %f, %f, %f", name, r, mat.m[r][0], mat.m[r][1], mat.m[r][2], mat.m[r][3]);
  };

  T8_LOG_DEBUG("=== DUMP STATE (frame %d, %s, dt=%fs) ===", frame, apiName.c_str(), dt);
  T8_LOG_DEBUG("Cam Eye: %f, %f, %f", cam.Eye.x, cam.Eye.y, cam.Eye.z);
  T8_LOG_DEBUG("Cam Pitch: %f Roll: %f Yaw: %f", camEffPitch, cam.Roll, camEffYaw);
  T8_LOG_DEBUG("LightCam Eye: %f, %f, %f", lightCam.Eye.x, lightCam.Eye.y, lightCam.Eye.z);
  T8_LOG_DEBUG("LightCam Pitch: %f Roll: %f Yaw: %f", lcEffPitch, lightCam.Roll, lcEffYaw);
  dumpMatrix("Cam.View", cam.View);
  dumpMatrix("Cam.Projection", cam.Projection);
  dumpMatrix("Cam.VP", cam.VP);
  dumpMatrix("LightCam.View", lightCam.View);
  dumpMatrix("LightCam.Projection", lightCam.Projection);
  dumpMatrix("LightCam.VP", lightCam.VP);

  T8_LOG_DEBUG("Lights (%zu):", props.Lights.size());
  for (size_t i = 0; i < props.Lights.size(); i++) {
    const Light& lt = props.Lights[i];
    T8_LOG_DEBUG("  [%zu] Pos=(%f, %f, %f) Col=(%f, %f, %f) R=%f",
              i, lt.Position.x, lt.Position.y, lt.Position.z,
              lt.Color.x, lt.Color.y, lt.Color.z, lt.radius);
  }
}

void FrameDumper::WriteSnapshot(const std::string& path,
                                Camera& cam, Camera& lightCam,
                                 const SceneProps& props,
                                 int frame, const std::string& apiName, float dt,
                                 Camera* omniCams, const XVECTOR3* omniLightPos,
                                 const SnapshotSkinnedJson* skinned) {
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
  sp.bloom_threshold = props.BloomThreshold;
  sp.tone_map_white_level = props.ToneMapWhiteLevel;
  sp.luminance_tau = props.LuminanceTau;
  sp.luminance_mode = props.LuminanceMode;
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
  sp.debug_mode = props.DebugMode;
  sp.ambient_color = std::array<float, 3>{props.AmbientColor.x, props.AmbientColor.y, props.AmbientColor.z};
  sp.toggle_dof = props.ToogleDOF;
  sp.toggle_parallax = props.ToogleParallax;
  sp.toggle_parallax_shadow = props.ToogleParallaxShadow;
  sp.toggle_godrays = props.ToogleGodRays;
  sp.auto_focus = props.AutoFocus;
  sp.shadow_bias = props.ShadowBias;
  sp.shadow_min = props.ShadowMin;
  sp.env_factor = props.EnvFactor;
  sp.ibl_factor = props.IBLFactor;
  sp.ibl_mip_count = props.IBLMipCount;
  sp.ibl_diffuse_mip_level = props.IBLDiffuseMipLevel;
  sp.ibl_brdf_lut_enabled = props.IBLBRDFLUTEnabled;
  sp.godrays_factor = props.GodRaysFactor;
  sp.material_emissive_intensity = props.MaterialEmissiveIntensity;
  sp.material_transmission_multiplier = props.MaterialTransmissionMultiplier;
  sp.material_refraction_strength = props.MaterialRefractionStrength;
  sp.lightmap_intensity = props.LightmapIntensity;
  sp.point_lights_enabled = props.PointLightsEnabled;
  sp.deferred_light_volumes_enabled = props.DeferredLightVolumesEnabled;
  sp.debug_deferred_lights_packed = static_cast<int>(props.DebugDeferredLightsPacked);
  sp.debug_deferred_lights_directional = static_cast<int>(props.DebugDeferredLightsDirectional);
  sp.debug_deferred_lights_point_volumes = static_cast<int>(props.DebugDeferredLightsPointVolumes);
  sp.debug_deferred_lights_considered = static_cast<int>(props.DebugDeferredLightsConsidered);
  sp.debug_deferred_lights_active_limit = static_cast<int>(props.DebugDeferredLightsActiveLimit);
  sp.debug_deferred_lights_scene_total = static_cast<int>(props.DebugDeferredLightsSceneTotal);
  sp.debug_deferred_lights_frustum_culled = static_cast<int>(props.DebugDeferredLightsFrustumCulled);
  sp.debug_deferred_lights_disabled = static_cast<int>(props.DebugDeferredLightsDisabled);
  sp.debug_deferred_lights_zero_intensity = static_cast<int>(props.DebugDeferredLightsZeroIntensity);
  sp.debug_deferred_lights_max_capped = static_cast<int>(props.DebugDeferredLightsMaxCapped);
  sp.debug_deferred_light_volume_screen_percent = props.DebugDeferredLightVolumeScreenPercent;
  sp.debug_deferred_light_tiles_x = static_cast<int>(props.DebugDeferredLightTilesX);
  sp.debug_deferred_light_tiles_y = static_cast<int>(props.DebugDeferredLightTilesY);
  sp.debug_deferred_light_tile_count = static_cast<int>(props.DebugDeferredLightTileCount);
  sp.debug_deferred_light_tile_size = static_cast<int>(props.DebugDeferredLightTileSize);
  sp.debug_deferred_light_active_tiles = static_cast<int>(props.DebugDeferredLightActiveTiles);
  sp.debug_deferred_light_tile_light_refs = static_cast<int>(props.DebugDeferredLightTileLightRefs);
  sp.debug_deferred_light_average_lights_per_tile = props.DebugDeferredLightAverageLightsPerTile;
  sp.debug_deferred_light_max_lights_in_tile = static_cast<int>(props.DebugDeferredLightMaxLightsInTile);
  sp.debug_deferred_light_saturated_tiles = static_cast<int>(props.DebugDeferredLightSaturatedTiles);
  sp.parallax_shadow_min_layers = props.ParallaxShadowMinLayers;
  sp.parallax_shadow_max_layers = props.ParallaxShadowMaxLayers;
  sp.parallax_shadow_softness = props.ParallaxShadowSoftness;
  sp.parallax_shadow_strength = props.ParallaxShadowStrength;
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

  if (skinned && skinned->has_skin) {
    out.skinned = *skinned;
  }

  SaveSnapshot(path, out);
}

} // namespace t850
