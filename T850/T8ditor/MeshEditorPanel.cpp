/*********************************************************
 * T8ditor — Mesh Editor hosted window (extracted from EditorApp.cpp).
 *
 * Defines the EditorApp methods that drive the embedded "Mesh Edit"
 * window: a hosted RagdollEditor scene that previews a single mesh in
 * its own ImGui viewport, with cubemap/IBL and profile application.
 * Behaviour is identical to the original in-EditorApp implementation;
 * only the file location changed (Phase 4b of the editor refactor).
 *********************************************************/

#include "EditorApp.h"
#include "EditorWorld.h"
#include "EditorInternal.h"
#include "EditorMath.h"
#include "EditorUtil.h"
#include "EditorViewportUtil.h"
#include "EditorImGui.h"

#include <core/EngineContext.h>
#include <scene/IBLResources.h>
#include <scene/RenderSkinnedMesh.h>
#include <utils/InputManager.h>
#include <utils/RuntimeProfile.h>
#include <utils/Log.h>
#include <video/BaseDriver.h>

#include <imgui.h>
#include <imgui/DevGuiContext.h>
#include <SDL3/SDL.h>

#ifdef OS_WINDOWS
#include <core/windows/Win32Framework.h>
#endif

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace t8ditor {
namespace {
  // Aliases into the shared EditorWorld (same storage as EditorApp.cpp).
  auto& g_objects       = GetEditorWorld().objects;
  auto& g_selectedIdx   = GetEditorWorld().selectedIdx;
  auto& g_selectionType = GetEditorWorld().selectionType;
}
void EditorApp::OpenMeshEditor(int objectIndex) {
  if (objectIndex < 0 || objectIndex >= (int)g_objects.size()) {
    return;
  }

  SceneObject& obj = g_objects[objectIndex];
  if (m_meshEditorScene && m_meshEditorSceneLoaded) {
    m_meshEditorScene->OnDestoryScene();
  }
  m_meshEditorScene.reset();
  m_meshEditorSceneLoaded = false;
  m_meshEditorObjectIndex = objectIndex;
  m_meshEditorWindow.Open(true);
  InvalidateEditorFrozenFrame();

  g_selectedIdx = objectIndex;
  g_selectionType = 0;
  ClearMixedSelection();
  AddMixedSelection(0, objectIndex);

  t850::AABB bounds;
  if (GetEditorObjectWorldAABB(obj, bounds) && bounds.IsValid()) {
    m_meshEditorOrbitTarget = bounds.Center();
    const XVECTOR3 ext = bounds.Extents();
    const float radius = (std::max)(0.25f, std::sqrt(ext.x * ext.x + ext.y * ext.y + ext.z * ext.z));
    m_meshEditorOrbitDistance = radius * 2.8f;
  } else {
    m_meshEditorOrbitTarget = obj.wireframe.Position();
    m_meshEditorOrbitDistance = 4.0f;
  }
  m_meshEditorOrbitYaw = -0.75f;
  m_meshEditorOrbitPitch = 0.35f;
  m_meshEditorFovDeg = 45.0f;
  m_meshEditorCameraInitialized = true;
  m_meshEditorSceneReady = false;
  m_meshEditorDebugLogFramesRemaining = 8;

  T8_LOG_INFO("[T8ditor] Requested native editor window title='Mesh Edit' object='%s'", obj.name.c_str());
}

void EditorApp::CloseMeshEditor() {
  if (pFramework && pFramework->pVideoDriver) {
    pFramework->pVideoDriver->WaitForGPU();
  }
  if (m_meshEditorScene && m_meshEditorSceneLoaded) {
    m_meshEditorScene->OnDestoryScene();
  }
  if (m_imguiReady) {
    ImGuiLogCaptureStart();
  }
  m_meshEditorScene.reset();
  m_meshEditorWindow.Reset(true);
  m_meshEditorObjectIndex = -1;
  DestroyMeshEditorViewportTarget();
  IManager.xDelta = 0;
  IManager.yDelta = 0;
  for (int i = 0; i < MAXMOUSEBUTTONS; ++i) {
    IManager.MouseButtonStates[0][i] = false;
    IManager.MouseButtonStates[1][i] = false;
  }
  if (pFramework && pFramework->pVideoDriver) {
    pFramework->pVideoDriver->SetDepthStencilState(t850::BaseDriver::DEPTH_DEFAULT);
    pFramework->pVideoDriver->SetBlendState(t850::BaseDriver::BLEND_DEFAULT);
    pFramework->pVideoDriver->SetCullFace(t850::BaseDriver::FRONT_FACES);
    int mainW = m_lastW;
    int mainH = m_lastH;
#ifdef OS_WINDOWS
    if (auto* w32 = static_cast<t850::Win32Framework*>(pFramework)) {
      if (w32->m_pWindow) {
        SDL_GetWindowSizeInPixels(w32->m_pWindow, &mainW, &mainH);
      }
    }
#endif
    if (mainW > 0 && mainH > 0) {
      m_lastW = mainW;
      m_lastH = mainH;
      m_camera.SetViewportSize(mainW, mainH);
      pFramework->pVideoDriver->SetViewport(0.0f, 0.0f, static_cast<float>(mainW), static_cast<float>(mainH));
      pFramework->pVideoDriver->SetScissorRect(0, 0, mainW, mainH);
    }
  }
  if (!m_sceneProps.pCameras.empty()) {
    m_sceneProps.SetPrimaryCamera(&m_camera.GetCameraMutable());
  }
}

bool EditorApp::EnsureMeshEditorViewportTarget(int width, int height) {
  if (!pFramework || !pFramework->pVideoDriver || width <= 0 || height <= 0) {
    return false;
  }

  t850::BaseDriver* driver = pFramework->pVideoDriver;
  t850::RenderViewportDesc gbufferDesc;
  gbufferDesc.colorFormats = {
      t850::BaseRT::RGBA8,
      t850::BaseRT::RGBA16F,
      t850::BaseRT::RGBA8,
      t850::BaseRT::RGBA8,
      t850::BaseRT::RGBA16F,
      t850::BaseRT::RGBA8,
      t850::BaseRT::RGBA8};
  gbufferDesc.depthFormat = t850::BaseRT::F32;
  gbufferDesc.minWidth = 64;
  gbufferDesc.minHeight = 64;

  t850::RenderViewportDesc outputDesc;
  outputDesc.colorCount = 1;
  outputDesc.colorFormat = t850::BaseRT::RGBA8;
  outputDesc.depthFormat = t850::BaseRT::F32;
  outputDesc.minWidth = 64;
  outputDesc.minHeight = 64;

  if (!m_meshEditorGBufferTarget.Ensure(driver, width, height, gbufferDesc)) {
    DestroyMeshEditorViewportTarget();
    T8_LOG_ERROR("[T8ditor] Failed to create Mesh Edit GBuffer RT %dx%d", width, height);
    return false;
  }

  if (!m_meshEditorViewport.Ensure(driver, width, height, outputDesc)) {
    DestroyMeshEditorViewportTarget();
    T8_LOG_ERROR("[T8ditor] Failed to create Mesh Edit viewport RT %dx%d", width, height);
    return false;
  }

  T8_LOG_INFO("[T8ditor] Mesh Edit viewport RTs created gbuffer=%d output=%d size=%dx%d",
              m_meshEditorGBufferTarget.Handle(),
              m_meshEditorViewport.Handle(),
              m_meshEditorViewport.Width(),
              m_meshEditorViewport.Height());
  return true;
}

void EditorApp::DestroyMeshEditorViewportTarget() {
  if (pFramework && pFramework->pVideoDriver) {
    m_meshEditorGBufferTarget.Destroy(pFramework->pVideoDriver);
    m_meshEditorViewport.Destroy(pFramework->pVideoDriver);
  }
}

void EditorApp::DestroyMeshEditorSceneResources() {
  t850::BaseDriver* driver = (pFramework && pFramework->pVideoDriver) ? pFramework->pVideoDriver : nullptr;
  auto destroyTexture = [&](int& textureIndex) {
    if (driver && textureIndex >= 0) {
      driver->DestroyTexture(textureIndex);
    }
    textureIndex = -1;
  };

  destroyTexture(m_meshEditorEnvMapIdx);
  destroyTexture(m_meshEditorDiffuseIBLTexIndex);
  destroyTexture(m_meshEditorSpecularIBLTexIndex);
  destroyTexture(m_meshEditorBrdfLUTTexIndex);
  destroyTexture(m_meshEditorSheenIBLTexIndex);
  destroyTexture(m_meshEditorCharlieLUTTexIndex);
  destroyTexture(m_meshEditorSheenELUTTexIndex);

  if (m_meshEditorSSAOTextureReady) {
    m_meshEditorSceneProps.SSAOKernel.Destroy();
    m_meshEditorSSAOTextureReady = false;
  }

  m_meshEditorEnvMaps = t850::EnvironmentMapSet{};
  m_meshEditorCurrentCubemapPath.clear();
  m_meshEditorCurrentCubemapIndex = -1;
  m_meshEditorSceneModelKey.clear();
  m_meshEditorSceneReady = false;
  m_meshEditorSceneProps = SceneProps{};
  m_meshEditorSceneProps.SSAOKernel.NoiseTex = nullptr;
}

void EditorApp::SetMeshEditorCubemap(const std::string& cubemapPath) {
  const std::string normalizedPath = NormalizeEditorResourcePath(cubemapPath);
  if (normalizedPath.empty() || !pFramework || !pFramework->pVideoDriver) {
    return;
  }
  if (m_meshEditorEnvMapIdx >= 0 &&
      EditorResourcePathEquals(normalizedPath, m_meshEditorCurrentCubemapPath)) {
    return;
  }

  t850::BaseDriver* driver = pFramework->pVideoDriver;
  auto destroyTexture = [&](int& textureIndex) {
    if (textureIndex >= 0) {
      driver->DestroyTexture(textureIndex);
      textureIndex = -1;
    }
  };

  destroyTexture(m_meshEditorEnvMapIdx);
  if (m_meshEditorSceneSetup.environmentDiffuseIBL.empty()) {
    destroyTexture(m_meshEditorDiffuseIBLTexIndex);
  }
  if (m_meshEditorSceneSetup.environmentSpecularIBL.empty()) {
    destroyTexture(m_meshEditorSpecularIBLTexIndex);
  }
  if (m_meshEditorSceneSetup.environmentSheenIBL.empty()) {
    destroyTexture(m_meshEditorSheenIBLTexIndex);
  }

  m_meshEditorEnvMapIdx = driver->CreateTexture(normalizedPath);
  if (m_meshEditorEnvMapIdx < 0) {
    T8_LOG_ERROR("[T8ditor] Mesh Edit failed to load cubemap '%s'", normalizedPath.c_str());
    m_meshEditorCurrentCubemapPath.clear();
    m_meshEditorEnvMaps = t850::EnvironmentMapSet{};
    return;
  }

  m_meshEditorCurrentCubemapPath = normalizedPath;
  m_meshEditorEnvMaps.SetFallback(m_meshEditorEnvMapIdx);
  t850::LoadEnvironmentIBLResources(
      driver,
      {m_meshEditorSceneSetup.environmentDiffuseIBL,
       m_meshEditorSceneSetup.environmentSpecularIBL,
       m_meshEditorSceneSetup.environmentBrdfLUT,
       m_meshEditorSceneSetup.environmentSheenIBL,
       m_meshEditorSceneSetup.environmentCharlieLUT,
       m_meshEditorSceneSetup.environmentSheenELUT},
      m_meshEditorEnvMaps,
      m_meshEditorDiffuseIBLTexIndex,
      m_meshEditorSpecularIBLTexIndex,
      m_meshEditorBrdfLUTTexIndex,
      m_meshEditorSheenIBLTexIndex,
      m_meshEditorCharlieLUTTexIndex,
      m_meshEditorSheenELUTTexIndex);
  t850::UpdateSceneIBLSettings(m_meshEditorSceneProps, driver, m_meshEditorEnvMaps);

  const t850::SelectorDesc* cubemapDesc =
      FindEditorSelectorDesc(m_meshEditorSceneSetup.descriptor.selectors, "cubemap");
  if (cubemapDesc) {
    m_meshEditorCurrentCubemapIndex = EditorCubemapSelectorIndexForPath(*cubemapDesc, normalizedPath);
  }
}

void EditorApp::ApplyMeshEditorProfileState(SceneObject& obj, const t850::SandboxProfileDesc& state) {
  const bool hasCubemapPath = state.cubemap_path.has_value() &&
      !NormalizeEditorResourcePath(*state.cubemap_path).empty();
  if (hasCubemapPath) {
    SetMeshEditorCubemap(*state.cubemap_path);
  }

  t850::RenderSkinnedMesh* skinned = GetSkinnedMesh(obj);

  for (const auto& value : state.sliders) {
    if (value.name == "exposure") m_meshEditorSceneProps.Exposure = value.value;
    else if (value.name == "bloom_factor") m_meshEditorSceneProps.BloomFactor = value.value;
    else if (value.name == "bloom_threshold") m_meshEditorSceneProps.BloomThreshold = value.value;
    else if (value.name == "tm_white_level") m_meshEditorSceneProps.ToneMapWhiteLevel = value.value;
    else if (value.name == "tm_adapt_tau") m_meshEditorSceneProps.LuminanceTau = value.value;
    else if (value.name == "pcf_radius") m_meshEditorSceneProps.PCFScale = value.value;
    else if (value.name == "pcf_samples") m_meshEditorSceneProps.PCFSamples = value.value;
    else if (value.name == "ssao_kernel_size") { m_meshEditorSceneProps.SSAOKernel.KernelSize = (int)value.value; m_meshEditorSceneProps.SSAOKernel.Update(); }
    else if (value.name == "ssao_radius") m_meshEditorSceneProps.SSAOKernel.Radius = value.value;
    else if (value.name == "dof_aperture") m_meshEditorSceneProps.Aperture = value.value;
    else if (value.name == "dof_focal_length") m_meshEditorSceneProps.FocalLength = value.value;
    else if (value.name == "dof_max_coc") m_meshEditorSceneProps.MaxCoc = value.value;
    else if (value.name == "dof_far_samples") m_meshEditorSceneProps.DOF_Far_Samples_squared = value.value;
    else if (value.name == "dof_near_samples") m_meshEditorSceneProps.DOF_Near_Samples_squared = value.value;
    else if (value.name == "light_volume_steps") m_meshEditorSceneProps.LightVolumeSteps = value.value;
    else if (value.name == "godrays_factor") m_meshEditorSceneProps.GodRaysFactor = value.value;
    else if (value.name == "fov") m_meshEditorFovDeg = value.value;
    else if (value.name == "light_radius_scale") m_meshEditorSceneProps.LightRadiusScale = value.value;
    else if (value.name == "light_intensity_scale") m_meshEditorSceneProps.LightIntensityScale = value.value;
    else if (value.name == "lightmap_intensity") m_meshEditorSceneProps.LightmapIntensity = value.value;
    else if (value.name == "shadow_bias") m_meshEditorSceneProps.ShadowBias = value.value;
    else if (value.name == "shadow_min") m_meshEditorSceneProps.ShadowMin = value.value;
    else if (value.name == "env_factor") m_meshEditorSceneProps.EnvFactor = value.value;
    else if (value.name == "ibl_factor") m_meshEditorSceneProps.IBLFactor = value.value;
    else if (value.name == "ibl_mip_count") m_meshEditorSceneProps.IBLMipCount = (std::max)(0.0f, value.value);
    else if (value.name == "ibl_diffuse_mip_level") m_meshEditorSceneProps.IBLDiffuseMipLevel = (std::max)(0.0f, value.value);
    else if (value.name == "ibl_brdf_lut_enabled") m_meshEditorSceneProps.IBLBRDFLUTEnabled = value.value > 0.5f ? 1.0f : 0.0f;
    else if (value.name == "material_emissive_intensity") m_meshEditorSceneProps.MaterialEmissiveIntensity = value.value;
    else if (value.name == "material_transmission_multiplier") m_meshEditorSceneProps.MaterialTransmissionMultiplier = value.value;
    else if (value.name == "material_refraction_strength") m_meshEditorSceneProps.MaterialRefractionStrength = value.value;

    for (int kernelIndex = 0; kernelIndex < (int)m_meshEditorSceneProps.pGaussKernels.size(); ++kernelIndex) {
      GaussFilter* kernel = m_meshEditorSceneProps.pGaussKernels[kernelIndex];
      if (!kernel) continue;
      const std::string prefix = "gauss_" + std::to_string(kernelIndex) + "_";
      if (value.name == prefix + "radius") { kernel->radius = value.value; kernel->Update(); }
      else if (value.name == prefix + "sigma") { kernel->sigma = value.value; kernel->Update(); }
    }
  }

  for (const auto& value : state.checkboxes) {
    if (value.name == "shadow_toggle") m_meshEditorSceneProps.ToogleShadow = value.value ? 1 : 0;
    else if (value.name == "ssao_toggle") m_meshEditorSceneProps.ToogleSSAO = value.value ? 1 : 0;
    else if (value.name == "show_wireframe") m_meshEditorShowWireframe = value.value;
    else if (value.name == "show_skeleton") m_meshEditorShowSkeleton = value.value && skinned && skinned->HasSkinData();
    else if (value.name == "point_lights_enabled") m_meshEditorSceneProps.PointLightsEnabled = value.value;
    else if (value.name == "debug_luminance") {
      m_meshEditorSceneProps.DebugLuminanceEnabled = value.value;
      if (!value.value) m_meshEditorSceneProps.DebugAdaptedLuminanceValid = false;
    }
  }

  for (const auto& value : state.selectors) {
    if (value.name == "cubemap" && !hasCubemapPath) {
      const t850::SelectorDesc* cubemapDesc =
          FindEditorSelectorDesc(m_meshEditorSceneSetup.descriptor.selectors, "cubemap");
      if (cubemapDesc && value.value >= 0 && value.value < (int)cubemapDesc->options.size()) {
        m_meshEditorCurrentCubemapIndex = value.value;
        SetMeshEditorCubemap(EditorCubemapPathForSelectorIndex(*cubemapDesc, value.value));
      }
    } else if (value.name == "luminance_mode") {
      m_meshEditorSceneProps.LuminanceMode = value.value;
    }

    for (int kernelIndex = 0; kernelIndex < (int)m_meshEditorSceneProps.pGaussKernels.size(); ++kernelIndex) {
      GaussFilter* kernel = m_meshEditorSceneProps.pGaussKernels[kernelIndex];
      if (!kernel) continue;
      const std::string name = "gauss_" + std::to_string(kernelIndex) + "_kernel_size";
      if (value.name == name) { kernel->kernelSize = value.value; kernel->Update(); }
    }
  }

  for (const auto& lightState : state.lights) {
    if (lightState.index < 0 || lightState.index >= (int)m_meshEditorSceneProps.Lights.size()) {
      continue;
    }
    Light& light = m_meshEditorSceneProps.Lights[lightState.index];
    if (lightState.position.has_value()) light.Position = EditorVec3FromArray(*lightState.position, 1.0f);
    if (lightState.direction.has_value()) {
      XVECTOR3 direction = EditorVec3FromArray(*lightState.direction, 0.0f);
      if (direction.Length() > 0.0001f) {
        direction.Normalize();
        light.Direction = direction;
      }
    }
    if (lightState.color.has_value()) light.Color = EditorVec3FromArray(*lightState.color, 0.0f);
    if (lightState.diameter.has_value()) light.radius = (std::max)(0.001f, *lightState.diameter * 0.5f);
    if (lightState.intensity.has_value()) light.Intensity = *lightState.intensity;
  }

  if (state.frustum_culling.has_value()) {
    m_meshEditorSceneProps.FrustumCullingEnabled = *state.frustum_culling;
  }

  auto applyOrbit = [&](const t850::SandboxOrbitCameraDesc& orbit) {
    const XVECTOR3 target = EditorVec3FromArray(orbit.target, 1.0f);
    const XVECTOR3 panOffset = EditorVec3FromArray(orbit.pan_offset, 0.0f);
    m_meshEditorOrbitTarget = target + panOffset;
    m_meshEditorOrbitTarget.w = 1.0f;
    m_meshEditorOrbitYaw = orbit.yaw;
    m_meshEditorOrbitPitch = orbit.pitch;
    m_meshEditorOrbitDistance = (std::max)(0.001f, orbit.distance);
    m_meshEditorCameraInitialized = true;
  };

  if (state.camera.has_value()) {
    const auto& cameraState = *state.camera;
    m_meshEditorFovDeg = cameraState.fov;
    if (cameraState.orbit.has_value()) {
      applyOrbit(*cameraState.orbit);
    } else {
      m_meshEditorCamera.Eye = EditorVec3FromArray(cameraState.eye, 1.0f);
      m_meshEditorOrbitYaw = cameraState.yaw;
      m_meshEditorOrbitPitch = cameraState.pitch;
      m_meshEditorCameraInitialized = true;
    }
  } else if (state.orbit_camera.has_value()) {
    applyOrbit(*state.orbit_camera);
  }

  (void)state.animations;
  (void)state.current_keyframe;
}

void EditorApp::ApplyMeshEditorProfiles(SceneObject& obj) {
  const t850::SandboxProfileDesc* baseProfile = nullptr;
  const t850::SandboxProfileDesc* runtimeProfile = nullptr;
  int bestRuntimeScore = -1;
  for (const auto& profile : m_meshEditorSceneSetup.descriptor.profiles) {
    const bool modelSpecific = !profile.model.empty();
    const bool modelMatches = !modelSpecific ||
        MeshEditorProfileModelKey(profile.model) == m_meshEditorSceneModelKey;
    if (!modelMatches) {
      continue;
    }

    const bool hasTarget = !profile.name.empty() ||
                           !profile.platform.empty() ||
                           !profile.architecture.empty() ||
                           !profile.gpu_family.empty() ||
                           !profile.gpu_name_contains.empty();
    if (!hasTarget && modelSpecific) {
      baseProfile = &profile;
      continue;
    }

    const int score = t850::ScoreSceneProfileMatch(profile, m_meshEditorSceneModelKey);
    if (score > bestRuntimeScore) {
      bestRuntimeScore = score;
      runtimeProfile = &profile;
    }
  }

  if (baseProfile) {
    ApplyMeshEditorProfileState(obj, *baseProfile);
  }
  if (runtimeProfile && runtimeProfile != baseProfile) {
    ApplyMeshEditorProfileState(obj, *runtimeProfile);
  }
}

bool EditorApp::EnsureMeshEditorSceneState(SceneObject& obj) {
  const std::string meshPath = obj.meshPath.empty() ? obj.name : obj.meshPath;
  const std::string modelKey = MeshEditorProfileModelKey(meshPath);
  if (m_meshEditorSceneReady && m_meshEditorSceneModelKey == modelKey) {
    return true;
  }

  if (!m_meshEditorSceneSetupLoaded) {
    if (!m_meshEditorSceneSetup.Load("Scenes/RagdollEditor.json")) {
      T8_LOG_ERROR("[T8ditor] Mesh Edit failed to load Scenes/RagdollEditor.json");
      return false;
    }
    m_meshEditorSceneSetupLoaded = true;
  }

  if (m_meshEditorSSAOTextureReady) {
    m_meshEditorSceneProps.SSAOKernel.Destroy();
    m_meshEditorSSAOTextureReady = false;
  }
  m_meshEditorSceneProps = SceneProps{};
  m_meshEditorSceneProps.SSAOKernel.NoiseTex = nullptr;
  m_meshEditorSceneProps.SSAOKernel.InitTexture();
  m_meshEditorSSAOTextureReady = true;
  m_meshEditorSceneSetup.Apply(m_meshEditorSceneProps);
  if (!m_meshEditorRenderGraphLoaded) {
    if (!m_meshEditorRenderGraph.Load("Scenes/RagdollEditor_RenderGraph.json")) {
      T8_LOG_ERROR("[T8ditor] Mesh Edit failed to load Scenes/RagdollEditor_RenderGraph.json");
      return false;
    }
    m_meshEditorRenderGraph.CreateRenderTargets(pFramework->pVideoDriver, m_meshEditorSceneProps);
    m_meshEditorRenderGraphLoaded = true;
  }
  if (m_meshEditorSceneProps.pCameras.empty()) {
    m_meshEditorSceneProps.AddCamera(&m_meshEditorCamera);
  }
  if (!m_meshEditorSceneProps.pLightCameras.empty()) {
    m_meshEditorSceneProps.ActiveLightCamera = 0;
  }
  m_meshEditorSceneProps.pCullingCamera = &m_meshEditorCamera;
  m_meshEditorSceneModelKey = modelKey;
  m_meshEditorShowWireframe = false;
  m_meshEditorShowSkeleton = false;

  if (Camera* sceneCamera = m_meshEditorSceneSetup.GetCamera(0)) {
    m_meshEditorFovDeg = Rad2Deg(sceneCamera->Fov);
  } else {
    m_meshEditorFovDeg = 45.0f;
  }

  std::string startupCubemapPath = NormalizeEditorResourcePath(m_meshEditorSceneSetup.environmentMap);
  if (startupCubemapPath.empty()) {
    startupCubemapPath = "sky/Ennis.dds";
  }
  const t850::SelectorDesc* cubemapDesc =
      FindEditorSelectorDesc(m_meshEditorSceneSetup.descriptor.selectors, "cubemap");
  if (cubemapDesc) {
    const int environmentIndex = EditorCubemapSelectorIndexForPath(*cubemapDesc, startupCubemapPath);
    m_meshEditorCurrentCubemapIndex = environmentIndex >= 0
        ? environmentIndex
        : (std::max)(0, (std::min)(cubemapDesc->default_index,
                                   static_cast<int>(cubemapDesc->options.size()) - 1));
    if (environmentIndex < 0 && m_meshEditorCurrentCubemapIndex >= 0) {
      startupCubemapPath = EditorCubemapPathForSelectorIndex(*cubemapDesc, m_meshEditorCurrentCubemapIndex);
    }
  }

  auto applyStartupProfileCubemap = [&](const t850::SandboxProfileDesc& profile) {
    if (profile.cubemap_path.has_value() &&
        !NormalizeEditorResourcePath(*profile.cubemap_path).empty()) {
      startupCubemapPath = NormalizeEditorResourcePath(*profile.cubemap_path);
      if (cubemapDesc) {
        m_meshEditorCurrentCubemapIndex = EditorCubemapSelectorIndexForPath(*cubemapDesc, startupCubemapPath);
      }
      return;
    }

    if (cubemapDesc) {
      const int selectorIndex = EditorCubemapSelectorIndexFromProfile(profile);
      if (selectorIndex >= 0 && selectorIndex < static_cast<int>(cubemapDesc->options.size())) {
        m_meshEditorCurrentCubemapIndex = selectorIndex;
        startupCubemapPath = EditorCubemapPathForSelectorIndex(*cubemapDesc, selectorIndex);
      }
    }
  };

  const t850::SandboxProfileDesc* baseProfile = nullptr;
  const t850::SandboxProfileDesc* runtimeProfile = nullptr;
  int bestRuntimeScore = -1;
  for (const auto& profile : m_meshEditorSceneSetup.descriptor.profiles) {
    const bool modelSpecific = !profile.model.empty();
    const bool modelMatches = !modelSpecific ||
        MeshEditorProfileModelKey(profile.model) == m_meshEditorSceneModelKey;
    if (!modelMatches) {
      continue;
    }
    const bool hasTarget = !profile.name.empty() ||
                           !profile.platform.empty() ||
                           !profile.architecture.empty() ||
                           !profile.gpu_family.empty() ||
                           !profile.gpu_name_contains.empty();
    if (!hasTarget && modelSpecific) {
      baseProfile = &profile;
      continue;
    }
    const int score = t850::ScoreSceneProfileMatch(profile, m_meshEditorSceneModelKey);
    if (score > bestRuntimeScore) {
      bestRuntimeScore = score;
      runtimeProfile = &profile;
    }
  }
  if (baseProfile) {
    applyStartupProfileCubemap(*baseProfile);
  }
  if (runtimeProfile && runtimeProfile != baseProfile) {
    applyStartupProfileCubemap(*runtimeProfile);
  }

  SetMeshEditorCubemap(startupCubemapPath);

  ApplyMeshEditorProfiles(obj);
  m_meshEditorSceneReady = true;
  T8_LOG_INFO("[T8ditor] Mesh Edit scene state ready model='%s' cubemap='%s'",
              m_meshEditorSceneModelKey.c_str(),
              m_meshEditorCurrentCubemapPath.c_str());
  return true;
}

bool EditorApp::EnsureMeshEditorEmbeddedScene(SceneObject& obj) {
  if (m_meshEditorScene && m_meshEditorSceneLoaded) {
    return true;
  }
  if (!pFramework || !pFramework->pVideoDriver || !obj.litInst.pBase) {
    return false;
  }

  m_meshEditorScene = std::make_unique<::RagdollEditor>();
  m_meshEditorScene->pFramework = pFramework;
  m_meshEditorScene->SetEngineContext(&t850::GetEngineContext());
  const std::string meshPath = obj.meshPath.empty() ? obj.name : obj.meshPath;
  m_meshEditorScene->UseExternalMesh(obj.litInst, meshPath);
  m_meshEditorScene->SetRenderViewport(m_meshEditorViewport.ImageMinX(),
                                       m_meshEditorViewport.ImageMinY(),
                                       m_meshEditorViewport.Width(),
                                       m_meshEditorViewport.Height());
  m_meshEditorScene->SetFinalOutputRT(m_meshEditorViewport.Handle());
  m_meshEditorScene->OnLoadScene();
  if (m_meshEditorScene->m_meshCount <= 0 || !m_meshEditorScene->Meshes[0].pBase) {
    T8_LOG_ERROR("[T8ditor] Mesh Edit failed to load embedded RagdollEditor mesh '%s'", meshPath.c_str());
    m_meshEditorScene->OnDestoryScene();
    m_meshEditorScene.reset();
    return false;
  }
  m_meshEditorSceneLoaded = true;
  T8_LOG_INFO("[T8ditor] Mesh Edit is hosting RagdollEditor scene for '%s'", meshPath.c_str());
  return true;
}

void EditorApp::DrawMeshEditorViewport(SceneObject& obj) {
  const EditorViewportSize desiredViewport = EditorViewportDesiredSize(ImGui::GetContentRegionAvail());
  t850::RenderViewportDesc viewportDesc;
  viewportDesc.minWidth = 64;
  viewportDesc.minHeight = 64;
  const bool shouldResizeRT =
      m_meshEditorViewport.ShouldResize(desiredViewport.width, desiredViewport.height, viewportDesc);

  if (shouldResizeRT) {
    if (pFramework && pFramework->pVideoDriver) {
      pFramework->pVideoDriver->WaitForGPU();
    }
    if (!EnsureMeshEditorViewportTarget(desiredViewport.width, desiredViewport.height)) {
      ImGui::TextDisabled("Mesh editor viewport unavailable.");
      return;
    }
    if (m_meshEditorScene && m_meshEditorSceneLoaded) {
      m_meshEditorScene->ResizeRenderTargets(
          m_meshEditorViewport.Width(),
          m_meshEditorViewport.Height(),
          m_meshEditorViewport.Handle());
    }
  }

  if (!m_meshEditorViewport.IsValid() || !pFramework || !pFramework->pVideoDriver) {
    ImGui::TextDisabled("Mesh editor viewport unavailable.");
    return;
  }

  const int viewportW = m_meshEditorViewport.Width();
  const int viewportH = m_meshEditorViewport.Height();
  const ImVec2 embeddedViewportSize((float)viewportW, (float)viewportH);
  const ImVec2 embeddedImageMin = ImGui::GetCursorScreenPos();
  m_meshEditorViewport.SetImageRect(embeddedImageMin, embeddedViewportSize);

  t850::BaseDriver* driver = pFramework->pVideoDriver;
  t850::BaseRT* gbufferRT = EditorRenderTarget(driver, m_meshEditorGBufferTarget.Handle());
  t850::BaseRT* rt = EditorRenderTarget(driver, m_meshEditorViewport.Handle());
  if (!EditorRenderTargetReady(gbufferRT, 7, true) ||
      !EditorRenderTargetReady(rt, 1, false)) {
    ImGui::TextDisabled("Mesh editor render targets are unavailable.");
    return;
  }

  if (!EnsureMeshEditorEmbeddedScene(obj)) {
    ImGui::TextDisabled("RagdollEditor scene host is unavailable.");
    return;
  }

  m_meshEditorScene->SetFinalOutputRT(m_meshEditorViewport.Handle());
  m_meshEditorScene->SetRenderViewport(embeddedImageMin.x, embeddedImageMin.y, viewportW, viewportH);
  m_meshEditorScene->OnUpdate(m_dtSecs);
  if (m_physics.IsInitialized()) {
    m_physics.Update(m_dtSecs);
  }
  m_meshEditorScene->OnDraw();

  if (!m_meshEditorViewport.DrawTexture(driver,
                                        EditorRenderTargetColor(rt),
                                        embeddedImageMin,
                                        embeddedViewportSize,
                                        "##MeshEditorViewportInput",
                                        "Mesh editor viewport texture is not available for ImGui.")) {
    return;
  }
}

void EditorApp::DrawMeshEditorWindow() {
  if (!m_meshEditorOpen) {
    return;
  }

  ImGuiSetNextNativeEditorWindow(128.0f, 128.0f, 920.0f, 720.0f);
  m_meshEditorOpenRequested = false;

  bool keepOpen = m_meshEditorOpen;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  if (!ImGui::Begin("Mesh Edit", &keepOpen, ImGuiWindowFlags_NoDocking)) {
    ImGui::End();
    ImGui::PopStyleVar();
    if (!keepOpen) {
      m_meshEditorWindow.RequestClose();
    }
    return;
  }

  if (!keepOpen) {
    ImGui::End();
    ImGui::PopStyleVar();
    m_meshEditorWindow.RequestClose();
    return;
  }

  m_meshEditorWindow.CaptureNativeViewport(ImGui::GetWindowViewport(), "Mesh Edit");

  if (m_meshEditorObjectIndex < 0 || m_meshEditorObjectIndex >= (int)g_objects.size()) {
    ImGui::TextWrapped("The mesh editor selection is no longer valid.");
    ImGui::End();
    ImGui::PopStyleVar();
    return;
  }

  SceneObject& obj = g_objects[m_meshEditorObjectIndex];
  m_meshEditorViewport.SetInputActive(false);
  m_meshEditorDockspaceId = (unsigned int)ImGui::GetID("MeshEditDockSpace");
  m_meshEditorDockClassId = (unsigned int)ImGui::GetID("MeshEditDockClass");
  ImGuiWindowClass meshEditClass{};
  meshEditClass.ClassId = (ImGuiID)m_meshEditorDockClassId;
  meshEditClass.DockingAllowUnclassed = false;
  ImGui::DockSpace(
      (ImGuiID)m_meshEditorDockspaceId,
      ImVec2(0.0f, 0.0f),
      ImGuiDockNodeFlags_None,
      &meshEditClass);

  ImGui::End();
  ImGui::PopStyleVar();

  if (m_meshEditorImGuiViewportId != 0) {
    ImGui::SetNextWindowViewport((ImGuiID)m_meshEditorImGuiViewportId);
  }
  ImGui::SetNextWindowClass(&meshEditClass);
  if (m_meshEditorDockspaceId != 0) {
    ImGui::SetNextWindowDockID((ImGuiID)m_meshEditorDockspaceId, ImGuiCond_FirstUseEver);
  }
  bool viewportOpen = true;
  if (ImGui::Begin("Mesh Edit Viewport##MeshEditSceneViewport",
                   &viewportOpen,
                   ImGuiWindowFlags_NoCollapse)) {
    DrawMeshEditorViewport(obj);
  }
  ImGui::End();

  if (m_meshEditorGuiVisible && m_meshEditorScene && m_meshEditorSceneLoaded) {
    ImGuiViewport* fallbackViewport = ImGui::GetMainViewport();
    const float baseX = m_meshEditorViewportSizeX > 0.0f ? m_meshEditorViewportPosX : fallbackViewport->WorkPos.x;
    const float baseY = m_meshEditorViewportSizeY > 0.0f ? m_meshEditorViewportPosY : fallbackViewport->WorkPos.y;
    const float baseW = m_meshEditorViewportSizeX > 0.0f ? m_meshEditorViewportSizeX : fallbackViewport->WorkSize.x;
    const float baseH = m_meshEditorViewportSizeY > 0.0f ? m_meshEditorViewportSizeY : fallbackViewport->WorkSize.y;
    const float panelW = (std::min)(420.0f, (std::max)(320.0f, baseW - 48.0f));
    const float panelH = (std::min)(680.0f, (std::max)(320.0f, baseH - 48.0f));
    const ImVec2 panelPos(
        (std::max)(baseX + 24.0f, baseX + baseW - panelW - 24.0f),
        baseY + 24.0f);
    if (m_meshEditorImGuiViewportId != 0) {
      ImGui::SetNextWindowViewport((ImGuiID)m_meshEditorImGuiViewportId);
    }
    ImGui::SetNextWindowPos(panelPos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowCollapsed(false, ImGuiCond_FirstUseEver);

    t850::DevGuiContext gui;
    gui.SetViewportId((ImGuiID)m_meshEditorImGuiViewportId);
    gui.SetIdSuffix("MeshEditRagdollEditor");
    gui.SetDockId((ImGuiID)m_meshEditorDockspaceId);
    gui.SetWindowClassId((ImGuiID)m_meshEditorDockClassId);
    const bool panelBegun = gui.BeginPanel("Scene Controls", &m_meshEditorGuiVisible);
    if (panelBegun) {
      ImGui::TextDisabled("Mesh Edit modal - press G to hide/show controls.");
      ImGui::Separator();
      m_meshEditorScene->DrawDevGui(gui);
    }
    gui.EndPanel();
  }
}

} // namespace t8ditor
