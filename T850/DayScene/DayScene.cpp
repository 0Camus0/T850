#include <DayScene.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <scene/IBLResources.h>
#include <scene/RenderMesh.h>
#include <physics/PhysicsAuthoring.h>
#include <utils/Log.h>
#include <core/Config.h>
#include <core/EngineContext.h>
#include <utils/ConfigRuntime.h>
#include <utils/RuntimeProfile.h>
#ifdef OS_WINDOWS
#include <core/windows/Win32Framework.h>
#endif
#ifdef OS_ANDROID
#include <android/input.h>
#include <core/android/AndroidFramework.h>
#include <video/vulkan/VulkanDriver.h>
#endif
#include <imgui/DevGuiContext.h>
#include <imgui.h>
using namespace t850;
using std::cout;
using std::endl;
using std::string;

namespace {
constexpr float kBenchmarkRunDurationSeconds = 10.0f;
constexpr float kBenchmarkFixedDeltaSeconds = 1.0f / 60.0f;
constexpr int kBenchmarkDefaultWarmupFrames = 30;
constexpr int kBenchmarkDefaultMeasuredFrames =
    static_cast<int>(kBenchmarkRunDurationSeconds / kBenchmarkFixedDeltaSeconds + 0.5f);
constexpr int kBenchmarkMatrixDurationSeconds = 90;
constexpr const char* kDaySceneSharedProfileName = "shared";

std::string JsonEscape(const std::string& value) {
  std::ostringstream out;
  for (char c : value) {
    switch (c) {
    case '\\': out << "\\\\"; break;
    case '"': out << "\\\""; break;
    case '\n': out << "\\n"; break;
    case '\r': out << "\\r"; break;
    case '\t': out << "\\t"; break;
    default: out << c; break;
    }
  }
  return out.str();
}

double Percentile(const std::vector<double>& sortedValues, double percentile) {
  if (sortedValues.empty()) return 0.0;
  const double position = (percentile / 100.0) * static_cast<double>(sortedValues.size() - 1);
  const size_t lower = static_cast<size_t>(std::floor(position));
  const size_t upper = static_cast<size_t>(std::ceil(position));
  const double t = position - static_cast<double>(lower);
  return sortedValues[lower] * (1.0 - t) + sortedValues[upper] * t;
}

bool NearlyEqual(float lhs, float rhs, float epsilon = 0.0001f) {
  return std::fabs(lhs - rhs) <= epsilon;
}

const t850::FloatOverrideDesc* FindFloatOverride(const std::vector<t850::FloatOverrideDesc>& values, const std::string& name) {
  for (const auto& value : values)
    if (value.name == name) return &value;
  return nullptr;
}

const t850::BoolOverrideDesc* FindBoolOverride(const std::vector<t850::BoolOverrideDesc>& values, const std::string& name) {
  for (const auto& value : values)
    if (value.name == name) return &value;
  return nullptr;
}

const t850::IntOverrideDesc* FindIntOverride(const std::vector<t850::IntOverrideDesc>& values, const std::string& name) {
  for (const auto& value : values)
    if (value.name == name) return &value;
  return nullptr;
}

#ifdef OS_ANDROID
struct AndroidVirtualControlsLayout {
  ImVec2 moveCenter;
  ImVec2 lookCenter;
  ImVec2 upCenter;
  ImVec2 downCenter;
  float stickRadius = 0.0f;
  float knobRadius = 0.0f;
  float buttonRadius = 0.0f;
};

float ClampFloat(float value, float minValue, float maxValue) {
  return (std::max)(minValue, (std::min)(maxValue, value));
}

AndroidVirtualControlsLayout BuildAndroidVirtualControlsLayout(float width, float height) {
  AndroidVirtualControlsLayout layout;
  width = (std::max)(width, 1.0f);
  height = (std::max)(height, 1.0f);
  const float shortest = (std::max)(1.0f, (std::min)(width, height));
  layout.stickRadius = ClampFloat(shortest * 0.12f, 64.0f, shortest * 0.18f);
  layout.knobRadius = layout.stickRadius * 0.38f;
  layout.buttonRadius = layout.stickRadius * 0.42f;
  const float margin = layout.stickRadius * 1.35f;
  const float centerY = height - margin;
  layout.moveCenter = ImVec2(margin, centerY);
  layout.lookCenter = ImVec2(width - margin, centerY);
  layout.upCenter = ImVec2(layout.moveCenter.x + layout.stickRadius * 1.55f,
                           layout.moveCenter.y - layout.stickRadius * 0.58f);
  layout.downCenter = ImVec2(layout.upCenter.x,
                             layout.moveCenter.y + layout.stickRadius * 0.58f);
  return layout;
}

bool PointInsideCircle(float x, float y, const ImVec2& center, float radius) {
  const float dx = x - center.x;
  const float dy = y - center.y;
  return (dx * dx + dy * dy) <= radius * radius;
}

XVECTOR2 StickAxisFromPoint(float x, float y, const ImVec2& center, float radius) {
  constexpr float kDeadZone = 0.12f;
  const float dx = x - center.x;
  const float dy = y - center.y;
  const float length = std::sqrt(dx * dx + dy * dy);
  if (length <= radius * kDeadZone) {
    return XVECTOR2(0.0f, 0.0f);
  }

  const float scale = 1.0f / (std::max)(radius, 1.0f);
  XVECTOR2 axis(dx * scale, dy * scale);
  const float axisLength = axis.Length();
  if (axisLength > 1.0f) {
    axis /= axisLength;
  }
  return axis;
}

int FindPointerIndexById(AInputEvent* event, int pointerId) {
  const size_t pointerCount = AMotionEvent_getPointerCount(event);
  for (size_t pointerIndex = 0; pointerIndex < pointerCount; ++pointerIndex) {
    if (AMotionEvent_getPointerId(event, pointerIndex) == pointerId) {
      return static_cast<int>(pointerIndex);
    }
  }
  return -1;
}

void DrawLabeledCircle(ImDrawList* drawList,
                       const ImVec2& center,
                       float radius,
                       const char* label,
                       ImU32 fillColor,
                       ImU32 lineColor,
                       ImU32 textColor) {
  drawList->AddCircleFilled(center, radius, fillColor, 32);
  drawList->AddCircle(center, radius, lineColor, 32, 2.0f);
  const ImVec2 textSize = ImGui::CalcTextSize(label);
  drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f), textColor, label);
}
#endif
}


void DayScene::CaptureSceneProfileState(t850::SandboxProfileDesc& state) const {
  state = t850::SandboxProfileDesc{};
  auto addFloat = [&](const char* name, float value) { state.sliders.push_back({name, value}); };
  auto addBool = [&](const char* name, bool value) { state.checkboxes.push_back({name, value}); };
  auto addInt = [&](const char* name, int value) { state.selectors.push_back({name, value}); };

  addFloat("exposure", SceneProp.Exposure);
  addFloat("bloom_factor", SceneProp.BloomFactor);
  addFloat("bloom_threshold", SceneProp.BloomThreshold);
  addFloat("tm_white_level", SceneProp.ToneMapWhiteLevel);
  addFloat("tm_adapt_tau", SceneProp.LuminanceTau);
  addFloat("pcf_radius", SceneProp.PCFScale);
  addFloat("pcf_samples", SceneProp.PCFSamples);
  addFloat("ssao_kernel_size", (float)SceneProp.SSAOKernel.KernelSize);
  addFloat("ssao_radius", SceneProp.SSAOKernel.Radius);
  addFloat("dof_aperture", SceneProp.Aperture);
  addFloat("dof_focal_length", SceneProp.FocalLength);
  addFloat("dof_max_coc", SceneProp.MaxCoc);
  addFloat("dof_far_samples", SceneProp.DOF_Far_Samples_squared);
  addFloat("dof_near_samples", SceneProp.DOF_Near_Samples_squared);
  addFloat("parallax_low_samples", SceneProp.ParallaxLowSamples);
  addFloat("parallax_high_samples", SceneProp.ParallaxHighSamples);
  addFloat("parallax_height", SceneProp.ParallaxHeight);
  addFloat("parallax_shadow_min_layers", SceneProp.ParallaxShadowMinLayers);
  addFloat("parallax_shadow_max_layers", SceneProp.ParallaxShadowMaxLayers);
  addFloat("parallax_shadow_softness", SceneProp.ParallaxShadowSoftness);
  addFloat("parallax_shadow_strength", SceneProp.ParallaxShadowStrength);
  addFloat("light_volume_steps", SceneProp.LightVolumeSteps);
  addFloat("godrays_factor", SceneProp.GodRaysFactor);
  addFloat("light_radius_scale", SceneProp.LightRadiusScale);
  addFloat("light_intensity_scale", SceneProp.LightIntensityScale);
  addFloat("lightmap_intensity", SceneProp.LightmapIntensity);
  addFloat("shadow_bias", SceneProp.ShadowBias);
  addFloat("shadow_min", SceneProp.ShadowMin);
  addFloat("env_factor", SceneProp.EnvFactor);
  addFloat("ibl_factor", SceneProp.IBLFactor);
  addFloat("material_emissive_intensity", SceneProp.MaterialEmissiveIntensity);
  addFloat("material_transmission_multiplier", SceneProp.MaterialTransmissionMultiplier);
  addFloat("material_refraction_strength", SceneProp.MaterialRefractionStrength);
  addFloat("navmesh_debug_offset", m_navMeshDebugOffset);
  if (ActiveCam) addFloat("fov", Rad2Deg(ActiveCam->Fov));

  for (int kernelIndex = 0; kernelIndex < (int)SceneProp.pGaussKernels.size(); ++kernelIndex) {
    GaussFilter* kernel = SceneProp.pGaussKernels[kernelIndex];
    if (!kernel) continue;
    std::string prefix = "gauss_" + std::to_string(kernelIndex) + "_";
    addFloat((prefix + "radius").c_str(), kernel->radius);
    addFloat((prefix + "sigma").c_str(), kernel->sigma);
    addInt((prefix + "kernel_size").c_str(), kernel->kernelSize);
  }

  addBool("shadow_toggle", SceneProp.ToogleShadow != 0);
  addBool("ssao_toggle", SceneProp.ToogleSSAO != 0);
  addBool("dof_auto_focus", SceneProp.AutoFocus);
  addBool("show_spline", m_showSpline);
  addBool("show_lights", m_showLights);
  addBool("show_physics", m_showPhysics);
  addBool("show_navmesh", m_showNavMesh);
  addBool("dof_toggle", SceneProp.ToogleDOF != 0);
  addBool("parallax_toggle", SceneProp.ToogleParallax != 0);
  addBool("parallax_shadow_toggle", SceneProp.ToogleParallaxShadow != 0);
  addBool("godrays_toggle", SceneProp.ToogleGodRays != 0);
  addBool("point_lights_enabled", SceneProp.PointLightsEnabled);
  addBool("debug_luminance", SceneProp.DebugLuminanceEnabled);
  addBool("frustum_culling", SceneProp.FrustumCullingEnabled);
  addBool("show_culling_debug", m_showCullStats);

  addInt("num_lights", SceneProp.ActiveLights);
  addInt("active_gauss_kernel", ChangeActiveGaussSelection);
  addInt("debug_render_target", m_debugRTSelection);
  addInt("active_camera", m_activeCameraIndex);
  addInt("cubemap", m_currentCubemapIndex);
  addInt("luminance_mode", SceneProp.LuminanceMode);
  addInt("navmesh_debug_shape", m_navMeshDebugShapeMode);
}

void DayScene::ApplySceneProfileState(const t850::SandboxProfileDesc& state) {
  for (const auto& value : state.sliders) {
    if (value.name == "exposure") SceneProp.Exposure = value.value;
    else if (value.name == "bloom_factor") SceneProp.BloomFactor = value.value;
    else if (value.name == "bloom_threshold") SceneProp.BloomThreshold = value.value;
    else if (value.name == "tm_white_level") SceneProp.ToneMapWhiteLevel = value.value;
    else if (value.name == "tm_adapt_tau") SceneProp.LuminanceTau = value.value;
    else if (value.name == "pcf_radius") SceneProp.PCFScale = value.value;
    else if (value.name == "pcf_samples") SceneProp.PCFSamples = value.value;
    else if (value.name == "ssao_kernel_size") { SceneProp.SSAOKernel.KernelSize = (int)value.value; SceneProp.SSAOKernel.Update(); }
    else if (value.name == "ssao_radius") SceneProp.SSAOKernel.Radius = value.value;
    else if (value.name == "dof_aperture") SceneProp.Aperture = value.value;
    else if (value.name == "dof_focal_length") SceneProp.FocalLength = value.value;
    else if (value.name == "dof_max_coc") SceneProp.MaxCoc = value.value;
    else if (value.name == "dof_far_samples") SceneProp.DOF_Far_Samples_squared = value.value;
    else if (value.name == "dof_near_samples") SceneProp.DOF_Near_Samples_squared = value.value;
    else if (value.name == "parallax_low_samples") SceneProp.ParallaxLowSamples = value.value;
    else if (value.name == "parallax_high_samples") SceneProp.ParallaxHighSamples = value.value;
    else if (value.name == "parallax_height") SceneProp.ParallaxHeight = value.value;
    else if (value.name == "parallax_shadow_min_layers") SceneProp.ParallaxShadowMinLayers = value.value;
    else if (value.name == "parallax_shadow_max_layers") SceneProp.ParallaxShadowMaxLayers = value.value;
    else if (value.name == "parallax_shadow_softness") SceneProp.ParallaxShadowSoftness = value.value;
    else if (value.name == "parallax_shadow_strength") SceneProp.ParallaxShadowStrength = value.value;
    else if (value.name == "light_volume_steps") SceneProp.LightVolumeSteps = value.value;
    else if (value.name == "godrays_factor") SceneProp.GodRaysFactor = value.value;
    else if (value.name == "light_radius_scale") SceneProp.LightRadiusScale = value.value;
    else if (value.name == "light_intensity_scale") SceneProp.LightIntensityScale = value.value;
    else if (value.name == "lightmap_intensity") SceneProp.LightmapIntensity = value.value;
    else if (value.name == "shadow_bias") SceneProp.ShadowBias = value.value;
    else if (value.name == "shadow_min") SceneProp.ShadowMin = value.value;
    else if (value.name == "env_factor") SceneProp.EnvFactor = value.value;
    else if (value.name == "ibl_factor") SceneProp.IBLFactor = value.value;
    else if (value.name == "material_emissive_intensity") SceneProp.MaterialEmissiveIntensity = value.value;
    else if (value.name == "material_transmission_multiplier") SceneProp.MaterialTransmissionMultiplier = value.value;
    else if (value.name == "material_refraction_strength") SceneProp.MaterialRefractionStrength = value.value;
    else if (value.name == "navmesh_debug_offset") m_navMeshDebugOffset = (std::max)(0.0f, (std::min)(0.25f, value.value));
    else if (value.name == "fov" && ActiveCam) { ActiveCam->SetFov(Deg2Rad(value.value)); ActiveCam->VP = ActiveCam->View * ActiveCam->Projection; VP = ActiveCam->VP; }

    for (int kernelIndex = 0; kernelIndex < (int)SceneProp.pGaussKernels.size(); ++kernelIndex) {
      GaussFilter* kernel = SceneProp.pGaussKernels[kernelIndex];
      if (!kernel) continue;
      std::string prefix = "gauss_" + std::to_string(kernelIndex) + "_";
      if (value.name == prefix + "radius") { kernel->radius = value.value; kernel->Update(); }
      else if (value.name == prefix + "sigma") { kernel->sigma = value.value; kernel->Update(); }
    }
  }

  for (const auto& value : state.checkboxes) {
    if (value.name == "shadow_toggle") SceneProp.ToogleShadow = value.value ? 1 : 0;
    else if (value.name == "ssao_toggle") SceneProp.ToogleSSAO = value.value ? 1 : 0;
    else if (value.name == "dof_auto_focus") SceneProp.AutoFocus = value.value;
    else if (value.name == "show_spline") m_showSpline = value.value;
    else if (value.name == "show_lights") m_showLights = value.value;
    else if (value.name == "show_physics") m_showPhysics = value.value;
    else if (value.name == "show_navmesh") {
      m_showNavMesh = value.value;
      if (m_showNavMesh && !m_navMesh.IsReady()) m_navMeshBuildAttempted = false;
    }
    else if (value.name == "dof_toggle") { SceneProp.ToogleDOF = value.value ? 1 : 0; m_renderGraph.SetPassEnabled("CoC", value.value); m_renderGraph.SetPassEnabled("Combine CoC", value.value); m_renderGraph.SetPassEnabled("DOF", value.value); m_renderGraph.SetPassEnabled("DOF 2", value.value); }
    else if (value.name == "parallax_toggle") { SceneProp.ToogleParallax = value.value ? 1 : 0; if (Meshes[0].pBase) Meshes[0].SetParallaxEnabled(value.value); }
    else if (value.name == "parallax_shadow_toggle") { SceneProp.ToogleParallaxShadow = value.value ? 1 : 0; SceneProp.ParallaxShadowStrength = value.value ? SceneProp.ParallaxShadowStrength : 0.0f; }
    else if (value.name == "godrays_toggle") SceneProp.ToogleGodRays = value.value ? 1 : 0;
    else if (value.name == "point_lights_enabled") SceneProp.PointLightsEnabled = value.value;
    else if (value.name == "debug_luminance") { SceneProp.DebugLuminanceEnabled = value.value; if (!value.value) SceneProp.DebugAdaptedLuminanceValid = false; }
    else if (value.name == "frustum_culling") SceneProp.FrustumCullingEnabled = SceneProp.FrustumCullingToggleAllowed && value.value;
    else if (value.name == "show_culling_debug") { m_showCullStats = value.value; SceneProp.ShowCullingDebug = value.value; }
  }

  for (const auto& value : state.selectors) {
    if (value.name == "num_lights") SceneProp.ActiveLights = value.value;
    else if (value.name == "active_gauss_kernel") ChangeActiveGaussSelection = value.value;
    else if (value.name == "debug_render_target") m_debugRTSelection = value.value;
    else if (value.name == "luminance_mode") SceneProp.LuminanceMode = value.value;
    else if (value.name == "active_camera") ApplyActiveCameraSelection(value.value);
    else if (value.name == "cubemap") m_currentCubemapIndex = value.value;
    else if (value.name == "navmesh_debug_shape") m_navMeshDebugShapeMode = (std::max)(0, (std::min)(1, value.value));
    for (int kernelIndex = 0; kernelIndex < (int)SceneProp.pGaussKernels.size(); ++kernelIndex) {
      GaussFilter* kernel = SceneProp.pGaussKernels[kernelIndex];
      if (!kernel) continue;
      std::string name = "gauss_" + std::to_string(kernelIndex) + "_kernel_size";
      if (value.name == name) { kernel->kernelSize = value.value; kernel->Update(); }
    }
  }

  if (Meshes[0].pBase) {
    Meshes[0].SetParallaxSettings(SceneProp.ParallaxLowSamples, SceneProp.ParallaxHighSamples, SceneProp.ParallaxHeight);
    Meshes[0].SetParallaxShadowSettings(SceneProp.ParallaxShadowMinLayers, SceneProp.ParallaxShadowMaxLayers,
                                         SceneProp.ParallaxShadowSoftness, SceneProp.ParallaxShadowStrength);
  }
}

t850::SandboxProfileDesc DayScene::BuildSparseSceneProfile(const t850::SandboxProfileDesc& current) const {
  t850::SandboxProfileDesc sparse;
  sparse.name = current.name;
  sparse.platform = current.platform;
  sparse.architecture = current.architecture;
  sparse.gpu_family = current.gpu_family;
  sparse.gpu_name_contains = current.gpu_name_contains;
  for (const auto& value : current.sliders) {
    const auto* baseline = FindFloatOverride(m_sceneProfileBaselineState.sliders, value.name);
    if (!baseline || !NearlyEqual(value.value, baseline->value)) sparse.sliders.push_back(value);
  }
  for (const auto& value : current.checkboxes) {
    const auto* baseline = FindBoolOverride(m_sceneProfileBaselineState.checkboxes, value.name);
    if (!baseline || value.value != baseline->value) sparse.checkboxes.push_back(value);
  }
  for (const auto& value : current.selectors) {
    const auto* baseline = FindIntOverride(m_sceneProfileBaselineState.selectors, value.name);
    if (!baseline || value.value != baseline->value) sparse.selectors.push_back(value);
  }
  return sparse;
}

bool DayScene::EnsureNavMeshBuilt() {
  if (m_navMesh.IsReady()) {
    return true;
  }
  if (m_navMeshBuildAttempted) {
    return false;
  }
  m_navMeshBuildAttempted = true;

  t850::navigation::NavMeshGeometry geometry;
  t850::navigation::NavSourceBuildStats sourceStats;
  std::string error;
  if (!t850::navigation::BuildGeometryFromPrimitiveInstances(Meshes, 1, geometry, &sourceStats, &error)) {
    T8_LOG_ERROR("[Navigation] DayScene navmesh geometry extraction failed: %s (considered=%d included=%d skippedInvisible=%d skippedSkinned=%d skippedInvalid=%d)",
                 error.c_str(),
                 sourceStats.considered,
                 sourceStats.included,
                 sourceStats.skippedInvisible,
                 sourceStats.skippedSkinned,
                 sourceStats.skippedInvalid);
    return false;
  }
  if (!m_navMesh.Build(geometry, t850::navigation::NavMeshBuildSettings(), &error)) {
    T8_LOG_ERROR("[Navigation] DayScene navmesh build failed: %s", error.c_str());
    return false;
  }

  m_navMeshDebugRenderer.Invalidate();
  const t850::navigation::NavMeshBuildStats& stats = m_navMesh.GetStats();
  T8_LOG_INFO("[Navigation] DayScene navmesh ready: sources=%d verts=%d tris=%d polys=%d",
              sourceStats.included, stats.vertexCount, stats.triangleCount, stats.polygonCount);
  return true;
}

void DayScene::LoadSceneProfile() {
  m_selectedProfileTargetIndex = 0;
  CaptureSceneProfileState(m_sceneProfileBaselineState);
  m_sceneProfileSavedState = m_sceneProfileBaselineState;
  m_sceneProfileReady = true;
  m_sceneProfileDirty = false;

  const t850::SandboxProfileDesc* bestProfile = nullptr;
  for (const auto& profile : m_sceneSetup.descriptor.profiles) {
    if (!profile.model.empty()) continue;
    if (profile.name == kDaySceneSharedProfileName) {
      bestProfile = &profile;
      break;
    }
    if (!bestProfile) {
      bestProfile = &profile;
    }
  }

  if (bestProfile) ApplySceneProfileState(*bestProfile);
  CaptureSceneProfileState(m_sceneProfileSavedState);

  const auto& runtime = t850::GetRuntimeProfileInfo();
  T8_LOG_INFO("[DayScene] Shared profile runtime='%s' platform=%s arch=%s gpu='%s' family=%s applied=%d",
              runtime.recommendedProfile.c_str(), runtime.platform.c_str(), runtime.architecture.c_str(),
              runtime.gpuName.c_str(), runtime.gpuFamily.c_str(), bestProfile ? 1 : 0);
}

void DayScene::SaveSceneProfile() {
  if (!m_sceneProfileReady) return;

  t850::SandboxProfileDesc current;
  CaptureSceneProfileState(current);
  t850::SandboxProfileDesc sparse = BuildSparseSceneProfile(current);
  sparse.name = kDaySceneSharedProfileName;
  sparse.platform.clear();
  sparse.architecture.clear();
  sparse.gpu_family.clear();
  sparse.gpu_name_contains.clear();
  sparse.model.clear();

  auto& profiles = m_sceneSetup.descriptor.profiles;
  profiles.clear();
  profiles.push_back(sparse);

  if (t850::SaveSceneDescriptor("Scenes/DayScene.json", m_sceneSetup.descriptor)) {
    m_sceneProfileSavedState = current;
    m_sceneProfileDirty = false;
    T8_LOG_INFO("[DayScene] Saved shared rendering profile for all runtimes");
  }
}

namespace {
std::string TimestampForFilename() {
  auto now = std::chrono::system_clock::now();
  std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
  std::tm localTime{};
#if defined(_WIN32)
  localtime_s(&localTime, &nowTime);
#else
  localtime_r(&nowTime, &localTime);
#endif
  std::ostringstream out;
  out << std::put_time(&localTime, "%Y%m%d_%H%M%S");
  return out.str();
}
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

void DayScene::InitVars() {
  SceneProp = SceneProps{};
  m_renderGraph = t850::RenderGraph{};
  EnvMaps = t850::EnvironmentMapSet{};
  EnvMapTexIndex = -1;
  DiffuseIBLTexIndex = -1;
  SpecularIBLTexIndex = -1;
  BrdfLUTTexIndex = -1;
  SheenIBLTexIndex = -1;
  CharlieLUTTexIndex = -1;
  SheenELUTTexIndex = -1;
  RTIndex = -1;
  GBufferPass = DeferredPass = DepthPass = ShadowAccumPass = BloomAccumPass = -1;
  BrightPassPass = ExtraHelperPass = Extra16FPass = GodRaysCalcPass = GodRaysCalcExtraPass = -1;
  AdaptedLumCurrentPass = AdaptedLumPrevPass = CoCPass = CoCHelperPass = CoCHelperPass2 = -1;
  DOFPass = CombineCoCPass = Extra16FPass5x5 = -1;
  splineWire = nullptr;
  m_pendingCubemap.clear();

  Position = XVECTOR3(0.0f, 0.0f, 0.0f);
  Orientation = XVECTOR3(0.0f, 0.0f, 0.0f);
  Scaling = XVECTOR3(1.0f, 1.0f, 1.0f);
  SelectedMesh = 0;

  CamSelection = NORMAL_CAM1;
  SceneSettingSelection = CHANGE_EXPOSURE;
  m_navMeshDebugOffset = 0.01f;
  m_navMeshDebugShapeMode = 0;

  // Default-initialize scene properties — JSON will overwrite them
  VP.Identity();
  SceneProp.ActiveCamera = 0;
  SceneProp.ActiveLights = 2;
  SceneProp.ActiveLightCamera = 0;
  SceneProp.ActiveGaussKernel = 0;
  SceneProp.AmbientColor = XVECTOR3(0, 0, 0);
  SceneProp.ToogleShadow = 1;
  SceneProp.ToogleSSAO = 1;
  SceneProp.ToogleDOF = 0;
  SceneProp.ToogleParallax = 0;
  SceneProp.ToogleParallaxShadow = 0;
  SceneProp.ToogleGodRays = 0;
  SceneProp.DebugMode = 0;
  SceneProp.ShadowBias = 0.0f;
  SceneProp.ShadowMin = 0.0f;
  SceneProp.EnvFactor = 0.0f;
  SceneProp.IBLFactor = 0.0f;
  SceneProp.IBLMipCount = 4.0f;
  SceneProp.IBLBRDFLUTEnabled = 0.0f;
  SceneProp.GodRaysFactor = 0.0f;
  SceneProp.ShadowMapResolution = 1024.0f;
  SceneProp.GoodRaysResolution = 0.0f;
  SceneProp.PCFScale = 1.7f;
  SceneProp.PCFSamples = 1.0f;
  SceneProp.ParallaxLowSamples = 0.0f;
  SceneProp.ParallaxHighSamples = 0.0f;
  SceneProp.ParallaxHeight = 0.0f;
  SceneProp.ParallaxShadowMinLayers = 0.0f;
  SceneProp.ParallaxShadowMaxLayers = 0.0f;
  SceneProp.ParallaxShadowSoftness = 0.0f;
  SceneProp.ParallaxShadowStrength = 0.0f;
  SceneProp.LightVolumeSteps = 0.0f;
  SceneProp.Exposure = 0.0f;
  SceneProp.BloomFactor = 0.35f;
  SceneProp.BloomThreshold = 0.0f;
  SceneProp.ToneMapWhiteLevel = 5.5f;
  SceneProp.LuminanceTau = 1.1f;
  SceneProp.FrameDeltaSec = 0.0f;
  SceneProp.Aperture = 0.0f;
  SceneProp.FocalLength = 0.0f;
  SceneProp.FocusDepth = 0.0f;
  SceneProp.MaxCoc = 0.0f;
  SceneProp.DOF_Near_Samples_squared = 0.0f;
  SceneProp.DOF_Far_Samples_squared = 0.0f;
  SceneProp.AutoFocus = false;

  if (!m_sceneSetup.Load("Scenes/DayScene.json")) {
    T8_LOG_ERROR("[DayScene] Failed to load Scenes/DayScene.json");
    return;
  }

  if (m_sceneSetup.cameras.size() < 2 && !m_sceneSetup.cameras.empty()) {
    Camera& mainCam = m_sceneSetup.cameras[0];
    Camera spectator;
    XVECTOR3 spectatorPos(mainCam.Eye.x, mainCam.Eye.y + 20.0f, mainCam.Eye.z - 60.0f, 1.0f);
    if (mainCam.Ortho)
      spectator.InitOrtho(spectatorPos, mainCam.Width, mainCam.Height, mainCam.NPlane, mainCam.FPlane, mainCam.LeftHanded);
    else
      spectator.InitPerspective(spectatorPos, mainCam.Fov, mainCam.AspectRatio, mainCam.NPlane, mainCam.FPlane, mainCam.LeftHanded);
    spectator.Speed = mainCam.Speed;
    spectator.Pitch = mainCam.Pitch;
    spectator.Roll = mainCam.Roll;
    spectator.Yaw = mainCam.Yaw;
    spectator.Update(0.0f);
    m_sceneSetup.cameras.push_back(spectator);
  }

  for (auto& selector : m_sceneSetup.descriptor.selectors) {
    if (selector.name == "active_camera") {
      selector.options = {"Spline", "Free"};
      selector.default_index = 0;
      break;
    }
  }

  m_sceneSetup.Apply(SceneProp);

  ActiveCam = m_sceneSetup.GetCamera(0);
  SceneProp.pCullingCamera = ActiveCam;
  SceneProp.FrustumCullingToggleAllowed = g_config.cullingLoadMode != t850::Config::CullingLoadMode::Disabled;
  SceneProp.FrustumCullingEnabled = g_config.cullingLoadMode == t850::Config::CullingLoadMode::FullOnLoad;
  ChangeActiveGaussSelection = SHADOW_KERNEL;
  m_debugRTSelection = 0;
  m_showSpline = false;
  m_showLights = false;
  m_showPhysics = false;
#ifdef OS_ANDROID
  ResetAndroidVirtualControls();
#endif
  m_spectatorCameraEnabled = false;
  m_activeCameraIndex = 0;
  m_tourTimeSec = 0.0f;
  m_benchmarkFrameTimesMs.clear();
  m_benchmarkCullingTotals = BenchmarkCullingTotals{};
  if (g_config.flags.benchmarkMatrix &&
      m_benchmarkMatrixInitialized &&
      m_benchmarkMatrixRunIndex < m_benchmarkMatrixRuns.size()) {
    m_benchmarkActiveOffscreen = m_benchmarkMatrixRuns[m_benchmarkMatrixRunIndex].offscreen;
  } else {
    m_benchmarkActiveOffscreen = g_config.flags.benchmark && g_config.flags.offscreen;
  }
  if (g_config.flags.benchmark) {
    g_config.flags.offscreen = false;
  }
  InitializeBenchmarkMatrix();
  if (g_config.flags.benchmark) {
    if (!g_config.flags.benchmarkMatrix) {
      ResetBenchmarkRunCapture();
    }
    m_benchmarkFrameTimesMs.reserve(12000);
    if (!g_config.flags.benchmarkMatrix) {
      m_benchmarkStatus = "Running benchmark " + std::to_string(g_config.width) + "x" + std::to_string(g_config.height) +
          (g_config.flags.offscreen ? " offscreen" : " onscreen");
    }
  }
  RTIndex = -1;

  FrameDumperConfig dumpCfg;
  dumpCfg.dumpEnabled     = g_config.flags.dumpEnabled;
  dumpCfg.dumpByFrame     = g_config.flags.dumpByFrame;
  dumpCfg.dumpFrame       = g_config.dumpFrame;
  dumpCfg.dumpSeconds     = g_config.dumpSeconds;
  dumpCfg.debugFrames     = g_config.flags.debugFrames;
  dumpCfg.keepRunning     = g_config.flags.keepRunning;
  dumpCfg.replaySnapshotPath = g_config.replaySnapshotPath;
  dumpCfg.sceneIndex      = g_config.startScene;
  m_dumper.Init(dumpCfg);
}
void DayScene::CreateAssets() {
  //Create RT's via RenderGraph
  if (!m_renderGraph.Load("Scenes/DayScene_RenderGraph.json")) {
    T8_LOG_ERROR("[DayScene] Failed to load render graph");
    return;
  }
  LoadSceneProfile();
  m_renderGraph.CreateRenderTargets(pFramework->pVideoDriver, SceneProp);
  m_renderGraph.PrintGraph();
  m_benchmarkOffscreenOutputRT = -1;
  if (g_config.flags.benchmark && m_benchmarkActiveOffscreen) {
    const int outputWidth = g_config.width;
    const int outputHeight = g_config.height;
    m_benchmarkOffscreenOutputRT = pFramework->pVideoDriver->CreateRT(
        1,
        BaseRT::RGBA8,
        BaseRT::F32,
        outputWidth,
        outputHeight,
        false);
    if (m_benchmarkOffscreenOutputRT < 0) {
      T8_LOG_ERROR("[Benchmark] Failed to create explicit offscreen output RT %dx%d", outputWidth, outputHeight);
    }
  }

  RebindRenderGraphOutputs();

  //
  PrimitiveMgr.SetEngineContext(pEngineContext);
  PrimitiveMgr.Init();
  PrimitiveMgr.SetVP(&VP);
  m_flare.Init(PrimitiveMgr);

  SceneProp.SSAOKernel.InitTexture();

  EnvMapTexIndex = g_pBaseDriver->CreateTexture(m_sceneSetup.environmentMap);
  EnvMaps.SetFallback(EnvMapTexIndex);
  LoadEnvironmentIBLResources(
    g_pBaseDriver,
    {m_sceneSetup.environmentDiffuseIBL, m_sceneSetup.environmentSpecularIBL, m_sceneSetup.environmentBrdfLUT,
     m_sceneSetup.environmentSheenIBL, m_sceneSetup.environmentCharlieLUT, m_sceneSetup.environmentSheenELUT},
    EnvMaps,
    DiffuseIBLTexIndex,
    SpecularIBLTexIndex,
    BrdfLUTTexIndex,
    SheenIBLTexIndex,
    CharlieLUTTexIndex,
    SheenELUTTexIndex);
  UpdateSceneIBLSettings(SceneProp, g_pBaseDriver, EnvMaps);

  int index = PrimitiveMgr.CreateMesh("Models/SkyBox.glb");
  Meshes[1].CreateInstance(PrimitiveMgr.GetPrimitive(index), &VP);
  Meshes[1].TranslateAbsolute(0.0, -10.0f, 0.0f);
  Meshes[1].Update();

  index = PrimitiveMgr.CreateMesh("Models/SponzaEsc.glb");
  Meshes[0].CreateInstance(PrimitiveMgr.GetPrimitive(index), &VP);
  Meshes[0].Update();
  if (pEngineContext && pEngineContext->physics && pEngineContext->physics->IsInitialized()) {
    RenderMesh* sponzaMesh = dynamic_cast<RenderMesh*>(Meshes[0].pBase);
    PhysicsTriangleMeshCookSettings cookSettings;
    cookSettings.maxTrianglesPerLeaf = 8;
    cookSettings.buildQuality = PhysicsMeshBuildQuality::FavorRuntimePerformance;
    cookSettings.useDiskCache = true;

    PhysicsCookStats cookStats;
    if (sponzaMesh && AttachStaticTriangleMeshBody(*pEngineContext->physics, Meshes[0], *sponzaMesh, cookSettings, &cookStats)) {
      T8_LOG_INFO("[DayScene] Sponza physics mesh ready: cache=%s vertices=%u triangles=%u extract=%.2fms load=%.2fms cook=%.2fms save=%.2fms total=%.2fms path='%s'",
                  cookStats.cacheHit ? "hit" : "miss",
                  cookStats.vertexCount,
                  cookStats.triangleCount,
                  cookStats.extractionMs,
                  cookStats.cacheLoadMs,
                  cookStats.cookMs,
                  cookStats.cacheSaveMs,
                  cookStats.totalMs,
                  cookStats.cachePath.c_str());
    } else {
      T8_LOG_ERROR("[DayScene] Failed to create Sponza physics mesh");
    }
  }

  index = PrimitiveMgr.CreateSpline(m_sceneSetup.splines[0]);
  splineWire = (SplineWireframe*)PrimitiveMgr.GetPrimitive(index);
  splineInst.CreateInstance(splineWire, &VP);
  m.Identity();
  Quads[0].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  RebindRenderGraphOutputs();

  Quads[1].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[2].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[3].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[4].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[5].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[6].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[7].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);

  PrimitiveMgr.SetSceneProps(&SceneProp);

  m_wireframeSphere.Create(8, 16);
  m_wireframeArrow.Create(24, 6);
  m_physicsDebugRenderer.Create();
  m_navMeshDebugRenderer.Create();
  m_navMesh.Clear();
  m_navMeshBuildAttempted = false;
  m_debugText.LoadFromFile(24, "Fonts/Martius-LV9L4.ttf", 512.0f);

  t850::Spline& m_spline = m_sceneSetup.splines[0];
  t850::SplineAgent& m_agent = m_sceneSetup.agents[0];
  m_agent.m_actualPoint = m_spline.GetPoint(m_spline.GetNormalizedOffset(0));
  const int attachedCamera = m_sceneSetup.descriptor.splines[0].attached_camera;
  if (Camera* splineCamera = m_sceneSetup.GetCamera(attachedCamera)) {
    splineCamera->AttachAgent(m_agent);
    splineCamera->m_lookAtCenter = false;
  }
  ApplyActiveCameraSelection(m_activeCameraIndex);

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
  Quads[7].TranslateAbsolute(0.0f, 0.0f, 0.0f);
  Quads[7].Update();

  // Apply persisted toggle states that need post-asset setup
  bool dofOn = (SceneProp.ToogleDOF != 0);
  m_renderGraph.SetPassEnabled("CoC", dofOn);
  m_renderGraph.SetPassEnabled("Combine CoC", dofOn);
  m_renderGraph.SetPassEnabled("DOF", dofOn);
  m_renderGraph.SetPassEnabled("DOF 2", dofOn);
  Meshes[0].SetParallaxEnabled(SceneProp.ToogleParallax != 0);
  Meshes[0].SetParallaxShadowEnabled(SceneProp.ToogleParallaxShadow != 0);

  // Initialize light camera direction so LightDir is valid from the first frame
  {
    Camera& LightCam = m_sceneSetup.lightCameras[0];
    LightCam.Update(0.0f);
    SceneProp.Lights[0].Position = LightCam.Eye;
    SceneProp.Lights[0].Direction = LightCam.Look;
  }

  // Sync cubemap index to match loaded environment_map
  auto& selDescs = m_sceneSetup.descriptor.selectors;
  for (auto& sd : selDescs) {
    if (sd.name == "cubemap") {
      std::string envFile = m_sceneSetup.environmentMap;
      size_t slashPos = envFile.rfind('/');
      if (slashPos != std::string::npos) envFile = envFile.substr(slashPos + 1);
      for (int i = 0; i < (int)sd.options.size(); i++) {
        if (sd.options[i] == envFile) { m_currentCubemapIndex = i; break; }
      }
      break;
    }
  }
}

void DayScene::OnLoadScene() {
  InitVars();
  CreateAssets();
}

void DayScene::OnDestoryScene() {
  DestroyAssets();
}

void DayScene::RecordBenchmarkRenderedFrame(double frameMs) {
  if (!g_config.flags.benchmark)
    return;
  if (m_benchmarkFinished)
    return;
  if (m_benchmarkSimulationFrame <= m_benchmarkWarmupFrames)
    return;
  if (m_benchmarkTargetFrames > 0 &&
      static_cast<int>(m_benchmarkFrameTimesMs.size()) >= m_benchmarkTargetFrames)
    return;

  const auto frameEndTime = std::chrono::steady_clock::now();
  if (!m_benchmarkWallClockStarted) {
    m_benchmarkWallClockStarted = true;
    m_benchmarkWallClockStart = frameEndTime - std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double, std::milli>(frameMs));
  }

  m_benchmarkFrameTimesMs.push_back(frameMs);
  if (m_benchmarkFrameTimesMs.size() == 1) {
    T8_LOG_INFO("[Benchmark] First measured frame after warmup=%d simulationFrame=%d targetFrames=%d targetSeconds=%.2f",
                m_benchmarkWarmupFrames, m_benchmarkSimulationFrame, m_benchmarkTargetFrames,
                m_benchmarkTargetDurationSeconds);
  }
  if (Meshes[0].pBase) {
    RenderMesh* rm = static_cast<RenderMesh*>(Meshes[0].pBase);
    m_benchmarkCullingTotals.samples++;
    m_benchmarkCullingTotals.meshTests += rm->m_cullingMeshTests;
    m_benchmarkCullingTotals.subsetTests += rm->m_cullingSubsetTests;
    m_benchmarkCullingTotals.clusterTests += rm->m_cullingClusterTests;
    m_benchmarkCullingTotals.drawCalls += rm->m_drawCalls;
    m_benchmarkCullingTotals.renderStateChanges += rm->m_renderStateChanges;
    m_benchmarkCullingTotals.totalIndices += rm->m_totalIndices;
    m_benchmarkCullingTotals.drawnIndices += rm->m_drawnIndices;
    m_benchmarkCullingTotals.culledIndices += rm->m_culledIndices;
    m_benchmarkCullingTotals.cullingCpuMs += rm->m_cullingCpuMs;
  }

  if (m_benchmarkTargetFrames > 0 &&
      static_cast<int>(m_benchmarkFrameTimesMs.size()) >= m_benchmarkTargetFrames &&
      !m_benchmarkFinishPending) {
    if (g_config.flags.benchmarkFinalFrameDump && m_benchmarkPendingFinalFramePath.empty()) {
      m_benchmarkPendingFinalFramePath = CaptureBenchmarkFinalFrame();
    }
    T8_LOG_INFO("[Benchmark] Queue finish measuredFrames=%zu targetFrames=%d duration=%.2f",
                m_benchmarkFrameTimesMs.size(), m_benchmarkTargetFrames, static_cast<float>(m_benchmarkTargetFrames) * DtSecs);
    QueueBenchmarkFinish(static_cast<float>(m_benchmarkTargetFrames) * DtSecs);
  }
  if (m_benchmarkTargetFrames <= 0 &&
      m_benchmarkTargetDurationSeconds > 0.0f &&
      m_benchmarkTimelineReachedEnd &&
      !m_benchmarkFinishPending) {
    const float elapsedSecs = std::chrono::duration<float>(frameEndTime - m_benchmarkWallClockStart).count();
    if (elapsedSecs >= m_benchmarkTargetDurationSeconds) {
      if (g_config.flags.benchmarkFinalFrameDump && m_benchmarkPendingFinalFramePath.empty()) {
        m_benchmarkPendingFinalFramePath = CaptureBenchmarkFinalFrame();
      }
      T8_LOG_INFO("[Benchmark] Queue finish measuredFrames=%zu elapsed=%.2f targetSeconds=%.2f",
                  m_benchmarkFrameTimesMs.size(), elapsedSecs, m_benchmarkTargetDurationSeconds);
      QueueBenchmarkFinish(m_benchmarkTargetDurationSeconds);
    }
  }
}

std::string DayScene::BuildBenchmarkOutputPath() const {
  if (!g_config.benchmarkOutputPath.empty())
    return g_config.benchmarkOutputPath;

  const char* apiTag = g_pBaseDriver
    ? t850::config::ApiTag(g_pBaseDriver->m_currentAPI)
    : t850::config::ApiTag(t850::config::ParseGraphicsApi(g_config.api, GraphicsApi::D3D11));
  const int benchmarkWidth = (g_pBaseDriver && g_pBaseDriver->width > 0) ? g_pBaseDriver->width : g_config.width;
  const int benchmarkHeight = (g_pBaseDriver && g_pBaseDriver->height > 0) ? g_pBaseDriver->height : g_config.height;

  std::ostringstream out;
  out << "benchmark_stats_dayscene_" << apiTag << "_"
      << benchmarkWidth << "x" << benchmarkHeight << "_culling_"
      << (SceneProp.FrustumCullingEnabled ? "on" : "off") << "_"
      << TimestampForFilename() << ".json";
  return out.str();
}

std::string DayScene::BuildBenchmarkFinalFramePath() const {
  if (!g_config.flags.benchmarkFinalFrameDump)
    return {};

  std::filesystem::path outputDir;
  if (!g_config.benchmarkFinalFrameDir.empty()) {
    outputDir = g_config.benchmarkFinalFrameDir;
  } else if (IsBenchmarkMatrixActive() && !m_benchmarkMatrixReportPath.empty()) {
    std::filesystem::path reportPath(m_benchmarkMatrixReportPath);
    outputDir = reportPath.parent_path() / "final_frames";
  } else {
    outputDir = "benchmark_final_frames";
  }

  const char* apiTag = g_pBaseDriver
    ? t850::config::ApiTag(g_pBaseDriver->m_currentAPI)
    : t850::config::ApiTag(t850::config::ParseGraphicsApi(g_config.api, GraphicsApi::D3D11));
  int benchmarkWidth = (g_pBaseDriver && g_pBaseDriver->width > 0) ? g_pBaseDriver->width : g_config.width;
  int benchmarkHeight = (g_pBaseDriver && g_pBaseDriver->height > 0) ? g_pBaseDriver->height : g_config.height;
  bool offscreen = m_benchmarkActiveOffscreen;

  if (IsBenchmarkMatrixActive() && m_benchmarkMatrixRunIndex < m_benchmarkMatrixRuns.size()) {
    const BenchmarkMatrixRun& run = m_benchmarkMatrixRuns[m_benchmarkMatrixRunIndex];
    apiTag = run.apiTag.c_str();
    benchmarkWidth = run.width;
    benchmarkHeight = run.height;
    offscreen = run.offscreen;
  }

  std::ostringstream fileName;
  fileName << "dayscene_" << apiTag << "_"
           << benchmarkWidth << "x" << benchmarkHeight << "_"
           << (offscreen ? "offscreen" : "onscreen") << "_final";
  return (outputDir / fileName.str()).string();
}

std::string DayScene::CaptureBenchmarkFinalFrame() {
  if (!g_config.flags.benchmarkFinalFrameDump)
    return {};

  const std::string pathString = BuildBenchmarkFinalFramePath();
  if (pathString.empty())
    return {};

  auto* driver = pFramework ? pFramework->pVideoDriver : nullptr;
  if (!driver) {
    T8_LOG_ERROR("[BenchmarkFinalFrame] Cannot capture final frame: driver is null");
    return {};
  }

  std::filesystem::path path(pathString);
  std::filesystem::path actualPath(pathString + ".ppm");
  if (!actualPath.parent_path().empty()) {
    std::filesystem::create_directories(actualPath.parent_path());
  }

  driver->WaitForGPU();
  if (m_benchmarkActiveOffscreen) {
    if (m_benchmarkOffscreenOutputRT < 0) {
      T8_LOG_ERROR("[BenchmarkFinalFrame] Cannot capture offscreen final frame: offscreenRT=%d",
                   m_benchmarkOffscreenOutputRT);
      return {};
    }
    driver->SaveRTToFile(m_benchmarkOffscreenOutputRT, BaseDriver::COLOR0_ATTACHMENT, path.string());
  } else {
    driver->SaveScreenshot(path.string());
  }

  std::error_code ec;
  const bool exists = std::filesystem::exists(actualPath, ec);
  ec.clear();
  const auto writtenBytes = exists ? std::filesystem::file_size(actualPath, ec) : 0;
  if (exists && !ec && writtenBytes > 0) {
    T8_LOG_INFO("[BenchmarkFinalFrame] Wrote '%s'", actualPath.string().c_str());
    return actualPath.string();
  }

  T8_LOG_ERROR("[BenchmarkFinalFrame] Failed to write '%s'", actualPath.string().c_str());
  return {};
}

void DayScene::QueueBenchmarkFinish(float durationSecs) {
  m_benchmarkFinishPending = true;
  m_benchmarkPendingDurationSeconds = durationSecs;
}

void DayScene::WriteBenchmarkResults(float durationSecs) const {
  std::vector<double> sorted = m_benchmarkFrameTimesMs;
  std::sort(sorted.begin(), sorted.end());

  const size_t frameCount = m_benchmarkFrameTimesMs.size();
  const double totalMs = std::accumulate(m_benchmarkFrameTimesMs.begin(), m_benchmarkFrameTimesMs.end(), 0.0);
  const double averageMs = frameCount > 0 ? totalMs / static_cast<double>(frameCount) : 0.0;
  double variance = 0.0;
  for (double frameMs : m_benchmarkFrameTimesMs) {
    const double diff = frameMs - averageMs;
    variance += diff * diff;
  }
  const double stdDevMs = frameCount > 0 ? std::sqrt(variance / static_cast<double>(frameCount)) : 0.0;

  const std::string outputPath = BuildBenchmarkOutputPath();
  std::filesystem::path path(outputPath);
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path());
  }

  std::ofstream file(path, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    T8_LOG_ERROR("[Benchmark] Failed to open '%s'", outputPath.c_str());
    return;
  }

  const char* apiTag = g_pBaseDriver
    ? t850::config::ApiTag(g_pBaseDriver->m_currentAPI)
    : t850::config::ApiTag(t850::config::ParseGraphicsApi(g_config.api, GraphicsApi::D3D11));
  const int benchmarkWidth = (g_pBaseDriver && g_pBaseDriver->width > 0) ? g_pBaseDriver->width : g_config.width;
  const int benchmarkHeight = (g_pBaseDriver && g_pBaseDriver->height > 0) ? g_pBaseDriver->height : g_config.height;

  file << std::fixed << std::setprecision(4);
  file << "{\n";
  file << "  \"scene\": \"DayScene\",\n";
  file << "  \"api\": \"" << JsonEscape(apiTag) << "\",\n";
  file << "  \"mode\": \"" << (m_benchmarkActiveOffscreen ? "offscreen" : "onscreen") << "\",\n";
  file << "  \"resolution\": { \"width\": " << benchmarkWidth << ", \"height\": " << benchmarkHeight << " },\n";
  file << "  \"offscreen\": " << (m_benchmarkActiveOffscreen ? "true" : "false") << ",\n";
  file << "  \"cullingEnabled\": " << (SceneProp.FrustumCullingEnabled ? "true" : "false") << ",\n";
  file << "  \"finishReason\": \"" << (m_benchmarkTargetFrames > 0 ? "fixed_frame_count" : "wall_clock_duration") << "\",\n";
  file << "  \"splineLength\": " << (m_sceneSetup.splines.empty() ? 0.0f : m_sceneSetup.splines[0].m_totalLength) << ",\n";
  file << "  \"measuredDurationSeconds\": " << durationSecs << ",\n";
  file << "  \"frameCount\": " << frameCount << ",\n";
  file << "  \"simulation\": {\n";
  file << "    \"fixedDtSeconds\": " << DtSecs << ",\n";
  file << "    \"warmupFrames\": " << m_benchmarkWarmupFrames << ",\n";
  file << "    \"measuredFrames\": " << (m_benchmarkTargetFrames > 0 ? m_benchmarkTargetFrames : static_cast<int>(frameCount)) << ",\n";
  file << "    \"targetDurationSeconds\": " << m_benchmarkTargetDurationSeconds << ",\n";
  file << "    \"simulationFrame\": " << m_benchmarkSimulationFrame << "\n";
  file << "  },\n";
  file << "  \"statsMs\": {\n";
  file << "    \"average\": " << averageMs << ",\n";
  file << "    \"median\": " << Percentile(sorted, 50.0) << ",\n";
  file << "    \"min\": " << (sorted.empty() ? 0.0 : sorted.front()) << ",\n";
  file << "    \"max\": " << (sorted.empty() ? 0.0 : sorted.back()) << ",\n";
  file << "    \"stdDev\": " << stdDevMs << ",\n";
  file << "    \"p01\": " << Percentile(sorted, 1.0) << ",\n";
  file << "    \"p05\": " << Percentile(sorted, 5.0) << ",\n";
  file << "    \"p10\": " << Percentile(sorted, 10.0) << ",\n";
  file << "    \"p25\": " << Percentile(sorted, 25.0) << ",\n";
  file << "    \"p50\": " << Percentile(sorted, 50.0) << ",\n";
  file << "    \"p75\": " << Percentile(sorted, 75.0) << ",\n";
  file << "    \"p90\": " << Percentile(sorted, 90.0) << ",\n";
  file << "    \"p95\": " << Percentile(sorted, 95.0) << ",\n";
  file << "    \"p99\": " << Percentile(sorted, 99.0) << "\n";
  file << "  },\n";
  file << "  \"statsFps\": {\n";
  file << "    \"average\": " << (averageMs > 0.0 ? 1000.0 / averageMs : 0.0) << ",\n";
  file << "    \"median\": " << (Percentile(sorted, 50.0) > 0.0 ? 1000.0 / Percentile(sorted, 50.0) : 0.0) << ",\n";
  file << "    \"min\": " << (!sorted.empty() && sorted.back() > 0.0 ? 1000.0 / sorted.back() : 0.0) << ",\n";
  file << "    \"max\": " << (!sorted.empty() && sorted.front() > 0.0 ? 1000.0 / sorted.front() : 0.0) << ",\n";
  file << "    \"p01\": " << (Percentile(sorted, 99.0) > 0.0 ? 1000.0 / Percentile(sorted, 99.0) : 0.0) << ",\n";
  file << "    \"p05\": " << (Percentile(sorted, 95.0) > 0.0 ? 1000.0 / Percentile(sorted, 95.0) : 0.0) << ",\n";
  file << "    \"p95\": " << (Percentile(sorted, 5.0) > 0.0 ? 1000.0 / Percentile(sorted, 5.0) : 0.0) << ",\n";
  file << "    \"p99\": " << (Percentile(sorted, 1.0) > 0.0 ? 1000.0 / Percentile(sorted, 1.0) : 0.0) << "\n";
  file << "  },\n";
  const RenderMesh* benchmarkMesh = Meshes[0].pBase ? static_cast<const RenderMesh*>(Meshes[0].pBase) : nullptr;
  const unsigned long long cullSamples = m_benchmarkCullingTotals.samples;
  auto avgCounter = [&](unsigned long long value) -> double {
    return cullSamples > 0 ? static_cast<double>(value) / static_cast<double>(cullSamples) : 0.0;
  };
  auto latestCounter = [&](unsigned long long RenderMesh::*field) -> unsigned long long {
    return benchmarkMesh ? benchmarkMesh->*field : 0ull;
  };
  file << "  \"cullingStats\": {\n";
  file << "    \"samples\": " << cullSamples << ",\n";
  file << "    \"latest\": {\n";
  file << "      \"meshTests\": " << latestCounter(&RenderMesh::m_cullingMeshTests) << ",\n";
  file << "      \"subsetTests\": " << latestCounter(&RenderMesh::m_cullingSubsetTests) << ",\n";
  file << "      \"clusterTests\": " << latestCounter(&RenderMesh::m_cullingClusterTests) << ",\n";
  file << "      \"drawCalls\": " << latestCounter(&RenderMesh::m_drawCalls) << ",\n";
  file << "      \"renderStateChanges\": " << latestCounter(&RenderMesh::m_renderStateChanges) << ",\n";
  file << "      \"totalIndices\": " << latestCounter(&RenderMesh::m_totalIndices) << ",\n";
  file << "      \"drawnIndices\": " << latestCounter(&RenderMesh::m_drawnIndices) << ",\n";
  file << "      \"culledIndices\": " << latestCounter(&RenderMesh::m_culledIndices) << ",\n";
  file << "      \"cullingCpuMs\": " << (benchmarkMesh ? benchmarkMesh->m_cullingCpuMs : 0.0) << "\n";
  file << "    },\n";
  file << "    \"averagePerFrame\": {\n";
  file << "      \"meshTests\": " << avgCounter(m_benchmarkCullingTotals.meshTests) << ",\n";
  file << "      \"subsetTests\": " << avgCounter(m_benchmarkCullingTotals.subsetTests) << ",\n";
  file << "      \"clusterTests\": " << avgCounter(m_benchmarkCullingTotals.clusterTests) << ",\n";
  file << "      \"drawCalls\": " << avgCounter(m_benchmarkCullingTotals.drawCalls) << ",\n";
  file << "      \"renderStateChanges\": " << avgCounter(m_benchmarkCullingTotals.renderStateChanges) << ",\n";
  file << "      \"totalIndices\": " << avgCounter(m_benchmarkCullingTotals.totalIndices) << ",\n";
  file << "      \"drawnIndices\": " << avgCounter(m_benchmarkCullingTotals.drawnIndices) << ",\n";
  file << "      \"culledIndices\": " << avgCounter(m_benchmarkCullingTotals.culledIndices) << ",\n";
  file << "      \"cullingCpuMs\": " << (cullSamples > 0 ? m_benchmarkCullingTotals.cullingCpuMs / static_cast<double>(cullSamples) : 0.0) << "\n";
  file << "    },\n";
  file << "    \"totals\": {\n";
  file << "      \"meshTests\": " << m_benchmarkCullingTotals.meshTests << ",\n";
  file << "      \"subsetTests\": " << m_benchmarkCullingTotals.subsetTests << ",\n";
  file << "      \"clusterTests\": " << m_benchmarkCullingTotals.clusterTests << ",\n";
  file << "      \"drawCalls\": " << m_benchmarkCullingTotals.drawCalls << ",\n";
  file << "      \"renderStateChanges\": " << m_benchmarkCullingTotals.renderStateChanges << ",\n";
  file << "      \"totalIndices\": " << m_benchmarkCullingTotals.totalIndices << ",\n";
  file << "      \"drawnIndices\": " << m_benchmarkCullingTotals.drawnIndices << ",\n";
  file << "      \"culledIndices\": " << m_benchmarkCullingTotals.culledIndices << ",\n";
  file << "      \"cullingCpuMs\": " << m_benchmarkCullingTotals.cullingCpuMs << "\n";
  file << "    }\n";
  file << "  },\n";
  file << "  \"frameTimesMs\": [";
  for (size_t i = 0; i < m_benchmarkFrameTimesMs.size(); ++i) {
    if (i > 0) file << ", ";
    file << m_benchmarkFrameTimesMs[i];
  }
  file << "]\n";
  file << "}\n";
  T8_LOG_INFO("[Benchmark] Wrote %zu frame samples to '%s'", frameCount, outputPath.c_str());
}

void DayScene::InitializeBenchmarkMatrix() {
  if (m_benchmarkMatrixInitialized || !g_config.flags.benchmarkMatrix) {
    return;
  }

  m_benchmarkMatrixRuns.clear();
  m_benchmarkMatrixResults.clear();
  m_benchmarkMatrixRunIndex = 0;
  m_benchmarkFinished = false;
  if (g_config.benchmarkDurationSeconds <= 0 && g_config.benchmarkFrameLimit <= 0) {
    g_config.benchmarkDurationSeconds = kBenchmarkMatrixDurationSeconds;
  }

  std::vector<BenchmarkMatrixRun> runs;
#ifdef OS_ANDROID
  const std::vector<std::pair<t850::GraphicsApi::E, std::string>> apis = {
    {t850::GraphicsApi::VULKAN, "vulkan"}
  };
#else
  const std::vector<std::pair<t850::GraphicsApi::E, std::string>> apis = {
    {t850::GraphicsApi::D3D11, "d3d11"},
    {t850::GraphicsApi::D3D12, "d3d12"},
    {t850::GraphicsApi::VULKAN, "vulkan"},
    {t850::GraphicsApi::OPENGL, "gl"}
  };
#endif
  const std::vector<std::pair<int, int>> resolutions = {
    {1920, 1080},
    {2560, 1440},
    {3840, 2160}
  };
  const bool modes[] = {false, true};

  if (!g_config.benchmarkReportPath.empty()) {
    m_benchmarkMatrixReportPath = g_config.benchmarkReportPath;
  } else {
    std::ostringstream report;
    report << "benchmark_reports/dayscene_matrix_" << TimestampForFilename() << "/DayScene_Benchmark_Report.md";
    m_benchmarkMatrixReportPath = report.str();
  }
  const std::filesystem::path reportPath(m_benchmarkMatrixReportPath);
  const std::filesystem::path outputDir = reportPath.parent_path().empty()
    ? std::filesystem::path(".")
    : reportPath.parent_path();

  for (const auto& api : apis) {
    for (const auto& resolution : resolutions) {
      for (bool offscreen : modes) {
        BenchmarkMatrixRun run;
        run.api = api.first;
        run.apiTag = api.second;
        run.width = resolution.first;
        run.height = resolution.second;
        run.offscreen = offscreen;
        std::ostringstream fileName;
        fileName << "dayscene_" << run.apiTag << "_" << run.width << "x" << run.height
                 << "_" << (run.offscreen ? "offscreen" : "onscreen") << ".json";
        run.outputPath = (outputDir / fileName.str()).string();
        runs.push_back(run);
      }
    }
  }

  m_benchmarkMatrixRuns = std::move(runs);
  m_benchmarkMatrixInitialized = true;
  if (!m_benchmarkMatrixRuns.empty()) {
    ApplyBenchmarkMatrixRun(0, false);
  }
}

bool DayScene::IsBenchmarkMatrixActive() const {
  return g_config.flags.benchmarkMatrix && m_benchmarkMatrixInitialized && !m_benchmarkMatrixRuns.empty();
}

void DayScene::RebindRenderGraphOutputs() {
  GBufferPass      = m_renderGraph.GetRTHandle("GBuffer");
  DeferredPass     = m_renderGraph.GetRTHandle("Deferred");
  Extra16FPass     = m_renderGraph.GetRTHandle("Extra16F");
  DepthPass        = m_renderGraph.GetRTHandle("DepthPass");
  ShadowAccumPass  = m_renderGraph.GetRTHandle("ShadowAccum");
  ExtraHelperPass  = m_renderGraph.GetRTHandle("ExtraHelper");
  BloomAccumPass   = m_renderGraph.GetRTHandle("BloomAccum");
  BrightPassPass   = m_renderGraph.GetRTHandle("BrightPass");
  GodRaysCalcPass  = m_renderGraph.GetRTHandle("GodRaysCalc");
  GodRaysCalcExtraPass = m_renderGraph.GetRTHandle("GodRaysCalcExtra");
  AdaptedLumCurrentPass = m_renderGraph.GetRTHandle("AdaptedLumCurrent");
  AdaptedLumPrevPass = m_renderGraph.GetRTHandle("AdaptedLumPrev");
  CoCPass          = m_renderGraph.GetRTHandle("CoC");
  CombineCoCPass   = m_renderGraph.GetRTHandle("CombineCoC");
  CoCHelperPass    = m_renderGraph.GetRTHandle("CoCHelper");
  CoCHelperPass2   = m_renderGraph.GetRTHandle("CoCHelper2");

  auto* driver = pFramework ? pFramework->pVideoDriver : nullptr;
  if (driver && GBufferPass >= 0 && GBufferPass < static_cast<int>(driver->RTs.size()) && driver->RTs[GBufferPass]) {
    auto* gbuffer = driver->RTs[GBufferPass];
    for (int slot = 0; slot < 4; ++slot) {
      Quads[0].SetTexture(slot < static_cast<int>(gbuffer->vColorTextures.size()) ? gbuffer->vColorTextures[slot] : nullptr, slot);
    }
    Quads[0].SetTexture(gbuffer->pDepthTexture, 4);
  } else {
    T8_LOG_ERROR("[DayScene] Cannot bind GBuffer textures, handle=%d", GBufferPass);
  }
  Quads[0].SetEnvironmentMap((g_pBaseDriver && EnvMapTexIndex >= 0) ? g_pBaseDriver->GetTexture(EnvMapTexIndex) : nullptr);
  T8_LOG_INFO("[DayScene] Rebound render graph outputs: gbuffer=%d deferred=%d extra=%d depth=%d env=%d",
              GBufferPass, DeferredPass, Extra16FPass, DepthPass, EnvMapTexIndex);
}

void DayScene::ResetSceneStateForBenchmarkRun() {
  t850::Texture* preservedSSAONoise = SceneProp.SSAOKernel.NoiseTex;
  SceneProp = SceneProps{};
  SceneProp.SSAOKernel.NoiseTex = preservedSSAONoise;
  VP.Identity();
  if (!m_sceneSetup.Load("Scenes/DayScene.json")) {
    T8_LOG_ERROR("[BenchmarkMatrix] Failed to reload DayScene descriptor for benchmark reset");
    return;
  }
  if (m_sceneSetup.cameras.size() < 2 && !m_sceneSetup.cameras.empty()) {
    Camera& mainCam = m_sceneSetup.cameras[0];
    Camera spectator;
    XVECTOR3 spectatorPos(mainCam.Eye.x, mainCam.Eye.y + 20.0f, mainCam.Eye.z - 60.0f, 1.0f);
    if (mainCam.Ortho)
      spectator.InitOrtho(spectatorPos, mainCam.Width, mainCam.Height, mainCam.NPlane, mainCam.FPlane, mainCam.LeftHanded);
    else
      spectator.InitPerspective(spectatorPos, mainCam.Fov, mainCam.AspectRatio, mainCam.NPlane, mainCam.FPlane, mainCam.LeftHanded);
    spectator.Speed = mainCam.Speed;
    spectator.Pitch = mainCam.Pitch;
    spectator.Roll = mainCam.Roll;
    spectator.Yaw = mainCam.Yaw;
    spectator.Update(0.0f);
    m_sceneSetup.cameras.push_back(spectator);
  }
  for (auto& selector : m_sceneSetup.descriptor.selectors) {
    if (selector.name == "active_camera") {
      selector.options = {"Spline", "Free"};
      selector.default_index = 0;
      break;
    }
  }
  m_sceneSetup.Apply(SceneProp);
  if (!SceneProp.SSAOKernel.NoiseTex) {
    SceneProp.SSAOKernel.InitTexture();
  }
  ActiveCam = m_sceneSetup.GetCamera(0);
  SceneProp.pCullingCamera = ActiveCam;
  SceneProp.FrustumCullingToggleAllowed = g_config.cullingLoadMode != t850::Config::CullingLoadMode::Disabled;
  SceneProp.FrustumCullingEnabled = g_config.cullingLoadMode == t850::Config::CullingLoadMode::FullOnLoad;
  ChangeActiveGaussSelection = SHADOW_KERNEL;
  m_debugRTSelection = 0;
  m_showSpline = false;
  m_showLights = false;
  m_showPhysics = false;
#ifdef OS_ANDROID
  ResetAndroidVirtualControls();
#endif
  m_spectatorCameraEnabled = false;
  m_activeCameraIndex = 0;
  PrimitiveMgr.SetVP(&VP);
  PrimitiveMgr.SetSceneProps(&SceneProp);
  if (!m_sceneSetup.splines.empty()) {
    if (splineWire) {
      splineWire->m_spline = &m_sceneSetup.splines[0];
    }
    t850::Spline& spline = m_sceneSetup.splines[0];
    t850::SplineAgent& agent = m_sceneSetup.agents[0];
    agent.m_actualPoint = spline.GetPoint(spline.GetNormalizedOffset(0));
    const int attachedCamera = m_sceneSetup.descriptor.splines[0].attached_camera;
    if (Camera* splineCamera = m_sceneSetup.GetCamera(attachedCamera)) {
      splineCamera->AttachAgent(agent);
      splineCamera->m_lookAtCenter = false;
    }
  }
  ApplyActiveCameraSelection(m_activeCameraIndex);
  FrameDumperConfig dumpCfg;
  dumpCfg.dumpEnabled = g_config.flags.dumpEnabled;
  dumpCfg.dumpByFrame = g_config.flags.dumpByFrame;
  dumpCfg.dumpFrame = g_config.dumpFrame;
  dumpCfg.dumpSeconds = g_config.dumpSeconds;
  dumpCfg.debugFrames = g_config.flags.debugFrames;
  dumpCfg.keepRunning = g_config.flags.keepRunning;
  dumpCfg.replaySnapshotPath = g_config.replaySnapshotPath;
  dumpCfg.sceneIndex = g_config.startScene;
  m_dumper.Init(dumpCfg);
  ResetBenchmarkRunCapture();
}

void DayScene::ResetBenchmarkRunCapture() {
  m_tourTimeSec = 0.0f;
  m_benchmarkFrameTimesMs.clear();
  m_benchmarkCullingTotals = BenchmarkCullingTotals{};
  m_benchmarkFinished = false;
  m_benchmarkFinishPending = false;
  m_benchmarkPendingDurationSeconds = 0.0f;
  m_benchmarkTargetDurationSeconds = 0.0f;
  m_benchmarkTimelineElapsedSeconds = 0.0f;
  m_benchmarkTimelineReachedEnd = false;
  m_benchmarkWallClockStarted = false;
  m_benchmarkWallClockStart = {};
  m_benchmarkSimulationFrame = 0;
  m_benchmarkWarmupFrames = kBenchmarkDefaultWarmupFrames;
  if (g_config.benchmarkFrameLimit > 0) {
    m_benchmarkTargetFrames = g_config.benchmarkFrameLimit;
  } else if (g_config.benchmarkDurationSeconds > 0) {
    m_benchmarkTargetDurationSeconds = static_cast<float>(g_config.benchmarkDurationSeconds);
    m_benchmarkTargetFrames = 0;
  } else {
    m_benchmarkTargetFrames = kBenchmarkDefaultMeasuredFrames;
  }
  T8_LOG_INFO("[Benchmark] Reset targetFrames=%d targetSeconds=%.2f warmupFrames=%d durationSeconds=%d frameLimit=%d fixedDt=%.6f",
              m_benchmarkTargetFrames, m_benchmarkTargetDurationSeconds, m_benchmarkWarmupFrames, g_config.benchmarkDurationSeconds,
              g_config.benchmarkFrameLimit,
              g_config.benchmarkFixedDt > 0.0f ? g_config.benchmarkFixedDt : kBenchmarkFixedDeltaSeconds);
  m_benchmarkPendingFinalFramePath.clear();
}

void DayScene::ApplyBenchmarkMatrixRun(std::size_t runIndex, bool recreateRenderer) {
  if (runIndex >= m_benchmarkMatrixRuns.size()) {
    return;
  }

  m_benchmarkMatrixRunIndex = runIndex;
  const BenchmarkMatrixRun& run = m_benchmarkMatrixRuns[m_benchmarkMatrixRunIndex];
  g_config.flags.benchmark = true;
  g_config.flags.benchmarkMatrix = true;
  g_config.api = run.apiTag;
  g_config.width = run.width;
  g_config.height = run.height;
  m_benchmarkActiveOffscreen = run.offscreen;
  g_config.flags.offscreen = false;
  g_config.benchmarkOutputPath = run.outputPath;
  g_config.startScene = 1;
  if (pFramework) {
    pFramework->aplicationDescriptor.api = run.api;
    pFramework->aplicationDescriptor.width = run.width;
    pFramework->aplicationDescriptor.height = run.height;
  }
  ResetBenchmarkRunCapture();
  m_benchmarkStatus = "Running " + run.apiTag + " " +
      std::to_string(run.width) + "x" + std::to_string(run.height) + " " +
      (run.offscreen ? "offscreen" : "onscreen");
  T8_LOG_INFO("[BenchmarkMatrix] %zu/%zu %s", m_benchmarkMatrixRunIndex + 1, m_benchmarkMatrixRuns.size(), m_benchmarkStatus.c_str());

  if (recreateRenderer && pFramework) {
    const bool sameApi = pFramework->pVideoDriver && pFramework->pVideoDriver->m_currentAPI == run.api;
    if (sameApi) {
      ResetBenchmarkSameApiRun();
    } else {
      pFramework->ChangeAPI(run.api);
    }
  }
}

void DayScene::ResetBenchmarkSameApiRun() {
  auto* driver = pFramework ? pFramework->pVideoDriver : nullptr;
  if (!driver) {
    return;
  }

  T8_LOG_INFO("[BenchmarkMatrix] Same-API reset begin api=%s size=%dx%d offscreen=%d",
              g_config.api.c_str(), g_config.width, g_config.height, m_benchmarkActiveOffscreen ? 1 : 0);
  driver->WaitForGPU();
  driver->DestroyOffscreenTargets();
  m_renderGraph.DestroyRenderTargets(driver);
  if (m_benchmarkOffscreenOutputRT >= 0) {
    driver->DestroyRT(m_benchmarkOffscreenOutputRT);
    m_benchmarkOffscreenOutputRT = -1;
  }
  if (driver->width != g_config.width || driver->height != g_config.height) {
#ifdef OS_WINDOWS
    bool resized = false;
    if (auto* win32 = dynamic_cast<t850::Win32Framework*>(pFramework)) {
      resized = win32->ResizeApplicationWindow(g_config.width, g_config.height);
    }
    if (!resized) {
#else
    {
#endif
      const bool resizedSwapchain = driver->ResizeSwapchain(g_config.width, g_config.height);
      if (!resizedSwapchain) {
      T8_LOG_INFO("[BenchmarkMatrix] ResizeSwapchain unavailable/failed; updating driver dimensions only");
      driver->SetDimensions(g_config.width, g_config.height);
      }
    }
  }

  ResetSceneStateForBenchmarkRun();
  m_renderGraph = t850::RenderGraph{};
  if (!m_renderGraph.Load("Scenes/DayScene_RenderGraph.json")) {
    T8_LOG_ERROR("[BenchmarkMatrix] Failed to reload render graph for benchmark reset");
    return;
  }
  LoadSceneProfile();
  m_renderGraph.CreateRenderTargets(driver, SceneProp, g_config.width, g_config.height);
  RebindRenderGraphOutputs();

  if (m_benchmarkActiveOffscreen) {
    m_benchmarkOffscreenOutputRT = driver->CreateRT(1, BaseRT::RGBA8, BaseRT::F32, g_config.width, g_config.height, false);
    if (m_benchmarkOffscreenOutputRT < 0) {
      T8_LOG_ERROR("[BenchmarkMatrix] Failed to create explicit offscreen output RT %dx%d", g_config.width, g_config.height);
    }
  }

  const bool dofOn = SceneProp.ToogleDOF != 0;
  m_renderGraph.SetPassEnabled("CoC", dofOn);
  m_renderGraph.SetPassEnabled("Combine CoC", dofOn);
  m_renderGraph.SetPassEnabled("DOF", dofOn);
  m_renderGraph.SetPassEnabled("DOF 2", dofOn);
  Meshes[0].SetParallaxEnabled(SceneProp.ToogleParallax != 0);
  Meshes[0].SetParallaxShadowEnabled(SceneProp.ToogleParallaxShadow != 0);
  Meshes[0].SetParallaxSettings(SceneProp.ParallaxLowSamples, SceneProp.ParallaxHighSamples, SceneProp.ParallaxHeight);
  Meshes[0].SetParallaxShadowSettings(SceneProp.ParallaxShadowMinLayers, SceneProp.ParallaxShadowMaxLayers,
                                       SceneProp.ParallaxShadowSoftness, SceneProp.ParallaxShadowStrength);
  T8_LOG_INFO("[BenchmarkMatrix] Same-API reset complete gbuffer=%d deferred=%d offscreenRT=%d textures=%zu rts=%zu",
              GBufferPass, DeferredPass, m_benchmarkOffscreenOutputRT, driver->Textures.size(), driver->RTs.size());
}

void DayScene::FinishBenchmarkRun(float durationSecs) {
  const std::string finalFramePath = m_benchmarkPendingFinalFramePath;
  m_benchmarkFinishPending = false;
  m_benchmarkPendingDurationSeconds = 0.0f;
  m_benchmarkPendingFinalFramePath.clear();
  WriteBenchmarkResults(durationSecs);

  std::vector<double> sorted = m_benchmarkFrameTimesMs;
  std::sort(sorted.begin(), sorted.end());
  const double totalMs = std::accumulate(m_benchmarkFrameTimesMs.begin(), m_benchmarkFrameTimesMs.end(), 0.0);
  const double averageMs = m_benchmarkFrameTimesMs.empty() ? 0.0 : totalMs / static_cast<double>(m_benchmarkFrameTimesMs.size());

  if (IsBenchmarkMatrixActive()) {
    const BenchmarkMatrixRun& run = m_benchmarkMatrixRuns[m_benchmarkMatrixRunIndex];
    BenchmarkMatrixResult result;
    result.run = run;
    result.averageMs = averageMs;
    result.medianMs = Percentile(sorted, 50.0);
    result.averageFps = averageMs > 0.0 ? 1000.0 / averageMs : 0.0;
    result.medianFps = result.medianMs > 0.0 ? 1000.0 / result.medianMs : 0.0;
    result.minFps = !sorted.empty() && sorted.back() > 0.0 ? 1000.0 / sorted.back() : 0.0;
    result.maxFps = !sorted.empty() && sorted.front() > 0.0 ? 1000.0 / sorted.front() : 0.0;
    result.frameCount = static_cast<int>(m_benchmarkFrameTimesMs.size());
    result.durationSeconds = durationSecs;
    result.finalFramePath = finalFramePath;
    m_benchmarkMatrixResults.push_back(result);

    const std::size_t nextRun = m_benchmarkMatrixRunIndex + 1;
    if (nextRun < m_benchmarkMatrixRuns.size()) {
      ApplyBenchmarkMatrixRun(nextRun, true);
      return;
    }

    WriteBenchmarkMatrixReport();
    m_benchmarkFinished = true;
    m_benchmarkStatus = "Benchmark matrix complete: " + m_benchmarkMatrixReportPath;
    return;
  }

  m_benchmarkFinished = true;
  m_benchmarkStatus = "Benchmark complete: " + BuildBenchmarkOutputPath();
}

void DayScene::WriteBenchmarkMatrixReport() const {
  if (m_benchmarkMatrixReportPath.empty()) {
    return;
  }
  std::filesystem::path path(m_benchmarkMatrixReportPath);
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path());
  }
  std::ofstream file(path, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    T8_LOG_ERROR("[BenchmarkMatrix] Failed to open report '%s'", m_benchmarkMatrixReportPath.c_str());
    return;
  }
  double maxFps = 10.0;
  for (const BenchmarkMatrixResult& result : m_benchmarkMatrixResults) {
    maxFps = (std::max)(maxFps, result.averageFps);
  }
  maxFps = std::ceil(maxFps / 10.0) * 10.0;

  file << "# DayScene Benchmark Report\n\n";
  file << "## Average FPS Chart\n\n";
  file << "```mermaid\n";
  file << "xychart-beta\n";
  file << "  title \"DayScene Average FPS\"\n";
  file << "  x-axis [";
  for (std::size_t i = 0; i < m_benchmarkMatrixResults.size(); ++i) {
    const auto& result = m_benchmarkMatrixResults[i];
    if (i > 0) file << ", ";
    file << "\"" << result.run.apiTag << " " << result.run.width << "x" << result.run.height << " "
         << (result.run.offscreen ? "offscreen" : "onscreen") << "\"";
  }
  file << "]\n";
  file << "  y-axis \"FPS\" 0 --> " << static_cast<int>(maxFps) << "\n";
  file << "  bar [";
  for (std::size_t i = 0; i < m_benchmarkMatrixResults.size(); ++i) {
    if (i > 0) file << ", ";
    file << std::fixed << std::setprecision(2) << m_benchmarkMatrixResults[i].averageFps;
  }
  file << "]\n";
  file << "```\n\n";
  file << "| API | Mode | Resolution | Avg FPS | Median FPS | Min FPS | Max FPS | Avg ms | Median ms | Frames | Duration s | JSON | Final frame |\n";
  file << "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|\n";
  for (const BenchmarkMatrixResult& result : m_benchmarkMatrixResults) {
    std::string finalFrameCell = "-";
    if (!result.finalFramePath.empty()) {
      const std::filesystem::path finalFramePath(result.finalFramePath);
      std::string finalFrameLink = finalFramePath.generic_string();
      if (!path.parent_path().empty()) {
        const std::filesystem::path relativePath = finalFramePath.lexically_relative(path.parent_path());
        if (!relativePath.empty())
          finalFrameLink = relativePath.generic_string();
      }
      finalFrameCell = "[" + finalFramePath.filename().string() + "](" + finalFrameLink + ")";
    }
    file << "| " << result.run.apiTag << " | " << (result.run.offscreen ? "offscreen" : "onscreen")
         << " | " << result.run.width << "x" << result.run.height
         << " | " << result.averageFps
         << " | " << result.medianFps
         << " | " << result.minFps
         << " | " << result.maxFps
         << " | " << result.averageMs
         << " | " << result.medianMs
         << " | " << result.frameCount
         << " | " << result.durationSeconds
         << " | [" << std::filesystem::path(result.run.outputPath).filename().string()
         << "](" << std::filesystem::path(result.run.outputPath).filename().string() << ")"
         << " | " << finalFrameCell << " |\n";
  }
  T8_LOG_INFO("[BenchmarkMatrix] Wrote report '%s'", m_benchmarkMatrixReportPath.c_str());
}

void DayScene::DrawBenchmarkMatrixGui(t850::DevGuiContext& gui) {
  (void)gui;
  if (!g_config.flags.benchmark || (!IsBenchmarkMatrixActive() && m_benchmarkStatus.empty())) {
    return;
  }

  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2((std::min)(720.0f, io.DisplaySize.x - 48.0f), 0.0f), ImGuiCond_Always);
  const bool displayOffscreenMode = m_benchmarkActiveOffscreen;
  ImGui::SetNextWindowBgAlpha(displayOffscreenMode ? 0.92f : 0.72f);
  if (displayOffscreenMode) {
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0.0f, 0.0f), io.DisplaySize, IM_COL32(0, 0, 0, 255));
  }
  if (!ImGui::Begin("DayScene Benchmark", nullptr,
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::End();
    return;
  }

  float runProgress = 0.0f;
  const int totalRunFrames = (std::max)(1, m_benchmarkWarmupFrames + m_benchmarkTargetFrames);
  if (m_benchmarkTargetDurationSeconds > 0.0f) {
    const float elapsedSecs = m_benchmarkWallClockStarted
        ? std::chrono::duration<float>(std::chrono::steady_clock::now() - m_benchmarkWallClockStart).count()
        : 0.0f;
    runProgress = std::clamp(elapsedSecs / m_benchmarkTargetDurationSeconds, 0.0f, 1.0f);
  } else {
    runProgress = std::clamp(static_cast<float>(m_benchmarkSimulationFrame) / static_cast<float>(totalRunFrames), 0.0f, 1.0f);
  }
  const float matrixProgress = IsBenchmarkMatrixActive()
      ? (static_cast<float>(m_benchmarkMatrixRunIndex) + runProgress) / static_cast<float>(m_benchmarkMatrixRuns.size())
      : runProgress;

  ImGui::TextWrapped("%s", m_benchmarkStatus.empty() ? "Running benchmark..." : m_benchmarkStatus.c_str());
  if (m_benchmarkTargetDurationSeconds > 0.0f) {
    ImGui::Text("Elapsed: %.2f / %.1f sec  Warmup: %d frames  Measured frames: %zu  dt: %.6f",
                m_benchmarkTimelineElapsedSeconds,
                m_benchmarkTargetDurationSeconds,
                m_benchmarkWarmupFrames,
                m_benchmarkFrameTimesMs.size(),
                DtSecs);
  } else {
    ImGui::Text("Frame: %d / %d  Warmup: %d  Measured: %zu / %d  dt: %.6f",
                m_benchmarkSimulationFrame,
                totalRunFrames,
                m_benchmarkWarmupFrames,
                m_benchmarkFrameTimesMs.size(),
                m_benchmarkTargetFrames,
                DtSecs);
  }
  if (IsBenchmarkMatrixActive()) {
    const BenchmarkMatrixRun& run = m_benchmarkMatrixRuns[m_benchmarkMatrixRunIndex];
    ImGui::Text("Run %zu / %zu", m_benchmarkMatrixRunIndex + 1, m_benchmarkMatrixRuns.size());
    ImGui::Text("API: %s  Resolution: %dx%d  Mode: %s",
                run.apiTag.c_str(), run.width, run.height, run.offscreen ? "offscreen" : "onscreen");
  }
  ImGui::ProgressBar(runProgress, ImVec2(-1.0f, 0.0f), "Current run");
  if (IsBenchmarkMatrixActive()) {
    ImGui::ProgressBar(matrixProgress, ImVec2(-1.0f, 0.0f), "Matrix");
  }
  if (m_benchmarkFinished && !m_benchmarkMatrixReportPath.empty()) {
    ImGui::TextWrapped("Report: %s", m_benchmarkMatrixReportPath.c_str());
  }
  if (!m_benchmarkMatrixResults.empty()) {
    ImGui::SeparatorText("Results");
    double maxFps = 1.0;
    for (const BenchmarkMatrixResult& result : m_benchmarkMatrixResults) {
      maxFps = (std::max)(maxFps, result.averageFps);
    }
    for (const BenchmarkMatrixResult& result : m_benchmarkMatrixResults) {
      const float normalized = static_cast<float>(result.averageFps / maxFps);
      const std::string label =
          result.run.apiTag + " " +
          std::to_string(result.run.width) + "x" + std::to_string(result.run.height) + " " +
          (result.run.offscreen ? "offscreen" : "onscreen");
      ImGui::Text("%s  avg %.2f fps  med %.2f  min %.2f  max %.2f",
                  label.c_str(),
                  result.averageFps,
                  result.medianFps,
                  result.minFps,
                  result.maxFps);
      ImGui::ProgressBar(normalized, ImVec2(-1.0f, 0.0f), "");
    }
  }
  ImGui::End();
}

void DayScene::ApplyActiveCameraSelection(int selection) {
  Camera* mainCam = m_sceneSetup.GetCamera(0);
  Camera* spectatorCam = m_sceneSetup.GetCamera(1);
  if (!mainCam)
    return;

  const int maxSelection = 1;
  if (selection < 0)
    selection = 0;
  if (selection > maxSelection)
    selection = maxSelection;

  m_activeCameraIndex = selection;
#ifdef OS_ANDROID
  if (m_activeCameraIndex != 1) {
    ResetAndroidVirtualControls();
  }
#endif
  if (m_activeCameraIndex == 0) {
    if (t850::SplineAgent* agent = m_sceneSetup.GetAgent(0)) {
      mainCam->AttachAgent(*agent);
      mainCam->m_lookAtCenter = false;
    }
  } else if (m_activeCameraIndex == 1) {
    mainCam->DettachAgent();
    mainCam->m_externalControl = false;
  }

  ActiveCam = m_spectatorCameraEnabled && spectatorCam ? spectatorCam : mainCam;
  SceneProp.pCullingCamera = mainCam;
  if (!SceneProp.pCameras.empty())
    SceneProp.pCameras[0] = ActiveCam;
  mainCam->Update(0.0f);
  if (ActiveCam) {
    ActiveCam->Update(0.0f);
    VP = ActiveCam->VP;
  }
}

void DayScene::SetSpectatorCameraEnabled(bool enabled) {
  Camera* mainCam = m_sceneSetup.GetCamera(0);
  Camera* spectatorCam = m_sceneSetup.GetCamera(1);
  if (!mainCam)
    return;

  m_spectatorCameraEnabled = enabled && spectatorCam;
  ActiveCam = m_spectatorCameraEnabled ? spectatorCam : mainCam;

  if (spectatorCam) {
    spectatorCam->DettachAgent();
    spectatorCam->m_externalControl = false;
  }

  SceneProp.pCullingCamera = mainCam;
  if (!SceneProp.pCameras.empty())
    SceneProp.pCameras[0] = ActiveCam;
  if (ActiveCam) {
    ActiveCam->Update(0.0f);
    VP = ActiveCam->VP;
  }
}

void DayScene::SetSpectatorDebugEnabled(bool enabled) {
  SetSpectatorCameraEnabled(enabled);
  if (!m_spectatorCameraEnabled) {
    ApplyActiveCameraSelection(m_activeCameraIndex);
  }
  m_showCullStats = m_spectatorCameraEnabled;
  SceneProp.ShowCullingDebug = m_showCullStats;
}

#ifdef OS_ANDROID
bool DayScene::AndroidVirtualControlsVisible() const {
  return m_activeCameraIndex == 1 && ActiveCam != nullptr;
}

bool DayScene::AndroidVirtualControlsActive() const {
  return m_androidMovePointerId >= 0 || m_androidLookPointerId >= 0 ||
         m_androidUpPointerId >= 0 || m_androidDownPointerId >= 0;
}

void DayScene::ResetAndroidVirtualControls() {
  m_androidMovePointerId = -1;
  m_androidLookPointerId = -1;
  m_androidUpPointerId = -1;
  m_androidDownPointerId = -1;
  m_androidMoveAxis = XVECTOR2(0.0f, 0.0f);
  m_androidLookAxis = XVECTOR2(0.0f, 0.0f);
  m_androidMoveUp = false;
  m_androidMoveDown = false;
}

bool DayScene::HandleAndroidVirtualControls(AInputEvent* event) {
  if (!event || AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) {
    return false;
  }

  if (!AndroidVirtualControlsVisible()) {
    ResetAndroidVirtualControls();
    return false;
  }

  ImGuiIO& io = ImGui::GetIO();
  float width = io.DisplaySize.x;
  float height = io.DisplaySize.y;
  if (pFramework && pFramework->pVideoDriver) {
    if (pFramework->pVideoDriver->width > 0) {
      width = static_cast<float>(pFramework->pVideoDriver->width);
    }
    if (pFramework->pVideoDriver->height > 0) {
      height = static_cast<float>(pFramework->pVideoDriver->height);
    }
  }
  if (width <= 0.0f || height <= 0.0f) {
    return false;
  }

  const AndroidVirtualControlsLayout layout = BuildAndroidVirtualControlsLayout(width, height);
  const int32_t rawAction = AMotionEvent_getAction(event);
  const int32_t action = rawAction & AMOTION_EVENT_ACTION_MASK;
  int32_t actionPointerIndex =
      (rawAction & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
  const size_t pointerCount = AMotionEvent_getPointerCount(event);
  if (pointerCount == 0) {
    ResetAndroidVirtualControls();
    return false;
  }
  if (actionPointerIndex < 0 || actionPointerIndex >= static_cast<int32_t>(pointerCount)) {
    actionPointerIndex = 0;
  }

  auto resetPointer = [&](int pointerId) {
    if (pointerId == m_androidMovePointerId) {
      m_androidMovePointerId = -1;
      m_androidMoveAxis = XVECTOR2(0.0f, 0.0f);
    }
    if (pointerId == m_androidLookPointerId) {
      m_androidLookPointerId = -1;
      m_androidLookAxis = XVECTOR2(0.0f, 0.0f);
    }
    if (pointerId == m_androidUpPointerId) {
      m_androidUpPointerId = -1;
      m_androidMoveUp = false;
    }
    if (pointerId == m_androidDownPointerId) {
      m_androidDownPointerId = -1;
      m_androidMoveDown = false;
    }
  };

  if (action == AMOTION_EVENT_ACTION_CANCEL) {
    ResetAndroidVirtualControls();
    return true;
  }

  if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP) {
    const int pointerId = AMotionEvent_getPointerId(event, actionPointerIndex);
    if (action == AMOTION_EVENT_ACTION_UP) {
      ResetAndroidVirtualControls();
      return true;
    }
    resetPointer(pointerId);
    return true;
  }

  auto capturePointer = [&](int pointerIndex) {
    const int pointerId = AMotionEvent_getPointerId(event, pointerIndex);
    const float x = AMotionEvent_getX(event, pointerIndex);
    const float y = AMotionEvent_getY(event, pointerIndex);

    if (m_androidUpPointerId < 0 && PointInsideCircle(x, y, layout.upCenter, layout.buttonRadius * 1.2f)) {
      m_androidUpPointerId = pointerId;
      m_androidMoveUp = true;
      return true;
    }
    if (m_androidDownPointerId < 0 && PointInsideCircle(x, y, layout.downCenter, layout.buttonRadius * 1.2f)) {
      m_androidDownPointerId = pointerId;
      m_androidMoveDown = true;
      return true;
    }
    if (m_androidMovePointerId < 0 && PointInsideCircle(x, y, layout.moveCenter, layout.stickRadius * 1.45f)) {
      m_androidMovePointerId = pointerId;
      m_androidMoveAxis = StickAxisFromPoint(x, y, layout.moveCenter, layout.stickRadius);
      return true;
    }
    if (m_androidLookPointerId < 0 && PointInsideCircle(x, y, layout.lookCenter, layout.stickRadius * 1.45f)) {
      m_androidLookPointerId = pointerId;
      m_androidLookAxis = StickAxisFromPoint(x, y, layout.lookCenter, layout.stickRadius);
      return true;
    }
    return false;
  };

  if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
    capturePointer(actionPointerIndex);
    return true;
  }

  if (action != AMOTION_EVENT_ACTION_MOVE) {
    return true;
  }

  const int moveIndex = FindPointerIndexById(event, m_androidMovePointerId);
  if (moveIndex >= 0) {
    m_androidMoveAxis = StickAxisFromPoint(AMotionEvent_getX(event, moveIndex),
                                           AMotionEvent_getY(event, moveIndex),
                                           layout.moveCenter,
                                           layout.stickRadius);
  } else {
    m_androidMovePointerId = -1;
    m_androidMoveAxis = XVECTOR2(0.0f, 0.0f);
  }

  const int lookIndex = FindPointerIndexById(event, m_androidLookPointerId);
  if (lookIndex >= 0) {
    m_androidLookAxis = StickAxisFromPoint(AMotionEvent_getX(event, lookIndex),
                                           AMotionEvent_getY(event, lookIndex),
                                           layout.lookCenter,
                                           layout.stickRadius);
  } else {
    m_androidLookPointerId = -1;
    m_androidLookAxis = XVECTOR2(0.0f, 0.0f);
  }

  const int upIndex = FindPointerIndexById(event, m_androidUpPointerId);
  if (upIndex >= 0) {
    m_androidMoveUp = PointInsideCircle(AMotionEvent_getX(event, upIndex),
                                        AMotionEvent_getY(event, upIndex),
                                        layout.upCenter,
                                        layout.buttonRadius * 1.2f);
    if (!m_androidMoveUp) {
      m_androidUpPointerId = -1;
    }
  } else {
    m_androidUpPointerId = -1;
    m_androidMoveUp = false;
  }

  const int downIndex = FindPointerIndexById(event, m_androidDownPointerId);
  if (downIndex >= 0) {
    m_androidMoveDown = PointInsideCircle(AMotionEvent_getX(event, downIndex),
                                          AMotionEvent_getY(event, downIndex),
                                          layout.downCenter,
                                          layout.buttonRadius * 1.2f);
    if (!m_androidMoveDown) {
      m_androidDownPointerId = -1;
    }
  } else {
    m_androidDownPointerId = -1;
    m_androidMoveDown = false;
  }

  return true;
}

void DayScene::ApplyAndroidVirtualControls() {
  if (!AndroidVirtualControlsVisible() || !ActiveCam) {
    ResetAndroidVirtualControls();
    return;
  }

  constexpr float kAxisThreshold = 0.02f;
  constexpr float kLookYawRadiansPerSecond = 2.6f;
  constexpr float kLookPitchRadiansPerSecond = 2.2f;

  if (m_androidMoveAxis.y < -kAxisThreshold) {
    ActiveCam->MoveForward(DtSecs * -m_androidMoveAxis.y);
  } else if (m_androidMoveAxis.y > kAxisThreshold) {
    ActiveCam->MoveBackward(DtSecs * m_androidMoveAxis.y);
  }

  if (m_androidMoveAxis.x < -kAxisThreshold) {
    ActiveCam->StrafeLeft(DtSecs * -m_androidMoveAxis.x);
  } else if (m_androidMoveAxis.x > kAxisThreshold) {
    ActiveCam->StrafeRight(DtSecs * m_androidMoveAxis.x);
  }

  if (m_androidMoveUp) {
    ActiveCam->MoveUp(DtSecs);
  }
  if (m_androidMoveDown) {
    ActiveCam->MoveDown(DtSecs);
  }

  if (std::fabs(m_androidLookAxis.x) > kAxisThreshold) {
    ActiveCam->MoveYaw(m_androidLookAxis.x * kLookYawRadiansPerSecond * DtSecs);
  }
  if (std::fabs(m_androidLookAxis.y) > kAxisThreshold) {
    ActiveCam->MovePitch(m_androidLookAxis.y * kLookPitchRadiansPerSecond * DtSecs);
  }
}

void DayScene::DrawAndroidVirtualControls(bool guiVisible) {
  if (guiVisible || !AndroidVirtualControlsVisible()) {
    ResetAndroidVirtualControls();
    return;
  }

  ImGuiIO& io = ImGui::GetIO();
  float width = io.DisplaySize.x;
  float height = io.DisplaySize.y;
  if (pFramework && pFramework->pVideoDriver) {
    if (pFramework->pVideoDriver->width > 0) {
      width = static_cast<float>(pFramework->pVideoDriver->width);
    }
    if (pFramework->pVideoDriver->height > 0) {
      height = static_cast<float>(pFramework->pVideoDriver->height);
    }
  }
  if (width <= 0.0f || height <= 0.0f) {
    return;
  }

  const AndroidVirtualControlsLayout layout = BuildAndroidVirtualControlsLayout(width, height);
  ImDrawList* drawList = ImGui::GetForegroundDrawList();
  if (!drawList) {
    return;
  }

  const ImU32 stickFill = IM_COL32(36, 44, 56, 76);
  const ImU32 stickLine = IM_COL32(220, 230, 255, 120);
  const ImU32 knobFill = IM_COL32(90, 170, 255, 120);
  const ImU32 buttonFill = IM_COL32(40, 50, 65, 96);
  const ImU32 buttonActiveFill = IM_COL32(90, 170, 255, 150);
  const ImU32 textColor = IM_COL32(235, 245, 255, 190);

  auto drawStick = [&](const ImVec2& center, const XVECTOR2& axis) {
    drawList->AddCircleFilled(center, layout.stickRadius, stickFill, 48);
    drawList->AddCircle(center, layout.stickRadius, stickLine, 48, 2.0f);
    drawList->AddCircle(center, layout.stickRadius * 0.42f, IM_COL32(220, 230, 255, 42), 32, 1.0f);
    const ImVec2 knob(center.x + axis.x * layout.stickRadius,
                      center.y + axis.y * layout.stickRadius);
    drawList->AddCircleFilled(knob, layout.knobRadius, knobFill, 32);
    drawList->AddCircle(knob, layout.knobRadius, stickLine, 32, 2.0f);
  };

  drawStick(layout.moveCenter, m_androidMoveAxis);
  drawStick(layout.lookCenter, m_androidLookAxis);
  DrawLabeledCircle(drawList, layout.upCenter, layout.buttonRadius, "UP",
                    m_androidMoveUp ? buttonActiveFill : buttonFill, stickLine, textColor);
  DrawLabeledCircle(drawList, layout.downCenter, layout.buttonRadius, "DN",
                    m_androidMoveDown ? buttonActiveFill : buttonFill, stickLine, textColor);
}
#endif

void DayScene::DestroyAssets() {
  if (m_benchmarkOffscreenOutputRT >= 0 && pFramework && pFramework->pVideoDriver) {
    pFramework->pVideoDriver->DestroyRT(m_benchmarkOffscreenOutputRT);
    m_benchmarkOffscreenOutputRT = -1;
  }
  SceneProp.SSAOKernel.Destroy();
  m_debugText.Destroy();
  m_physicsDebugRenderer.Destroy();
  m_navMeshDebugRenderer.Destroy();
  m_navMesh.Clear();
  m_navMeshBuildAttempted = false;
  m_wireframeSphere.Destroy();
  m_wireframeArrow.Destroy();
  PrimitiveMgr.DestroyPrimitives();
  pFramework->pVideoDriver->DestroyRTs();
  //pFramework->pVideoDriver->DestroyShaders();
  //pFramework->pVideoDriver->DestroyTextures();
  //pFramework->pVideoDriver->DestroyTechniques();
}

void DayScene::OnUpdate(float _DtSecs) {
  Camera& Cam = m_sceneSetup.cameras[0];
  Camera& LightCam = m_sceneSetup.lightCameras[0];
  Camera* lightCamPtr = SceneProp.pLightCameras.empty() ? &LightCam : SceneProp.pLightCameras[0];
  t850::SplineAgent& m_agent = m_sceneSetup.agents[0];
  bool splineJourneyFinished = false;
  const bool benchmarkMode = g_config.flags.benchmark;
  const float benchmarkFixedDt = g_config.benchmarkFixedDt > 0.0f ? g_config.benchmarkFixedDt : kBenchmarkFixedDeltaSeconds;
  const bool benchmarkWallClockTimeline =
      benchmarkMode && m_benchmarkTargetDurationSeconds > 0.0f && m_benchmarkTargetFrames <= 0;
  float effectiveDt = benchmarkMode ? benchmarkFixedDt : _DtSecs;
  if (m_benchmarkFinishPending && g_config.flags.benchmark) {
    DtSecs = effectiveDt;
    SceneProp.FrameDeltaSec = DtSecs;
    FinishBenchmarkRun(m_benchmarkPendingDurationSeconds > 0.0f ? m_benchmarkPendingDurationSeconds : kBenchmarkRunDurationSeconds);
    return;
  }
  if (m_benchmarkFinished && g_config.flags.benchmark) {
    DtSecs = effectiveDt;
    SceneProp.FrameDeltaSec = DtSecs;
    return;
  }

  if (benchmarkWallClockTimeline && m_benchmarkSimulationFrame >= m_benchmarkWarmupFrames) {
    const auto now = std::chrono::steady_clock::now();
    if (!m_benchmarkWallClockStarted) {
      m_benchmarkWallClockStarted = true;
      m_benchmarkWallClockStart = now;
      m_benchmarkTimelineElapsedSeconds = 0.0f;
      m_benchmarkTimelineReachedEnd = false;
    }
    const float elapsedSecs = std::chrono::duration<float>(now - m_benchmarkWallClockStart).count();
    const float timelineSecs = std::clamp(elapsedSecs, 0.0f, m_benchmarkTargetDurationSeconds);
    effectiveDt = (std::max)(0.0f, timelineSecs - m_benchmarkTimelineElapsedSeconds);
    m_benchmarkTimelineElapsedSeconds = timelineSecs;
    if (timelineSecs >= m_benchmarkTargetDurationSeconds) {
      m_benchmarkTimelineReachedEnd = true;
    }
  }

  // Benchmarks are fixed-duration runs; normal mode still follows the spline journey.
  if (g_config.flags.benchmark || Cam.m_externalControl)
    m_tourTimeSec += effectiveDt;
  DtSecs = effectiveDt;
  SceneProp.FrameDeltaSec = DtSecs;
  if (benchmarkMode) {
    ++m_benchmarkSimulationFrame;
  }

  // Apply deferred cubemap change BEFORE rendering begins.
  if (!m_pendingCubemap.empty()) {
    // Flush GPU before destroying — D3D12 may still reference the old
    // texture from the previous frame's command list.
    g_pBaseDriver->WaitForGPU();
    int newEnvMapTexIndex = g_pBaseDriver->CreateTexture(m_pendingCubemap);
    if (newEnvMapTexIndex >= 0) {
      if (EnvMapTexIndex >= 0 && EnvMapTexIndex != newEnvMapTexIndex)
        g_pBaseDriver->DestroyTexture(EnvMapTexIndex);
      EnvMapTexIndex = newEnvMapTexIndex;
      if (m_sceneSetup.environmentDiffuseIBL.empty() && DiffuseIBLTexIndex >= 0) {
        g_pBaseDriver->DestroyTexture(DiffuseIBLTexIndex);
        DiffuseIBLTexIndex = -1;
      }
      if (m_sceneSetup.environmentSpecularIBL.empty() && SpecularIBLTexIndex >= 0) {
        g_pBaseDriver->DestroyTexture(SpecularIBLTexIndex);
        SpecularIBLTexIndex = -1;
      }
      if (m_sceneSetup.environmentSheenIBL.empty() && SheenIBLTexIndex >= 0) {
        g_pBaseDriver->DestroyTexture(SheenIBLTexIndex);
        SheenIBLTexIndex = -1;
      }
      EnvMaps.SetFallback(EnvMapTexIndex);
      LoadEnvironmentIBLResources(
        g_pBaseDriver,
        {m_sceneSetup.environmentDiffuseIBL, m_sceneSetup.environmentSpecularIBL, m_sceneSetup.environmentBrdfLUT,
         m_sceneSetup.environmentSheenIBL, m_sceneSetup.environmentCharlieLUT, m_sceneSetup.environmentSheenELUT},
        EnvMaps,
        DiffuseIBLTexIndex,
        SpecularIBLTexIndex,
        BrdfLUTTexIndex,
        SheenIBLTexIndex,
        CharlieLUTTexIndex,
        SheenELUTTexIndex);
      EnvMaps.BrdfLUT = BrdfLUTTexIndex;
      EnvMaps.CharlieIBL = SheenIBLTexIndex;
      EnvMaps.CharlieLUT = CharlieLUTTexIndex;
      EnvMaps.SheenELUT = SheenELUTTexIndex;
      UpdateSceneIBLSettings(SceneProp, g_pBaseDriver, EnvMaps);
      Quads[0].SetEnvironmentMap(g_pBaseDriver->GetTexture(EnvMapTexIndex));
    } else {
      T8_LOG_ERROR("[DayScene] Failed to load cubemap '%s'; keeping previous cubemap", m_pendingCubemap.c_str());
    }
    m_pendingCubemap.clear();
  }

  Meshes[0].SetParallaxSettings(SceneProp.ParallaxLowSamples, SceneProp.ParallaxHighSamples, SceneProp.ParallaxHeight);

  // Replay snapshot: load and apply (one-time)
  if (m_dumper.HasPendingReplay()) {
    if (m_dumper.LoadReplaySnapshot()) {
      m_dumper.ApplySnapshot(Cam, LightCam, SceneProp);
      VP = ActiveCam ? ActiveCam->VP : Cam.VP;
    }
  }
  m_dumper.UpdateReplayState();

  // Normal camera/light updates (skipped when feed is active or dump pending)
  if (!m_dumper.SkipCameraUpdates()) {
    m_agent.Update(DtSecs);
    splineJourneyFinished = Cam.m_externalControl && m_agent.FinishedJourneyThisUpdate();
    Cam.Update(DtSecs);
    if (ActiveCam && ActiveCam != &Cam && ActiveCam != lightCamPtr)
      ActiveCam->Update(DtSecs);
    lightCamPtr->Yaw -= 0.008f *DtSecs;
    lightCamPtr->Update(DtSecs);
    VP = ActiveCam ? ActiveCam->VP : Cam.VP;
    SceneProp.pCullingCamera = &Cam;
    // Capture light position AFTER auto-rotation so shadow matches lighting
    SceneProp.Lights[0].Position = LightCam.Eye;
    SceneProp.Lights[0].Direction = LightCam.Look;
  }

  if (splineJourneyFinished) {
    m_tourTimeSec = 0.0f;
#ifdef T850_HEADLESS
    exit(0);
#else
    pFramework->pBaseApp->LoadScene(1);
#endif
  }
}

void DayScene::OnInput(InputManager* IManager) {
  Camera& Cam = m_sceneSetup.cameras[0];

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
    T8_LOG_VERBOSE("Position[%f,%f,%f] Rot[%f,%f,%f] Sc[%f]", Position.x, Position.y, Position.z, Orientation.x, Orientation.y, Orientation.z, Scaling.x);
  }

  if (IManager->PressedOnceKey(T800K_k)) {
    T8_LOG_INFO("Position[%f, %f, %f]", ActiveCam->Eye.x, ActiveCam->Eye.y, ActiveCam->Eye.z);
    T8_LOG_INFO("Orientation[%f, %f, %f]", ActiveCam->Pitch, ActiveCam->Roll, ActiveCam->Yaw);
  }


  if (IManager->PressedOnceKey(T800K_c)) {
    int nextCamera = m_activeCameraIndex + 1;
    if (nextCamera > 1)
      nextCamera = 0;
    ApplyActiveCameraSelection(nextCamera);
  }

  // Toggle spline-guided / free camera
  if (IManager->PressedOnceKey(T800K_t)) {
    if (Cam.m_externalControl) {
      ApplyActiveCameraSelection(1);
      T8_LOG_INFO("[CAMERA] Switched to FREE camera");
    }
    else {
      ApplyActiveCameraSelection(0);
      T8_LOG_INFO("[CAMERA] Switched to SPLINE camera");
    }
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

  if (IManager->PressedOnceKey(T800K_F10)) {
    m_dumper.RequestDump();
  }

  if (IManager->PressedOnceKey(T800K_F2)) {
    if (!SceneProp.FrustumCullingToggleAllowed) {
      m_showCullStats = false;
    } else {
      m_showCullStats = m_spectatorCameraEnabled ? true : !m_showCullStats;
    }
    SceneProp.ShowCullingDebug = m_showCullStats;
  }

  if (IManager->PressedOnceKey(T800K_F4)) {
    m_showPhysics = !m_showPhysics;
    T8_LOG_INFO("[PHYSICS] Debug draw %s", m_showPhysics ? "enabled" : "disabled");
  }

  if (IManager->PressedOnceKey(T800K_KP6) || IManager->PressedOnceKey(T800K_6)) {
    if (SceneProp.FrustumCullingToggleAllowed) {
      const bool requested = !SceneProp.FrustumCullingEnabled;
      if (!requested || !Meshes[0].pBase || static_cast<RenderMesh*>(Meshes[0].pBase)->EnsureCullingMetadata()) {
        SceneProp.FrustumCullingEnabled = requested;
      }
      T8_LOG_INFO("[CULLING] Frustum culling %s", SceneProp.FrustumCullingEnabled ? "enabled" : "disabled");
    } else {
      SceneProp.FrustumCullingEnabled = false;
      T8_LOG_INFO("[CULLING] Frustum culling locked off by startup policy");
    }
  }

  if (IManager->PressedOnceKey(T800K_1)) {
    pFramework->ChangeAPI(GraphicsApi::D3D11);
  }

  if (IManager->PressedOnceKey(T800K_2)) {
    pFramework->ChangeAPI(GraphicsApi::OPENGL);
  }
  if (IManager->PressedOnceKey(T800K_3)) {
    pFramework->ChangeAPI(GraphicsApi::D3D12);
  }
  if (IManager->PressedOnceKey(T800K_4)) {
    pFramework->ChangeAPI(GraphicsApi::VULKAN);
  }

  if (IManager->PressedOnceKey(T800K_5)) {
    if (SceneProp.FrustumCullingToggleAllowed) {
      SetSpectatorDebugEnabled(!m_spectatorCameraEnabled);
      T8_LOG_INFO("[CAMERA] Spectator camera %s", m_spectatorCameraEnabled ? "enabled" : "disabled");
    } else {
      SetSpectatorDebugEnabled(false);
      T8_LOG_INFO("[CAMERA] Spectator camera locked off by culling startup policy");
    }
  }

  // Skip mouse-driven camera movement when replay snapshot is active
  if (!m_dumper.IsReplayActive()) {
#ifdef OS_ANDROID
    if (AndroidVirtualControlsVisible()) {
      ApplyAndroidVirtualControls();
    } else {
      float yaw = 0.005f*static_cast<float>(IManager->xDelta);
      ActiveCam->MoveYaw(yaw);
      float pitch = 0.005f*static_cast<float>(IManager->yDelta);
      ActiveCam->MovePitch(pitch);
    }
#else
    float yaw = 0.005f*static_cast<float>(IManager->xDelta);
    ActiveCam->MoveYaw(yaw);
    float pitch = 0.005f*static_cast<float>(IManager->yDelta);
    ActiveCam->MovePitch(pitch);
#endif
  }
}

void DayScene::OnDraw() {
  SceneProp.ShowCullingDebug = m_showCullStats;
  Camera& Cam = m_sceneSetup.cameras[0];
  Camera& LightCam = m_sceneSetup.lightCameras[0];
  Camera* viewCam = ActiveCam ? ActiveCam : &Cam;
  SceneProp.pCullingCamera = &Cam;

  // Execute the render graph (all passes up to and including HDR Composition)
  const int finalOutputRT = m_benchmarkActiveOffscreen ? m_benchmarkOffscreenOutputRT : -1;
  m_renderGraph.Execute(
    pFramework->pVideoDriver,
    SceneProp,
    Meshes, 2,
    Quads,
    viewCam,
    &LightCam,
    nullptr,
    EnvMaps,
    finalOutputRT
  );

  if (m_benchmarkFinishPending && g_config.flags.benchmarkFinalFrameDump && m_benchmarkPendingFinalFramePath.empty()) {
    m_benchmarkPendingFinalFramePath = CaptureBenchmarkFinalFrame();
  }

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
  // Debug RT override: draw selected render target fullscreen
  if (m_debugRTSelection > 0) {
    int selected = -1;
    int attachment = BaseDriver::COLOR0_ATTACHMENT;
    switch (m_debugRTSelection) {
    case 1:  selected = GBufferPass;     attachment = BaseDriver::COLOR0_ATTACHMENT; break; // Albedo
    case 2:  selected = GBufferPass;     attachment = BaseDriver::COLOR1_ATTACHMENT; break; // Normals
    case 3:  selected = GBufferPass;     attachment = BaseDriver::COLOR2_ATTACHMENT; break; // Specular
    case 4:  selected = GBufferPass;     attachment = BaseDriver::COLOR3_ATTACHMENT; break; // Geo/material
    case 5:  selected = GBufferPass;     attachment = BaseDriver::DEPTH_ATTACHMENT;  break; // GBuf Depth
    case 6:  selected = DepthPass;       attachment = BaseDriver::DEPTH_ATTACHMENT;  break; // Shadow Map
    case 7:  selected = ShadowAccumPass; attachment = BaseDriver::COLOR0_ATTACHMENT; break; // Shadow Accum
    case 8:  selected = DeferredPass;    attachment = BaseDriver::COLOR0_ATTACHMENT; break; // Deferred
    case 9:  selected = Extra16FPass;    attachment = BaseDriver::COLOR0_ATTACHMENT; break; // Extra16F
    case 10: selected = ExtraHelperPass; attachment = BaseDriver::COLOR0_ATTACHMENT; break; // HDR
    case 11: selected = BloomAccumPass;  attachment = BaseDriver::COLOR0_ATTACHMENT; break; // Bloom
    case 12: selected = GodRaysCalcPass; attachment = BaseDriver::COLOR0_ATTACHMENT; break; // God Rays
    case 13: selected = CoCPass;         attachment = BaseDriver::COLOR0_ATTACHMENT; break; // CoC
    case 14: selected = BrightPassPass;  attachment = BaseDriver::COLOR0_ATTACHMENT; break; // Bright
    case 15: selected = AdaptedLumCurrentPass; attachment = BaseDriver::COLOR0_ATTACHMENT; break; // Adapted Lum
    }
    if (selected >= 0) {
      Quads[7].SetTexture(pFramework->pVideoDriver->GetRTTexture(selected, attachment), 0);
      ShaderKey dbgKey(0); dbgKey.setPass(PassType::FSQUAD_1_TEX); dbgKey.bits |= ShaderKey::HAS_TEXCOORD0;
      Quads[7].SetGlobalKey(dbgKey);
      Quads[7].Draw();
#ifdef OS_ANDROID
      if (auto* vkDriver = static_cast<VulkanDriver*>(pFramework->pVideoDriver)) {
        vkDriver->SetLatePresentSource(selected, attachment);
      }
#endif
    }
  }

  auto drawDebugOverlays = [this, viewCam]() {
    if (m_showPhysics) {
      t850::EngineContext* engineContext = GetEngineContext();
      if (!engineContext) engineContext = &t850::GetEngineContext();
      if (engineContext && engineContext->physics && m_physicsDebugRenderer.IsReady()) {
        m_physicsDebugRenderer.SetDepthTexture(nullptr);
        m_physicsDebugRenderer.SetDepthTestEnabled(false);
        m_physicsDebugRenderer.SetViewport(g_pBaseDriver->width, g_pBaseDriver->height);
        m_physicsDebugRenderer.SetFarPlane(viewCam ? viewCam->FPlane : 1000.0f);
        pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
        pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
        m_physicsDebugRenderer.Draw(*engineContext->physics, VP);
      }
    }

    if (m_showNavMesh && m_navMeshDebugRenderer.IsReady() && EnsureNavMeshBuilt()) {
      Texture* depthTexture = nullptr;
      if (GBufferPass >= 0 && GBufferPass < (int)pFramework->pVideoDriver->RTs.size()) {
        if (auto* gbufRT = pFramework->pVideoDriver->RTs[GBufferPass]) {
          depthTexture = gbufRT->pDepthTexture;
        }
      }
      if (depthTexture) {
        m_navMeshDebugRenderer.SetVerticalOffset(m_navMeshDebugOffset);
        m_navMeshDebugRenderer.SetGraphVerticalOffset(m_navMeshDebugOffset + 0.005f);
        m_navMeshDebugRenderer.SetShapeMode(m_navMeshDebugShapeMode == 1
            ? t850::navigation::NavMeshDebugShapeMode::Nodes
            : t850::navigation::NavMeshDebugShapeMode::Geometry);
        m_navMeshDebugRenderer.SetDepthTexture(depthTexture);
        m_navMeshDebugRenderer.SetViewport(g_pBaseDriver->width, g_pBaseDriver->height);
        m_navMeshDebugRenderer.SetFarPlane(viewCam ? viewCam->FPlane : 1000.0f);
        pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
        pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
        m_navMeshDebugRenderer.Draw(m_navMesh, VP);
      }
    }
  };

#ifdef OS_ANDROID
  if (m_showPhysics || m_showNavMesh) {
    if (auto* vkDriver = static_cast<VulkanDriver*>(pFramework->pVideoDriver)) {
      vkDriver->SetPrePresentOverlayCallback(drawDebugOverlays);
    }
  }
#else
  drawDebugOverlays();
#endif

  if (SceneProp.pCameras[0]->Eye.y > 80) {
    m_flare.Draw();
  }

  // Draw spline wireframe overlay
  if (m_showSpline) {
    splineInst.Draw();
  }

  // Draw light gizmos overlay
  if (m_showLights) {
    unsigned int numLights = SceneProp.ActiveLights;
    if (numLights > SceneProp.Lights.size())
      numLights = static_cast<unsigned int>(SceneProp.Lights.size());
    for (unsigned int i = 0; i < numLights; i++) {
      Light& light = SceneProp.Lights[i];
      if (light.Type == LIGHT_DIRECTIONAL) {
        // Draw arrow gizmo at an editor position above scene center
        XVECTOR3 gizmoPos(0.0f, 80.0f, 0.0f);
        m_wireframeArrow.Draw(VP, gizmoPos, light.Direction, 10.0f);
      } else if (SceneProp.PointLightsEnabled) {
        m_wireframeSphere.Draw(VP, light.Position, light.radius);
      }
    }
  }

  if (m_showCullStats && Meshes[0].pBase) {
    RenderMesh* rm = static_cast<RenderMesh*>(Meshes[0].pBase);
    int w = g_pBaseDriver->width;
    int h = g_pBaseDriver->height;

    pFramework->pVideoDriver->SetBlendState(BaseDriver::ALPHA_BLEND);
    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);

    char buf[256];
    XVECTOR3 yellow(1.0f, 1.0f, 0.2f);
    XVECTOR3 gray(0.7f, 0.7f, 0.7f);
    const float statScale = 0.56f;
    const float lineHeight = 34.0f * statScale * ((float)h / 720.0f);
    const float bottomMargin = 26.0f * ((float)h / 720.0f);
    float y = (float)h - bottomMargin - lineHeight * 4.0f;
    auto drawCenteredStat = [&](const XVECTOR3& color, const char* text) {
      float textW = m_debugText.MeasurePixel(text, w, h) * statScale;
      float x = ((float)w - textW) * 0.5f;
      m_debugText.DrawPixelScaled(x, y, statScale, statScale, w, h, color, text);
      y += lineHeight;
    };

    snprintf(buf, sizeof(buf), "Sponza meshes: %d/%d  culled %d",
            rm->m_visibleMeshes, rm->m_totalMeshes, rm->m_culledMeshes);
    drawCenteredStat(yellow, buf);

    snprintf(buf, sizeof(buf), "Sponza subsets: %d/%d  culled %d  drawn %d",
            rm->m_visibleSubsets, rm->m_totalSubsets, rm->m_culledSubsets, rm->m_drawnSubsets);
    drawCenteredStat(yellow, buf);

    snprintf(buf, sizeof(buf), "Sponza clusters: %d/%d  culled %d  drawn %d",
            rm->m_visibleClusters, rm->m_totalClusters, rm->m_culledClusters, rm->m_drawnClusters);
    drawCenteredStat(yellow, buf);

        snprintf(buf, sizeof(buf), "GBuffer indices: %llu/%llu  6/KP6: culling %s  F2: cull stats",
          rm->m_drawnIndices, rm->m_totalIndices,
          SceneProp.FrustumCullingEnabled ? "ON" : "OFF");
    drawCenteredStat(gray, buf);

    pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::DEPTH_DEFAULT);
  }

  // RT Dump via FrameDumper (skip when profiling — GPU queries conflict with dump's cmd buffer reset)
  if (m_dumper.ShouldDump(DtSecs) && !g_config.flags.profile) {
    std::vector<RTDumpEntry> rts = {
      {GBufferPass,     BaseDriver::COLOR0_ATTACHMENT, "GBuffer_Color0"},
      {GBufferPass,     BaseDriver::COLOR1_ATTACHMENT, "GBuffer_Normals"},
      {GBufferPass,     BaseDriver::COLOR2_ATTACHMENT, "GBuffer_Color2"},
      {GBufferPass,     BaseDriver::COLOR3_ATTACHMENT, "GBuffer_Color3"},
      {GBufferPass,     BaseDriver::COLOR4_ATTACHMENT, "GBuffer_Emissive"},
      {GBufferPass,     BaseDriver::COLOR5_ATTACHMENT, "GBuffer_Sheen"},
      {GBufferPass,     BaseDriver::COLOR6_ATTACHMENT, "GBuffer_SpecularOcclusion"},
      {GBufferPass,     BaseDriver::DEPTH_ATTACHMENT,  "GBuffer_Depth"},
      {DepthPass,       BaseDriver::DEPTH_ATTACHMENT,  "ShadowMap_Depth"},
      {ShadowAccumPass, BaseDriver::COLOR0_ATTACHMENT, "ShadowAccum"},
      {DeferredPass,    BaseDriver::COLOR0_ATTACHMENT, "Deferred"},
      {Extra16FPass,    BaseDriver::COLOR0_ATTACHMENT, "Extra16F"},
      {ExtraHelperPass, BaseDriver::COLOR0_ATTACHMENT, "HDR_Final"},
      {BloomAccumPass,  BaseDriver::COLOR0_ATTACHMENT, "Bloom"},
      {GodRaysCalcPass, BaseDriver::COLOR0_ATTACHMENT, "GodRays"},
      {AdaptedLumCurrentPass, BaseDriver::COLOR0_ATTACHMENT, "AdaptedLumCurrent"},
      {AdaptedLumPrevPass, BaseDriver::COLOR0_ATTACHMENT, "AdaptedLumPrev"},
    };
    m_dumper.DumpFrame(pFramework->pVideoDriver, Cam, LightCam, SceneProp, rts, DtSecs);
    if (m_dumper.ShouldExit() && !g_config.flags.profile) exit(0);
  }

#endif
}

void  DayScene::ChangeSettingsOnPlus() {
  Camera& LightCam = m_sceneSetup.lightCameras[0];
  switch (SceneSettingSelection) {
  case CHANGE_EXPOSURE: {
    float prevVal = SceneProp.Exposure;
    SceneProp.Exposure += 0.1f;
    T8_LOG_VERBOSE("[CHANGE_EXPOSURE] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.Exposure);
  }break;
  case CHANGE_BLOOM_FACTOR: {
    float prevVal = SceneProp.BloomFactor;
    SceneProp.BloomFactor += 0.1f;
    T8_LOG_VERBOSE("[CHANGE_BLOOM_FACTOR] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.BloomFactor);
  }break;
  case CHANGE_BLOOM_THRESHOLD: {
    float prevVal = SceneProp.BloomThreshold;
    SceneProp.BloomThreshold += 0.5f;
    T8_LOG_VERBOSE("[CHANGE_BLOOM_THRESHOLD] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.BloomThreshold);
  }break;
  case CHANGE_TM_WHITE_LEVEL: {
    float prevVal = SceneProp.ToneMapWhiteLevel;
    SceneProp.ToneMapWhiteLevel += 0.25f;
    T8_LOG_VERBOSE("[CHANGE_TM_WHITE_LEVEL] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.ToneMapWhiteLevel);
  }break;
  case CHANGE_TM_ADAPT_TAU: {
    float prevVal = SceneProp.LuminanceTau;
    SceneProp.LuminanceTau += 0.1f;
    T8_LOG_VERBOSE("[CHANGE_TM_ADAPT_TAU] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.LuminanceTau);
  }break;
  case CHANGE_NUM_LIGHTS: {
    int prevVal = SceneProp.ActiveLights;
    SceneProp.ActiveLights *= 2;
    if (SceneProp.ActiveLights >= 127) {
      SceneProp.ActiveLights = 127;
    }
    T8_LOG_VERBOSE("[CHANGE_NUM_LIGHTS] Previous Value[%d] Actual Value[%d]", prevVal, SceneProp.ActiveLights);
  }break;
  case CHANGE_ACTIVE_GAUSS_KERNEL: {
    int prevVal = ChangeActiveGaussSelection;
    ChangeActiveGaussSelection++;
    if (ChangeActiveGaussSelection >= (int)SceneProp.pGaussKernels.size()) {
      ChangeActiveGaussSelection = static_cast<int>(SceneProp.pGaussKernels.size()) - 1;
    }
    T8_LOG_VERBOSE("[CHANGE_ACTIVE_GAUSS_KERNEL] Previous Value[%d] Actual Value[%d]", prevVal, ChangeActiveGaussSelection);
  }break;
  case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
    int prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize += 2;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    T8_LOG_VERBOSE("[CHANGE_GAUSS_KERNEL_SAMPLE_COUNT] Previous Value[%d] Actual Value[%d]", prevVal, SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize);
  }break;
  case CHANGE_GAUSS_KERNEL_RADIUS: {
    float prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius += 0.5;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    T8_LOG_VERBOSE("[CHANGE_GAUSS_KERNEL_RADIUS] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius);
  }break;
  case CHANGE_GAUSS_KERNEL_DEVIATION: {
    float prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma += 0.5;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    T8_LOG_VERBOSE("[CHANGE_GAUSS_KERNEL_DEVIATION] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma);
  }break;
  case CHANGE_PCF_RADIUS: {
    float prevVal = SceneProp.PCFScale;
    SceneProp.PCFScale += 0.1f;
    T8_LOG_VERBOSE("[CHANGE_PCF_RADIUS] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.PCFScale);
  }break;
  case CHANGE_PCF_SAMPLES: {
    float prevVal = SceneProp.PCFSamples;
    SceneProp.PCFSamples++;
    T8_LOG_VERBOSE("[CHANGE_PCF_SAMPLES] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.PCFSamples);
  }break;
  case CHANGE_SSAO_KERNEL_SIZE: {
    float prevVal = (float)SceneProp.SSAOKernel.KernelSize;
    SceneProp.SSAOKernel.KernelSize += 2;
    SceneProp.SSAOKernel.Update();
    T8_LOG_VERBOSE("[CHANGE_SSAO_KERNEL_SIZE] Previous Value[%f] Actual Value[%d]", prevVal, SceneProp.SSAOKernel.KernelSize);
  }break;
  case CHANGE_SSAO_RADIUS: {
    float prevVal = SceneProp.SSAOKernel.Radius;
    SceneProp.SSAOKernel.Radius += 0.5f;
    T8_LOG_VERBOSE("[CHANGE_SSAO_RADIUS] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.SSAOKernel.Radius);
  }break;
  case CHANGE_DOF_APERTURE: {
    float prevVal = SceneProp.Aperture;
    SceneProp.Aperture += 10.0f;
    T8_LOG_VERBOSE("[CHANGE_DOF_APERTURE] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.Aperture);
  }break;
  case CHANGE_DOF_FOCAL_LENGHT: {
    float prevVal = SceneProp.FocalLength;
    SceneProp.FocalLength += 10.0f;
    T8_LOG_VERBOSE("[CHANGE_DOF_FOCAL_LENGHT] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.FocalLength);
  }break;
  case CHANGE_DOF_MAX_COC: {
    float prevVal = SceneProp.MaxCoc;
    SceneProp.MaxCoc += 0.5f;
    T8_LOG_VERBOSE("[CHANGE_DOF_MAX_COC] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.MaxCoc);
  }break;
  case CHANGE_DOF_FAR_SAMPLE: {
    float prevVal = SceneProp.DOF_Far_Samples_squared;
    SceneProp.DOF_Far_Samples_squared += 1.0f;
    T8_LOG_VERBOSE("[CHANGE_DOF_FAR_SAMPLE] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.DOF_Far_Samples_squared);
  }break;
  case CHANGE_DOF_NEAR_SAMPLE: {
    float prevVal = SceneProp.DOF_Near_Samples_squared;
    SceneProp.DOF_Near_Samples_squared += 1.0f;
    T8_LOG_VERBOSE("[CHANGE_DOF_NEAR_SAMPLE] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.DOF_Near_Samples_squared);
  }break;
  case CHANGE_DOF_AUTO_FOCUS: {
    bool prevVal = SceneProp.AutoFocus;
    SceneProp.AutoFocus = true;
    T8_LOG_VERBOSE("[CHANGE_DOF_AUTO_FOCUS] Previous Value[%d] Actual Value[%d]", (int)prevVal, (int)SceneProp.AutoFocus);
  }break;
  case CHANGE_PARALLAX_LOW_SAMPLES: {
    float prevVal = SceneProp.ParallaxLowSamples;
    SceneProp.ParallaxLowSamples += 5.0f;
    T8_LOG_VERBOSE("[CHANGE_PARALLAX_LOW_SAMPLES] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.ParallaxLowSamples);
  }break;
  case CHANGE_PARALLAX_HIGH_SAMPLES: {
    float prevVal = SceneProp.ParallaxHighSamples;
    SceneProp.ParallaxHighSamples += 10.0f;
    T8_LOG_VERBOSE("[CHANGE_PARALLAX_HIGH_SAMPLES] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.ParallaxHighSamples);
  }break;
  case CHANGE_PARALLAX_HEIGHT: {
    float prevVal = SceneProp.ParallaxHeight;
    SceneProp.ParallaxHeight += 0.01f;
    T8_LOG_VERBOSE("[CHANGE_PARALLAX_HEIGHT] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.ParallaxHeight);
  }break;
  case CHANGE_LIGHT_VOLUME_STEPS: {
    float prevVal = SceneProp.LightVolumeSteps;
    SceneProp.LightVolumeSteps += 16.0f;
    T8_LOG_VERBOSE("[CHANGE_LIGHT_VOLUME_STEPS] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.LightVolumeSteps);
  }break;
  case CHANGE_PCF_TOOGLE: {
	  int prevVal = SceneProp.ToogleShadow;
	  SceneProp.ToogleShadow = 1;
	  T8_LOG_VERBOSE("[CHANGE_PCF_TOOGLE] Previous Value[%d] Actual Value[%d]", prevVal, SceneProp.ToogleShadow);
  }break;
  case CHANGLE_SSAO_TOOGLE: {
	  int prevVal = SceneProp.ToogleSSAO;
	  SceneProp.ToogleSSAO = 1;
	  T8_LOG_VERBOSE("[CHANGLE_SSAO_TOOGLE] Previous Value[%d] Actual Value[%d]", prevVal, SceneProp.ToogleSSAO);
  }break;
  case CHANGE_LIGHT_NEAR_PLANE: {
    float prevVal = LightCam.NPlane;
    LightCam.NPlane += 1.0f;
    LightCam.CreatePojection();
    LightCam.Update(0);
    T8_LOG_VERBOSE("[CHANGE_LIGHT_NEAR_PLANE] Previous Value[%f] Actual Value[%f]", prevVal, LightCam.NPlane);
  }break;
  case CHANGE_LIGHT_FAR_PLANE: {
    float prevVal = LightCam.FPlane;
    LightCam.FPlane += 10.0f;
    LightCam.CreatePojection();
    LightCam.Update(0);
    T8_LOG_VERBOSE("[CHANGE_LIGHT_FAR_PLANE] Previous Value[%f] Actual Value[%f]", prevVal, LightCam.FPlane);
  }break;
  case CHANGE_FOV: {
    float prevVal = ActiveCam->Fov;
    ActiveCam->SetFov(ActiveCam->Fov + Deg2Rad(5.0f));
    if (ActiveCam->Fov > Deg2Rad(150.0f)) ActiveCam->SetFov(Deg2Rad(150.0f));
    T8_LOG_VERBOSE("[CHANGE_FOV] Previous Value[%f] Actual Value[%f]", Rad2Deg(prevVal), Rad2Deg(ActiveCam->Fov));
  }break;
  case CHANGE_SHOW_SPLINE: {
    m_showSpline = true;
    T8_LOG_VERBOSE("[CHANGE_SHOW_SPLINE] Value[%d]", (int)m_showSpline);
  }break;
  case CHANGE_SHOW_LIGHTS: {
    m_showLights = true;
    T8_LOG_VERBOSE("[CHANGE_SHOW_LIGHTS] Value[%d]", (int)m_showLights);
  }break;
  case CHANGE_SHOW_PHYSICS: {
    m_showPhysics = true;
    T8_LOG_VERBOSE("[CHANGE_SHOW_PHYSICS] Value[%d]", (int)m_showPhysics);
  }break;
  case CHANGE_LIGHT_INTENSITY: {
    if (!SceneProp.Lights.empty()) {
      float prevVal = SceneProp.Lights[0].Intensity;
      SceneProp.Lights[0].Intensity += 0.5f;
      if (SceneProp.Lights[0].Intensity > 20.0f) SceneProp.Lights[0].Intensity = 20.0f;
      T8_LOG_VERBOSE("[CHANGE_LIGHT_INTENSITY] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.Lights[0].Intensity);
    }
  }break;
  case CHANGE_LIGHT_RADIUS_SCALE: {
    float prevVal = SceneProp.LightRadiusScale;
    SceneProp.LightRadiusScale += 0.25f;
    T8_LOG_VERBOSE("[CHANGE_LIGHT_RADIUS_SCALE] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.LightRadiusScale);
  }break;
  case CHANGE_LIGHT_INTENSITY_SCALE: {
    float prevVal = SceneProp.LightIntensityScale;
    SceneProp.LightIntensityScale += 0.25f;
    T8_LOG_VERBOSE("[CHANGE_LIGHT_INTENSITY_SCALE] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.LightIntensityScale);
  }break;
  case CHANGE_LIGHTMAP_INTENSITY: {
    float prevVal = SceneProp.LightmapIntensity;
    SceneProp.LightmapIntensity += 0.25f;
    T8_LOG_VERBOSE("[CHANGE_LIGHTMAP_INTENSITY] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.LightmapIntensity);
  }break;
  }
}

void  DayScene::ChangeSettingsOnMinus() {
  Camera& LightCam = m_sceneSetup.lightCameras[0];
  switch (SceneSettingSelection) {
  case CHANGE_EXPOSURE: {
    float prevVal = SceneProp.Exposure;
    SceneProp.Exposure -= 0.1f;
    T8_LOG_VERBOSE("[CHANGE_EXPOSURE] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.Exposure);
  }break;
  case CHANGE_BLOOM_FACTOR: {
    float prevVal = SceneProp.BloomFactor;
    SceneProp.BloomFactor -= 0.1f;
    T8_LOG_VERBOSE("[CHANGE_BLOOM_FACTOR] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.BloomFactor);
  }break;
  case CHANGE_BLOOM_THRESHOLD: {
    float prevVal = SceneProp.BloomThreshold;
    SceneProp.BloomThreshold -= 0.5f;
    if (SceneProp.BloomThreshold < 0.0f) SceneProp.BloomThreshold = 0.0f;
    T8_LOG_VERBOSE("[CHANGE_BLOOM_THRESHOLD] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.BloomThreshold);
  }break;
  case CHANGE_TM_WHITE_LEVEL: {
    float prevVal = SceneProp.ToneMapWhiteLevel;
    SceneProp.ToneMapWhiteLevel -= 0.25f;
    if (SceneProp.ToneMapWhiteLevel < 0.5f) SceneProp.ToneMapWhiteLevel = 0.5f;
    T8_LOG_VERBOSE("[CHANGE_TM_WHITE_LEVEL] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.ToneMapWhiteLevel);
  }break;
  case CHANGE_TM_ADAPT_TAU: {
    float prevVal = SceneProp.LuminanceTau;
    SceneProp.LuminanceTau -= 0.1f;
    if (SceneProp.LuminanceTau < 0.05f) SceneProp.LuminanceTau = 0.05f;
    T8_LOG_VERBOSE("[CHANGE_TM_ADAPT_TAU] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.LuminanceTau);
  }break;
  case CHANGE_NUM_LIGHTS: {
    int prevVal = SceneProp.ActiveLights;
    SceneProp.ActiveLights /= 2;
    if (SceneProp.ActiveLights <= 0) {
      SceneProp.ActiveLights = 1;
    }
    T8_LOG_VERBOSE("[CHANGE_NUM_LIGHTS] Previous Value[%d] Actual Value[%d]", prevVal, SceneProp.ActiveLights);
  }break;
  case CHANGE_ACTIVE_GAUSS_KERNEL: {
    int prevVal = ChangeActiveGaussSelection;
    ChangeActiveGaussSelection--;
    if (ChangeActiveGaussSelection < 0) {
      ChangeActiveGaussSelection = 0;
    }
    T8_LOG_VERBOSE("[CHANGE_ACTIVE_GAUSS_KERNEL] Previous Value[%d] Actual Value[%d]", prevVal, ChangeActiveGaussSelection);
  }break;
  case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
    int prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize -= 2;
    if (SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize <= 2) {
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize = 3;
    }
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    T8_LOG_VERBOSE("[CHANGE_GAUSS_KERNEL_SAMPLE_COUNT] Previous Value[%d] Actual Value[%d]", prevVal, SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize);
  }break;
  case CHANGE_GAUSS_KERNEL_RADIUS: {
    float prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius -= 0.5f;
    if (SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius <= 0.5f) {
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius = 0.5f;
    }
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    T8_LOG_VERBOSE("[CHANGE_GAUSS_KERNEL_RADIUS] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius);
  }break;
  case CHANGE_GAUSS_KERNEL_DEVIATION: {
    float prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma -= 0.5;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    T8_LOG_VERBOSE("[CHANGE_GAUSS_KERNEL_DEVIATION] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma);
  }break;
  case CHANGE_PCF_RADIUS: {
    float prevVal = SceneProp.PCFScale;
    SceneProp.PCFScale -= 0.1f;
    T8_LOG_VERBOSE("[CHANGE_PCF_RADIUS] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.PCFScale);
  }break;
  case CHANGE_PCF_SAMPLES: {
    float prevVal = SceneProp.PCFSamples;
    SceneProp.PCFSamples--;
    T8_LOG_VERBOSE("[CHANGE_PCF_SAMPLES] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.PCFSamples);
  }break;
  case CHANGE_SSAO_KERNEL_SIZE: {
    float prevVal = (float)SceneProp.SSAOKernel.KernelSize;
    SceneProp.SSAOKernel.KernelSize -= 2;
    SceneProp.SSAOKernel.Update();
    T8_LOG_VERBOSE("[CHANGE_SSAO_KERNEL_SIZE] Previous Value[%f] Actual Value[%d]", prevVal, SceneProp.SSAOKernel.KernelSize);
  }break;
  case CHANGE_SSAO_RADIUS: {
    float prevVal = SceneProp.SSAOKernel.Radius;
    SceneProp.SSAOKernel.Radius -= 0.5f;
    T8_LOG_VERBOSE("[CHANGE_SSAO_RADIUS] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.SSAOKernel.Radius);
  }break;
  case CHANGE_DOF_APERTURE: {
    float prevVal = SceneProp.Aperture;
    SceneProp.Aperture -= 10.0f;
    T8_LOG_VERBOSE("[CHANGE_DOF_APERTURE] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.Aperture);
  }break;
  case CHANGE_DOF_FOCAL_LENGHT: {
    float prevVal = SceneProp.FocalLength;
    SceneProp.FocalLength -= 10.0f;
    T8_LOG_VERBOSE("[CHANGE_DOF_FOCAL_LENGHT] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.FocalLength);
  }break;
  case CHANGE_DOF_MAX_COC: {
    float prevVal = SceneProp.MaxCoc;
    SceneProp.MaxCoc -= 0.5f;
    T8_LOG_VERBOSE("[CHANGE_DOF_MAX_COC] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.MaxCoc);
  }break;
  case CHANGE_DOF_FAR_SAMPLE: {
    float prevVal = SceneProp.DOF_Far_Samples_squared;
    SceneProp.DOF_Far_Samples_squared -= 1.0f;
    T8_LOG_VERBOSE("[CHANGE_DOF_FAR_SAMPLE] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.DOF_Far_Samples_squared);
  }break;
  case CHANGE_DOF_NEAR_SAMPLE: {
    float prevVal = SceneProp.DOF_Near_Samples_squared;
    SceneProp.DOF_Near_Samples_squared -= 1.0f;
    T8_LOG_VERBOSE("[CHANGE_DOF_NEAR_SAMPLE] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.DOF_Near_Samples_squared);
  }break;
  case CHANGE_DOF_AUTO_FOCUS: {
    bool prevVal = SceneProp.AutoFocus;
    SceneProp.AutoFocus = false;
    T8_LOG_VERBOSE("[CHANGE_DOF_AUTO_FOCUS] Previous Value[%d] Actual Value[%d]", (int)prevVal, (int)SceneProp.AutoFocus);
  }break;
  case CHANGE_PARALLAX_LOW_SAMPLES: {
    float prevVal = SceneProp.ParallaxLowSamples;
    SceneProp.ParallaxLowSamples -= 5.0f;
    T8_LOG_VERBOSE("[CHANGE_PARALLAX_LOW_SAMPLES] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.ParallaxLowSamples);
  }break;
  case CHANGE_PARALLAX_HIGH_SAMPLES: {
    float prevVal = SceneProp.ParallaxHighSamples;
    SceneProp.ParallaxHighSamples -= 10.0f;
    T8_LOG_VERBOSE("[CHANGE_PARALLAX_HIGH_SAMPLES] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.ParallaxHighSamples);
  }break;
  case CHANGE_PARALLAX_HEIGHT: {
    float prevVal = SceneProp.ParallaxHeight;
    SceneProp.ParallaxHeight -= 0.01f;
    T8_LOG_VERBOSE("[CHANGE_PARALLAX_HEIGHT] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.ParallaxHeight);
  }break;
  case CHANGE_LIGHT_VOLUME_STEPS: {
    float prevVal = SceneProp.LightVolumeSteps;
    SceneProp.LightVolumeSteps -= 16.0f;
    T8_LOG_VERBOSE("[CHANGE_LIGHT_VOLUME_STEPS] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.LightVolumeSteps);
  }break;
  case CHANGE_PCF_TOOGLE: {
	  int prevVal = SceneProp.ToogleShadow;
	  SceneProp.ToogleShadow = 0;
	  T8_LOG_VERBOSE("[CHANGE_PCF_TOOGLE] Previous Value[%d] Actual Value[%d]", prevVal, SceneProp.ToogleShadow);
  }break;
  case CHANGLE_SSAO_TOOGLE: {
	  int prevVal = SceneProp.ToogleSSAO;
	  SceneProp.ToogleSSAO = 0;
	  T8_LOG_VERBOSE("[CHANGLE_SSAO_TOOGLE] Previous Value[%d] Actual Value[%d]", prevVal, SceneProp.ToogleSSAO);
  }break;
  case CHANGE_LIGHT_NEAR_PLANE: {
    float prevVal = LightCam.NPlane;
    LightCam.NPlane -= 1.0f;
    if (LightCam.NPlane < 0.1f) LightCam.NPlane = 0.1f;
    LightCam.CreatePojection();
    LightCam.Update(0);
    T8_LOG_VERBOSE("[CHANGE_LIGHT_NEAR_PLANE] Previous Value[%f] Actual Value[%f]", prevVal, LightCam.NPlane);
  }break;
  case CHANGE_LIGHT_FAR_PLANE: {
    float prevVal = LightCam.FPlane;
    LightCam.FPlane -= 10.0f;
    if (LightCam.FPlane < 1.0f) LightCam.FPlane = 1.0f;
    LightCam.CreatePojection();
    LightCam.Update(0);
    T8_LOG_VERBOSE("[CHANGE_LIGHT_FAR_PLANE] Previous Value[%f] Actual Value[%f]", prevVal, LightCam.FPlane);
  }break;
  case CHANGE_FOV: {
    float prevVal = ActiveCam->Fov;
    ActiveCam->SetFov(ActiveCam->Fov - Deg2Rad(5.0f));
    if (ActiveCam->Fov < Deg2Rad(60.0f)) ActiveCam->SetFov(Deg2Rad(60.0f));
    T8_LOG_VERBOSE("[CHANGE_FOV] Previous Value[%f] Actual Value[%f]", Rad2Deg(prevVal), Rad2Deg(ActiveCam->Fov));
  }break;
  case CHANGE_SHOW_SPLINE: {
    m_showSpline = false;
    T8_LOG_VERBOSE("[CHANGE_SHOW_SPLINE] Value[%d]", (int)m_showSpline);
  }break;
  case CHANGE_SHOW_LIGHTS: {
    m_showLights = false;
    T8_LOG_VERBOSE("[CHANGE_SHOW_LIGHTS] Value[%d]", (int)m_showLights);
  }break;
  case CHANGE_SHOW_PHYSICS: {
    m_showPhysics = false;
    T8_LOG_VERBOSE("[CHANGE_SHOW_PHYSICS] Value[%d]", (int)m_showPhysics);
  }break;
  case CHANGE_LIGHT_INTENSITY: {
    if (!SceneProp.Lights.empty()) {
      float prevVal = SceneProp.Lights[0].Intensity;
      SceneProp.Lights[0].Intensity -= 0.5f;
      if (SceneProp.Lights[0].Intensity < 0.1f) SceneProp.Lights[0].Intensity = 0.1f;
      T8_LOG_VERBOSE("[CHANGE_LIGHT_INTENSITY] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.Lights[0].Intensity);
    }
  }break;
  case CHANGE_LIGHT_RADIUS_SCALE: {
    float prevVal = SceneProp.LightRadiusScale;
    SceneProp.LightRadiusScale -= 0.25f;
    if (SceneProp.LightRadiusScale < 0.01f) SceneProp.LightRadiusScale = 0.01f;
    T8_LOG_VERBOSE("[CHANGE_LIGHT_RADIUS_SCALE] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.LightRadiusScale);
  }break;
  case CHANGE_LIGHT_INTENSITY_SCALE: {
    float prevVal = SceneProp.LightIntensityScale;
    SceneProp.LightIntensityScale -= 0.25f;
    if (SceneProp.LightIntensityScale < 0.0f) SceneProp.LightIntensityScale = 0.0f;
    T8_LOG_VERBOSE("[CHANGE_LIGHT_INTENSITY_SCALE] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.LightIntensityScale);
  }break;
  case CHANGE_LIGHTMAP_INTENSITY: {
    float prevVal = SceneProp.LightmapIntensity;
    SceneProp.LightmapIntensity -= 0.25f;
    if (SceneProp.LightmapIntensity < 0.0f) SceneProp.LightmapIntensity = 0.0f;
    T8_LOG_VERBOSE("[CHANGE_LIGHTMAP_INTENSITY] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.LightmapIntensity);
  }break;
  }
}

void DayScene::printCurrSelection() {
  Camera& LightCam = m_sceneSetup.lightCameras[0];
  switch (SceneSettingSelection) {
  case CHANGE_EXPOSURE: {
    T8_LOG_VERBOSE("Option[CHANGE_EXPOSURE] Value[%f]", SceneProp.Exposure);
  }break;
  case CHANGE_BLOOM_FACTOR: {
    T8_LOG_VERBOSE("Option[CHANGE_BLOOM_FACTOR] Value[%f]", SceneProp.BloomFactor);
  }break;
  case CHANGE_BLOOM_THRESHOLD: {
    T8_LOG_VERBOSE("Option[CHANGE_BLOOM_THRESHOLD] Value[%f]", SceneProp.BloomThreshold);
  }break;
  case CHANGE_TM_WHITE_LEVEL: {
    T8_LOG_VERBOSE("Option[CHANGE_TM_WHITE_LEVEL] Value[%f]", SceneProp.ToneMapWhiteLevel);
  }break;
  case CHANGE_TM_ADAPT_TAU: {
    T8_LOG_VERBOSE("Option[CHANGE_TM_ADAPT_TAU] Value[%f]", SceneProp.LuminanceTau);
  }break;
  case CHANGE_NUM_LIGHTS: {
    T8_LOG_VERBOSE("Option[CHANGE_NUM_LIGHTS] Value[%d]", SceneProp.ActiveLights);
  }break;
  case CHANGE_ACTIVE_GAUSS_KERNEL: {
    T8_LOG_VERBOSE("Option[CHANGE_ACTIVE_GAUSS_KERNEL] Value[%d]", ChangeActiveGaussSelection);
  }break;
  case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
    T8_LOG_VERBOSE("Option[CHANGE_GAUSS_KERNEL_SAMPLE_COUNT] Value[%d]", SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize);
  }break;
  case CHANGE_GAUSS_KERNEL_RADIUS: {
    T8_LOG_VERBOSE("Option[CHANGE_GAUSS_KERNEL_RADIUS] Value[%f]", SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius);
  }break;
  case CHANGE_GAUSS_KERNEL_DEVIATION: {
    T8_LOG_VERBOSE("Option[CHANGE_GAUSS_KERNEL_DEVIATION] Value[%f]", SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma);
  }break;
  case CHANGE_PCF_RADIUS: {
    T8_LOG_VERBOSE("Option[CHANGE_PCF_RADIUS] Value[%f]", SceneProp.PCFScale);
  }break;
  case CHANGE_PCF_SAMPLES: {
    T8_LOG_VERBOSE("Option[CHANGE_PCF_SAMPLES] Value[%f]", SceneProp.PCFSamples);
  }break;
  case CHANGE_SSAO_KERNEL_SIZE: {
    T8_LOG_VERBOSE("Option[CHANGE_SSAO_KERNEL_SIZE] Value[%d]", SceneProp.SSAOKernel.KernelSize);
  }break;
  case CHANGE_SSAO_RADIUS: {
    T8_LOG_VERBOSE("Option[CHANGE_SSAO_RADIUS] Value[%f]", SceneProp.SSAOKernel.Radius);
  }break;
  case CHANGE_DOF_APERTURE: {
    T8_LOG_VERBOSE("Option[CHANGE_DOF_APERTURE] Value[%f]", SceneProp.Aperture);
  }break;
  case CHANGE_DOF_FOCAL_LENGHT: {
    T8_LOG_VERBOSE("Option[CHANGE_DOF_FOCAL_LENGHT] Value[%f]", SceneProp.FocalLength);
  }break;
  case CHANGE_DOF_MAX_COC: {
    T8_LOG_VERBOSE("Option[CHANGE_DOF_MAX_COC] Value[%f]", SceneProp.MaxCoc);
  }break;
  case CHANGE_DOF_FAR_SAMPLE: {
    T8_LOG_VERBOSE("Option[CHANGE_DOF_FAR_SAMPLE] Value[%f]", SceneProp.DOF_Far_Samples_squared);
  }break;
  case CHANGE_DOF_NEAR_SAMPLE: {
    T8_LOG_VERBOSE("Option[CHANGE_DOF_NEAR_SAMPLE] Value[%f]", SceneProp.DOF_Near_Samples_squared);
  }break;
  case CHANGE_DOF_AUTO_FOCUS: {
    T8_LOG_VERBOSE("Option[CHANGE_DOF_AUTO_FOCUS] Value[%d]", (int)SceneProp.AutoFocus);
  }break;
  case CHANGE_PARALLAX_LOW_SAMPLES: {
    T8_LOG_VERBOSE("Option[CHANGE_PARALLAX_LOW_SAMPLES] Value[%f]", SceneProp.ParallaxLowSamples);
  }break;
  case CHANGE_PARALLAX_HIGH_SAMPLES: {
    T8_LOG_VERBOSE("Option[CHANGE_PARALLAX_HIGH_SAMPLES] Value[%f]", SceneProp.ParallaxHighSamples);
  }break;
  case CHANGE_PARALLAX_HEIGHT: {
    T8_LOG_VERBOSE("Option[CHANGE_PARALLAX_HEIGHT] Value[%f]", SceneProp.ParallaxHeight);
  }break;
  case CHANGE_LIGHT_VOLUME_STEPS: {
    T8_LOG_VERBOSE("Option[CHANGE_LIGHT_VOLUME_STEPS] Value[%f]", SceneProp.LightVolumeSteps);
  }break;
  case CHANGE_PCF_TOOGLE: {
	  T8_LOG_VERBOSE("Option[CHANGE_PCF_TOOGLE] Value[%d]", SceneProp.ToogleShadow);
  }break;
  case CHANGLE_SSAO_TOOGLE: {
	  T8_LOG_VERBOSE("Option[CHANGLE_SSAO_TOOGLE] Value[%d]", SceneProp.ToogleSSAO);
  }break;
  case CHANGE_LIGHT_NEAR_PLANE: {
    T8_LOG_VERBOSE("Option[CHANGE_LIGHT_NEAR_PLANE] Value[%f]", LightCam.NPlane);
  }break;
  case CHANGE_LIGHT_FAR_PLANE: {
    T8_LOG_VERBOSE("Option[CHANGE_LIGHT_FAR_PLANE] Value[%f]", LightCam.FPlane);
  }break;
  case CHANGE_FOV: {
    T8_LOG_VERBOSE("Option[CHANGE_FOV] Value[%f]", Rad2Deg(ActiveCam->Fov));
  }break;
  case CHANGE_SHOW_SPLINE: {
    T8_LOG_VERBOSE("Option[CHANGE_SHOW_SPLINE] Value[%d]", (int)m_showSpline);
  }break;
  case CHANGE_SHOW_LIGHTS: {
    T8_LOG_VERBOSE("Option[CHANGE_SHOW_LIGHTS] Value[%d]", (int)m_showLights);
  }break;
  case CHANGE_SHOW_PHYSICS: {
    T8_LOG_VERBOSE("Option[CHANGE_SHOW_PHYSICS] Value[%d]", (int)m_showPhysics);
  }break;
  case CHANGE_LIGHT_INTENSITY: {
    if (!SceneProp.Lights.empty())
      T8_LOG_VERBOSE("Option[CHANGE_LIGHT_INTENSITY] Value[%f]", SceneProp.Lights[0].Intensity);
  }break;
  case CHANGE_LIGHT_RADIUS_SCALE: {
    T8_LOG_VERBOSE("Option[CHANGE_LIGHT_RADIUS_SCALE] Value[%f]", SceneProp.LightRadiusScale);
  }break;
  case CHANGE_LIGHT_INTENSITY_SCALE: {
    T8_LOG_VERBOSE("Option[CHANGE_LIGHT_INTENSITY_SCALE] Value[%f]", SceneProp.LightIntensityScale);
  }break;
  case CHANGE_LIGHTMAP_INTENSITY: {
    T8_LOG_VERBOSE("Option[CHANGE_LIGHTMAP_INTENSITY] Value[%f]", SceneProp.LightmapIntensity);
  }break;
  }
}

int DayScene::FindLightOption(int activeLights) {
  // Match against the selector options: "1","2","4","8","16","32","64","127"
  auto& selPairs = m_sceneSetup.descriptor.selectors;
  for (auto& sd : selPairs) {
    if (sd.name == "num_lights") {
      for (size_t i = 0; i < sd.options.size(); i++) {
        if (std::atoi(sd.options[i].c_str()) == activeLights) return (int)i;
      }
    }
  }
  return 0;
}

void DayScene::DrawDevGui(t850::DevGuiContext& gui) {
  struct Mapping { const char* name; int settingIndex; };

  static const Mapping sliderMappings[] = {
    {"exposure", CHANGE_EXPOSURE},
    {"bloom_factor", CHANGE_BLOOM_FACTOR},
    {"bloom_threshold", CHANGE_BLOOM_THRESHOLD},
    {"tm_white_level", CHANGE_TM_WHITE_LEVEL},
    {"tm_adapt_tau", CHANGE_TM_ADAPT_TAU},
    {"pcf_radius", CHANGE_PCF_RADIUS},
    {"pcf_samples", CHANGE_PCF_SAMPLES},
    {"ssao_kernel_size", CHANGE_SSAO_KERNEL_SIZE},
    {"ssao_radius", CHANGE_SSAO_RADIUS},
    {"dof_aperture", CHANGE_DOF_APERTURE},
    {"dof_focal_length", CHANGE_DOF_FOCAL_LENGHT},
    {"dof_max_coc", CHANGE_DOF_MAX_COC},
    {"dof_far_samples", CHANGE_DOF_FAR_SAMPLE},
    {"dof_near_samples", CHANGE_DOF_NEAR_SAMPLE},
    {"parallax_low_samples", CHANGE_PARALLAX_LOW_SAMPLES},
    {"parallax_high_samples", CHANGE_PARALLAX_HIGH_SAMPLES},
    {"parallax_height", CHANGE_PARALLAX_HEIGHT},
    {"parallax_shadow_min_layers", CHANGE_PARALLAX_SHADOW_MIN_LAYERS},
    {"parallax_shadow_max_layers", CHANGE_PARALLAX_SHADOW_MAX_LAYERS},
    {"parallax_shadow_softness", CHANGE_PARALLAX_SHADOW_SOFTNESS},
    {"parallax_shadow_strength", CHANGE_PARALLAX_SHADOW_STRENGTH},
    {"light_volume_steps", CHANGE_LIGHT_VOLUME_STEPS},
    {"godrays_factor", CHANGE_GODRAYS_FACTOR},
    {"gauss_kernel_radius", CHANGE_GAUSS_KERNEL_RADIUS},
    {"gauss_kernel_deviation", CHANGE_GAUSS_KERNEL_DEVIATION},
    {"fov", CHANGE_FOV},
    {"light_intensity", CHANGE_LIGHT_INTENSITY},
    {"light_radius_scale", CHANGE_LIGHT_RADIUS_SCALE},
    {"light_intensity_scale", CHANGE_LIGHT_INTENSITY_SCALE},
    {"lightmap_intensity", CHANGE_LIGHTMAP_INTENSITY},
    {"shadow_bias", CHANGE_SHADOW_BIAS},
    {"shadow_min", CHANGE_SHADOW_MIN},
    {"env_factor", CHANGE_ENV_FACTOR},
    {"ibl_factor", CHANGE_IBL_FACTOR},
    {"material_emissive_intensity", CHANGE_MATERIAL_EMISSIVE_INTENSITY},
    {"material_transmission_multiplier", CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER},
    {"material_refraction_strength", CHANGE_MATERIAL_REFRACTION_STRENGTH},
  };

  static const Mapping checkboxMappings[] = {
    {"shadow_toggle", CHANGE_PCF_TOOGLE},
    {"ssao_toggle", CHANGLE_SSAO_TOOGLE},
    {"dof_auto_focus", CHANGE_DOF_AUTO_FOCUS},
    {"show_spline", CHANGE_SHOW_SPLINE},
    {"show_lights", CHANGE_SHOW_LIGHTS},
    {"show_physics", CHANGE_SHOW_PHYSICS},
    {"show_navmesh", CHANGE_SHOW_NAVMESH},
    {"dof_toggle", CHANGE_DOF_TOGGLE},
    {"parallax_toggle", CHANGE_PARALLAX_TOGGLE},
    {"parallax_shadow_toggle", CHANGE_PARALLAX_SHADOW_TOGGLE},
    {"godrays_toggle", CHANGE_GODRAYS_TOGGLE},
    {"point_lights_enabled", CHANGE_POINT_LIGHTS_ENABLED},
  };

  static const Mapping selectorMappings[] = {
    {"num_lights", CHANGE_NUM_LIGHTS},
    {"active_gauss_kernel", CHANGE_ACTIVE_GAUSS_KERNEL},
    {"gauss_kernel_sample_count", CHANGE_GAUSS_KERNEL_SAMPLE_COUNT},
    {"debug_render_target", CHANGE_DEBUG_RT},
    {"active_camera", CHANGE_ACTIVE_CAMERA},
    {"cubemap", CHANGE_CUBEMAP},
    {"luminance_mode", CHANGE_LUMINANCE_MODE},
  };

  auto findSetting = [](const std::string& name, const Mapping* mappings, int count) {
    for (int i = 0; i < count; ++i) {
      if (name == mappings[i].name) return mappings[i].settingIndex;
    }
    return -1;
  };

  auto activeKernel = [&]() -> GaussFilter* {
    if (ChangeActiveGaussSelection < 0 || ChangeActiveGaussSelection >= (int)SceneProp.pGaussKernels.size()) return nullptr;
    return SceneProp.pGaussKernels[ChangeActiveGaussSelection];
  };

  auto getSliderValue = [&](int settingIndex, float& value) -> bool {
    GaussFilter* kernel = activeKernel();
    switch (settingIndex) {
    case CHANGE_EXPOSURE: value = SceneProp.Exposure; return true;
    case CHANGE_BLOOM_FACTOR: value = SceneProp.BloomFactor; return true;
    case CHANGE_BLOOM_THRESHOLD: value = SceneProp.BloomThreshold; return true;
    case CHANGE_TM_WHITE_LEVEL: value = SceneProp.ToneMapWhiteLevel; return true;
    case CHANGE_TM_ADAPT_TAU: value = SceneProp.LuminanceTau; return true;
    case CHANGE_PCF_RADIUS: value = SceneProp.PCFScale; return true;
    case CHANGE_PCF_SAMPLES: value = SceneProp.PCFSamples; return true;
    case CHANGE_SSAO_KERNEL_SIZE: value = (float)SceneProp.SSAOKernel.KernelSize; return true;
    case CHANGE_SSAO_RADIUS: value = SceneProp.SSAOKernel.Radius; return true;
    case CHANGE_DOF_APERTURE: value = SceneProp.Aperture; return true;
    case CHANGE_DOF_FOCAL_LENGHT: value = SceneProp.FocalLength; return true;
    case CHANGE_DOF_MAX_COC: value = SceneProp.MaxCoc; return true;
    case CHANGE_DOF_FAR_SAMPLE: value = SceneProp.DOF_Far_Samples_squared; return true;
    case CHANGE_DOF_NEAR_SAMPLE: value = SceneProp.DOF_Near_Samples_squared; return true;
    case CHANGE_PARALLAX_LOW_SAMPLES: value = SceneProp.ParallaxLowSamples; return true;
    case CHANGE_PARALLAX_HIGH_SAMPLES: value = SceneProp.ParallaxHighSamples; return true;
    case CHANGE_PARALLAX_HEIGHT: value = SceneProp.ParallaxHeight; return true;
    case CHANGE_PARALLAX_SHADOW_MIN_LAYERS: value = SceneProp.ParallaxShadowMinLayers; return true;
    case CHANGE_PARALLAX_SHADOW_MAX_LAYERS: value = SceneProp.ParallaxShadowMaxLayers; return true;
    case CHANGE_PARALLAX_SHADOW_SOFTNESS: value = SceneProp.ParallaxShadowSoftness; return true;
    case CHANGE_PARALLAX_SHADOW_STRENGTH: value = SceneProp.ParallaxShadowStrength; return true;
    case CHANGE_LIGHT_VOLUME_STEPS: value = SceneProp.LightVolumeSteps; return true;
    case CHANGE_GODRAYS_FACTOR: value = SceneProp.GodRaysFactor; return true;
    case CHANGE_GAUSS_KERNEL_RADIUS: if (!kernel) return false; value = kernel->radius; return true;
    case CHANGE_GAUSS_KERNEL_DEVIATION: if (!kernel) return false; value = kernel->sigma; return true;
    case CHANGE_FOV: if (!ActiveCam) return false; value = Rad2Deg(ActiveCam->Fov); return true;
    case CHANGE_LIGHT_INTENSITY: if (SceneProp.Lights.empty()) return false; value = SceneProp.Lights[0].Intensity; return true;
    case CHANGE_LIGHT_RADIUS_SCALE: value = SceneProp.LightRadiusScale; return true;
    case CHANGE_LIGHT_INTENSITY_SCALE: value = SceneProp.LightIntensityScale; return true;
    case CHANGE_LIGHTMAP_INTENSITY: value = SceneProp.LightmapIntensity; return true;
    case CHANGE_SHADOW_BIAS: value = SceneProp.ShadowBias; return true;
    case CHANGE_SHADOW_MIN: value = SceneProp.ShadowMin; return true;
    case CHANGE_ENV_FACTOR: value = SceneProp.EnvFactor; return true;
    case CHANGE_IBL_FACTOR: value = SceneProp.IBLFactor; return true;
    case CHANGE_MATERIAL_EMISSIVE_INTENSITY: value = SceneProp.MaterialEmissiveIntensity; return true;
    case CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER: value = SceneProp.MaterialTransmissionMultiplier; return true;
    case CHANGE_MATERIAL_REFRACTION_STRENGTH: value = SceneProp.MaterialRefractionStrength; return true;
    }
    return false;
  };

  auto setSliderValue = [&](int settingIndex, float value) {
    GaussFilter* kernel = activeKernel();
    switch (settingIndex) {
    case CHANGE_EXPOSURE: SceneProp.Exposure = value; break;
    case CHANGE_BLOOM_FACTOR: SceneProp.BloomFactor = value; break;
    case CHANGE_BLOOM_THRESHOLD: SceneProp.BloomThreshold = value; break;
    case CHANGE_TM_WHITE_LEVEL: SceneProp.ToneMapWhiteLevel = value; break;
    case CHANGE_TM_ADAPT_TAU: SceneProp.LuminanceTau = value; break;
    case CHANGE_PCF_RADIUS: SceneProp.PCFScale = value; break;
    case CHANGE_PCF_SAMPLES: SceneProp.PCFSamples = value; break;
    case CHANGE_SSAO_KERNEL_SIZE: SceneProp.SSAOKernel.KernelSize = (int)value; SceneProp.SSAOKernel.Update(); break;
    case CHANGE_SSAO_RADIUS: SceneProp.SSAOKernel.Radius = value; break;
    case CHANGE_DOF_APERTURE: SceneProp.Aperture = value; break;
    case CHANGE_DOF_FOCAL_LENGHT: SceneProp.FocalLength = value; break;
    case CHANGE_DOF_MAX_COC: SceneProp.MaxCoc = value; break;
    case CHANGE_DOF_FAR_SAMPLE: SceneProp.DOF_Far_Samples_squared = value; break;
    case CHANGE_DOF_NEAR_SAMPLE: SceneProp.DOF_Near_Samples_squared = value; break;
    case CHANGE_PARALLAX_LOW_SAMPLES: SceneProp.ParallaxLowSamples = value; break;
    case CHANGE_PARALLAX_HIGH_SAMPLES: SceneProp.ParallaxHighSamples = value; break;
    case CHANGE_PARALLAX_HEIGHT: SceneProp.ParallaxHeight = value; break;
    case CHANGE_PARALLAX_SHADOW_MIN_LAYERS: SceneProp.ParallaxShadowMinLayers = value; break;
    case CHANGE_PARALLAX_SHADOW_MAX_LAYERS: SceneProp.ParallaxShadowMaxLayers = value; break;
    case CHANGE_PARALLAX_SHADOW_SOFTNESS: SceneProp.ParallaxShadowSoftness = value; break;
    case CHANGE_PARALLAX_SHADOW_STRENGTH: SceneProp.ParallaxShadowStrength = value; break;
    case CHANGE_LIGHT_VOLUME_STEPS: SceneProp.LightVolumeSteps = value; break;
    case CHANGE_GODRAYS_FACTOR: SceneProp.GodRaysFactor = value; break;
    case CHANGE_GAUSS_KERNEL_RADIUS: if (kernel) { kernel->radius = value; kernel->Update(); } break;
    case CHANGE_GAUSS_KERNEL_DEVIATION: if (kernel) { kernel->sigma = value; kernel->Update(); } break;
    case CHANGE_FOV:
      if (ActiveCam) {
        ActiveCam->SetFov(Deg2Rad(value));
        ActiveCam->VP = ActiveCam->View * ActiveCam->Projection;
        VP = ActiveCam->VP;
      }
      break;
    case CHANGE_LIGHT_INTENSITY: if (!SceneProp.Lights.empty()) SceneProp.Lights[0].Intensity = value; break;
    case CHANGE_LIGHT_RADIUS_SCALE: SceneProp.LightRadiusScale = value; break;
    case CHANGE_LIGHT_INTENSITY_SCALE: SceneProp.LightIntensityScale = value; break;
    case CHANGE_LIGHTMAP_INTENSITY: SceneProp.LightmapIntensity = value; break;
    case CHANGE_SHADOW_BIAS: SceneProp.ShadowBias = value; break;
    case CHANGE_SHADOW_MIN: SceneProp.ShadowMin = value; break;
    case CHANGE_ENV_FACTOR: SceneProp.EnvFactor = value; break;
    case CHANGE_IBL_FACTOR: SceneProp.IBLFactor = value; break;
    case CHANGE_MATERIAL_EMISSIVE_INTENSITY: SceneProp.MaterialEmissiveIntensity = value; break;
    case CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER: SceneProp.MaterialTransmissionMultiplier = value; break;
    case CHANGE_MATERIAL_REFRACTION_STRENGTH: SceneProp.MaterialRefractionStrength = value; break;
    }
  };

  auto getCheckboxValue = [&](int settingIndex, bool& value) -> bool {
    switch (settingIndex) {
    case CHANGE_PCF_TOOGLE: value = (SceneProp.ToogleShadow != 0); return true;
    case CHANGLE_SSAO_TOOGLE: value = (SceneProp.ToogleSSAO != 0); return true;
    case CHANGE_DOF_AUTO_FOCUS: value = SceneProp.AutoFocus; return true;
    case CHANGE_SHOW_SPLINE: value = m_showSpline; return true;
    case CHANGE_SHOW_LIGHTS: value = m_showLights; return true;
    case CHANGE_SHOW_PHYSICS: value = m_showPhysics; return true;
    case CHANGE_SHOW_NAVMESH: value = m_showNavMesh; return true;
    case CHANGE_DOF_TOGGLE: value = (SceneProp.ToogleDOF != 0); return true;
    case CHANGE_PARALLAX_TOGGLE: value = (SceneProp.ToogleParallax != 0); return true;
    case CHANGE_PARALLAX_SHADOW_TOGGLE: value = (SceneProp.ToogleParallaxShadow != 0); return true;
    case CHANGE_GODRAYS_TOGGLE: value = (SceneProp.ToogleGodRays != 0); return true;
    case CHANGE_POINT_LIGHTS_ENABLED: value = SceneProp.PointLightsEnabled; return true;
    }
    return false;
  };

  auto setCheckboxValue = [&](int settingIndex, bool value) {
    switch (settingIndex) {
    case CHANGE_PCF_TOOGLE: SceneProp.ToogleShadow = value ? 1 : 0; break;
    case CHANGLE_SSAO_TOOGLE: SceneProp.ToogleSSAO = value ? 1 : 0; break;
    case CHANGE_DOF_AUTO_FOCUS: SceneProp.AutoFocus = value; break;
    case CHANGE_SHOW_SPLINE: m_showSpline = value; break;
    case CHANGE_SHOW_LIGHTS: m_showLights = value; break;
    case CHANGE_SHOW_PHYSICS: m_showPhysics = value; break;
    case CHANGE_SHOW_NAVMESH:
      m_showNavMesh = value;
      if (m_showNavMesh && !m_navMesh.IsReady()) m_navMeshBuildAttempted = false;
      break;
    case CHANGE_DOF_TOGGLE:
      SceneProp.ToogleDOF = value ? 1 : 0;
      m_renderGraph.SetPassEnabled("CoC", value);
      m_renderGraph.SetPassEnabled("Combine CoC", value);
      m_renderGraph.SetPassEnabled("DOF", value);
      m_renderGraph.SetPassEnabled("DOF 2", value);
      break;
    case CHANGE_PARALLAX_TOGGLE:
      SceneProp.ToogleParallax = value ? 1 : 0;
      Meshes[0].SetParallaxEnabled(value);
      break;
    case CHANGE_PARALLAX_SHADOW_TOGGLE:
      SceneProp.ToogleParallaxShadow = value ? 1 : 0;
      SceneProp.ParallaxShadowStrength = value ? 1.0f : 0.0f;
      break;
    case CHANGE_GODRAYS_TOGGLE: SceneProp.ToogleGodRays = value ? 1 : 0; break;
    case CHANGE_POINT_LIGHTS_ENABLED: SceneProp.PointLightsEnabled = value; break;
    }
  };

  auto getSelectorIndex = [&](const t850::SelectorDesc& desc, int settingIndex, int& selectedIndex) -> bool {
    switch (settingIndex) {
    case CHANGE_NUM_LIGHTS: selectedIndex = FindLightOption(SceneProp.ActiveLights); return true;
    case CHANGE_ACTIVE_GAUSS_KERNEL: selectedIndex = ChangeActiveGaussSelection; return true;
    case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
      GaussFilter* kernel = activeKernel();
      if (!kernel) return false;
      for (int i = 0; i < (int)desc.options.size(); ++i) {
        if (std::atoi(desc.options[i].c_str()) == kernel->kernelSize) { selectedIndex = i; return true; }
      }
      selectedIndex = desc.default_index;
      return true;
    }
    case CHANGE_DEBUG_RT: selectedIndex = m_debugRTSelection; return true;
    case CHANGE_ACTIVE_CAMERA: selectedIndex = m_activeCameraIndex; return true;
    case CHANGE_CUBEMAP: selectedIndex = m_currentCubemapIndex; return true;
    case CHANGE_LUMINANCE_MODE: selectedIndex = SceneProp.LuminanceMode; return true;
    }
    return false;
  };

  auto setSelectorIndex = [&](const t850::SelectorDesc& desc, int settingIndex, int selectedIndex) {
    if (selectedIndex < 0 || selectedIndex >= (int)desc.options.size()) return;
    switch (settingIndex) {
    case CHANGE_NUM_LIGHTS: SceneProp.ActiveLights = std::atoi(desc.options[selectedIndex].c_str()); break;
    case CHANGE_ACTIVE_GAUSS_KERNEL: ChangeActiveGaussSelection = selectedIndex; break;
    case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
      GaussFilter* kernel = activeKernel();
      if (kernel) { kernel->kernelSize = std::atoi(desc.options[selectedIndex].c_str()); kernel->Update(); }
    } break;
    case CHANGE_DEBUG_RT: m_debugRTSelection = selectedIndex; break;
    case CHANGE_ACTIVE_CAMERA: ApplyActiveCameraSelection(selectedIndex); break;
    case CHANGE_CUBEMAP:
      if (selectedIndex != m_currentCubemapIndex) {
        m_currentCubemapIndex = selectedIndex;
        m_pendingCubemap = "sky/" + desc.options[selectedIndex];
      }
      break;
    case CHANGE_LUMINANCE_MODE: SceneProp.LuminanceMode = selectedIndex; break;
    }
  };

  if (gui.BeginSection("Controls")) {
    for (const auto& desc : m_sceneSetup.descriptor.sliders) {
      int settingIndex = findSetting(desc.name, sliderMappings, (int)(sizeof(sliderMappings) / sizeof(sliderMappings[0])));
      if (settingIndex < 0) continue;
      float value = 0.0f;
      if (getSliderValue(settingIndex, value) && gui.Slider(desc, value)) {
        setSliderValue(settingIndex, value);
      }
    }
    Meshes[0].SetParallaxSettings(SceneProp.ParallaxLowSamples, SceneProp.ParallaxHighSamples, SceneProp.ParallaxHeight);
    Meshes[0].SetParallaxShadowSettings(SceneProp.ParallaxShadowMinLayers, SceneProp.ParallaxShadowMaxLayers,
                                         SceneProp.ParallaxShadowSoftness, SceneProp.ParallaxShadowStrength);
  }

  if (gui.BeginSection("Toggles")) {
    t850::CheckboxDesc pointLightsDesc;
    pointLightsDesc.name = "point_lights_enabled";
    pointLightsDesc.label = "Dynamic point lights";
    bool pointLightsEnabled = SceneProp.PointLightsEnabled;
    if (gui.Checkbox(pointLightsDesc, pointLightsEnabled)) {
      SceneProp.PointLightsEnabled = pointLightsEnabled;
    }

    for (const auto& desc : m_sceneSetup.descriptor.checkboxes) {
      int settingIndex = findSetting(desc.name, checkboxMappings, (int)(sizeof(checkboxMappings) / sizeof(checkboxMappings[0])));
      if (settingIndex < 0) continue;
      bool value = false;
      if (getCheckboxValue(settingIndex, value) && gui.Checkbox(desc, value)) {
        setCheckboxValue(settingIndex, value);
      }
    }
    t850::SliderDesc navOffsetDesc;
    navOffsetDesc.name = "navmesh_debug_offset";
    navOffsetDesc.label = "NavMesh offset";
    navOffsetDesc.min_val = 0.0f;
    navOffsetDesc.max_val = 0.25f;
    navOffsetDesc.step = 0.001f;
    navOffsetDesc.default_val = 0.01f;
    gui.Slider(navOffsetDesc, m_navMeshDebugOffset);
    const char* navShapeOptions[] = { "Geometry", "Nodes" };
    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("NavMesh magenta", &m_navMeshDebugShapeMode, navShapeOptions, 2);
  }

  if (gui.BeginSection("Selectors")) {
    for (const auto& desc : m_sceneSetup.descriptor.selectors) {
      int settingIndex = findSetting(desc.name, selectorMappings, (int)(sizeof(selectorMappings) / sizeof(selectorMappings[0])));
      if (settingIndex < 0) continue;
      int selectedIndex = 0;
      if (getSelectorIndex(desc, settingIndex, selectedIndex) && gui.Combo(desc, selectedIndex)) {
        setSelectorIndex(desc, settingIndex, selectedIndex);
      }
    }
  }

  if (m_sceneProfileReady) {
    t850::SandboxProfileDesc currentProfileState;
    CaptureSceneProfileState(currentProfileState);
    t850::SandboxProfileDesc currentSparse = BuildSparseSceneProfile(currentProfileState);
    t850::SandboxProfileDesc savedSparse = BuildSparseSceneProfile(m_sceneProfileSavedState);
    m_sceneProfileDirty = currentSparse.sliders != savedSparse.sliders || currentSparse.checkboxes != savedSparse.checkboxes || currentSparse.selectors != savedSparse.selectors;
    if (gui.BeginSection("Profile")) {
      const auto& runtime = t850::GetRuntimeProfileInfo();
      std::string gpuText = runtime.gpuName.empty() ? runtime.gpuFamily : runtime.gpuName;
      if (gpuText.empty()) gpuText = "unknown GPU";
      else if (!runtime.gpuFamily.empty() && runtime.gpuFamily != runtime.gpuName) gpuText += " (" + runtime.gpuFamily + ")";
      std::string runtimeText = "Runtime: " + runtime.platform + " / " + runtime.architecture + " / " + gpuText;
      gui.Text(runtimeText.c_str());
      gui.Text("Rendering profile: Shared DayScene profile (all devices)");
      bool canSaveProfile = m_sceneProfileDirty;
      if (gui.Button("Save Profile", canSaveProfile)) {
        SaveSceneProfile();
      }
    }
  }

  if (gui.BeginSection("Culling")) {
    t850::CheckboxDesc spectatorDesc;
    spectatorDesc.name = "spectator_camera";
    spectatorDesc.label = "Spectator camera (5)";
    spectatorDesc.enabled = SceneProp.FrustumCullingToggleAllowed;
    bool spectatorEnabled = m_spectatorCameraEnabled;
    if (gui.Checkbox(spectatorDesc, spectatorEnabled)) {
      SetSpectatorDebugEnabled(spectatorEnabled);
    }

    t850::CheckboxDesc cullingDesc;
    cullingDesc.name = "frustum_culling";
    cullingDesc.label = "Frustum culling";
    cullingDesc.enabled = SceneProp.FrustumCullingToggleAllowed;
    bool cullingEnabled = SceneProp.FrustumCullingEnabled;
    if (gui.Checkbox(cullingDesc, cullingEnabled)) {
      if (!cullingEnabled || !Meshes[0].pBase || static_cast<RenderMesh*>(Meshes[0].pBase)->EnsureCullingMetadata()) {
        SceneProp.FrustumCullingEnabled = cullingEnabled;
      }
    }

    t850::CheckboxDesc statsDesc;
    statsDesc.name = "show_culling_debug";
    statsDesc.label = "Culling stats and frustum";
    statsDesc.enabled = SceneProp.FrustumCullingToggleAllowed;
    bool showCulling = m_spectatorCameraEnabled ? true : m_showCullStats;
    if (gui.Checkbox(statsDesc, showCulling)) {
      m_showCullStats = m_spectatorCameraEnabled ? true : showCulling;
      SceneProp.ShowCullingDebug = m_showCullStats;
    }
  }
}

#ifdef OS_ANDROID
void DayScene::DrawAndroidPhysicsPanel(t850::DevGuiContext& gui) {
  if (!gui.BeginSection("Physics")) {
    return;
  }

  bool showPhysics = m_showPhysics;
  if (ImGui::Checkbox("Physics Debug", &showPhysics)) {
    m_showPhysics = showPhysics;
    T8_LOG_INFO("[PHYSICS] Debug draw %s", m_showPhysics ? "enabled" : "disabled");
  }
  bool showNavMesh = m_showNavMesh;
  if (ImGui::Checkbox("NavMesh Debug", &showNavMesh)) {
    m_showNavMesh = showNavMesh;
    if (m_showNavMesh && !m_navMesh.IsReady()) m_navMeshBuildAttempted = false;
    T8_LOG_INFO("[Navigation] Debug draw %s", m_showNavMesh ? "enabled" : "disabled");
  }
  const char* navShapeOptions[] = { "Geometry", "Nodes" };
  ImGui::SetNextItemWidth(170.0f);
  ImGui::Combo("NavMesh magenta", &m_navMeshDebugShapeMode, navShapeOptions, 2);
  ImGui::SetNextItemWidth(220.0f);
  if (ImGui::SliderFloat("NavMesh offset", &m_navMeshDebugOffset, 0.0f, 0.25f, "%.3f")) {
    m_navMeshDebugOffset = (std::max)(0.0f, (std::min)(0.25f, m_navMeshDebugOffset));
  }
  ImGui::TextWrapped("Left triple-tap opens this physics panel. Right triple-tap opens full scene controls.");
}
#endif

void DayScene::SaveSceneState() {
  // Sync cubemap path back to descriptor before saving
  auto& selDescs = m_sceneSetup.descriptor.selectors;
  for (auto& sd : selDescs) {
    if (sd.name == "cubemap" && m_currentCubemapIndex < (int)sd.options.size()) {
      m_sceneSetup.descriptor.environment_map = "sky/" + sd.options[m_currentCubemapIndex];
      m_sceneSetup.environmentMap = m_sceneSetup.descriptor.environment_map;
      sd.default_index = m_currentCubemapIndex;
    }
  }

  // Sync control descriptor defaults to match current runtime state.
  for (auto& sd : m_sceneSetup.descriptor.sliders) {
    if (sd.name == "shadow_bias") sd.default_val = SceneProp.ShadowBias;
    else if (sd.name == "shadow_min")  sd.default_val = SceneProp.ShadowMin;
    else if (sd.name == "env_factor")  sd.default_val = SceneProp.EnvFactor;
    else if (sd.name == "ibl_factor")   sd.default_val = SceneProp.IBLFactor;
    else if (sd.name == "godrays_factor") sd.default_val = SceneProp.GodRaysFactor;
    else if (sd.name == "material_emissive_intensity") sd.default_val = SceneProp.MaterialEmissiveIntensity;
    else if (sd.name == "material_transmission_multiplier") sd.default_val = SceneProp.MaterialTransmissionMultiplier;
    else if (sd.name == "material_refraction_strength") sd.default_val = SceneProp.MaterialRefractionStrength;
    else if (sd.name == "lightmap_intensity") sd.default_val = SceneProp.LightmapIntensity;
  }
  for (auto& cd : m_sceneSetup.descriptor.checkboxes) {
    if (cd.name == "dof_toggle")       cd.default_val = (SceneProp.ToogleDOF != 0);
    else if (cd.name == "parallax_toggle")  cd.default_val = (SceneProp.ToogleParallax != 0);
    else if (cd.name == "godrays_toggle")   cd.default_val = (SceneProp.ToogleGodRays != 0);
    else if (cd.name == "shadow_toggle")    cd.default_val = (SceneProp.ToogleShadow != 0);
    else if (cd.name == "ssao_toggle")      cd.default_val = (SceneProp.ToogleSSAO != 0);
    else if (cd.name == "dof_auto_focus")   cd.default_val = SceneProp.AutoFocus;
    else if (cd.name == "show_navmesh")      cd.default_val = m_showNavMesh;
    else if (cd.name == "point_lights_enabled") cd.default_val = SceneProp.PointLightsEnabled;
  }

  m_sceneSetup.SaveState(this, "Scenes/DayScene.json");
}
