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
#ifdef OS_ANDROID
#include <android/input.h>
#include <video/vulkan/VulkanDriver.h>
#endif
#include <imgui/DevGuiContext.h>
using namespace t850;
using std::cout;
using std::endl;
using std::string;

namespace {
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
  const float shortest = (std::max)(1.0f, (std::min)(width, height));
  layout.stickRadius = ClampFloat(shortest * 0.105f, 72.0f, 135.0f);
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
  addFloat("shadow_bias", SceneProp.ShadowBias);
  addFloat("shadow_min", SceneProp.ShadowMin);
  addFloat("env_factor", SceneProp.EnvFactor);
  addFloat("ibl_factor", SceneProp.IBLFactor);
  addFloat("material_emissive_intensity", SceneProp.MaterialEmissiveIntensity);
  addFloat("material_transmission_multiplier", SceneProp.MaterialTransmissionMultiplier);
  addFloat("material_refraction_strength", SceneProp.MaterialRefractionStrength);
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
  addBool("dof_toggle", SceneProp.ToogleDOF != 0);
  addBool("parallax_toggle", SceneProp.ToogleParallax != 0);
  addBool("parallax_shadow_toggle", SceneProp.ToogleParallaxShadow != 0);
  addBool("godrays_toggle", SceneProp.ToogleGodRays != 0);
  addBool("frustum_culling", SceneProp.FrustumCullingEnabled);
  addBool("show_culling_debug", m_showCullStats);

  addInt("num_lights", SceneProp.ActiveLights);
  addInt("active_gauss_kernel", ChangeActiveGaussSelection);
  addInt("debug_render_target", m_debugRTSelection);
  addInt("active_camera", m_activeCameraIndex);
  addInt("cubemap", m_currentCubemapIndex);
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
    else if (value.name == "shadow_bias") SceneProp.ShadowBias = value.value;
    else if (value.name == "shadow_min") SceneProp.ShadowMin = value.value;
    else if (value.name == "env_factor") SceneProp.EnvFactor = value.value;
    else if (value.name == "ibl_factor") SceneProp.IBLFactor = value.value;
    else if (value.name == "material_emissive_intensity") SceneProp.MaterialEmissiveIntensity = value.value;
    else if (value.name == "material_transmission_multiplier") SceneProp.MaterialTransmissionMultiplier = value.value;
    else if (value.name == "material_refraction_strength") SceneProp.MaterialRefractionStrength = value.value;
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
    else if (value.name == "dof_toggle") { SceneProp.ToogleDOF = value.value ? 1 : 0; m_renderGraph.SetPassEnabled("CoC", value.value); m_renderGraph.SetPassEnabled("Combine CoC", value.value); m_renderGraph.SetPassEnabled("DOF", value.value); m_renderGraph.SetPassEnabled("DOF 2", value.value); }
    else if (value.name == "parallax_toggle") { SceneProp.ToogleParallax = value.value ? 1 : 0; if (Meshes[0].pBase) Meshes[0].SetParallaxEnabled(value.value); }
    else if (value.name == "parallax_shadow_toggle") { SceneProp.ToogleParallaxShadow = value.value ? 1 : 0; SceneProp.ParallaxShadowStrength = value.value ? SceneProp.ParallaxShadowStrength : 0.0f; }
    else if (value.name == "godrays_toggle") SceneProp.ToogleGodRays = value.value ? 1 : 0;
    else if (value.name == "frustum_culling") SceneProp.FrustumCullingEnabled = SceneProp.FrustumCullingToggleAllowed && value.value;
    else if (value.name == "show_culling_debug") { m_showCullStats = value.value; SceneProp.ShowCullingDebug = value.value; }
  }

  for (const auto& value : state.selectors) {
    if (value.name == "num_lights") SceneProp.ActiveLights = value.value;
    else if (value.name == "active_gauss_kernel") ChangeActiveGaussSelection = value.value;
    else if (value.name == "debug_render_target") m_debugRTSelection = value.value;
    else if (value.name == "active_camera") ApplyActiveCameraSelection(value.value);
    else if (value.name == "cubemap") m_currentCubemapIndex = value.value;
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

void DayScene::LoadSceneProfile() {
  m_selectedProfileTargetIndex = t850::DefaultProfileTargetIndex();
  CaptureSceneProfileState(m_sceneProfileBaselineState);
  m_sceneProfileSavedState = m_sceneProfileBaselineState;
  m_sceneProfileReady = true;
  m_sceneProfileDirty = false;

  const t850::SandboxProfileDesc* bestProfile = nullptr;
  int bestScore = -1;
  for (const auto& profile : m_sceneSetup.descriptor.profiles) {
    if (!profile.model.empty()) continue;
    const bool hasTarget = !profile.name.empty() || !profile.platform.empty() || !profile.architecture.empty() ||
                           !profile.gpu_family.empty() || !profile.gpu_name_contains.empty();
    if (!hasTarget) continue;
    int score = t850::ScoreSceneProfileMatch(profile);
    if (score > bestScore) {
      bestScore = score;
      bestProfile = &profile;
    }
  }

  if (bestProfile) ApplySceneProfileState(*bestProfile);
  CaptureSceneProfileState(m_sceneProfileSavedState);

  const auto& runtime = t850::GetRuntimeProfileInfo();
  T8_LOG_INFO("[DayScene] Profile runtime='%s' platform=%s arch=%s gpu='%s' family=%s applied=%d",
              runtime.recommendedProfile.c_str(), runtime.platform.c_str(), runtime.architecture.c_str(),
              runtime.gpuName.c_str(), runtime.gpuFamily.c_str(), bestProfile ? 1 : 0);
}

void DayScene::SaveSceneProfile() {
  if (!m_sceneProfileReady) return;

  t850::SandboxProfileDesc current;
  CaptureSceneProfileState(current);
  t850::SandboxProfileDesc sparse = BuildSparseSceneProfile(current);
  t850::ApplyProfileTarget(sparse, m_selectedProfileTargetIndex);

  t850::SandboxProfileDesc target;
  t850::ApplyProfileTarget(target, m_selectedProfileTargetIndex);
  auto& profiles = m_sceneSetup.descriptor.profiles;
  auto existing = std::find_if(profiles.begin(), profiles.end(), [&](const t850::SandboxProfileDesc& profile) {
    return profile.model.empty() && profile.name == target.name && profile.platform == target.platform &&
           profile.architecture == target.architecture && profile.gpu_family == target.gpu_family &&
           profile.gpu_name_contains == target.gpu_name_contains;
  });

  bool hasOverrides = !sparse.sliders.empty() || !sparse.checkboxes.empty() || !sparse.selectors.empty();
  if (hasOverrides) {
    if (existing == profiles.end()) profiles.push_back(sparse);
    else *existing = sparse;
  } else if (existing != profiles.end()) {
    profiles.erase(existing);
  }

  if (t850::SaveSceneDescriptor("Scenes/DayScene.json", m_sceneSetup.descriptor)) {
    m_sceneProfileSavedState = current;
    m_sceneProfileDirty = false;
    T8_LOG_INFO("[DayScene] Saved profile '%s'", target.name.empty() ? "pc/base" : target.name.c_str());
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
  Position = XVECTOR3(0.0f, 0.0f, 0.0f);
  Orientation = XVECTOR3(0.0f, 0.0f, 0.0f);
  Scaling = XVECTOR3(1.0f, 1.0f, 1.0f);
  SelectedMesh = 0;

  CamSelection = NORMAL_CAM1;
  SceneSettingSelection = CHANGE_EXPOSURE;

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
  if (g_config.flags.benchmark) {
    m_benchmarkFrameTimesMs.reserve(12000);
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

  // Alias RT handles for FrameDumper and debug display
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
  LuminanceMapPass = m_renderGraph.GetRTHandle("LuminanceMap");
  AdaptedLumCurrentPass = m_renderGraph.GetRTHandle("AdaptedLumCurrent");
  AdaptedLumPrevPass = m_renderGraph.GetRTHandle("AdaptedLumPrev");
  CoCPass          = m_renderGraph.GetRTHandle("CoC");
  CombineCoCPass   = m_renderGraph.GetRTHandle("CombineCoC");
  CoCHelperPass    = m_renderGraph.GetRTHandle("CoCHelper");
  CoCHelperPass2   = m_renderGraph.GetRTHandle("CoCHelper2");

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

  m_wireframeSphere.Create(8, 16);
  m_wireframeArrow.Create(24, 6);
  m_physicsDebugRenderer.Create();
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

void DayScene::RecordBenchmarkFrame(float dtSecs) {
  if (!g_config.flags.benchmark)
    return;
  m_benchmarkFrameTimesMs.push_back(static_cast<double>(dtSecs) * 1000.0);
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
  file << "  \"resolution\": { \"width\": " << benchmarkWidth << ", \"height\": " << benchmarkHeight << " },\n";
  file << "  \"cullingEnabled\": " << (SceneProp.FrustumCullingEnabled ? "true" : "false") << ",\n";
  file << "  \"finishReason\": \"spline_journey_complete\",\n";
  file << "  \"splineLength\": " << (m_sceneSetup.splines.empty() ? 0.0f : m_sceneSetup.splines[0].m_totalLength) << ",\n";
  file << "  \"measuredDurationSeconds\": " << durationSecs << ",\n";
  file << "  \"frameCount\": " << frameCount << ",\n";
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

  const float width = pFramework && pFramework->pVideoDriver
      ? static_cast<float>(pFramework->pVideoDriver->width)
      : ImGui::GetIO().DisplaySize.x;
  const float height = pFramework && pFramework->pVideoDriver
      ? static_cast<float>(pFramework->pVideoDriver->height)
      : ImGui::GetIO().DisplaySize.y;
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
    bool handled = false;
    if (pointerId == m_androidMovePointerId) {
      m_androidMovePointerId = -1;
      m_androidMoveAxis = XVECTOR2(0.0f, 0.0f);
      handled = true;
    }
    if (pointerId == m_androidLookPointerId) {
      m_androidLookPointerId = -1;
      m_androidLookAxis = XVECTOR2(0.0f, 0.0f);
      handled = true;
    }
    if (pointerId == m_androidUpPointerId) {
      m_androidUpPointerId = -1;
      m_androidMoveUp = false;
      handled = true;
    }
    if (pointerId == m_androidDownPointerId) {
      m_androidDownPointerId = -1;
      m_androidMoveDown = false;
      handled = true;
    }
    return handled;
  };

  if (action == AMOTION_EVENT_ACTION_CANCEL) {
    ResetAndroidVirtualControls();
    return true;
  }

  if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP) {
    const int pointerId = AMotionEvent_getPointerId(event, actionPointerIndex);
    if (action == AMOTION_EVENT_ACTION_UP) {
      const bool handled = m_androidMovePointerId >= 0 || m_androidLookPointerId >= 0 ||
                           m_androidUpPointerId >= 0 || m_androidDownPointerId >= 0;
      ResetAndroidVirtualControls();
      return handled;
    }
    return resetPointer(pointerId);
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
    return capturePointer(actionPointerIndex);
  }

  if (action != AMOTION_EVENT_ACTION_MOVE) {
    return false;
  }

  bool handled = false;
  const int moveIndex = FindPointerIndexById(event, m_androidMovePointerId);
  if (moveIndex >= 0) {
    m_androidMoveAxis = StickAxisFromPoint(AMotionEvent_getX(event, moveIndex),
                                           AMotionEvent_getY(event, moveIndex),
                                           layout.moveCenter,
                                           layout.stickRadius);
    handled = true;
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
    handled = true;
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
    handled = true;
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
    handled = true;
    if (!m_androidMoveDown) {
      m_androidDownPointerId = -1;
    }
  } else {
    m_androidDownPointerId = -1;
    m_androidMoveDown = false;
  }

  return handled;
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
  if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) {
    return;
  }

  const AndroidVirtualControlsLayout layout = BuildAndroidVirtualControlsLayout(io.DisplaySize.x, io.DisplaySize.y);
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
  SceneProp.SSAOKernel.Destroy();
  m_debugText.Destroy();
  m_physicsDebugRenderer.Destroy();
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

  // Only advance scene timer when spline camera is driving the tour
  if (Cam.m_externalControl)
    m_tourTimeSec += _DtSecs;
  DtSecs = _DtSecs;
  SceneProp.FrameDeltaSec = DtSecs;
  RecordBenchmarkFrame(DtSecs);

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
    const float finishedDurationSec = m_tourTimeSec;
    m_tourTimeSec = 0.0f;
    if (g_config.flags.benchmark) {
      WriteBenchmarkResults(finishedDurationSec);
      exit(0);
    }
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

  if (IManager->PressedOnceKey(T800K_SPACE)) {
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
  m_renderGraph.Execute(
    pFramework->pVideoDriver,
    SceneProp,
    Meshes, 2,
    Quads,
    viewCam,
    &LightCam,
    nullptr,
    EnvMaps
  );

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
  // RT Dump via FrameDumper (skip when profiling — GPU queries conflict with dump's cmd buffer reset)
#ifdef T8_ENABLE_PROFILER
  if (m_dumper.ShouldDump(DtSecs) && !g_config.flags.profile) {
#else
  if (m_dumper.ShouldDump(DtSecs)) {
#endif
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
      {LuminanceMapPass, BaseDriver::COLOR0_ATTACHMENT, "LuminanceMap"},
      {AdaptedLumCurrentPass, BaseDriver::COLOR0_ATTACHMENT, "AdaptedLumCurrent"},
      {AdaptedLumPrevPass, BaseDriver::COLOR0_ATTACHMENT, "AdaptedLumPrev"},
    };
    m_dumper.DumpFrame(pFramework->pVideoDriver, Cam, LightCam, SceneProp, rts, DtSecs);
#ifdef T8_ENABLE_PROFILER
    if (m_dumper.ShouldExit() && !g_config.flags.profile) exit(0);
#else
    if (m_dumper.ShouldExit()) exit(0);
#endif
  }

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
    case 13: selected = LuminanceMapPass;attachment = BaseDriver::COLOR0_ATTACHMENT; break; // Luminance
    case 14: selected = CoCPass;         attachment = BaseDriver::COLOR0_ATTACHMENT; break; // CoC
    case 15: selected = BrightPassPass;  attachment = BaseDriver::COLOR0_ATTACHMENT; break; // Bright
    case 16: selected = AdaptedLumCurrentPass; attachment = BaseDriver::COLOR0_ATTACHMENT; break; // Adapted Lum
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

  auto drawPhysicsDebugOverlay = [this, viewCam]() {
    if (!m_showPhysics) {
      return;
    }

    t850::EngineContext* engineContext = GetEngineContext();
    if (!engineContext) engineContext = &t850::GetEngineContext();
    if (!engineContext || !engineContext->physics || !m_physicsDebugRenderer.IsReady()) {
      return;
    }

    m_physicsDebugRenderer.SetDepthTexture(nullptr);
    m_physicsDebugRenderer.SetDepthTestEnabled(false);
    m_physicsDebugRenderer.SetViewport(g_pBaseDriver->width, g_pBaseDriver->height);
    m_physicsDebugRenderer.SetFarPlane(viewCam ? viewCam->FPlane : 1000.0f);
    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
    pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
    m_physicsDebugRenderer.Draw(*engineContext->physics, VP);
  };

#ifdef OS_ANDROID
  if (m_showPhysics) {
    if (auto* vkDriver = static_cast<VulkanDriver*>(pFramework->pVideoDriver)) {
      vkDriver->SetPrePresentOverlayCallback(drawPhysicsDebugOverlay);
    }
  }
#else
  drawPhysicsDebugOverlay();
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
      } else {
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

void DayScene::PopulateGUI(t850::GUIManager& gui) {
  struct SliderMapping {
    const char* name;
    int settingIndex;
  };
  static const SliderMapping mappings[] = {
    {"exposure",              CHANGE_EXPOSURE},
    {"bloom_factor",          CHANGE_BLOOM_FACTOR},
    {"bloom_threshold",       CHANGE_BLOOM_THRESHOLD},
    {"tm_white_level",        CHANGE_TM_WHITE_LEVEL},
    {"tm_adapt_tau",          CHANGE_TM_ADAPT_TAU},
    {"pcf_radius",            CHANGE_PCF_RADIUS},
    {"pcf_samples",           CHANGE_PCF_SAMPLES},
    {"ssao_kernel_size",      CHANGE_SSAO_KERNEL_SIZE},
    {"ssao_radius",           CHANGE_SSAO_RADIUS},
    {"dof_aperture",          CHANGE_DOF_APERTURE},
    {"dof_focal_length",      CHANGE_DOF_FOCAL_LENGHT},
    {"dof_max_coc",           CHANGE_DOF_MAX_COC},
    {"dof_far_samples",       CHANGE_DOF_FAR_SAMPLE},
    {"dof_near_samples",      CHANGE_DOF_NEAR_SAMPLE},
    {"parallax_low_samples",  CHANGE_PARALLAX_LOW_SAMPLES},
    {"parallax_high_samples", CHANGE_PARALLAX_HIGH_SAMPLES},
    {"parallax_height",       CHANGE_PARALLAX_HEIGHT},
    {"parallax_shadow_min_layers", CHANGE_PARALLAX_SHADOW_MIN_LAYERS},
    {"parallax_shadow_max_layers", CHANGE_PARALLAX_SHADOW_MAX_LAYERS},
    {"parallax_shadow_softness",   CHANGE_PARALLAX_SHADOW_SOFTNESS},
    {"parallax_shadow_strength",   CHANGE_PARALLAX_SHADOW_STRENGTH},
    {"light_volume_steps",    CHANGE_LIGHT_VOLUME_STEPS},
    {"godrays_factor",        CHANGE_GODRAYS_FACTOR},
    {"gauss_kernel_radius",   CHANGE_GAUSS_KERNEL_RADIUS},
    {"gauss_kernel_deviation", CHANGE_GAUSS_KERNEL_DEVIATION},
    {"fov",                    CHANGE_FOV},
    {"light_intensity",        CHANGE_LIGHT_INTENSITY},
    {"shadow_bias",             CHANGE_SHADOW_BIAS},
    {"shadow_min",              CHANGE_SHADOW_MIN},
    {"env_factor",              CHANGE_ENV_FACTOR},
    {"ibl_factor",               CHANGE_IBL_FACTOR},
    {"material_emissive_intensity", CHANGE_MATERIAL_EMISSIVE_INTENSITY},
    {"material_transmission_multiplier", CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER},
    {"material_refraction_strength", CHANGE_MATERIAL_REFRACTION_STRENGTH},
  };

  auto& sliderDescs = m_sceneSetup.descriptor.sliders;
  for (auto& sd : sliderDescs) {
    int settingIdx = -1;
    for (auto& m : mappings) {
      if (sd.name == m.name) {
        settingIdx = m.settingIndex;
        break;
      }
    }
    gui.AddSlider(sd, settingIdx);
  }

  // Checkbox mappings
  struct CheckboxMapping {
    const char* name;
    int settingIndex;
  };
  static const CheckboxMapping cbMappings[] = {
    {"shadow_toggle",   CHANGE_PCF_TOOGLE},
    {"ssao_toggle",     CHANGLE_SSAO_TOOGLE},
    {"dof_auto_focus",  CHANGE_DOF_AUTO_FOCUS},
    {"show_spline",    CHANGE_SHOW_SPLINE},
    {"show_lights",    CHANGE_SHOW_LIGHTS},
    {"show_physics",   CHANGE_SHOW_PHYSICS},
    {"dof_toggle",     CHANGE_DOF_TOGGLE},
    {"parallax_toggle", CHANGE_PARALLAX_TOGGLE},
    {"parallax_shadow_toggle", CHANGE_PARALLAX_SHADOW_TOGGLE},
    {"godrays_toggle", CHANGE_GODRAYS_TOGGLE},
  };

  auto& cbDescs = m_sceneSetup.descriptor.checkboxes;
  for (auto& cd : cbDescs) {
    int settingIdx = -1;
    for (auto& m : cbMappings) {
      if (cd.name == m.name) {
        settingIdx = m.settingIndex;
        break;
      }
    }
    gui.AddCheckbox(cd, settingIdx);
  }

  // Selector mappings
  struct SelectorMapping {
    const char* name;
    int settingIndex;
  };
  static const SelectorMapping selMappings[] = {
    {"num_lights",          CHANGE_NUM_LIGHTS},
    {"active_gauss_kernel",      CHANGE_ACTIVE_GAUSS_KERNEL},
    {"gauss_kernel_sample_count", CHANGE_GAUSS_KERNEL_SAMPLE_COUNT},
    {"debug_render_target",       CHANGE_DEBUG_RT},
    {"active_camera",               CHANGE_ACTIVE_CAMERA},
    {"cubemap",                     CHANGE_CUBEMAP},
  };

  auto& selDescs = m_sceneSetup.descriptor.selectors;
  for (auto& sd : selDescs) {
    int settingIdx = -1;
    for (auto& m : selMappings) {
      if (sd.name == m.name) {
        settingIdx = m.settingIndex;
        break;
      }
    }
    gui.AddSelector(sd, settingIdx);
  }
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
    {"dof_toggle", CHANGE_DOF_TOGGLE},
    {"parallax_toggle", CHANGE_PARALLAX_TOGGLE},
    {"parallax_shadow_toggle", CHANGE_PARALLAX_SHADOW_TOGGLE},
    {"godrays_toggle", CHANGE_GODRAYS_TOGGLE},
  };

  static const Mapping selectorMappings[] = {
    {"num_lights", CHANGE_NUM_LIGHTS},
    {"active_gauss_kernel", CHANGE_ACTIVE_GAUSS_KERNEL},
    {"gauss_kernel_sample_count", CHANGE_GAUSS_KERNEL_SAMPLE_COUNT},
    {"debug_render_target", CHANGE_DEBUG_RT},
    {"active_camera", CHANGE_ACTIVE_CAMERA},
    {"cubemap", CHANGE_CUBEMAP},
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
    case CHANGE_DOF_TOGGLE: value = (SceneProp.ToogleDOF != 0); return true;
    case CHANGE_PARALLAX_TOGGLE: value = (SceneProp.ToogleParallax != 0); return true;
    case CHANGE_PARALLAX_SHADOW_TOGGLE: value = (SceneProp.ToogleParallaxShadow != 0); return true;
    case CHANGE_GODRAYS_TOGGLE: value = (SceneProp.ToogleGodRays != 0); return true;
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
    for (const auto& desc : m_sceneSetup.descriptor.checkboxes) {
      int settingIndex = findSetting(desc.name, checkboxMappings, (int)(sizeof(checkboxMappings) / sizeof(checkboxMappings[0])));
      if (settingIndex < 0) continue;
      bool value = false;
      if (getCheckboxValue(settingIndex, value) && gui.Checkbox(desc, value)) {
        setCheckboxValue(settingIndex, value);
      }
    }
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

      t850::SelectorDesc targetDesc;
      targetDesc.name = "profile_target";
      targetDesc.label = "Save target";
      for (const auto& target : t850::GetProfileTargets()) targetDesc.options.push_back(target.label);
      targetDesc.default_index = m_selectedProfileTargetIndex;
      int targetIndex = m_selectedProfileTargetIndex;
      if (gui.Combo(targetDesc, targetIndex)) {
        m_selectedProfileTargetIndex = targetIndex;
      }
      bool canSaveProfile = m_sceneProfileDirty || m_selectedProfileTargetIndex != t850::DefaultProfileTargetIndex();
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

void DayScene::SyncToGUI(t850::GUIManager& gui) {
  for (auto& sp : gui.GetSliderPairs()) {
    auto* slider = sp.slider;
    switch (slider->settingIndex) {
    case CHANGE_EXPOSURE:           slider->SetValue(SceneProp.Exposure); break;
    case CHANGE_BLOOM_FACTOR:       slider->SetValue(SceneProp.BloomFactor); break;
    case CHANGE_BLOOM_THRESHOLD:     slider->SetValue(SceneProp.BloomThreshold); break;
    case CHANGE_TM_WHITE_LEVEL:     slider->SetValue(SceneProp.ToneMapWhiteLevel); break;
    case CHANGE_TM_ADAPT_TAU:       slider->SetValue(SceneProp.LuminanceTau); break;
    case CHANGE_PCF_RADIUS:         slider->SetValue(SceneProp.PCFScale); break;
    case CHANGE_PCF_SAMPLES:        slider->SetValue(SceneProp.PCFSamples); break;
    case CHANGE_SSAO_KERNEL_SIZE:   slider->SetValue((float)SceneProp.SSAOKernel.KernelSize); break;
    case CHANGE_SSAO_RADIUS:        slider->SetValue(SceneProp.SSAOKernel.Radius); break;
    case CHANGE_DOF_APERTURE:       slider->SetValue(SceneProp.Aperture); break;
    case CHANGE_DOF_FOCAL_LENGHT:   slider->SetValue(SceneProp.FocalLength); break;
    case CHANGE_DOF_MAX_COC:        slider->SetValue(SceneProp.MaxCoc); break;
    case CHANGE_DOF_FAR_SAMPLE:     slider->SetValue(SceneProp.DOF_Far_Samples_squared); break;
    case CHANGE_DOF_NEAR_SAMPLE:    slider->SetValue(SceneProp.DOF_Near_Samples_squared); break;
    case CHANGE_PARALLAX_LOW_SAMPLES:  slider->SetValue(SceneProp.ParallaxLowSamples); break;
    case CHANGE_PARALLAX_HIGH_SAMPLES: slider->SetValue(SceneProp.ParallaxHighSamples); break;
    case CHANGE_PARALLAX_HEIGHT:    slider->SetValue(SceneProp.ParallaxHeight); break;
    case CHANGE_PARALLAX_SHADOW_MIN_LAYERS: slider->SetValue(SceneProp.ParallaxShadowMinLayers); break;
    case CHANGE_PARALLAX_SHADOW_MAX_LAYERS: slider->SetValue(SceneProp.ParallaxShadowMaxLayers); break;
    case CHANGE_PARALLAX_SHADOW_SOFTNESS:   slider->SetValue(SceneProp.ParallaxShadowSoftness); break;
    case CHANGE_PARALLAX_SHADOW_STRENGTH:   slider->SetValue(SceneProp.ParallaxShadowStrength); break;
    case CHANGE_LIGHT_VOLUME_STEPS: slider->SetValue(SceneProp.LightVolumeSteps); break;
    case CHANGE_GODRAYS_FACTOR:    slider->SetValue(SceneProp.GodRaysFactor); break;
    case CHANGE_GAUSS_KERNEL_RADIUS:   slider->SetValue(SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius); break;
    case CHANGE_GAUSS_KERNEL_DEVIATION: slider->SetValue(SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma); break;
    case CHANGE_FOV:                slider->SetValue(Rad2Deg(ActiveCam->Fov)); break;
    case CHANGE_LIGHT_INTENSITY:    if (!SceneProp.Lights.empty()) slider->SetValue(SceneProp.Lights[0].Intensity); break;
    case CHANGE_SHADOW_BIAS:        slider->SetValue(SceneProp.ShadowBias); break;
    case CHANGE_SHADOW_MIN:         slider->SetValue(SceneProp.ShadowMin); break;
    case CHANGE_ENV_FACTOR:         slider->SetValue(SceneProp.EnvFactor); break;
    case CHANGE_IBL_FACTOR:         slider->SetValue(SceneProp.IBLFactor); break;
    case CHANGE_MATERIAL_EMISSIVE_INTENSITY: slider->SetValue(SceneProp.MaterialEmissiveIntensity); break;
    case CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER: slider->SetValue(SceneProp.MaterialTransmissionMultiplier); break;
    case CHANGE_MATERIAL_REFRACTION_STRENGTH: slider->SetValue(SceneProp.MaterialRefractionStrength); break;
    }
  }
  for (auto& cp : gui.GetCheckboxPairs()) {
    auto* cb = cp.checkbox;
    switch (cb->settingIndex) {
    case CHANGE_PCF_TOOGLE:     cb->checked = (SceneProp.ToogleShadow != 0); break;
    case CHANGLE_SSAO_TOOGLE:   cb->checked = (SceneProp.ToogleSSAO != 0); break;
    case CHANGE_DOF_AUTO_FOCUS: cb->checked = SceneProp.AutoFocus; break;
    case CHANGE_SHOW_SPLINE:    cb->checked = m_showSpline; break;
    case CHANGE_SHOW_LIGHTS:    cb->checked = m_showLights; break;
    case CHANGE_SHOW_PHYSICS:   cb->checked = m_showPhysics; break;
    case CHANGE_DOF_TOGGLE:     cb->checked = (SceneProp.ToogleDOF != 0); break;
    case CHANGE_PARALLAX_TOGGLE: cb->checked = (SceneProp.ToogleParallax != 0); break;
    case CHANGE_PARALLAX_SHADOW_TOGGLE: cb->checked = (SceneProp.ToogleParallaxShadow != 0); break;
    case CHANGE_GODRAYS_TOGGLE: cb->checked = (SceneProp.ToogleGodRays != 0); break;
    }
  }
  for (auto& sp : gui.GetSelectorPairs()) {
    auto* sel = sp.selector;
    switch (sel->settingIndex) {
    case CHANGE_NUM_LIGHTS:         sel->selectedIndex = FindLightOption(SceneProp.ActiveLights); break;
    case CHANGE_ACTIVE_GAUSS_KERNEL: sel->selectedIndex = ChangeActiveGaussSelection; break;
    case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
      int ks = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize;
      // Find matching option index for the kernel size
      for (int i = 0; i < (int)sel->options.size(); i++) {
        if (std::atoi(sel->options[i].c_str()) == ks) { sel->selectedIndex = i; break; }
      }
    } break;
    case CHANGE_DEBUG_RT: sel->selectedIndex = m_debugRTSelection; break;
    case CHANGE_ACTIVE_CAMERA: sel->selectedIndex = m_activeCameraIndex; break;
    case CHANGE_CUBEMAP: sel->selectedIndex = m_currentCubemapIndex; break;
    }
  }
}

void DayScene::SyncFromGUI(t850::GUIManager& gui) {
  for (auto& sp : gui.GetSliderPairs()) {
    auto* slider = sp.slider;
    if (!slider->knobDragging && !slider->knobHover) continue;
    switch (slider->settingIndex) {
    case CHANGE_EXPOSURE:           SceneProp.Exposure = slider->value; break;
    case CHANGE_BLOOM_FACTOR:       SceneProp.BloomFactor = slider->value; break;
    case CHANGE_BLOOM_THRESHOLD:     SceneProp.BloomThreshold = slider->value; break;
    case CHANGE_TM_WHITE_LEVEL:     SceneProp.ToneMapWhiteLevel = slider->value; break;
    case CHANGE_TM_ADAPT_TAU:       SceneProp.LuminanceTau = slider->value; break;
    case CHANGE_PCF_RADIUS:         SceneProp.PCFScale = slider->value; break;
    case CHANGE_PCF_SAMPLES:        SceneProp.PCFSamples = slider->value; break;
    case CHANGE_SSAO_KERNEL_SIZE:   SceneProp.SSAOKernel.KernelSize = (int)slider->value; SceneProp.SSAOKernel.Update(); break;
    case CHANGE_SSAO_RADIUS:        SceneProp.SSAOKernel.Radius = slider->value; break;
    case CHANGE_DOF_APERTURE:       SceneProp.Aperture = slider->value; break;
    case CHANGE_DOF_FOCAL_LENGHT:   SceneProp.FocalLength = slider->value; break;
    case CHANGE_DOF_MAX_COC:        SceneProp.MaxCoc = slider->value; break;
    case CHANGE_DOF_FAR_SAMPLE:     SceneProp.DOF_Far_Samples_squared = slider->value; break;
    case CHANGE_DOF_NEAR_SAMPLE:    SceneProp.DOF_Near_Samples_squared = slider->value; break;
    case CHANGE_PARALLAX_LOW_SAMPLES:  SceneProp.ParallaxLowSamples = slider->value; break;
    case CHANGE_PARALLAX_HIGH_SAMPLES: SceneProp.ParallaxHighSamples = slider->value; break;
    case CHANGE_PARALLAX_HEIGHT:    SceneProp.ParallaxHeight = slider->value; break;
    case CHANGE_PARALLAX_SHADOW_MIN_LAYERS: SceneProp.ParallaxShadowMinLayers = slider->value; break;
    case CHANGE_PARALLAX_SHADOW_MAX_LAYERS: SceneProp.ParallaxShadowMaxLayers = slider->value; break;
    case CHANGE_PARALLAX_SHADOW_SOFTNESS:   SceneProp.ParallaxShadowSoftness = slider->value; break;
    case CHANGE_PARALLAX_SHADOW_STRENGTH:   SceneProp.ParallaxShadowStrength = slider->value; break;
    case CHANGE_LIGHT_VOLUME_STEPS: SceneProp.LightVolumeSteps = slider->value; break;
    case CHANGE_GODRAYS_FACTOR:    SceneProp.GodRaysFactor = slider->value; break;
    case CHANGE_GAUSS_KERNEL_RADIUS:
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius = slider->value;
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
      break;
    case CHANGE_GAUSS_KERNEL_DEVIATION:
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma = slider->value;
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
      break;
    case CHANGE_FOV:
      ActiveCam->SetFov(Deg2Rad(slider->value));
      // Recompute VP so FOV changes are visible even when paused
      // (Camera::Update is skipped while paused, leaving VP stale).
      ActiveCam->VP = ActiveCam->View * ActiveCam->Projection;
      VP = ActiveCam->VP;
      break;
    case CHANGE_LIGHT_INTENSITY:
      if (!SceneProp.Lights.empty()) SceneProp.Lights[0].Intensity = slider->value;
      break;
    case CHANGE_SHADOW_BIAS:
      SceneProp.ShadowBias = slider->value;
      break;
    case CHANGE_SHADOW_MIN:
      SceneProp.ShadowMin = slider->value;
      break;
    case CHANGE_ENV_FACTOR:
      SceneProp.EnvFactor = slider->value;
      break;
    case CHANGE_IBL_FACTOR:
      SceneProp.IBLFactor = slider->value;
      break;
    case CHANGE_MATERIAL_EMISSIVE_INTENSITY:
      SceneProp.MaterialEmissiveIntensity = slider->value;
      break;
    case CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER:
      SceneProp.MaterialTransmissionMultiplier = slider->value;
      break;
    case CHANGE_MATERIAL_REFRACTION_STRENGTH:
      SceneProp.MaterialRefractionStrength = slider->value;
      break;
    }
  }

  // Apply parallax settings immediately so changes are visible even when paused.
  Meshes[0].SetParallaxSettings(SceneProp.ParallaxLowSamples, SceneProp.ParallaxHighSamples, SceneProp.ParallaxHeight);
  Meshes[0].SetParallaxShadowSettings(SceneProp.ParallaxShadowMinLayers, SceneProp.ParallaxShadowMaxLayers,
                                       SceneProp.ParallaxShadowSoftness, SceneProp.ParallaxShadowStrength);

  for (auto& cp : gui.GetCheckboxPairs()) {
    auto* cb = cp.checkbox;
    if (!cb->justToggled) continue;
    switch (cb->settingIndex) {
    case CHANGE_PCF_TOOGLE:     SceneProp.ToogleShadow = cb->checked ? 1 : 0; break;
    case CHANGLE_SSAO_TOOGLE:   SceneProp.ToogleSSAO = cb->checked ? 1 : 0; break;
    case CHANGE_DOF_AUTO_FOCUS: SceneProp.AutoFocus = cb->checked; break;
    case CHANGE_SHOW_SPLINE:    m_showSpline = cb->checked; break;
    case CHANGE_SHOW_LIGHTS:    m_showLights = cb->checked; break;
    case CHANGE_SHOW_PHYSICS:   m_showPhysics = cb->checked; break;
    case CHANGE_DOF_TOGGLE:
      SceneProp.ToogleDOF = cb->checked ? 1 : 0;
      m_renderGraph.SetPassEnabled("CoC", cb->checked);
      m_renderGraph.SetPassEnabled("Combine CoC", cb->checked);
      m_renderGraph.SetPassEnabled("DOF", cb->checked);
      m_renderGraph.SetPassEnabled("DOF 2", cb->checked);
      break;
    case CHANGE_PARALLAX_TOGGLE:
      SceneProp.ToogleParallax = cb->checked ? 1 : 0;
      Meshes[0].SetParallaxEnabled(cb->checked);
      break;
    case CHANGE_PARALLAX_SHADOW_TOGGLE:
      SceneProp.ToogleParallaxShadow = cb->checked ? 1 : 0;
      // Toggle via strength uniform: 0 = disabled, saved value = enabled
      if (cb->checked) {
        SceneProp.ParallaxShadowStrength = 1.0f; // restore default
      } else {
        SceneProp.ParallaxShadowStrength = 0.0f; // disable
      }
      break;
    case CHANGE_GODRAYS_TOGGLE:
      SceneProp.ToogleGodRays = cb->checked ? 1 : 0;
      break;
    }
  }
  for (auto& sp : gui.GetSelectorPairs()) {
    auto* sel = sp.selector;
    if (!sel->justChanged) continue;
    switch (sel->settingIndex) {
    case CHANGE_NUM_LIGHTS: {
      int val = std::atoi(sel->CurrentOption().c_str());
      SceneProp.ActiveLights = val;
    } break;
    case CHANGE_ACTIVE_GAUSS_KERNEL:
      ChangeActiveGaussSelection = sel->selectedIndex;
      break;
    case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
      int newSize = std::atoi(sel->CurrentOption().c_str());
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize = newSize;
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    } break;
    case CHANGE_DEBUG_RT:
      m_debugRTSelection = sel->selectedIndex;
      break;
    case CHANGE_ACTIVE_CAMERA: {
      ApplyActiveCameraSelection(sel->selectedIndex);
    } break;
    case CHANGE_CUBEMAP: {
      if (sel->selectedIndex != m_currentCubemapIndex) {
        m_currentCubemapIndex = sel->selectedIndex;
        m_pendingCubemap = "sky/" + sel->CurrentOption();
      }
    } break;
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

  // Sync GUI element defaults to match current runtime state
  for (auto& sd : m_sceneSetup.descriptor.sliders) {
    if (sd.name == "shadow_bias") sd.default_val = SceneProp.ShadowBias;
    else if (sd.name == "shadow_min")  sd.default_val = SceneProp.ShadowMin;
    else if (sd.name == "env_factor")  sd.default_val = SceneProp.EnvFactor;
    else if (sd.name == "ibl_factor")   sd.default_val = SceneProp.IBLFactor;
    else if (sd.name == "godrays_factor") sd.default_val = SceneProp.GodRaysFactor;
    else if (sd.name == "material_emissive_intensity") sd.default_val = SceneProp.MaterialEmissiveIntensity;
    else if (sd.name == "material_transmission_multiplier") sd.default_val = SceneProp.MaterialTransmissionMultiplier;
    else if (sd.name == "material_refraction_strength") sd.default_val = SceneProp.MaterialRefractionStrength;
  }
  for (auto& cd : m_sceneSetup.descriptor.checkboxes) {
    if (cd.name == "dof_toggle")       cd.default_val = (SceneProp.ToogleDOF != 0);
    else if (cd.name == "parallax_toggle")  cd.default_val = (SceneProp.ToogleParallax != 0);
    else if (cd.name == "godrays_toggle")   cd.default_val = (SceneProp.ToogleGodRays != 0);
    else if (cd.name == "shadow_toggle")    cd.default_val = (SceneProp.ToogleShadow != 0);
    else if (cd.name == "ssao_toggle")      cd.default_val = (SceneProp.ToogleSSAO != 0);
    else if (cd.name == "dof_auto_focus")   cd.default_val = SceneProp.AutoFocus;
  }

  m_sceneSetup.SaveState(this, "Scenes/DayScene.json");
}
