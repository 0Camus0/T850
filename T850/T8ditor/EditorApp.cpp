/*********************************************************
 * T8ditor - EditorApp implementation. See header.
 *********************************************************/

#include "EditorApp.h"
#include "SceneObject.h"
#include "SceneGraph.h"
#include "EditorScene.h"
#include "EditorSceneGizmos.h"
#include "UndoRedo.h"

#include <core/Core.h>
#include <video/BaseDriver.h>
#include <scene/RenderGraph.h>
#include <scene/RenderSkinnedMesh.h>
#include <utils/InputManager.h>
#include <utils/Log.h>
#include <utils/xMaths.h>
#include <utils/Picking.h>
#include <debug/FrameDumper.h>

#include <Descriptors.h>

#include <cmath>
#include <filesystem>
#include <set>
#include <map>

#include <imgui.h>
#include <ImGuizmo.h>
#include <SDL3/SDL.h>

#ifdef OS_WINDOWS
#include <core/windows/Win32Framework.h>
#endif

namespace t850 {
  extern Device* T8Device;
}

namespace t8ditor {

namespace {
  std::string g_startupMeshPath;
  const float kRadToDeg = 180.0f / xPI;
  const float kDegToRad = xPI / 180.0f;

  // Persistent skybox (editor backdrop, separate from scene meshes).
  t850::PrimitiveManager g_skyboxMgr;
  t850::PrimitiveInst    g_skyboxInst;
  int                    g_skyboxPrimId = -1;
  bool                   g_skyboxReady  = false;

  // Multi-mesh scene objects (file-scope because EditorApp.h is locked).
  std::vector<SceneObject> g_objects;
  int                      g_selectedIdx = -1;

  // Multi-selection: set of selected mesh indices
  std::set<int>            g_multiSelect;

  // Groups (persistent and temporary)
  std::vector<SceneGroup>  g_groups;           // persistent groups
  SceneGroup               g_tempGroup;        // temporary group from multi-select
  int                      g_activeGroupIdx = -1; // index into g_groups, or -1 for temp/none

  // Cameras and lights in the scene
  std::vector<SceneCamera> g_cameras;
  std::vector<SceneLight>  g_lights;

  // Selection: what type of entity is selected
  // 0=mesh, 1=camera, 2=light. Index is g_selectedIdx into the respective vector.
  int g_selectionType = 0;  // 0=mesh by default

  // Marquee box selection state
  bool     g_marqueeActive = false;
  ImVec2   g_marqueeStart  = {0, 0};

  // Active camera index (-1 = default editor camera)
  int g_activeCameraIdx = -1;

  // Undo/redo
  UndoStack g_undoStack;

  // ImGuizmo drag tracking
  bool           g_gizmoDragging = false;
  TransformState g_gizmoDragStart;

  // Persistent camera for scene camera viewport switching
  ::Camera g_viewCamera;

  // Deferred render graph
  t850::RenderGraph   g_renderGraph;
  t850::PrimitiveInst g_quads[8];
  bool                g_deferredReady = false;
  XMATRIX44           g_quadVP;  // persistent identity matrix for screen-space quads

  // RT debug: which RT attachment to display (-1 = backbuffer)
  int g_debugRT = -1;

  // Dummy 1x1 white texture for shadow slot
  t850::Texture* g_dummyWhiteTex = nullptr;

  // Dummy environment map (1x1 gray cube for skybox matID=0)
  int g_dummyEnvMapIdx = -1;

  // Pending scene load — deferred to execute before next frame's BeginFrame
  std::string g_pendingLoadPath;

  // Frame dumper for RT snapshot debugging (space key)
  t850::FrameDumper g_dumper;
  bool              g_dumperInited = false;
}

void SetStartupMeshPath(const std::string& p) {
  g_startupMeshPath = p;
}

// Helpers to access current selection
static SceneObject* SelectedObject() {
  if (g_selectionType == 0 && g_selectedIdx >= 0 && g_selectedIdx < (int)g_objects.size())
    return &g_objects[g_selectedIdx];
  return nullptr;
}

void EditorApp::InitVars() {
  m_dtTimer.Init();
  m_dtTimer.Update();
  m_dtSecs = 0.0f;
  m_firstFrame = true;
  T8_LOG_INFO("[T8ditor] EditorApp::InitVars");
}

void EditorApp::CreateAssets() {
  if (m_assetsCreated) return;
  if (!pFramework || !pFramework->pVideoDriver) {
    T8_LOG_ERROR("[T8ditor] CreateAssets called before driver init");
    return;
  }

  const auto& desc = pFramework->aplicationDescriptor;
  const int w = (int)desc.width;
  const int h = (int)desc.height;

  m_camera.Init(w, h, 50.0f);
  m_camera.SetTarget(XVECTOR3(0.0f, 0.0f, 0.0f));
  m_camera.Frame();
  m_lastW = w;
  m_lastH = h;

  if (!m_lines.Create())
    T8_LOG_ERROR("[T8ditor] EditorLineRenderer::Create failed");
  m_grid.Create(10, 1.0f);
  m_gizmo.Create();

  m_sceneProps.AddCamera(&m_camera.GetCameraMutable());
  m_sceneProps.AddDirectionalLight(
    XVECTOR3(0.0f, -1.0f, 0.0f), XVECTOR3(1.0f, 1.0f, 1.0f), 1.5f, true);
  m_sceneProps.ActiveLights = 1;
  m_sceneProps.AmbientColor = XVECTOR3(0.15f, 0.15f, 0.15f);
  m_sceneProps.EnvFactor = 0.3f;  // reduced env reflections (no HDR tone mapping)

  XMatIdentity(m_vp);
  m_primMgr.Init();
  m_primMgr.SetVP(&m_vp);
  m_primMgr.SetSceneProps(&m_sceneProps);

  // Load skybox as persistent editor backdrop
  if (std::filesystem::exists("Models/SkyBox.X")) {
    g_skyboxMgr.Init();
    g_skyboxMgr.SetVP(&m_vp);
    g_skyboxMgr.SetSceneProps(&m_sceneProps);
    int sid = g_skyboxMgr.CreateMesh("Models/SkyBox.X");
    if (sid >= 0) {
      g_skyboxPrimId = sid;
      g_skyboxInst.CreateInstance(g_skyboxMgr.GetPrimitive(sid), &m_vp);
      g_skyboxInst.Update();
      g_skyboxMgr.SetSceneProps(&m_sceneProps);
      g_skyboxReady = true;
    }
  }

  // Set up deferred render graph
  if (g_renderGraph.Load("Scenes/T8ditor_RenderGraph.json")) {
    g_renderGraph.CreateRenderTargets(pFramework->pVideoDriver, m_sceneProps);
    XMatIdentity(g_quadVP);
    for (int i = 0; i < 8; ++i) {
      g_quads[i].CreateInstance(m_primMgr.GetPrimitive(t850::PrimitiveManager::QUAD), &g_quadVP);
      g_quads[i].Update();
    }
    // Bind the G-buffer textures to quads[0] — the deferred lighting quad reads from these
    if (!pFramework->pVideoDriver->RTs.empty()) {
      auto* gbufferRT = pFramework->pVideoDriver->RTs[0];
      for (int j = 0; j < (int)gbufferRT->vColorTextures.size() && j < 4; ++j)
        g_quads[0].SetTexture(gbufferRT->vColorTextures[j], j);
      if (gbufferRT->vColorTextures.size() > 4)
        g_quads[0].SetTexture(gbufferRT->vColorTextures[4], 9);
      if (gbufferRT->pDepthTexture)
        g_quads[0].SetTexture(gbufferRT->pDepthTexture, 4);
    }
    g_deferredReady = true;
    m_primMgr.SetSceneProps(&m_sceneProps); // re-set so QUAD gets pScProp

    // Create a 1x1 white texture for shadow slot (deferred shader reads
    // shadow from tex5; without it, Shadow=0 and everything multiplies to black)
    unsigned char white[4] = { 255, 255, 255, 255 };
    g_dummyWhiteTex = t850::T8Device->CreateTextureFromMemory(white, 1, 1, 4, "dummyWhite");

    // Load environment cubemap for skybox (matID=0 in deferred shader samples texEnv)
    g_dummyEnvMapIdx = t850::g_pBaseDriver->CreateTexture("sky/CubeMap_SkyWater.dds");
    if (g_dummyEnvMapIdx >= 0) {
      g_quads[0].SetEnvironmentMap(t850::g_pBaseDriver->GetTexture(g_dummyEnvMapIdx));
      T8_LOG_INFO("[T8ditor] Environment cubemap loaded");
    }

    T8_LOG_INFO("[T8ditor] Deferred render graph ready");
  } else {
    T8_LOG_ERROR("[T8ditor] Render graph load failed — using forward fallback");
  }

  // Initialize frame dumper (space key to dump)
  {
    t850::FrameDumperConfig cfg;
    cfg.debugFrames = true;
    cfg.keepRunning = true;
    g_dumper.Init(cfg);
    g_dumperInited = true;
  }

  if (!g_startupMeshPath.empty() && g_startupMeshPath != "Models/SkyBox.X")
    ImportMesh(g_startupMeshPath);

  m_assetsCreated = true;

#ifdef OS_WINDOWS
  {
    auto* w32 = static_cast<t850::Win32Framework*>(pFramework);
    if (w32 && w32->m_pWindow)
      SDL_SetWindowResizable(w32->m_pWindow, true);
  }
#endif

  m_imguiReady = ImGuiInit(pFramework);
  if (!m_imguiReady)
    T8_LOG_ERROR("[T8ditor] ImGui init failed");
  else
    ImGuiLogCaptureStart();

  T8_LOG_INFO("[T8ditor] CreateAssets done (%dx%d)", w, h);
}

void EditorApp::ImportMesh(const std::string& path) {
  if (!std::filesystem::exists(path)) {
    T8_LOG_ERROR("[T8ditor] Mesh file not found: %s", path.c_str());
    return;
  }

  // Create a new scene object (append, don't replace)
  int id = m_primMgr.CreateMesh(path.c_str());
  if (id < 0) {
    T8_LOG_ERROR("[T8ditor] Failed to load mesh: %s", path.c_str());
    return;
  }

  g_objects.emplace_back();
  SceneObject& obj = g_objects.back();
  obj.primId = id;
  obj.name   = path;
  obj.litInst.CreateInstance(m_primMgr.GetPrimitive(id), &m_vp);
  obj.litInst.Update();

  m_primMgr.SetSceneProps(&m_sceneProps);

  obj.wireframe.Load(path);

  // Select the newly imported mesh
  g_selectedIdx = (int)g_objects.size() - 1;

  // Frame the camera on it
  m_camera.SetTarget(obj.wireframe.LocalCenter());
  m_camera.Frame();

  T8_LOG_INFO("[T8ditor] Loaded mesh [%d]: %s", g_selectedIdx, path.c_str());
}

void EditorApp::LoadAssets() {}

void EditorApp::DestroyAssets() {
  if (m_imguiReady) {
    ImGuiLogCaptureStop();
    ImGuiShutdown();
    m_imguiReady = false;
  }
  // Release textures created via CreateTextureFromMemory (not tracked by driver)
  if (g_dummyWhiteTex) { g_dummyWhiteTex->release(); g_dummyWhiteTex = nullptr; }
  // g_dummyEnvMapIdx is tracked in the driver's Textures vector and destroyed by DestroyTextures()
  g_dummyEnvMapIdx = -1;

  m_primMgr.DestroyPrimitives();
  g_objects.clear();
  g_cameras.clear();
  g_lights.clear();
  g_selectedIdx = -1;
  g_selectionType = 0;
  g_activeCameraIdx = -1;
  g_multiSelect.clear();
  g_groups.clear();
  g_activeGroupIdx = -1;
  g_undoStack.Clear();
  if (g_skyboxReady) {
    g_skyboxMgr.DestroyPrimitives();
    g_skyboxPrimId = -1;
    g_skyboxReady = false;
  }
  m_gizmo.Destroy();
  m_grid.Destroy();
  m_lines.Destroy();
  m_assetsCreated = false;
  T8_LOG_INFO("[T8ditor] DestroyAssets");
}

void EditorApp::CheckResize() {
#ifdef OS_WINDOWS
  auto* w32 = static_cast<t850::Win32Framework*>(pFramework);
  if (!w32 || !w32->m_pWindow) return;
  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(w32->m_pWindow, &w, &h);
  if (w > 0 && h > 0 && (w != m_lastW || h != m_lastH)) {
    if (pFramework->pVideoDriver->ResizeSwapchain(w, h)) {
      m_lastW = w;
      m_lastH = h;
      m_camera.SetViewportSize(w, h);
      pFramework->aplicationDescriptor.width  = w;
      pFramework->aplicationDescriptor.height = h;

      // Recreate deferred render targets at new resolution
      if (g_deferredReady) {
        T8_LOG_INFO("[T8ditor] Resize: flushing GPU before RT recreation");
        pFramework->pVideoDriver->FlushGPUResources();
        T8_LOG_INFO("[T8ditor] Resize: destroying old RTs");
        // Destroy old RTs
        pFramework->pVideoDriver->DestroyRTs();
        T8_LOG_INFO("[T8ditor] Resize: creating new RTs at %dx%d", w, h);
        // Recreate at new size (CreateRT with w=0,h=0 uses driver width/height)
        g_renderGraph.CreateRenderTargets(pFramework->pVideoDriver, m_sceneProps);
        T8_LOG_INFO("[T8ditor] Resize: rebinding textures");
        // Rebind G-buffer textures to quads
        if (!pFramework->pVideoDriver->RTs.empty()) {
          auto* gbufferRT = pFramework->pVideoDriver->RTs[0];
          for (int j = 0; j < (int)gbufferRT->vColorTextures.size() && j < 4; ++j)
            g_quads[0].SetTexture(gbufferRT->vColorTextures[j], j);
          if (gbufferRT->vColorTextures.size() > 4)
            g_quads[0].SetTexture(gbufferRT->vColorTextures[4], 9);
          if (gbufferRT->pDepthTexture)
            g_quads[0].SetTexture(gbufferRT->pDepthTexture, 4);
        }
        if (g_dummyWhiteTex)
          g_quads[0].SetTexture(g_dummyWhiteTex, 5);
        if (g_dummyEnvMapIdx >= 0)
          g_quads[0].SetEnvironmentMap(t850::g_pBaseDriver->GetTexture(g_dummyEnvMapIdx));
        T8_LOG_INFO("[T8ditor] Render targets recreated at %dx%d", w, h);
      }
    }
  }
#endif
}

void EditorApp::OnUpdate() {
  m_dtTimer.Update();
  m_dtSecs = m_dtTimer.GetDTSecs();
  if (m_firstFrame) { m_dtSecs = 1.0f / 60.0f; m_firstFrame = false; }
  m_sceneProps.FrameDeltaSec = m_dtSecs;

  // Execute deferred scene load BEFORE any GPU work this frame
  if (!g_pendingLoadPath.empty()) {
    SceneFile sf;
    if (LoadSceneFromFile(g_pendingLoadPath, sf)) {
      // Flush all GPU work from previous frames
      pFramework->pVideoDriver->WaitForGPU();

      // Destroy old scene
      m_primMgr.DestroyPrimitives();
      g_objects.clear();
      g_cameras.clear();
      g_lights.clear();
      g_selectedIdx = -1;
      g_selectionType = 0;
      g_activeCameraIdx = -1;
      g_multiSelect.clear();
      g_groups.clear();
      g_activeGroupIdx = -1;
      g_undoStack.Clear();
      m_primMgr.Init();
      m_primMgr.SetVP(&m_vp);
      m_primMgr.SetSceneProps(&m_sceneProps);

      // Recreate deferred quads from fresh QUAD primitive
      if (g_deferredReady) {
        for (int i = 0; i < 8; ++i) {
          g_quads[i].CreateInstance(m_primMgr.GetPrimitive(t850::PrimitiveManager::QUAD), &g_quadVP);
          g_quads[i].Update();
        }
        if (!pFramework->pVideoDriver->RTs.empty()) {
          auto* gbufferRT = pFramework->pVideoDriver->RTs[0];
          for (int j = 0; j < (int)gbufferRT->vColorTextures.size() && j < 4; ++j)
            g_quads[0].SetTexture(gbufferRT->vColorTextures[j], j);
          if (gbufferRT->vColorTextures.size() > 4)
            g_quads[0].SetTexture(gbufferRT->vColorTextures[4], 9);
          if (gbufferRT->pDepthTexture)
            g_quads[0].SetTexture(gbufferRT->pDepthTexture, 4);
        }
        if (g_dummyWhiteTex)
          g_quads[0].SetTexture(g_dummyWhiteTex, 5);
        if (g_dummyEnvMapIdx >= 0)
          g_quads[0].SetEnvironmentMap(t850::g_pBaseDriver->GetTexture(g_dummyEnvMapIdx));
        m_primMgr.SetSceneProps(&m_sceneProps);
      }

      // Load mesh objects
      for (auto& od : sf.objects) {
        ImportMesh(od.mesh);
        if (!g_objects.empty()) {
          auto& obj = g_objects.back();
          obj.name = od.name;
          obj.wireframe.Position() = XVECTOR3(od.position.x, od.position.y, od.position.z);
          obj.wireframe.EulerRadians() = XVECTOR3(
            od.rotation.x * kDegToRad, od.rotation.y * kDegToRad, od.rotation.z * kDegToRad);
          obj.wireframe.Scale() = XVECTOR3(od.scale.x, od.scale.y, od.scale.z);
          obj.visible  = od.visible;
          obj.frozen   = od.frozen;
          obj.showWire = od.show_wire;
        }
      }

      // Load cameras
      for (auto& cd : sf.cameras) {
        SceneCamera c;
        c.name = cd.name; c.type = (CameraType)cd.type;
        c.position = XVECTOR3(cd.position.x, cd.position.y, cd.position.z);
        c.target = XVECTOR3(cd.target.x, cd.target.y, cd.target.z);
        c.fovDeg = cd.fov_deg; c.orthoW = cd.ortho_w; c.orthoH = cd.ortho_h;
        c.nearPlane = cd.near_plane; c.farPlane = cd.far_plane;
        c.visible = cd.visible; c.frozen = cd.frozen;
        g_cameras.push_back(c);
      }

      // Load lights
      for (auto& ld : sf.lights) {
        SceneLight l;
        l.name = ld.name; l.type = (EditorLightType)ld.type;
        l.position = XVECTOR3(ld.position.x, ld.position.y, ld.position.z);
        l.direction = XVECTOR3(ld.direction.x, ld.direction.y, ld.direction.z);
        l.color = XVECTOR3(ld.color.x, ld.color.y, ld.color.z);
        l.intensity = ld.intensity; l.radius = ld.radius; l.enabled = ld.enabled;
        l.visible = ld.visible; l.frozen = ld.frozen;
        g_lights.push_back(l);
      }

      // Restore editor state
      m_panels.showSkybox    = sf.editor.show_skybox;
      m_panels.showWireframe = sf.editor.show_wireframe;
      m_camera.SetTarget(XVECTOR3(sf.editor.camera_target.x,
                                   sf.editor.camera_target.y,
                                   sf.editor.camera_target.z));
      m_camera.SetOrbitState(sf.editor.camera_yaw,
                             sf.editor.camera_pitch,
                             sf.editor.camera_distance);
      g_selectedIdx = -1;
    }
    g_pendingLoadPath.clear();
  }

  CheckResize();

  OnInput();
  OnDraw();
}

void EditorApp::OnInput() {
  const ImGuiIO& io = ImGui::GetIO();
  const bool imguiWantsMouse    = io.WantCaptureMouse;
  const bool imguiWantsKeyboard = io.WantCaptureKeyboard;

  if (!imguiWantsKeyboard) {
    const bool ctrlDown = IManager.PressedKey(T800K_LCTRL) || IManager.PressedKey(T800K_RCTRL);
    const bool shiftDown = IManager.PressedKey(T800K_LSHIFT) || IManager.PressedKey(T800K_RSHIFT);

    if (IManager.PressedOnceKey(T800K_q)) m_gizmo.SetMode(GizmoMode::Select);
    if (IManager.PressedOnceKey(T800K_w)) m_gizmo.SetMode(GizmoMode::Translate);
    if (IManager.PressedOnceKey(T800K_e)) m_gizmo.SetMode(GizmoMode::Rotate);
    if (IManager.PressedOnceKey(T800K_r)) m_gizmo.SetMode(GizmoMode::Scale);

    // Z key behavior:
    // Ctrl+Z = undo, Ctrl+Shift+Z = redo
    // Z alone: if mesh selected → frame camera on it; else reset camera
    if (IManager.PressedOnceKey(T800K_z)) {
      if (ctrlDown && shiftDown)
        g_undoStack.Redo();
      else if (ctrlDown)
        g_undoStack.Undo();
      else {
        SceneObject* sel = SelectedObject();
        if (sel && sel->wireframe.IsLoaded()) {
          // Center camera on the selected model's position with default viewing angle
          XVECTOR3 modelPos = sel->wireframe.Position();
          m_camera.SetTarget(modelPos);
          m_camera.ResetViewAngle();  // reset yaw/pitch/distance to default, keep target
          T8_LOG_INFO("[T8ditor] View centered on model at (%.1f, %.1f, %.1f)",
                      modelPos.x, modelPos.y, modelPos.z);
        } else {
          m_camera.ResetToDefault();
        }
      }
    }
    // Ctrl+Y also redoes
    if (ctrlDown && IManager.PressedOnceKey(T800K_y))
      g_undoStack.Redo();

    // Space key — dump frame (all RTs + snapshot)
    if (IManager.PressedOnceKey(T800K_SPACE) && g_dumperInited)
      g_dumper.RequestDump();
  }

  // Delete key — works even when ImGui panels have focus (but not during text input)
  if (!io.WantTextInput && IManager.PressedOnceKey(T800K_DELETE) && g_selectedIdx >= 0) {
    if (g_selectionType == 1 && g_selectedIdx < (int)g_cameras.size()) {
      if (g_activeCameraIdx == g_selectedIdx) g_activeCameraIdx = -1;
      else if (g_activeCameraIdx > g_selectedIdx) g_activeCameraIdx--;
      g_cameras.erase(g_cameras.begin() + g_selectedIdx);
      g_selectedIdx = -1;
      T8_LOG_INFO("[T8ditor] Camera deleted");
    }
    else if (g_selectionType == 2 && g_selectedIdx < (int)g_lights.size()) {
      g_lights.erase(g_lights.begin() + g_selectedIdx);
      g_selectedIdx = -1;
      T8_LOG_INFO("[T8ditor] Light deleted");
    }
    else if (g_selectionType == 0 && g_selectedIdx < (int)g_objects.size()) {
      g_objects.erase(g_objects.begin() + g_selectedIdx);
      g_selectedIdx = -1;
      T8_LOG_INFO("[T8ditor] Mesh deleted");
    }
  }

  float wheel = ImGuiConsumeWheelDelta();
  bool blockWheel = imguiWantsMouse && !ImGuizmo::IsOver();
  m_camera.Update(m_dtSecs, IManager,
                  blockWheel ? 0.0f : wheel,
                  imguiWantsMouse,
                  imguiWantsKeyboard);

  if (!imguiWantsKeyboard)
    ProcessSelectionInput();

  if (!imguiWantsMouse) {
    // Skip mouse pick while multi-select gizmo is active (avoid clearing selection)
    if (!(g_multiSelect.size() > 1 && m_gizmo.Mode() != GizmoMode::Select && ImGuizmo::IsOver()))
      HandleMousePick();
  }
}

void EditorApp::ProcessSelectionInput() {
  SceneObject* sel = SelectedObject();
  if (!sel || !sel->wireframe.IsLoaded()) return;

  const float linRate = 5.0f * m_dtSecs;
  const float rotRate = 1.5f * m_dtSecs;
  const float sclStep = 1.0f + 0.5f * m_dtSecs;

  XVECTOR3& pos = sel->wireframe.Position();
  XVECTOR3& eul = sel->wireframe.EulerRadians();
  XVECTOR3& scl = sel->wireframe.Scale();

  if (IManager.PressedKey(T800K_l)) pos.x += linRate;
  if (IManager.PressedKey(T800K_j)) pos.x -= linRate;
  if (IManager.PressedKey(T800K_u)) pos.y += linRate;
  if (IManager.PressedKey(T800K_o)) pos.y -= linRate;
  if (IManager.PressedKey(T800K_i)) pos.z += linRate;
  if (IManager.PressedKey(T800K_k)) pos.z -= linRate;

  if (IManager.PressedKey(T800K_LEFTBRACKET))  eul.y -= rotRate;
  if (IManager.PressedKey(T800K_RIGHTBRACKET)) eul.y += rotRate;

  if (IManager.PressedKey(T800K_QUOTE)) {
    scl.x *= sclStep; scl.y *= sclStep; scl.z *= sclStep;
  }
  if (IManager.PressedKey(T800K_SEMICOLON)) {
    scl.x /= sclStep; scl.y /= sclStep; scl.z /= sclStep;
  }
}

// Project a world-space point to screen coordinates.
static ImVec2 WorldToScreen(const XVECTOR3& p, const XMATRIX44& vp, int w, int h) {
  // Row-vector: [x,y,z,1] * VP
  float cx = p.x*vp.m11 + p.y*vp.m21 + p.z*vp.m31 + vp.m41;
  float cy = p.x*vp.m12 + p.y*vp.m22 + p.z*vp.m32 + vp.m42;
  float cw = p.x*vp.m14 + p.y*vp.m24 + p.z*vp.m34 + vp.m44;
  if (std::abs(cw) < 1e-6f) return ImVec2(-1, -1);
  float ndcX = cx / cw;
  float ndcY = cy / cw;
  float sx = (ndcX * 0.5f + 0.5f) * w;
  float sy = (1.0f - (ndcY * 0.5f + 0.5f)) * h;
  return ImVec2(sx, sy);
}

// Test if any part of a world AABB projects inside a screen rectangle.
static bool AABBInScreenRect(const t850::AABB& box, const XMATRIX44& vp,
                              int viewW, int viewH,
                              float rMinX, float rMinY, float rMaxX, float rMaxY) {
  float sMinX = 1e30f, sMinY = 1e30f, sMaxX = -1e30f, sMaxY = -1e30f;
  float bmin[3] = { box.vMin.x, box.vMin.y, box.vMin.z };
  float bmax[3] = { box.vMax.x, box.vMax.y, box.vMax.z };
  for (int c = 0; c < 8; c++) {
    float lx = (c & 1) ? bmax[0] : bmin[0];
    float ly = (c & 2) ? bmax[1] : bmin[1];
    float lz = (c & 4) ? bmax[2] : bmin[2];
    ImVec2 s = WorldToScreen(XVECTOR3(lx, ly, lz), vp, viewW, viewH);
    if (s.x < sMinX) sMinX = s.x;
    if (s.y < sMinY) sMinY = s.y;
    if (s.x > sMaxX) sMaxX = s.x;
    if (s.y > sMaxY) sMaxY = s.y;
  }
  // Overlap test
  return !(sMaxX < rMinX || sMinX > rMaxX || sMaxY < rMinY || sMinY > rMaxY);
}

void EditorApp::HandleMousePick() {
  const bool shiftDown = IManager.PressedKey(T800K_LSHIFT) || IManager.PressedKey(T800K_RSHIFT);
  const bool selectMode = (m_gizmo.Mode() == GizmoMode::Select);

  // Marquee drag in Select mode (skip when Alt is held — Alt+left-drag is orbit)
  if (selectMode) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;
    const bool altDown = IManager.PressedKey(T800K_LALT) || IManager.PressedKey(T800K_RALT);

    // Start marquee on mouse press (only if Alt is not held)
    if (IManager.PressedOnceMouseButton(0) && !altDown) {
      g_marqueeActive = true;
      g_marqueeStart = ImVec2((float)IManager.mouseX, (float)IManager.mouseY);
    }

    // Finish marquee on mouse release
    if (g_marqueeActive && !IManager.PressedMouseButton(0)) {
      g_marqueeActive = false;
      ImVec2 mEnd((float)IManager.mouseX, (float)IManager.mouseY);
      float dx = mEnd.x - g_marqueeStart.x;
      float dy = mEnd.y - g_marqueeStart.y;

      if (std::abs(dx) < 5.0f && std::abs(dy) < 5.0f) {
        // Tiny drag = single click pick
        goto single_pick;
      }

      // Build rect (normalize min/max)
      float rMinX = (g_marqueeStart.x < mEnd.x) ? g_marqueeStart.x : mEnd.x;
      float rMinY = (g_marqueeStart.y < mEnd.y) ? g_marqueeStart.y : mEnd.y;
      float rMaxX = (g_marqueeStart.x > mEnd.x) ? g_marqueeStart.x : mEnd.x;
      float rMaxY = (g_marqueeStart.y > mEnd.y) ? g_marqueeStart.y : mEnd.y;

      if (!shiftDown) g_multiSelect.clear();

      for (int i = 0; i < (int)g_objects.size(); ++i) {
        if (!g_objects[i].wireframe.IsLoaded() || g_objects[i].frozen || !g_objects[i].visible)
          continue;
        t850::AABB worldBox = g_objects[i].wireframe.WorldAABB();
        if (AABBInScreenRect(worldBox, m_vp, m_lastW, m_lastH, rMinX, rMinY, rMaxX, rMaxY)) {
          g_multiSelect.insert(i);
        }
      }

      // Update single selection to match multi-select state
      if (g_multiSelect.size() == 1) {
        g_selectedIdx = *g_multiSelect.begin();
        g_selectionType = 0;
      } else if (g_multiSelect.size() > 1) {
        g_selectedIdx = *g_multiSelect.begin();
        g_selectionType = 0;
      } else {
        g_selectedIdx = -1;
      }
      return;
    }
    return;
  }

single_pick:
  if (!IManager.PressedOnceMouseButton(0) && !selectMode) return;

  XMATRIX44 invVP;
  m_vp.Inverse(&invVP);
  t850::Ray ray = t850::ScreenPointToRay(
    (float)IManager.mouseX, (float)IManager.mouseY,
    0, 0, m_lastW, m_lastH, invVP);

  // Test all objects, pick the closest
  float bestT = FLT_MAX;
  int   bestIdx  = -1;
  int   bestType = 0;

  // Test meshes
  for (int i = 0; i < (int)g_objects.size(); ++i) {
    if (!g_objects[i].wireframe.IsLoaded() || g_objects[i].frozen) continue;
    t850::AABB worldBox = g_objects[i].wireframe.WorldAABB();
    float t = 0.0f;
    if (t850::RayIntersectsAABB(ray, worldBox, t) && t < bestT) {
      bestT = t; bestIdx = i; bestType = 0;
    }
  }

  // Test cameras (AABB pick — virtual bounding box around position)
  for (int i = 0; i < (int)g_cameras.size(); ++i) {
    if (g_cameras[i].frozen || !g_cameras[i].visible) continue;
    float hs = 2.0f;
    t850::AABB box(
      XVECTOR3(g_cameras[i].position.x - hs, g_cameras[i].position.y - hs, g_cameras[i].position.z - hs),
      XVECTOR3(g_cameras[i].position.x + hs, g_cameras[i].position.y + hs, g_cameras[i].position.z + hs));
    float t = 0.0f;
    if (t850::RayIntersectsAABB(ray, box, t) && t < bestT) {
      bestT = t; bestIdx = i; bestType = 1;
    }
  }

  // Test lights (AABB pick — virtual bounding box around position)
  for (int i = 0; i < (int)g_lights.size(); ++i) {
    if (g_lights[i].frozen || !g_lights[i].visible) continue;
    float hs = (g_lights[i].type == EditorLightType::Omni) ? 2.5f : 2.0f;
    t850::AABB box(
      XVECTOR3(g_lights[i].position.x - hs, g_lights[i].position.y - hs, g_lights[i].position.z - hs),
      XVECTOR3(g_lights[i].position.x + hs, g_lights[i].position.y + hs, g_lights[i].position.z + hs));
    float t = 0.0f;
    if (t850::RayIntersectsAABB(ray, box, t) && t < bestT) {
      bestT = t; bestIdx = i; bestType = 2;
    }
  }

  if (bestIdx >= 0) {
    g_selectedIdx   = bestIdx;
    g_selectionType = bestType;

    // Multi-select: shift-click adds/removes from set (meshes only)
    if (bestType == 0) {
      if (shiftDown) {
        if (g_multiSelect.count(bestIdx))
          g_multiSelect.erase(bestIdx);
        else
          g_multiSelect.insert(bestIdx);
      } else {
        g_multiSelect.clear();
        // Check if clicked mesh belongs to a persistent group — select entire group
        bool foundGroup = false;
        for (auto& grp : g_groups) {
          if (grp.persistent && grp.members.count(bestIdx)) {
            g_multiSelect = grp.members;
            foundGroup = true;
            break;
          }
        }
        if (!foundGroup)
          g_multiSelect.insert(bestIdx);
      }
    } else {
      g_multiSelect.clear();
    }
  } else {
    g_selectedIdx = -1;
    // Auto-switch to Select mode when deselecting (hides orphaned gizmo)
    m_gizmo.SetMode(GizmoMode::Select);
    if (!shiftDown)
      g_multiSelect.clear();
  }
}

void EditorApp::OnDraw() {
  if (!pFramework || !pFramework->pVideoDriver) return;

  t850::BaseDriver* drv = pFramework->pVideoDriver;
  T8_LOG_TRACE("[T8ditor] OnDraw: BeginFrame...");
  drv->BeginFrame();
  T8_LOG_TRACE("[T8ditor] OnDraw: Clear...");
  drv->Clear();

  if (m_assetsCreated) {
    // Determine which camera drives rendering
    if (g_activeCameraIdx >= 0 && g_activeCameraIdx < (int)g_cameras.size()) {
      // Build a persistent Camera from the scene camera
      SceneCamera& sc = g_cameras[g_activeCameraIdx];
      float aspect = (m_lastW > 0 && m_lastH > 0) ? (float)m_lastW / (float)m_lastH : 16.0f/9.0f;
      if (sc.type == CameraType::Perspective) {
        g_viewCamera.InitPerspective(sc.position, sc.fovDeg * (xPI / 180.0f), aspect, sc.nearPlane, sc.farPlane);
      } else {
        g_viewCamera.InitOrtho(sc.position, sc.orthoW, sc.orthoH, sc.nearPlane, sc.farPlane);
      }
      g_viewCamera.Eye = sc.position;
      g_viewCamera.SetLookAt(sc.target);
      g_viewCamera.Update(0.0f);
      // Point the scene props active camera at our persistent camera
      if (!m_sceneProps.pCameras.empty())
        m_sceneProps.pCameras[0] = &g_viewCamera;
    } else {
      // Editor orbit camera
      if (!m_sceneProps.pCameras.empty())
        m_sceneProps.pCameras[0] = &m_camera.GetCameraMutable();
    }

    const ::Camera& cam = *m_sceneProps.pCameras[0];
    m_vp = cam.VP;

    // Update headlamp (light 0) direction from camera
    if (!m_sceneProps.Lights.empty()) {
      XVECTOR3 look = cam.Look;
      XVECTOR3 eye  = cam.Eye;
      XVECTOR3 dir(look.x - eye.x, look.y - eye.y, look.z - eye.z);
      float len = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
      if (len > 0.0001f) { dir.x /= len; dir.y /= len; dir.z /= len; }
      m_sceneProps.Lights[0].Direction = dir;
      m_sceneProps.Lights[0].Position  = eye;
    }

    // Sync scene lights from editor lights.
    // Headlamp is at index 0. Only ENABLED user lights are added after it.
    {
      // Count enabled lights
      int enabledCount = 0;
      for (auto& lt : g_lights)
        if (lt.enabled) enabledCount++;

      size_t needed = 1 + enabledCount; // headlamp + enabled user lights
      while (m_sceneProps.Lights.size() > needed)
        m_sceneProps.Lights.pop_back();
      while (m_sceneProps.Lights.size() < needed)
        m_sceneProps.Lights.push_back(Light{});

      // Update enabled user lights only (skip disabled)
      int slot = 1;
      for (auto& lt : g_lights) {
        if (!lt.enabled) continue;
        Light& target = m_sceneProps.Lights[slot++];
        target.Position  = lt.position;
        target.Direction = lt.direction;
        target.Color     = lt.color;
        target.Intensity = lt.intensity;
        target.radius    = lt.radius;
        target.Enabled   = 1;
        target.Type      = (lt.type == EditorLightType::Directional) ? LIGHT_DIRECTIONAL : LIGHT_POINT;
      }
      m_sceneProps.ActiveLights = (int)m_sceneProps.Lights.size();
    }

    // Update all mesh transforms
    int visibleCount = 0;
    for (int i = 0; i < (int)g_objects.size(); ++i) {
      SceneObject& obj = g_objects[i];
      if (obj.primId < 0 || !obj.visible) continue;
      const XVECTOR3& pos = obj.wireframe.Position();
      const XVECTOR3& eul = obj.wireframe.EulerRadians();
      const XVECTOR3& scl = obj.wireframe.Scale();
      obj.litInst.TranslateAbsolute(pos.x, pos.y, pos.z);
      obj.litInst.RotateXAbsolute(eul.x * kRadToDeg);
      obj.litInst.RotateYAbsolute(eul.y * kRadToDeg);
      obj.litInst.RotateZAbsolute(eul.z * kRadToDeg);
      obj.litInst.ScaleAbsolute(scl.x, scl.y, scl.z);
      obj.litInst.Update();
      visibleCount++;
    }

    // Render meshes: deferred via render graph on D3D11/D3D12, forward on GL
    bool useDeferred = g_deferredReady
                    && drv->m_currentAPI != t850::GraphicsApi::OPENGL;

    if (useDeferred) {
      // Build mesh array: skybox first (index 0), then scene meshes
      // The render graph JSON controls which indices are drawn in each pass.
      std::vector<t850::PrimitiveInst*> allMeshes;

      // Skybox at index 0
      if (g_skyboxReady && m_panels.showSkybox) {
        g_skyboxInst.Update();
        allMeshes.push_back(&g_skyboxInst);
      }

      // Scene meshes
      for (auto& obj : g_objects)
        if (obj.primId >= 0 && obj.visible)
          allMeshes.push_back(&obj.litInst);

      // Bind shadow dummy and env map to quads[0] before execute
      if (g_dummyWhiteTex)
        g_quads[0].SetTexture(g_dummyWhiteTex, 5);
      if (g_dummyEnvMapIdx >= 0)
        g_quads[0].SetEnvironmentMap(t850::g_pBaseDriver->GetTexture(g_dummyEnvMapIdx));

      // Execute the render graph (GBuffer -> Deferred -> BackBuffer)
      // RenderGraph::Execute needs a contiguous PrimitiveInst array.
      // We copy the instances (shallow — pBase pointer stays valid).
      std::vector<t850::PrimitiveInst> meshArray;
      meshArray.reserve(allMeshes.size());
      for (auto* p : allMeshes) meshArray.push_back(*p);

      // Update animation + bone texture before render passes (Vulkan requirement)
      for (auto& obj : g_objects) {
        if (obj.primId >= 0 && obj.visible && obj.litInst.pBase) {
          auto* sk = dynamic_cast<t850::RenderSkinnedMesh*>(obj.litInst.pBase);
          if (sk && sk->HasSkinData()) sk->UpdateAnimationAndBones();
        }
      }

      ::Camera* mainCam = m_sceneProps.pCameras[0];
      t850::EnvironmentMapSet editorEnvMaps;
      editorEnvMaps.SetFallback(g_dummyEnvMapIdx);
      T8_LOG_TRACE("[T8ditor] OnDraw: RenderGraph Execute (%d meshes)...", (int)meshArray.size());
      g_renderGraph.Execute(drv, m_sceneProps,
        meshArray.data(), (int)meshArray.size(),
        g_quads, mainCam, nullptr, nullptr,
        editorEnvMaps);
      T8_LOG_TRACE("[T8ditor] OnDraw: RenderGraph Execute done");

      // RT debug override: if a specific RT is selected, draw it to backbuffer
      if (g_debugRT >= 0) {
        drv->SetBlendState(t850::BaseDriver::BLEND_OPAQUE);
        drv->SetDepthStencilState(t850::BaseDriver::NONE);
        int gi = 0;
        t850::Texture* debugTex = nullptr;
        for (int rtIdx = 0; rtIdx < (int)drv->RTs.size() && !debugTex; ++rtIdx) {
          auto* rt = drv->RTs[rtIdx];
          if (!rt) continue;
          for (int ci = 0; ci < (int)rt->vColorTextures.size(); ++ci) {
            if (gi == g_debugRT) { debugTex = rt->vColorTextures[ci]; break; }
            gi++;
          }
          if (!debugTex && rt->pDepthTexture) {
            if (gi == g_debugRT) debugTex = rt->pDepthTexture;
            gi++;
          }
        }
        if (debugTex) {
          g_quads[7].SetTexture(debugTex, 0);
          t850::ShaderKey bk(0);
          bk.setPass(t850::PassType::BACKBUFFER);
          g_quads[7].SetGlobalKey(bk);
          g_quads[7].Draw();
        }
        drv->SetDepthStencilState(t850::BaseDriver::DEPTH_DEFAULT);
      }
    } else {
      // Forward rendering (GL, or deferred not ready)
      // Skybox forward
      if (g_skyboxReady && m_panels.showSkybox) {
        t850::ShaderKey fwdKey(0);
        fwdKey.setPass(t850::PassType::FORWARD);
        g_skyboxInst.SetGlobalKey(fwdKey);
        g_skyboxInst.Update();
        g_skyboxInst.Draw();
      }
      for (int i = 0; i < (int)g_objects.size(); ++i) {
        SceneObject& obj = g_objects[i];
        if (obj.primId < 0 || !obj.visible) continue;
        t850::ShaderKey fwdKey(0);
        fwdKey.setPass(t850::PassType::FORWARD);
        obj.litInst.SetGlobalKey(fwdKey);
        obj.litInst.Draw();
      }
    }

    // Wireframe overlays (drawn after deferred resolve, on backbuffer)
    // Bind GBuffer depth for depth-tested wireframe
    if (useDeferred) {
      int gbufHandle = g_renderGraph.GetRTHandle("GBuffer");
      if (gbufHandle >= 0 && gbufHandle < (int)drv->RTs.size()) {
        auto* gbufRT = drv->RTs[gbufHandle];
        m_lines.SetDepthTexture(gbufRT->pDepthTexture);
      }
      m_lines.SetViewport(m_lastW, m_lastH);
      m_lines.SetFarPlane(cam.FPlane);
    } else {
      m_lines.SetDepthTexture(nullptr);
    }

    for (int i = 0; i < (int)g_objects.size(); ++i) {
      SceneObject& obj = g_objects[i];
      if (obj.primId < 0 || !obj.visible) continue;
      bool isSelected = (g_selectionType == 0 && i == g_selectedIdx) || g_multiSelect.count(i);
      bool showWire = m_panels.showWireframe || isSelected || obj.showWire;
      if (!showWire) continue;

      // For skinned meshes, use GPU-skinned wireframe + skeleton (same as SandBox)
      t850::RenderSkinnedMesh* skinned = nullptr;
      if (obj.litInst.pBase)
        skinned = dynamic_cast<t850::RenderSkinnedMesh*>(obj.litInst.pBase);

      if (skinned && skinned->HasSkinData()) {
        // Bind GBuffer depth for shader-based wireframe occlusion
        int gbufHandle = g_renderGraph.GetRTHandle("GBuffer");
        if (gbufHandle >= 0 && gbufHandle < (int)drv->RTs.size()) {
          auto* gbufRT = drv->RTs[gbufHandle];
          skinned->SetWireframeDepthTex(gbufRT->pDepthTexture);
        }
        skinned->SetWireframeViewport(m_lastW, m_lastH);
        drv->SetDepthStencilState(t850::BaseDriver::NONE);
        skinned->DrawWireframe();
        drv->SetDepthStencilState(t850::BaseDriver::NONE);
        skinned->DrawSkeleton();
        drv->SetDepthStencilState(t850::BaseDriver::DEPTH_DEFAULT);
      } else if (obj.wireframe.IsLoaded() && m_lines.IsReady()) {
        XVECTOR3 savedColor = obj.wireframe.WireColor;
        if (g_multiSelect.count(i) && g_multiSelect.size() > 1)
          obj.wireframe.WireColor = XVECTOR3(0.4f, 0.8f, 1.0f, 1.0f); // cyan for multi-select
        else if (isSelected)
          obj.wireframe.WireColor = XVECTOR3(1.0f, 1.0f, 1.0f, 1.0f);
        else
          obj.wireframe.WireColor = XVECTOR3(0.45f, 0.45f, 0.45f, 1.0f);
        drv->SetDepthStencilState(t850::BaseDriver::READ);
        obj.wireframe.Draw(m_lines, cam.VP);
        drv->SetDepthStencilState(t850::BaseDriver::DEPTH_DEFAULT);
        obj.wireframe.WireColor = savedColor;
      }
    }

    // Camera and light viewport gizmos (only if visible)
    if (m_lines.IsReady()) {
      for (int i = 0; i < (int)g_cameras.size(); ++i)
        if (g_cameras[i].visible)
          DrawCameraGizmo(m_lines, cam.VP, g_cameras[i], g_selectionType == 1 && i == g_selectedIdx);
      for (int i = 0; i < (int)g_lights.size(); ++i)
        if (g_lights[i].visible)
          DrawLightGizmo(m_lines, cam.VP, g_lights[i], g_selectionType == 2 && i == g_selectedIdx);
    }

    // Grid
    if (m_lines.IsReady())
      m_grid.Draw(m_lines, cam.VP);
  }

  // ImGui overlay
  if (m_imguiReady) {
    ImGuiNewFrame();

    MenuAction menuAction = ImGuiDrawMenuBar(m_panels);

    int addCamera = -1, addLight = -1;
    bool wantsGroup = false, wantsUngroup = false;
    int mode = ImGuiDrawToolbar((int)m_gizmo.Mode(), addCamera, addLight,
                                 wantsGroup, wantsUngroup, g_multiSelect.size() >= 2);
    m_gizmo.SetMode((GizmoMode)mode);

    // Handle add camera/light from toolbar
    if (addCamera >= 0) {
      SceneCamera cam;
      cam.name = "Camera " + std::to_string(g_cameras.size());
      cam.type = (addCamera == 1) ? CameraType::Orthographic : CameraType::Perspective;
      g_cameras.push_back(cam);
      g_selectedIdx   = (int)g_cameras.size() - 1;
      g_selectionType = 1;
    }
    if (addLight >= 0) {
      SceneLight lt;
      lt.name = "Light " + std::to_string(g_lights.size());
      lt.type = (addLight == 1) ? EditorLightType::Omni : EditorLightType::Directional;
      g_lights.push_back(lt);
      g_selectedIdx   = (int)g_lights.size() - 1;
      g_selectionType = 2;
    }

    // Sync temp group from multi-select
    g_tempGroup.members = g_multiSelect;
    g_tempGroup.persistent = false;

    // Right-click context menu
    {
      bool hasSel = (g_selectedIdx >= 0) || !g_multiSelect.empty();
      bool hasMulti = g_multiSelect.size() >= 2;
      bool hasGrp = false;
      for (auto& grp : g_groups) {
        if (grp.persistent && grp.members == g_multiSelect) { hasGrp = true; break; }
      }
      ContextAction ctx = ImGuiDrawContextMenu(hasSel, hasMulti, hasGrp);
      if (ctx.setMode >= -1) m_gizmo.SetMode((GizmoMode)ctx.setMode);
      if (ctx.wantsGroup) wantsGroup = true;
      if (ctx.wantsUngroup) wantsUngroup = true;
      if (ctx.wantsDelete && g_selectedIdx >= 0) {
        if (g_selectionType == 0 && g_selectedIdx < (int)g_objects.size()) {
          g_objects.erase(g_objects.begin() + g_selectedIdx);
          g_multiSelect.erase(g_selectedIdx);
          g_selectedIdx = -1;
        } else if (g_selectionType == 1 && g_selectedIdx < (int)g_cameras.size()) {
          if (g_activeCameraIdx == g_selectedIdx) g_activeCameraIdx = -1;
          else if (g_activeCameraIdx > g_selectedIdx) g_activeCameraIdx--;
          g_cameras.erase(g_cameras.begin() + g_selectedIdx);
          g_selectedIdx = -1;
        } else if (g_selectionType == 2 && g_selectedIdx < (int)g_lights.size()) {
          g_lights.erase(g_lights.begin() + g_selectedIdx);
          g_selectedIdx = -1;
        }
      }
      if (ctx.wantsFrameView) {
        SceneObject* sel = SelectedObject();
        if (sel && sel->wireframe.IsLoaded()) {
          m_camera.SetTarget(sel->wireframe.Position());
          m_camera.ResetViewAngle();
        }
      }
      if (ctx.addCamera >= 0) {
        SceneCamera cam;
        cam.name = "Camera " + std::to_string(g_cameras.size());
        cam.type = (ctx.addCamera == 1) ? CameraType::Orthographic : CameraType::Perspective;
        g_cameras.push_back(cam);
        g_selectedIdx = (int)g_cameras.size() - 1;
        g_selectionType = 1;
      }
      if (ctx.addLight >= 0) {
        SceneLight lt;
        lt.name = "Light " + std::to_string(g_lights.size());
        lt.type = (ctx.addLight == 1) ? EditorLightType::Omni : EditorLightType::Directional;
        g_lights.push_back(lt);
        g_selectedIdx = (int)g_lights.size() - 1;
        g_selectionType = 2;
      }
    }

    // Group button / context menu: create persistent group
    if (wantsGroup && g_multiSelect.size() >= 2) {
      bool alreadyGrouped = false;
      for (auto& grp : g_groups) {
        if (grp.members == g_multiSelect) { alreadyGrouped = true; break; }
      }
      if (!alreadyGrouped) {
        SceneGroup grp;
        grp.name = "Group " + std::to_string(g_groups.size());
        grp.members = g_multiSelect;
        grp.persistent = true;
        g_groups.push_back(grp);
        g_activeGroupIdx = (int)g_groups.size() - 1;
        T8_LOG_INFO("[T8ditor] Created group '%s' with %d objects", grp.name.c_str(), (int)grp.members.size());
      }
    }

    // Ungroup button / context menu: dissolve group
    if (wantsUngroup && g_multiSelect.size() >= 2) {
      for (int gi = (int)g_groups.size() - 1; gi >= 0; gi--) {
        if (g_groups[gi].members == g_multiSelect) {
          T8_LOG_INFO("[T8ditor] Ungrouped '%s'", g_groups[gi].name.c_str());
          g_groups.erase(g_groups.begin() + gi);
          if (g_activeGroupIdx == gi) g_activeGroupIdx = -1;
          else if (g_activeGroupIdx > gi) g_activeGroupIdx--;
          break;
        }
      }
    }

    // Draw marquee selection rectangle (Select mode)
    if (g_marqueeActive && m_gizmo.Mode() == GizmoMode::Select) {
      ImVec2 mPos((float)IManager.mouseX, (float)IManager.mouseY);
      ImDrawList* dl = ImGui::GetBackgroundDrawList();
      dl->AddRectFilled(g_marqueeStart, mPos, IM_COL32(100, 100, 255, 40));
      dl->AddRect(g_marqueeStart, mPos, IM_COL32(100, 100, 255, 200), 0.0f, 0, 1.5f);
    }

    // Group / multi-select bounding box (corner brackets)
    if (g_multiSelect.size() > 1) {
      t850::AABB combined;
      bool first = true;
      for (int idx : g_multiSelect) {
        if (idx < 0 || idx >= (int)g_objects.size()) continue;
        if (!g_objects[idx].wireframe.IsLoaded() || !g_objects[idx].visible) continue;
        t850::AABB wb = g_objects[idx].wireframe.WorldAABB();
        if (first) { combined = wb; first = false; }
        else {
          if (wb.vMin.x < combined.vMin.x) combined.vMin.x = wb.vMin.x;
          if (wb.vMin.y < combined.vMin.y) combined.vMin.y = wb.vMin.y;
          if (wb.vMin.z < combined.vMin.z) combined.vMin.z = wb.vMin.z;
          if (wb.vMax.x > combined.vMax.x) combined.vMax.x = wb.vMax.x;
          if (wb.vMax.y > combined.vMax.y) combined.vMax.y = wb.vMax.y;
          if (wb.vMax.z > combined.vMax.z) combined.vMax.z = wb.vMax.z;
        }
      }
      if (!first) {
        const ::Camera& camBB = *m_sceneProps.pCameras[0];
        float bmin[3] = { combined.vMin.x, combined.vMin.y, combined.vMin.z };
        float bmax[3] = { combined.vMax.x, combined.vMax.y, combined.vMax.z };
        ImVec2 corners[8];
        bool allValid = true;
        for (int c = 0; c < 8; c++) {
          float lx = (c & 1) ? bmax[0] : bmin[0];
          float ly = (c & 2) ? bmax[1] : bmin[1];
          float lz = (c & 4) ? bmax[2] : bmin[2];
          corners[c] = WorldToScreen(XVECTOR3(lx, ly, lz), camBB.VP, m_lastW, m_lastH);
          if (corners[c].x < -5000 || corners[c].y < -5000) allValid = false;
        }
        if (allValid) {
          ImDrawList* dl = ImGui::GetBackgroundDrawList();
          ImU32 col = IM_COL32(100, 200, 255, 200);
          float thickness = 1.5f;
          int edges[12][2] = {
            {0,1},{2,3},{4,5},{6,7},
            {0,2},{1,3},{4,6},{5,7},
            {0,4},{1,5},{2,6},{3,7}
          };
          for (int e = 0; e < 12; e++) {
            ImVec2 a = corners[edges[e][0]];
            ImVec2 b = corners[edges[e][1]];
            float dx = b.x - a.x, dy = b.y - a.y;
            float frac = 0.25f;
            dl->AddLine(a, ImVec2(a.x + dx * frac, a.y + dy * frac), col, thickness);
            dl->AddLine(b, ImVec2(b.x - dx * frac, b.y - dy * frac), col, thickness);
          }
        }
      }
    }

    // ImGuizmo on selected entity
    ImGuizmoBeginFrame(0, 0, m_lastW, m_lastH, false);
    const ::Camera& cam2 = m_camera.GetCamera();

    // Multi-select group gizmo (meshes only, 2+ selected)
    // Scene-graph approach: root node at centroid, children at offsets.
    // ImGuizmo manipulates the root; children inherit the transform.
    if (g_multiSelect.size() > 1) {
      // Persistent scene-graph helper (survives across frames during drag)
      static GroupTransformHelper s_groupHelper;
      static std::map<int, TransformState> s_undoBeforeState;

      // Determine which group to use (persistent or temp)
      SceneGroup* activeGroup = &g_tempGroup;
      for (auto& grp : g_groups) {
        if (grp.members == g_multiSelect) { activeGroup = &grp; break; }
      }

      XVECTOR3 centroid = activeGroup->Centroid(g_objects);

      // When not dragging, show gizmo at current centroid
      // (use a temp matrix for display — the real one lives in s_groupHelper)
      XMATRIX44 displayMat;
      if (s_groupHelper.IsActive()) {
        // During drag: use the helper's persistent root matrix
        // (ImGuizmo already wrote into it last frame)
      } else {
        XMatTranslation(displayMat, centroid.x, centroid.y, centroid.z);
      }

      bool isUsingNow = ImGuizmo::IsUsing();

      // ── Drag start: build the scene graph ──
      if (isUsingNow && !g_gizmoDragging) {
        g_gizmoDragging = true;

        // Snapshot original state for undo
        std::map<int, XVECTOR3> positions, rotations, scales;
        s_undoBeforeState.clear();
        for (int idx : g_multiSelect) {
          if (idx >= 0 && idx < (int)g_objects.size()) {
            positions[idx] = g_objects[idx].wireframe.Position();
            rotations[idx] = g_objects[idx].wireframe.EulerRadians();
            scales[idx]    = g_objects[idx].wireframe.Scale();
            s_undoBeforeState[idx] = {
              g_objects[idx].wireframe.Position(),
              g_objects[idx].wireframe.EulerRadians(),
              g_objects[idx].wireframe.Scale()
            };
          }
        }

        // Build the node tree: root at centroid, children at offsets
        s_groupHelper.Begin(centroid, positions, rotations, scales);
      }

      // ── ImGuizmo manipulate ──
      int imguizmoMode = mode;
      if (imguizmoMode < 0) imguizmoMode = 0;

      // Get the matrix pointer: persistent root matrix during drag, temp display otherwise
      float* matPtr = s_groupHelper.IsActive()
                    ? s_groupHelper.RootMatrix()
                    : &displayMat.m[0][0];

      XMATRIX44 deltaMatrix;
      XMatIdentity(deltaMatrix);

      bool manipulated = ImGuizmo::Manipulate(
        &cam2.View.m[0][0], &cam2.Projection.m[0][0],
        (ImGuizmo::OPERATION)((imguizmoMode == 0) ? ImGuizmo::TRANSLATE
                            : (imguizmoMode == 1) ? ImGuizmo::ROTATE
                            :                       ImGuizmo::SCALEU),
        ImGuizmo::WORLD,
        matPtr, &deltaMatrix.m[0][0]);

      // ── Apply: recompute children's world positions from the scene graph ──
      if (manipulated && s_groupHelper.IsActive()) {
        // ImGuizmo already modified the root matrix in place via matPtr.
        // Recompute children's world transforms through the tree.
        s_groupHelper.Update();

        // Read back children's world transforms into the scene objects
        for (int idx : g_multiSelect) {
          if (idx < 0 || idx >= (int)g_objects.size()) continue;

          XMATRIX44 childWorld = s_groupHelper.ChildWorldMatrix(idx);
          float t[3], rDeg[3], sComp[3];
          ImGuizmo::DecomposeMatrixToComponents(&childWorld.m[0][0], t, rDeg, sComp);

          g_objects[idx].wireframe.Position() = XVECTOR3(t[0], t[1], t[2]);
          g_objects[idx].wireframe.EulerRadians() = XVECTOR3(
            rDeg[0] * kDegToRad,
            rDeg[1] * kDegToRad,
            rDeg[2] * kDegToRad);

          // For scale mode: also scale child meshes
          if (imguizmoMode == 2) {
            float sf = s_groupHelper.RootUniformScale();
            XVECTOR3 origScale = s_groupHelper.OriginalScale(idx);
            g_objects[idx].wireframe.Scale() = XVECTOR3(
              origScale.x * sf, origScale.y * sf, origScale.z * sf);
          }
        }
      }

      // ── Drag end: bake and tear down ──
      if (!isUsingNow && g_gizmoDragging) {
        g_gizmoDragging = false;
        s_groupHelper.End();

        // Push undo
        std::map<int, TransformState> afterState;
        for (int idx : g_multiSelect) {
          if (idx >= 0 && idx < (int)g_objects.size()) {
            afterState[idx] = {
              g_objects[idx].wireframe.Position(),
              g_objects[idx].wireframe.EulerRadians(),
              g_objects[idx].wireframe.Scale()
            };
          }
        }
        auto cmd = std::make_unique<GroupTransformCommand>(
          s_undoBeforeState, afterState,
          [](int idx, const TransformState& s) {
            if (idx >= 0 && idx < (int)g_objects.size()) {
              g_objects[idx].wireframe.Position()     = s.position;
              g_objects[idx].wireframe.EulerRadians() = s.eulerRad;
              g_objects[idx].wireframe.Scale()         = s.scale;
            }
          });
        g_undoStack.Push(std::move(cmd));
        s_undoBeforeState.clear();
      }
    }
    else if (g_selectionType == 0) {
      // ── Mesh gizmo ──
      SceneObject* sel = SelectedObject();
      if (sel && sel->wireframe.IsLoaded()) {
        XMATRIX44 worldMat = sel->wireframe.BuildWorld();

        bool isUsingNow = ImGuizmo::IsUsing();
        if (isUsingNow && !g_gizmoDragging) {
          g_gizmoDragging = true;
          g_gizmoDragStart = { sel->wireframe.Position(),
                               sel->wireframe.EulerRadians(),
                               sel->wireframe.Scale() };
        }

        bool manipulated = ImGuizmoManipulate(
          &cam2.View.m[0][0], &cam2.Projection.m[0][0],
          mode, &worldMat.m[0][0]);
        if (manipulated) {
          float translation[3], rotation[3], scale[3];
          ImGuizmo::DecomposeMatrixToComponents(&worldMat.m[0][0], translation, rotation, scale);
          // Clamp scale to a small positive value to prevent degenerate matrices
          const float kMinScale = 0.001f;
          for (int s = 0; s < 3; s++)
            if (scale[s] < kMinScale && scale[s] > -kMinScale)
              scale[s] = (scale[s] >= 0) ? kMinScale : -kMinScale;
          sel->wireframe.Position() = XVECTOR3(translation[0], translation[1], translation[2]);
          sel->wireframe.EulerRadians() = XVECTOR3(
            rotation[0] * kDegToRad, rotation[1] * kDegToRad, rotation[2] * kDegToRad);
          sel->wireframe.Scale() = XVECTOR3(scale[0], scale[1], scale[2]);
        }

        if (!isUsingNow && g_gizmoDragging) {
          g_gizmoDragging = false;
          TransformState after = { sel->wireframe.Position(),
                                   sel->wireframe.EulerRadians(),
                                   sel->wireframe.Scale() };
          int idx = g_selectedIdx;
          auto cmd = std::make_unique<TransformCommand>(
            idx, g_gizmoDragStart, after,
            [idx](const TransformState& s) {
              if (idx >= 0 && idx < (int)g_objects.size()) {
                g_objects[idx].wireframe.Position()     = s.position;
                g_objects[idx].wireframe.EulerRadians() = s.eulerRad;
                g_objects[idx].wireframe.Scale()         = s.scale;
              }
            });
          g_undoStack.Push(std::move(cmd));
        }
      }
    }
    else if (g_selectionType == 1 && g_selectedIdx >= 0 && g_selectedIdx < (int)g_cameras.size()) {
      // ── Camera gizmo — use translate-only via ImGuizmo ──
      SceneCamera& sc = g_cameras[g_selectedIdx];

      // Camera position gizmo
      ImGuizmo::SetID(0);
      XMATRIX44 worldMat;
      XMatTranslation(worldMat, sc.position.x, sc.position.y, sc.position.z);

      bool manipulated = ImGuizmo::Manipulate(
        &cam2.View.m[0][0], &cam2.Projection.m[0][0],
        ImGuizmo::TRANSLATE, ImGuizmo::WORLD,
        &worldMat.m[0][0], nullptr);

      if (manipulated) {
        float t[3], r[3], s[3];
        ImGuizmo::DecomposeMatrixToComponents(&worldMat.m[0][0], t, r, s);
        XVECTOR3 delta(t[0] - sc.position.x, t[1] - sc.position.y, t[2] - sc.position.z);
        sc.position = XVECTOR3(t[0], t[1], t[2]);
        sc.target.x += delta.x;
        sc.target.y += delta.y;
        sc.target.z += delta.z;
      }

      // Camera target gizmo (separate ImGuizmo ID so both can coexist)
      ImGuizmo::SetID(1);
      XMATRIX44 targetMat;
      XMatTranslation(targetMat, sc.target.x, sc.target.y, sc.target.z);

      bool targetMoved = ImGuizmo::Manipulate(
        &cam2.View.m[0][0], &cam2.Projection.m[0][0],
        ImGuizmo::TRANSLATE, ImGuizmo::WORLD,
        &targetMat.m[0][0], nullptr);

      if (targetMoved) {
        float t[3], r[3], s[3];
        ImGuizmo::DecomposeMatrixToComponents(&targetMat.m[0][0], t, r, s);
        sc.target = XVECTOR3(t[0], t[1], t[2]);
      }
      ImGuizmo::SetID(-1); // reset
    }
    else if (g_selectionType == 2 && g_selectedIdx >= 0 && g_selectedIdx < (int)g_lights.size()) {
      // ── Light gizmo ──
      SceneLight& sl = g_lights[g_selectedIdx];

      if (sl.type == EditorLightType::Omni && mode == 2) {
        // Scale mode for omni: use delta matrix to adjust radius
        XMATRIX44 worldMat;
        XMatScaling(worldMat, sl.radius, sl.radius, sl.radius);
        // Set translation
        worldMat.m[3][0] = sl.position.x;
        worldMat.m[3][1] = sl.position.y;
        worldMat.m[3][2] = sl.position.z;

        XMATRIX44 deltaMatrix;
        XMatIdentity(deltaMatrix);

        bool manipulated = ImGuizmo::Manipulate(
          &cam2.View.m[0][0], &cam2.Projection.m[0][0],
          ImGuizmo::SCALEU, ImGuizmo::WORLD,
          &worldMat.m[0][0], &deltaMatrix.m[0][0]);

        if (manipulated) {
          float dt[3], dr[3], ds[3];
          ImGuizmo::DecomposeMatrixToComponents(&deltaMatrix.m[0][0], dt, dr, ds);
          float deltaScale = (ds[0] + ds[1] + ds[2]) / 3.0f;
          sl.radius *= deltaScale;
          if (sl.radius < 0.1f) sl.radius = 0.1f;
        }
      } else {
        // Translate mode
        XMATRIX44 worldMat;
        XMatTranslation(worldMat, sl.position.x, sl.position.y, sl.position.z);

        bool manipulated = ImGuizmo::Manipulate(
          &cam2.View.m[0][0], &cam2.Projection.m[0][0],
          ImGuizmo::TRANSLATE, ImGuizmo::WORLD,
          &worldMat.m[0][0], nullptr);

        if (manipulated) {
          float t[3], r[3], s[3];
          ImGuizmo::DecomposeMatrixToComponents(&worldMat.m[0][0], t, r, s);
          sl.position = XVECTOR3(t[0], t[1], t[2]);
        }
      }
    }

    // Menu actions
    if (menuAction.wantsExit) {
#ifdef OS_WINDOWS
      auto* w32fw = static_cast<t850::Win32Framework*>(pFramework);
      w32fw->m_alive = false;
#endif
    }
    if (menuAction.wantsImportX) {
      std::string path = OpenFileDialog(
        L"3D Models (*.x;*.glb;*.gltf)\0*.x;*.glb;*.gltf\0DirectX Mesh (*.x)\0*.x\0glTF Binary (*.glb)\0*.glb\0glTF (*.gltf)\0*.gltf\0All Files (*.*)\0*.*\0",
        L"Import Mesh");
      if (!path.empty()) ImportMesh(path);
    }
    if (menuAction.wantsSaveScene) {
      std::string path = SaveFileDialog(
        L"T8ditor Scene (*.t8scene)\0*.t8scene\0JSON (*.json)\0*.json\0All Files (*.*)\0*.*\0",
        L"Save Scene", L"t8scene");
      if (!path.empty()) {
        SceneFile sf;
        sf.editor.camera_target   = { m_camera.GetTarget().x, m_camera.GetTarget().y, m_camera.GetTarget().z };
        sf.editor.camera_yaw      = m_camera.GetYaw();
        sf.editor.camera_pitch    = m_camera.GetPitch();
        sf.editor.camera_distance = m_camera.GetDistance();
        sf.editor.show_skybox     = m_panels.showSkybox;
        sf.editor.show_wireframe  = m_panels.showWireframe;
        for (auto& obj : g_objects) {
          SceneObjectDesc od;
          od.name     = obj.name;
          od.mesh     = obj.name;
          od.position = { obj.wireframe.Position().x, obj.wireframe.Position().y, obj.wireframe.Position().z };
          od.rotation = { obj.wireframe.EulerRadians().x * kRadToDeg,
                          obj.wireframe.EulerRadians().y * kRadToDeg,
                          obj.wireframe.EulerRadians().z * kRadToDeg };
          od.scale    = { obj.wireframe.Scale().x, obj.wireframe.Scale().y, obj.wireframe.Scale().z };
          od.visible   = obj.visible;
          od.frozen    = obj.frozen;
          od.show_wire = obj.showWire;
          sf.objects.push_back(od);
        }
        for (auto& c : g_cameras) {
          SceneCameraDesc cd;
          cd.name       = c.name;
          cd.type       = (int)c.type;
          cd.position   = { c.position.x, c.position.y, c.position.z };
          cd.target     = { c.target.x, c.target.y, c.target.z };
          cd.fov_deg    = c.fovDeg;
          cd.ortho_w    = c.orthoW;
          cd.ortho_h    = c.orthoH;
          cd.near_plane = c.nearPlane;
          cd.far_plane  = c.farPlane;
          cd.visible    = c.visible;
          cd.frozen     = c.frozen;
          sf.cameras.push_back(cd);
        }
        for (auto& l : g_lights) {
          SceneLightDesc ld;
          ld.name      = l.name;
          ld.type      = (int)l.type;
          ld.position  = { l.position.x, l.position.y, l.position.z };
          ld.direction = { l.direction.x, l.direction.y, l.direction.z };
          ld.color     = { l.color.x, l.color.y, l.color.z };
          ld.intensity = l.intensity;
          ld.radius    = l.radius;
          ld.enabled   = l.enabled;
          ld.visible   = l.visible;
          ld.frozen    = l.frozen;
          sf.lights.push_back(ld);
        }
        SaveSceneToFile(sf, path);
      }
    }
    if (menuAction.wantsLoadScene) {
      std::string path = OpenFileDialog(
        L"T8ditor Scene (*.t8scene)\0*.t8scene\0JSON (*.json)\0*.json\0All Files (*.*)\0*.*\0",
        L"Load Scene");
      if (!path.empty()) {
        // Defer the actual load to the start of the next frame (before BeginFrame)
        // to avoid destroying GPU resources mid-command-list on D3D12.
        g_pendingLoadPath = path;
      }
    }

    // Panels
    if (m_panels.showHierarchy) {
      ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);
      if (ImGui::Begin("Hierarchy")) {
        ImGui::TextDisabled("Eye=show  F=freeze  W=wire");
        ImGui::Separator();
        if (ImGui::TreeNodeEx("Scene Root", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen)) {
          // Meshes & Groups
          if (ImGui::TreeNodeEx("Meshes", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Track which meshes are in persistent groups
            std::set<int> groupedIndices;
            for (auto& grp : g_groups)
              for (int idx : grp.members)
                groupedIndices.insert(idx);

            // Show persistent groups as collapsible parents
            for (int gi = 0; gi < (int)g_groups.size(); ++gi) {
              auto& grp = g_groups[gi];
              ImGui::PushID(gi + 40000);
              bool allSelected = true;
              for (int idx : grp.members)
                if (!g_multiSelect.count(idx)) { allSelected = false; break; }

              ImGuiTreeNodeFlags grpFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
              if (allSelected) grpFlags |= ImGuiTreeNodeFlags_Selected;
              std::string grpLabel = "[G] " + grp.name;
              bool grpOpen = ImGui::TreeNodeEx(grpLabel.c_str(), grpFlags);
              if (ImGui::IsItemClicked()) {
                // Click on group selects all its members
                g_multiSelect = grp.members;
                if (!grp.members.empty()) {
                  g_selectedIdx = *grp.members.begin();
                  g_selectionType = 0;
                }
              }
              if (grpOpen) {
                for (int idx : grp.members) {
                  if (idx < 0 || idx >= (int)g_objects.size()) continue;
                  auto& o = g_objects[idx];
                  ImGui::PushID(idx + 10000);
                  ImGui::Checkbox("##vis", &o.visible); ImGui::SameLine();
                  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
                  if (g_multiSelect.count(idx)) flags |= ImGuiTreeNodeFlags_Selected;
                  ImGui::TreeNodeEx(o.name.c_str(), flags);
                  if (ImGui::IsItemClicked()) {
                    g_selectedIdx = idx; g_selectionType = 0;
                  }
                  ImGui::TreePop();
                  ImGui::PopID();
                }
                ImGui::TreePop();
              }
              ImGui::PopID();
            }

            // Show ungrouped meshes
            for (int i = 0; i < (int)g_objects.size(); ++i) {
              if (groupedIndices.count(i)) continue; // skip grouped
              auto& o = g_objects[i];
              ImGui::PushID(i + 10000);
              ImGui::Checkbox("##vis", &o.visible); ImGui::SameLine();
              ImGui::Checkbox("##frz", &o.frozen);  ImGui::SameLine();
              ImGui::Checkbox("##wir", &o.showWire); ImGui::SameLine();
              ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
              if ((g_selectionType == 0 && i == g_selectedIdx) || g_multiSelect.count(i))
                flags |= ImGuiTreeNodeFlags_Selected;
              if (o.frozen) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.5f,0.5f,1));
              bool nodeOpen = ImGui::TreeNodeEx(o.name.c_str(), flags);
              if (ImGui::IsItemClicked() && !o.frozen) {
                if (g_selectionType == 0 && g_selectedIdx == i) g_selectedIdx = -1;
                else { g_selectedIdx = i; g_selectionType = 0; }
              }
              if (o.frozen) ImGui::PopStyleColor();
              if (nodeOpen) ImGui::TreePop();
              ImGui::PopID();
            }
            ImGui::TreePop();
          }
          // Cameras
          if (ImGui::TreeNodeEx("Cameras", ImGuiTreeNodeFlags_DefaultOpen)) {
            {
              bool isDefault = (g_activeCameraIdx < 0);
              ImGui::PushID(20000);
              if (ImGui::RadioButton("##act", isDefault)) g_activeCameraIdx = -1;
              ImGui::SameLine();
              ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
              ImGui::TreeNodeEx("[E] Editor Camera", flags);
              ImGui::TreePop();
              ImGui::PopID();
            }
            for (int i = 0; i < (int)g_cameras.size(); ++i) {
              auto& c = g_cameras[i];
              ImGui::PushID(i + 20001);
              bool isActive = (g_activeCameraIdx == i);
              if (ImGui::RadioButton("##act", isActive))
                g_activeCameraIdx = isActive ? -1 : i;
              ImGui::SameLine();
              ImGui::Checkbox("##vis", &c.visible); ImGui::SameLine();
              ImGui::Checkbox("##frz", &c.frozen);  ImGui::SameLine();
              ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
              if (g_selectionType == 1 && i == g_selectedIdx) flags |= ImGuiTreeNodeFlags_Selected;
              if (c.frozen) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.5f,0.5f,1));
              const char* icon = (c.type == CameraType::Perspective) ? "[P] " : "[O] ";
              std::string label = icon + c.name;
              bool nodeOpen = ImGui::TreeNodeEx(label.c_str(), flags);
              if (ImGui::IsItemClicked() && !c.frozen) {
                if (g_selectionType == 1 && g_selectedIdx == i) g_selectedIdx = -1;
                else { g_selectedIdx = i; g_selectionType = 1; }
              }
              if (c.frozen) ImGui::PopStyleColor();
              if (nodeOpen) ImGui::TreePop();
              ImGui::PopID();
            }
            ImGui::TreePop();
          }
          // Lights
          if (ImGui::TreeNodeEx("Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (int i = 0; i < (int)g_lights.size(); ++i) {
              auto& l = g_lights[i];
              ImGui::PushID(i + 30000);
              ImGui::Checkbox("##en",  &l.enabled); ImGui::SameLine();
              ImGui::Checkbox("##vis", &l.visible); ImGui::SameLine();
              ImGui::Checkbox("##frz", &l.frozen);  ImGui::SameLine();
              ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
              if (g_selectionType == 2 && i == g_selectedIdx) flags |= ImGuiTreeNodeFlags_Selected;
              if (l.frozen) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.5f,0.5f,1));
              const char* icon = (l.type == EditorLightType::Directional) ? "[D] " : "[O] ";
              std::string label = icon + l.name;
              bool nodeOpen = ImGui::TreeNodeEx(label.c_str(), flags);
              if (ImGui::IsItemClicked() && !l.frozen) {
                if (g_selectionType == 2 && g_selectedIdx == i) g_selectedIdx = -1;
                else { g_selectedIdx = i; g_selectionType = 2; }
              }
              if (l.frozen) ImGui::PopStyleColor();
              if (nodeOpen) ImGui::TreePop();
              ImGui::PopID();
            }
            ImGui::TreePop();
          }
          ImGui::TreePop();
        }
      }
      ImGui::End();
    }

    // ── Inspector ──
    SceneObject* sel = SelectedObject();
    if (m_panels.showInspector && g_selectedIdx >= 0) {
      ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);
      if (ImGui::Begin("Inspector")) {
        if (g_selectionType == 0 && sel) {
          // Mesh inspector
          ImGui::SeparatorText("Transform");
          XVECTOR3 pos = sel->wireframe.Position();
          XVECTOR3 eulerDeg(sel->wireframe.EulerRadians().x * kRadToDeg,
                            sel->wireframe.EulerRadians().y * kRadToDeg,
                            sel->wireframe.EulerRadians().z * kRadToDeg);
          XVECTOR3 scl = sel->wireframe.Scale();
          float p[3] = {pos.x, pos.y, pos.z};
          float r[3] = {eulerDeg.x, eulerDeg.y, eulerDeg.z};
          float s[3] = {scl.x, scl.y, scl.z};
          if (ImGui::DragFloat3("Position", p, 0.1f)) { pos.x=p[0]; pos.y=p[1]; pos.z=p[2]; }
          if (ImGui::DragFloat3("Rotation", r, 0.5f)) { eulerDeg.x=r[0]; eulerDeg.y=r[1]; eulerDeg.z=r[2]; }
          if (ImGui::DragFloat3("Scale", s, 0.01f, 0.01f, 100.0f)) { scl.x=s[0]; scl.y=s[1]; scl.z=s[2]; }
          sel->wireframe.Position() = pos;
          sel->wireframe.EulerRadians() = XVECTOR3(eulerDeg.x*kDegToRad, eulerDeg.y*kDegToRad, eulerDeg.z*kDegToRad);
          sel->wireframe.Scale() = scl;
        }
        else if (g_selectionType == 1 && g_selectedIdx < (int)g_cameras.size()) {
          // Camera inspector
          SceneCamera& cam = g_cameras[g_selectedIdx];
          ImGui::SeparatorText("Camera");
          const char* types[] = { "Perspective", "Orthographic" };
          int t = (int)cam.type;
          if (ImGui::Combo("Type", &t, types, 2))
            cam.type = (CameraType)t;
          float cp[3] = {cam.position.x, cam.position.y, cam.position.z};
          if (ImGui::DragFloat3("Position", cp, 0.1f))
            cam.position = XVECTOR3(cp[0], cp[1], cp[2]);
          float ct[3] = {cam.target.x, cam.target.y, cam.target.z};
          if (ImGui::DragFloat3("Target", ct, 0.1f))
            cam.target = XVECTOR3(ct[0], ct[1], ct[2]);
          if (cam.type == CameraType::Perspective) {
            ImGui::DragFloat("FOV (deg)", &cam.fovDeg, 0.5f, 5.0f, 170.0f);
          } else {
            ImGui::DragFloat("Ortho Width", &cam.orthoW, 0.1f, 0.1f, 1000.0f);
            ImGui::DragFloat("Ortho Height", &cam.orthoH, 0.1f, 0.1f, 1000.0f);
          }
          ImGui::DragFloat("Near Plane", &cam.nearPlane, 0.01f, 0.001f, cam.farPlane - 0.01f);
          ImGui::DragFloat("Far Plane",  &cam.farPlane,  1.0f, cam.nearPlane + 0.01f, 100000.0f);
        }
        else if (g_selectionType == 2 && g_selectedIdx < (int)g_lights.size()) {
          // Light inspector
          SceneLight& lt = g_lights[g_selectedIdx];
          ImGui::SeparatorText("Light");
          const char* types[] = { "Directional", "Omni" };
          int t = (int)lt.type;
          if (ImGui::Combo("Type", &t, types, 2))
            lt.type = (EditorLightType)t;
          float lp[3] = {lt.position.x, lt.position.y, lt.position.z};
          if (ImGui::DragFloat3("Position", lp, 0.1f))
            lt.position = XVECTOR3(lp[0], lp[1], lp[2]);
          if (lt.type == EditorLightType::Directional) {
            float ld[3] = {lt.direction.x, lt.direction.y, lt.direction.z};
            if (ImGui::DragFloat3("Direction", ld, 0.01f)) {
              lt.direction = XVECTOR3(ld[0], ld[1], ld[2]);
              lt.direction.Normalize();
            }
          } else {
            ImGui::DragFloat("Radius", &lt.radius, 0.1f, 0.1f, 10000.0f);
          }
          float c[3] = {lt.color.x, lt.color.y, lt.color.z};
          if (ImGui::ColorEdit3("Color", c))
            lt.color = XVECTOR3(c[0], c[1], c[2]);
          ImGui::DragFloat("Intensity", &lt.intensity, 0.05f, 0.0f, 100.0f);
          ImGui::Checkbox("Enabled", &lt.enabled);
        }
      }
      ImGui::End();
    }

    if (m_panels.showConsole)
      ImGuiDrawConsolePanel();

    if (m_panels.showRTDebug)
      g_debugRT = ImGuiDrawRTDebugPanel(g_debugRT);

    T8_LOG_TRACE("[T8ditor] OnDraw: ImGuiRender...");
    ImGuiRender();
    T8_LOG_TRACE("[T8ditor] OnDraw: ImGuiRender done");
  }

  // Frame dump (space key) — dump all render graph RTs
  if (g_dumperInited && g_dumper.ShouldDump(m_dtSecs)) {
    int gbuf = g_renderGraph.GetRTHandle("GBuffer");
    int def  = g_renderGraph.GetRTHandle("Deferred");
    std::vector<t850::RTDumpEntry> rts;
    if (gbuf >= 0) {
      rts.push_back({gbuf, t850::BaseDriver::COLOR0_ATTACHMENT, "GBuffer_Albedo"});
      rts.push_back({gbuf, t850::BaseDriver::COLOR1_ATTACHMENT, "GBuffer_Normals"});
      rts.push_back({gbuf, t850::BaseDriver::COLOR2_ATTACHMENT, "GBuffer_PBR"});
      rts.push_back({gbuf, t850::BaseDriver::COLOR3_ATTACHMENT, "GBuffer_GeoNormals"});
      rts.push_back({gbuf, t850::BaseDriver::COLOR4_ATTACHMENT, "GBuffer_Emissive"});
      rts.push_back({gbuf, t850::BaseDriver::COLOR5_ATTACHMENT, "GBuffer_Sheen"});
      rts.push_back({gbuf, t850::BaseDriver::COLOR6_ATTACHMENT, "GBuffer_SpecularOcclusion"});
      rts.push_back({gbuf, t850::BaseDriver::DEPTH_ATTACHMENT,  "GBuffer_Depth"});
    }
    if (def >= 0) {
      rts.push_back({def, t850::BaseDriver::COLOR0_ATTACHMENT, "Deferred_Output"});
    }
    ::Camera dummyLightCam;
    g_dumper.DumpFrame(drv, m_camera.GetCameraMutable(), dummyLightCam, m_sceneProps, rts, m_dtSecs);
    T8_LOG_INFO("[T8ditor] Frame dumped to disk");
  }

  T8_LOG_TRACE("[T8ditor] OnDraw: SwapBuffers...");
  drv->SwapBuffers();
  T8_LOG_TRACE("[T8ditor] OnDraw: EndFrame...");
  drv->EndFrame();
  T8_LOG_TRACE("[T8ditor] OnDraw: done");
}

void EditorApp::OnPause()  { bPaused = true;  }
void EditorApp::OnResume() { bPaused = false; }
void EditorApp::OnReset()  {}
void EditorApp::LoadScene(int) {}

} // namespace t8ditor