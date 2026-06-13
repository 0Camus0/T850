/*********************************************************
 * T8ditor — Play Scene hosted window (extracted from EditorApp.cpp).
 *
 * Defines the EditorApp methods that drive the embedded "Play Scene"
 * runtime (SceneTemplate) hosted in its own ImGui viewport. Behaviour
 * is identical to the original in-EditorApp implementation; only the
 * file location changed (Phase 4a of the editor refactor).
 *********************************************************/

#include "EditorApp.h"
#include "EditorWorld.h"
#include "EditorInternal.h"
#include "EditorMath.h"
#include "EditorScene.h"
#include "EditorViewportUtil.h"
#include "EditorImGui.h"

#include <core/EngineContext.h>
#include <core/Config.h>
#include <utils/InputManager.h>
#include <utils/Log.h>
#include <video/BaseDriver.h>

#include <imgui.h>
#include <imgui/DevGuiContext.h>
#include <SDL3/SDL.h>

#ifdef OS_WINDOWS
#include <core/windows/Win32Framework.h>
#endif

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

namespace t8ditor {
namespace {
  // Aliases into the shared EditorWorld (same storage as EditorApp.cpp).
  auto& g_objects         = GetEditorWorld().objects;
  auto& g_selectedIdx     = GetEditorWorld().selectedIdx;
  auto& g_cameras         = GetEditorWorld().cameras;
  auto& g_lights          = GetEditorWorld().lights;
  auto& g_selectionType   = GetEditorWorld().selectionType;
  auto& g_activeCameraIdx = GetEditorWorld().activeCameraIdx;
}
bool EditorApp::ExportTemporaryPlayScene(std::string& outPath) {
  if (m_editorNavMeshAuthored && m_editorNavMeshDirty) {
    if (!CreateEditorNavMesh()) {
      m_playSceneStatus = "NavMesh is stale and failed to re-generate.";
      T8_LOG_ERROR("[T8ditor] Play Scene failed: stale NavMesh could not be regenerated before export");
      return false;
    }
  }

  std::error_code ec;
  std::filesystem::path tempDir = std::filesystem::temp_directory_path(ec);
  if (ec) {
    tempDir = std::filesystem::current_path();
  }
  tempDir /= "T850";
  tempDir /= "T8ditorPlay";
  std::filesystem::create_directories(tempDir, ec);
  if (ec) {
    T8_LOG_ERROR("[T8ditor] Cannot create play-scene temp directory '%s': %s",
                 tempDir.string().c_str(),
                 ec.message().c_str());
    return false;
  }

  const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  std::filesystem::path tempPath = tempDir / ("play_scene_" + std::to_string(stamp) + ".t8scene");
  outPath = tempPath.string();
  m_playSceneEditorSnapshot = RefreshVirtualEditorScene(outPath);
  T8_LOG_INFO("[T8ditor] Play Scene virtual scene refreshed: objects=%zu physics=%zu cameras=%zu lights=%zu",
              m_playSceneEditorSnapshot.objects.size(),
              m_playSceneEditorSnapshot.physics_entities.size(),
              m_playSceneEditorSnapshot.cameras.size(),
              m_playSceneEditorSnapshot.lights.size());
  m_playSceneHasVisibleObjects = std::any_of(m_playSceneEditorSnapshot.objects.begin(), m_playSceneEditorSnapshot.objects.end(), [](const SceneObjectDesc& object) {
    const std::string mesh = object.mesh.empty() ? object.name : object.mesh;
    return object.visible && !mesh.empty();
  });
  if (!SaveSceneToFile(m_playSceneEditorSnapshot, outPath)) {
    outPath.clear();
    return false;
  }
  return true;
}

void EditorApp::RestoreEditorStateAfterPlay() {
  const SceneFile& sf = m_playSceneEditorSnapshot;
  m_panels.showSkybox = sf.editor.show_skybox;
  m_panels.showWireframe = sf.editor.show_wireframe;
  m_camera.SetTarget(XVECTOR3(sf.editor.camera_target.x,
                              sf.editor.camera_target.y,
                              sf.editor.camera_target.z));
  m_camera.SetOrbitState(sf.editor.camera_yaw,
                         sf.editor.camera_pitch,
                         sf.editor.camera_distance);
  if (m_lastW > 0 && m_lastH > 0) {
    m_camera.SetViewportSize(m_lastW, m_lastH);
  }

  const std::size_t objectCount = (std::min)(g_objects.size(), sf.objects.size());
  for (std::size_t i = 0; i < objectCount; ++i) {
    SceneObject& obj = g_objects[i];
    const SceneObjectDesc& od = sf.objects[i];
    obj.wireframe.Position() = XVECTOR3(od.position.x, od.position.y, od.position.z);
    obj.wireframe.EulerRadians() = XVECTOR3(
        od.rotation.x * kDegToRad,
        od.rotation.y * kDegToRad,
        od.rotation.z * kDegToRad);
    obj.wireframe.Scale() = XVECTOR3(od.scale.x, od.scale.y, od.scale.z);
    obj.visible = od.visible;
    obj.mobileVisible = od.mobile_visible;
    obj.frozen = od.frozen;
    obj.showWire = od.show_wire;
    obj.showOrientation = od.show_orientation;
    obj.navAgentFrontYawOffsetDeg = od.nav_agent_front_yaw_offset_deg;
    obj.navAgentFaceYawSign = od.nav_agent_face_yaw_sign;
    obj.navAgentTargetMode = od.nav_agent_target_mode.empty() ? "direct" : od.nav_agent_target_mode;
    obj.navAgentFollowDistance = od.nav_agent_follow_distance;
    obj.navAgentSideOffset = od.nav_agent_side_offset;
    obj.navAgentFormationDepthStep = od.nav_agent_formation_depth_step;
    obj.navAgentSlot = od.nav_agent_slot;
    obj.physics = od.physics;
    obj.navigation = od.navigation;
    obj.ragdollAuthoringMeta = od.ragdoll_authoring;
  }

  g_cameras.clear();
  for (const auto& cd : sf.cameras) {
    SceneCamera c;
    c.name = cd.name;
    c.type = (CameraType)cd.type;
    c.position = XVECTOR3(cd.position.x, cd.position.y, cd.position.z);
    c.target = XVECTOR3(cd.target.x, cd.target.y, cd.target.z);
    c.fovDeg = cd.fov_deg;
    c.orthoW = cd.ortho_w;
    c.orthoH = cd.ortho_h;
    c.nearPlane = cd.near_plane;
    c.farPlane = cd.far_plane;
    c.visible = cd.visible;
    c.frozen = cd.frozen;
    g_cameras.push_back(c);
  }
  g_activeCameraIdx =
      (m_playScenePreviousActiveCameraIdx >= 0 &&
       m_playScenePreviousActiveCameraIdx < static_cast<int>(g_cameras.size()))
          ? m_playScenePreviousActiveCameraIdx
          : -1;

  g_lights.clear();
  for (const auto& ld : sf.lights) {
    SceneLight l;
    l.name = ld.name;
    l.type = (EditorLightType)ld.type;
    l.position = XVECTOR3(ld.position.x, ld.position.y, ld.position.z);
    l.direction = XVECTOR3(ld.direction.x, ld.direction.y, ld.direction.z);
    l.color = XVECTOR3(ld.color.x, ld.color.y, ld.color.z);
    l.intensity = ld.intensity;
    l.radius = ld.radius;
    l.enabled = ld.enabled;
    l.visible = ld.visible;
    l.frozen = ld.frozen;
    l.q3 = ld.q3;
    g_lights.push_back(l);
  }
  g_selectedIdx = -1;
  g_selectionType = 0;
  ClearMixedSelection();
  InvalidateSceneObjectTransformSnapshots();
  SyncSceneObjectTransforms();
  if (sf.navigation_mesh) {
    RestoreEditorNavMeshFromScene(*sf.navigation_mesh);
  } else {
    ResetEditorNavMeshState(false);
  }
}

void EditorApp::OpenPlayScene() {
  if (m_meshEditorOpen) {
    CloseMeshEditor();
  }
  if (m_playSceneOpen) {
    ClosePlayScene(false);
  }

  std::string tempPath;
  if (!ExportTemporaryPlayScene(tempPath)) {
    m_playSceneStatus = "Failed to export temporary play scene.";
    T8_LOG_ERROR("[T8ditor] Play Scene failed: temporary scene export failed");
    return;
  }

  m_playScenePreviousConfig = t850::g_config;
  m_playSceneHasPreviousConfig = true;
  m_playScenePreviousActiveCameraIdx = g_activeCameraIdx;
  m_playSceneTempPath = tempPath;
  m_playSceneWindow.Open(false);
  m_playSceneLaunchFailed = false;
  InvalidateEditorFrozenFrame();
  m_playSceneStatus = m_playSceneHasVisibleObjects
      ? "Starting Play Scene..."
      : "Temporary scene has no visible meshes. Add an object before Play.";
  T8_LOG_INFO("[T8ditor] Play Scene exported temporary scene '%s'", m_playSceneTempPath.c_str());
}

void EditorApp::ClosePlayScene(bool restoreEditorScene) {
  const std::string restorePath = m_playSceneTempPath;
  if (pFramework && pFramework->pVideoDriver) {
    pFramework->pVideoDriver->WaitForGPU();
  }
  if (m_playScene && m_playSceneLoaded) {
    m_playScene->OnDestoryScene();
  }
  m_playScene.reset();
  m_playSceneWindow.Reset(false);
  m_playSceneHasVisibleObjects = false;
  m_playSceneLaunchFailed = false;
  DestroyPlaySceneViewportTarget();
  if (m_playScenePhysics.IsInitialized()) {
    m_playScenePhysics.Shutdown();
  }
  m_playSceneEngineContext = t850::EngineContext{};

  if (m_playSceneHasPreviousConfig) {
    t850::g_config = m_playScenePreviousConfig;
    m_playSceneHasPreviousConfig = false;
  }
  if (restoreEditorScene) {
    RestoreEditorStateAfterPlay();
  }
  m_playScenePreviousActiveCameraIdx = -1;
  if (!restorePath.empty()) {
    std::error_code ec;
    std::filesystem::remove(restorePath, ec);
  }
  m_playSceneTempPath.clear();
  m_playSceneStatus.clear();
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

bool EditorApp::EnsurePlaySceneViewportTarget(int width, int height) {
  if (!pFramework || !pFramework->pVideoDriver || width <= 0 || height <= 0) {
    return false;
  }

  t850::RenderViewportDesc desc;
  desc.colorCount = 1;
  desc.colorFormat = t850::BaseRT::RGBA8;
  desc.depthFormat = t850::BaseRT::F32;
  desc.minWidth = 64;
  desc.minHeight = 64;
  if (!m_playSceneViewportTarget.Ensure(pFramework->pVideoDriver, width, height, desc)) {
    T8_LOG_ERROR("[T8ditor] Failed to create Play Scene viewport RT %dx%d", width, height);
    return false;
  }

  T8_LOG_INFO("[T8ditor] Play Scene viewport RT created output=%d size=%dx%d",
              m_playSceneViewportTarget.Handle(),
              m_playSceneViewportTarget.Width(),
              m_playSceneViewportTarget.Height());
  return true;
}

void EditorApp::DestroyPlaySceneViewportTarget() {
  if (pFramework && pFramework->pVideoDriver) {
    m_playSceneViewportTarget.Destroy(pFramework->pVideoDriver);
  }
  m_playSceneViewportImageMinX = 0.0f;
  m_playSceneViewportImageMinY = 0.0f;
  m_playSceneViewportImageSizeX = 0.0f;
  m_playSceneViewportImageSizeY = 0.0f;
}

bool EditorApp::EnsurePlaySceneRuntimeLoaded() {
  if (m_playSceneLoaded) {
    return true;
  }
  if (!pFramework || !pFramework->pVideoDriver || m_playSceneTempPath.empty() || !m_playSceneViewportTarget.IsValid()) {
    return false;
  }
  if (m_playSceneLaunchFailed) {
    return false;
  }
  if (!m_playSceneHasVisibleObjects) {
    return false;
  }

  m_playSceneEngineContext = t850::GetEngineContext();
  m_playSceneEngineContext.physics = &m_playScenePhysics;
  if (!m_playScenePhysics.IsInitialized()) {
    if (!m_playScenePhysics.Initialize() && m_playScenePhysics.IsAvailable()) {
      T8_LOG_ERROR("[T8ditor] Play Scene physics runtime failed to initialize");
    }
  }

  m_playScene = std::make_unique<::SceneTemplate>();
  m_playScene->pFramework = pFramework;
  SceneTemplateLaunchDesc launchDesc;
  launchDesc.sceneFilePath = m_playSceneTempPath;
  launchDesc.modelPath = t850::g_config.modelPath;
  launchDesc.width = m_playSceneViewportTarget.Width();
  launchDesc.height = m_playSceneViewportTarget.Height();
  launchDesc.startScene = 4;
  launchDesc.guiOnStart = false;
  m_playScene->SetLaunchDesc(launchDesc);
  m_playScene->SetEngineContext(&m_playSceneEngineContext);
  m_playScene->SetRenderSize(m_playSceneViewportTarget.Width(), m_playSceneViewportTarget.Height());
  m_playScene->SetFinalOutputRT(m_playSceneViewportTarget.Handle());
  m_playScene->OnLoadScene();
  if (m_playScene->m_meshCount <= 0) {
    m_playScene->OnDestoryScene();
    m_playScene.reset();
    m_playSceneStatus = "Play Scene did not load any visible meshes.";
    m_playSceneLaunchFailed = true;
    T8_LOG_ERROR("[T8ditor] Play Scene launch failed: runtime loaded no visible meshes from '%s'",
                 m_playSceneTempPath.c_str());
    return false;
  }
  m_playSceneLoaded = true;
  m_playSceneStatus.clear();
  T8_LOG_INFO("[T8ditor] Play Scene launched Quake3 scene from '%s'", m_playSceneTempPath.c_str());
  return true;
}

void EditorApp::DrawPlaySceneViewport() {
  ImVec2 available = ImGui::GetContentRegionAvail();
  if (m_playSceneLaunchFailed || !m_playSceneHasVisibleObjects) {
    ImGui::Dummy(ImVec2((std::max)(1.0f, available.x), 16.0f));
    ImGui::TextWrapped("%s", m_playSceneStatus.empty()
        ? "Temporary scene has no visible meshes. Add an object before Play."
        : m_playSceneStatus.c_str());
    return;
  }
  const EditorViewportSize desiredViewport = EditorViewportDesiredSize(available);
  t850::RenderViewportDesc viewportDesc;
  viewportDesc.minWidth = 64;
  viewportDesc.minHeight = 64;
  const bool shouldResizeRT =
      EditorViewportShouldResize(m_playSceneViewportTarget, desiredViewport.width, desiredViewport.height, viewportDesc);

  if (shouldResizeRT) {
    if (pFramework && pFramework->pVideoDriver) {
      pFramework->pVideoDriver->WaitForGPU();
    }
    if (!EnsurePlaySceneViewportTarget(desiredViewport.width, desiredViewport.height)) {
      ImGui::TextDisabled("Play Scene viewport unavailable.");
      return;
    }
    if (m_playScene && m_playSceneLoaded) {
      m_playScene->ResizeRenderTargets(
          m_playSceneViewportTarget.Width(),
          m_playSceneViewportTarget.Height(),
          m_playSceneViewportTarget.Handle());
    }
  }

  if (!pFramework || !pFramework->pVideoDriver || !m_playSceneViewportTarget.IsValid()) {
    ImGui::TextDisabled("Play Scene viewport unavailable.");
    return;
  }

  t850::BaseDriver* driver = pFramework->pVideoDriver;
  t850::BaseRT* rt = EditorRenderTarget(driver, m_playSceneViewportTarget.Handle());
  if (!EditorRenderTargetReady(rt, 1, false)) {
    ImGui::TextDisabled("Play Scene render target is unavailable.");
    return;
  }

  const ImVec2 imageSize((float)m_playSceneViewportTarget.Width(), (float)m_playSceneViewportTarget.Height());
  const ImVec2 imageMin = ImGui::GetCursorScreenPos();
  m_playSceneViewportImageMinX = imageMin.x;
  m_playSceneViewportImageMinY = imageMin.y;
  m_playSceneViewportImageSizeX = imageSize.x;
  m_playSceneViewportImageSizeY = imageSize.y;

  if (!EnsurePlaySceneRuntimeLoaded()) {
    ImGui::TextDisabled("%s", m_playSceneStatus.empty() ? "Play Scene is loading..." : m_playSceneStatus.c_str());
    return;
  }

  m_playScene->SetFinalOutputRT(m_playSceneViewportTarget.Handle());
  m_playScene->SetRenderSize(m_playSceneViewportTarget.Width(), m_playSceneViewportTarget.Height());
  m_playScene->OnUpdate(m_dtSecs);
  m_playScene->OnDraw();

  if (!DrawEditorViewportTexture(driver,
                                 EditorRenderTargetColor(rt),
                                 imageMin,
                                 imageSize,
                                 "##PlaySceneViewportInput",
                                 "Play Scene texture is not available for ImGui.",
                                 &m_playSceneViewportInputActive)) {
    return;
  }
}

void EditorApp::DrawPlaySceneWindow() {
  if (!m_playSceneOpen) {
    return;
  }

  ImGuiSetNextNativeEditorWindow(160.0f, 160.0f, 1280.0f, 720.0f);
  m_playSceneOpenRequested = false;
  bool keepOpen = m_playSceneOpen;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  const bool rootBegun = ImGui::Begin("Play Scene", &keepOpen, ImGuiWindowFlags_NoDocking);
  if (rootBegun) {
    if (ImGuiViewport* viewport = ImGui::GetWindowViewport()) {
      ApplyNativeWindowChrome(viewport, "Play Scene");
      m_playSceneImGuiViewportId = (unsigned int)viewport->ID;
    }
    m_playSceneDockspaceId = (unsigned int)ImGui::GetID("PlaySceneDockSpace");
    m_playSceneDockClassId = (unsigned int)ImGui::GetID("PlaySceneDockClass");
    ImGuiWindowClass playClass{};
    playClass.ClassId = (ImGuiID)m_playSceneDockClassId;
    playClass.DockingAllowUnclassed = false;
    ImGui::DockSpace(
        (ImGuiID)m_playSceneDockspaceId,
        ImVec2(0.0f, 0.0f),
        ImGuiDockNodeFlags_None,
        &playClass);
  }
  ImGui::End();
  ImGui::PopStyleVar();

  if (!keepOpen) {
    m_playSceneWindow.RequestClose();
    return;
  }

  ImGuiWindowClass playClass{};
  playClass.ClassId = (ImGuiID)m_playSceneDockClassId;
  playClass.DockingAllowUnclassed = false;
  if (m_playSceneImGuiViewportId != 0) {
    ImGui::SetNextWindowViewport((ImGuiID)m_playSceneImGuiViewportId);
  }
  ImGui::SetNextWindowClass(&playClass);
  if (m_playSceneDockspaceId != 0) {
    ImGui::SetNextWindowDockID((ImGuiID)m_playSceneDockspaceId, ImGuiCond_FirstUseEver);
  }
  bool viewportOpen = true;
  if (ImGui::Begin("Play Scene Viewport##PlaySceneViewport",
                   &viewportOpen,
                   ImGuiWindowFlags_NoCollapse)) {
    if (ImGui::Button("Stop")) {
      m_playSceneWindow.RequestClose();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Press G for runtime controls.");
    ImGui::SameLine();
    if (m_playSceneLaunchFailed) {
      ImGui::TextDisabled("%s", m_playSceneStatus.c_str());
    } else if (m_playSceneHasVisibleObjects) {
      ImGui::TextDisabled("Temporary scene: %s", m_playSceneTempPath.c_str());
    } else {
      ImGui::TextDisabled("Temporary scene has no visible meshes; nothing to run.");
    }
    ImGui::Separator();
    if (!m_playSceneCloseRequested) {
      DrawPlaySceneViewport();
    }
  }
  ImGui::End();

  if (m_playSceneGuiVisible && m_playScene && m_playSceneLoaded) {
    if (m_playSceneImGuiViewportId != 0) {
      ImGui::SetNextWindowViewport((ImGuiID)m_playSceneImGuiViewportId);
    }
    ImGui::SetNextWindowSize(ImVec2(440.0f, 680.0f), ImGuiCond_FirstUseEver);
    t850::DevGuiContext gui;
    gui.SetIdSuffix("PlaySceneQuake3");
    gui.SetViewportId((ImGuiID)m_playSceneImGuiViewportId);
    gui.SetDockId((ImGuiID)m_playSceneDockspaceId);
    gui.SetWindowClassId((ImGuiID)m_playSceneDockClassId);
    const bool panelBegun = gui.BeginPanel("Scene Controls", &m_playSceneGuiVisible);
    if (panelBegun) {
      ImGui::TextDisabled("Play Scene runtime controls - press G to hide.");
      ImGui::Separator();
      m_playScene->DrawDevGui(gui);
    }
    gui.EndPanel();
  }
}

} // namespace t8ditor
