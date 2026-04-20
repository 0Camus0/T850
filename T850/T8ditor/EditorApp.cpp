/*********************************************************
 * T8ditor - EditorApp implementation. See header.
 *********************************************************/

#include "EditorApp.h"
#include "SceneObject.h"
#include "EditorScene.h"
#include "EditorSceneGizmos.h"
#include "UndoRedo.h"

#include <core/Core.h>
#include <video/BaseDriver.h>
#include <utils/InputManager.h>
#include <utils/Log.h>
#include <utils/xMaths.h>
#include <utils/Picking.h>

#include <T8_descriptors.h>

#include <cmath>
#include <filesystem>

#include <imgui.h>
#include <ImGuizmo.h>
#include <SDL3/SDL.h>

#ifdef OS_WINDOWS
#include <core/windows/Win32Framework.h>
#endif

namespace t8ditor {

namespace {
  std::string g_startupMeshPath;
  const float kRadToDeg = 180.0f / xPI;
  const float kDegToRad = xPI / 180.0f;

  // Persistent skybox (editor backdrop, separate from scene meshes).
  t800::PrimitiveManager g_skyboxMgr;
  t800::PrimitiveInst    g_skyboxInst;
  int                    g_skyboxPrimId = -1;
  bool                   g_skyboxReady  = false;

  // Multi-mesh scene objects (file-scope because EditorApp.h is locked).
  std::vector<SceneObject> g_objects;
  int                      g_selectedIdx = -1;

  // Cameras and lights in the scene
  std::vector<SceneCamera> g_cameras;
  std::vector<SceneLight>  g_lights;

  // Selection: what type of entity is selected
  // 0=mesh, 1=camera, 2=light. Index is g_selectedIdx into the respective vector.
  int g_selectionType = 0;  // 0=mesh by default

  // Undo/redo
  UndoStack g_undoStack;

  // ImGuizmo drag tracking
  bool           g_gizmoDragging = false;
  TransformState g_gizmoDragStart;
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

  if (!g_startupMeshPath.empty() && g_startupMeshPath != "Models/SkyBox.X")
    ImportMesh(g_startupMeshPath);

  m_assetsCreated = true;

#ifdef OS_WINDOWS
  {
    auto* w32 = static_cast<t800::Win32Framework*>(pFramework);
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
  m_primMgr.DestroyPrimitives();
  g_objects.clear();
  g_cameras.clear();
  g_lights.clear();
  g_selectedIdx = -1;
  g_selectionType = 0;
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
  auto* w32 = static_cast<t800::Win32Framework*>(pFramework);
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
    }
  }
#endif
}

void EditorApp::OnUpdate() {
  m_dtTimer.Update();
  m_dtSecs = m_dtTimer.GetDTSecs();
  if (m_firstFrame) { m_dtSecs = 1.0f / 60.0f; m_firstFrame = false; }
  m_sceneProps.FrameDeltaSec = m_dtSecs;

  CheckResize();

  // Update camera orbit target only when selection changes (not every frame,
  // otherwise translating via ImGuizmo creates a feedback loop).
  {
    static int s_prevSelectedIdx = -2; // -2 = uninitialized
    if (g_selectedIdx != s_prevSelectedIdx) {
      s_prevSelectedIdx = g_selectedIdx;
      SceneObject* sel = SelectedObject();
      if (sel)
        m_camera.SetTarget(sel->wireframe.Position());
    }
  }

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

    if (IManager.PressedOnceKey(T800K_w)) m_gizmo.SetMode(GizmoMode::Translate);
    if (IManager.PressedOnceKey(T800K_e)) m_gizmo.SetMode(GizmoMode::Rotate);
    if (IManager.PressedOnceKey(T800K_r)) m_gizmo.SetMode(GizmoMode::Scale);

    // Z = reset camera (only without Ctrl). Ctrl+Z = undo, Ctrl+Shift+Z = redo.
    if (IManager.PressedOnceKey(T800K_z)) {
      if (ctrlDown && shiftDown)
        g_undoStack.Redo();
      else if (ctrlDown)
        g_undoStack.Undo();
      else
        m_camera.ResetToDefault();
    }
    // Ctrl+Y also redoes
    if (ctrlDown && IManager.PressedOnceKey(T800K_y))
      g_undoStack.Redo();
  }

  float wheel = ImGuiConsumeWheelDelta();
  bool blockWheel = imguiWantsMouse && !ImGuizmo::IsOver();
  m_camera.Update(m_dtSecs, IManager,
                  blockWheel ? 0.0f : wheel,
                  imguiWantsMouse,
                  imguiWantsKeyboard);

  if (!imguiWantsKeyboard)
    ProcessSelectionInput();

  if (!imguiWantsMouse)
    HandleMousePick();
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

void EditorApp::HandleMousePick() {
  if (!IManager.PressedOnceMouseButton(0)) return;

  XMATRIX44 invVP;
  m_vp.Inverse(&invVP);
  t800::Ray ray = t800::ScreenPointToRay(
    (float)IManager.mouseX, (float)IManager.mouseY,
    0, 0, m_lastW, m_lastH, invVP);

  // Test all objects, pick the closest
  float bestT = FLT_MAX;
  int   bestIdx = -1;
  for (int i = 0; i < (int)g_objects.size(); ++i) {
    if (!g_objects[i].wireframe.IsLoaded()) continue;
    t800::AABB worldBox = g_objects[i].wireframe.WorldAABB();
    float t = 0.0f;
    if (t800::RayIntersectsAABB(ray, worldBox, t) && t < bestT) {
      bestT = t;
      bestIdx = i;
    }
  }

  g_selectedIdx = bestIdx;
  g_selectionType = (bestIdx >= 0) ? 0 : g_selectionType;
  if (bestIdx < 0) g_selectedIdx = -1;  // click on nothing = deselect
}

void EditorApp::OnDraw() {
  if (!pFramework || !pFramework->pVideoDriver) return;

  t800::BaseDriver* drv = pFramework->pVideoDriver;
  drv->BeginFrame();
  drv->Clear();

  if (m_assetsCreated) {
    const ::Camera& cam = m_camera.GetCamera();
    m_vp = cam.VP;

    // Update headlamp direction
    if (!m_sceneProps.Lights.empty()) {
      XVECTOR3 look = cam.Look;
      XVECTOR3 eye  = cam.Eye;
      XVECTOR3 dir(look.x - eye.x, look.y - eye.y, look.z - eye.z);
      float len = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
      if (len > 0.0001f) { dir.x /= len; dir.y /= len; dir.z /= len; }
      m_sceneProps.Lights[0].Direction = dir;
      m_sceneProps.Lights[0].Position  = eye;
    }

    // Skybox (editor backdrop)
    if (g_skyboxReady && m_panels.showSkybox) {
      t800::ShaderKey fwdKey(0);
      fwdKey.setPass(t800::PassType::FORWARD);
      g_skyboxInst.SetGlobalKey(fwdKey);
      g_skyboxInst.Update();
      g_skyboxInst.Draw();
    }

    // All scene objects
    for (int i = 0; i < (int)g_objects.size(); ++i) {
      SceneObject& obj = g_objects[i];
      if (obj.primId < 0) continue;

      const XVECTOR3& pos = obj.wireframe.Position();
      const XVECTOR3& eul = obj.wireframe.EulerRadians();
      const XVECTOR3& scl = obj.wireframe.Scale();
      obj.litInst.TranslateAbsolute(pos.x, pos.y, pos.z);
      obj.litInst.RotateXAbsolute(eul.x * kRadToDeg);
      obj.litInst.RotateYAbsolute(eul.y * kRadToDeg);
      obj.litInst.RotateZAbsolute(eul.z * kRadToDeg);
      obj.litInst.ScaleAbsolute(scl.x, scl.y, scl.z);
      t800::ShaderKey fwdKey(0);
      fwdKey.setPass(t800::PassType::FORWARD);
      obj.litInst.SetGlobalKey(fwdKey);
      obj.litInst.Update();
      obj.litInst.Draw();

      // Wireframe overlay
      bool isSelected = (g_selectionType == 0 && i == g_selectedIdx);
      bool showWire = m_panels.showWireframe || isSelected;
      if (showWire && obj.wireframe.IsLoaded() && m_lines.IsReady()) {
        XVECTOR3 savedColor = obj.wireframe.WireColor;
        obj.wireframe.WireColor = isSelected
          ? XVECTOR3(1.0f, 1.0f, 1.0f, 1.0f)
          : XVECTOR3(0.45f, 0.45f, 0.45f, 1.0f);
        drv->SetDepthStencilState(t800::BaseDriver::NONE);
        obj.wireframe.Draw(m_lines, cam.VP);
        drv->SetDepthStencilState(t800::BaseDriver::DEPTH_DEFAULT);
        obj.wireframe.WireColor = savedColor;
      }
    }

    // Camera and light viewport gizmos
    if (m_lines.IsReady()) {
      for (int i = 0; i < (int)g_cameras.size(); ++i)
        DrawCameraGizmo(m_lines, cam.VP, g_cameras[i], g_selectionType == 1 && i == g_selectedIdx);
      for (int i = 0; i < (int)g_lights.size(); ++i)
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
    int mode = ImGuiDrawToolbar((int)m_gizmo.Mode(), addCamera, addLight);
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

    // ImGuizmo on selected object
    ImGuizmoBeginFrame(0, 0, m_lastW, m_lastH, false);
    SceneObject* sel = SelectedObject();
    if (sel && sel->wireframe.IsLoaded()) {
      const ::Camera& cam2 = m_camera.GetCamera();
      XMATRIX44 worldMat = sel->wireframe.BuildWorld();

      // Track drag start for undo
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
        sel->wireframe.Position() = XVECTOR3(translation[0], translation[1], translation[2]);
        sel->wireframe.EulerRadians() = XVECTOR3(
          rotation[0] * kDegToRad, rotation[1] * kDegToRad, rotation[2] * kDegToRad);
        sel->wireframe.Scale() = XVECTOR3(scale[0], scale[1], scale[2]);
      }

      // On drag end, push one undo command for the entire drag
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

    // Menu actions
    if (menuAction.wantsExit) {
#ifdef OS_WINDOWS
      auto* w32fw = static_cast<t800::Win32Framework*>(pFramework);
      w32fw->m_alive = false;
#endif
    }
    if (menuAction.wantsImportX) {
      std::string path = OpenFileDialog(
        L"DirectX Mesh (*.x)\0*.x\0All Files (*.*)\0*.*\0",
        L"Import .x Mesh");
      if (!path.empty()) ImportMesh(path);
    }
    if (menuAction.wantsSaveScene) {
      std::string path = SaveFileDialog(
        L"T8ditor Scene (*.t8scene)\0*.t8scene\0JSON (*.json)\0*.json\0All Files (*.*)\0*.*\0",
        L"Save Scene", L"t8scene");
      if (!path.empty()) {
        SceneFile sf;
        sf.editor.camera_target   = { m_camera.GetTarget().x, m_camera.GetTarget().y, m_camera.GetTarget().z };
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
        SceneFile sf;
        if (LoadSceneFromFile(path, sf)) {
          // Clear current scene
          m_primMgr.DestroyPrimitives();
          g_objects.clear();
          g_cameras.clear();
          g_lights.clear();
          g_selectedIdx = -1;
          g_selectionType = 0;
          g_undoStack.Clear();
          m_primMgr.Init();
          m_primMgr.SetVP(&m_vp);
          m_primMgr.SetSceneProps(&m_sceneProps);

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
            }
          }

          // Load cameras
          for (auto& cd : sf.cameras) {
            SceneCamera c;
            c.name      = cd.name;
            c.type      = (CameraType)cd.type;
            c.position  = XVECTOR3(cd.position.x, cd.position.y, cd.position.z);
            c.target    = XVECTOR3(cd.target.x, cd.target.y, cd.target.z);
            c.fovDeg    = cd.fov_deg;
            c.orthoW    = cd.ortho_w;
            c.orthoH    = cd.ortho_h;
            c.nearPlane = cd.near_plane;
            c.farPlane  = cd.far_plane;
            g_cameras.push_back(c);
          }

          // Load lights
          for (auto& ld : sf.lights) {
            SceneLight l;
            l.name      = ld.name;
            l.type      = (EditorLightType)ld.type;
            l.position  = XVECTOR3(ld.position.x, ld.position.y, ld.position.z);
            l.direction = XVECTOR3(ld.direction.x, ld.direction.y, ld.direction.z);
            l.color     = XVECTOR3(ld.color.x, ld.color.y, ld.color.z);
            l.intensity = ld.intensity;
            l.radius    = ld.radius;
            l.enabled   = ld.enabled;
            g_lights.push_back(l);
          }

          // Restore editor state
          m_panels.showSkybox    = sf.editor.show_skybox;
          m_panels.showWireframe = sf.editor.show_wireframe;
          m_camera.SetTarget(XVECTOR3(sf.editor.camera_target.x,
                                       sf.editor.camera_target.y,
                                       sf.editor.camera_target.z));
          g_selectedIdx = -1;
        }
      }
    }

    // Panels
    if (m_panels.showHierarchy) {
      ImGui::SetNextWindowSize(ImVec2(250, 400), ImGuiCond_FirstUseEver);
      if (ImGui::Begin("Hierarchy")) {
        if (ImGui::TreeNodeEx("Scene Root", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen)) {
          // Meshes
          if (!g_objects.empty() && ImGui::TreeNodeEx("Meshes", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (int i = 0; i < (int)g_objects.size(); ++i) {
              ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
              if (g_selectionType == 0 && i == g_selectedIdx) flags |= ImGuiTreeNodeFlags_Selected;
              bool nodeOpen = ImGui::TreeNodeEx(g_objects[i].name.c_str(), flags);
              if (ImGui::IsItemClicked()) {
                if (g_selectionType == 0 && g_selectedIdx == i) { g_selectedIdx = -1; }
                else { g_selectedIdx = i; g_selectionType = 0; }
              }
              if (nodeOpen) ImGui::TreePop();
            }
            ImGui::TreePop();
          }
          // Cameras
          if (!g_cameras.empty() && ImGui::TreeNodeEx("Cameras", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (int i = 0; i < (int)g_cameras.size(); ++i) {
              ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
              if (g_selectionType == 1 && i == g_selectedIdx) flags |= ImGuiTreeNodeFlags_Selected;
              const char* icon = (g_cameras[i].type == CameraType::Perspective) ? "[P] " : "[O] ";
              std::string label = icon + g_cameras[i].name;
              bool nodeOpen = ImGui::TreeNodeEx(label.c_str(), flags);
              if (ImGui::IsItemClicked()) {
                if (g_selectionType == 1 && g_selectedIdx == i) { g_selectedIdx = -1; }
                else { g_selectedIdx = i; g_selectionType = 1; }
              }
              if (nodeOpen) ImGui::TreePop();
            }
            ImGui::TreePop();
          }
          // Lights
          if (!g_lights.empty() && ImGui::TreeNodeEx("Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (int i = 0; i < (int)g_lights.size(); ++i) {
              ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
              if (g_selectionType == 2 && i == g_selectedIdx) flags |= ImGuiTreeNodeFlags_Selected;
              const char* icon = (g_lights[i].type == EditorLightType::Directional) ? "[D] " : "[O] ";
              std::string label = icon + g_lights[i].name;
              bool nodeOpen = ImGui::TreeNodeEx(label.c_str(), flags);
              if (ImGui::IsItemClicked()) {
                if (g_selectionType == 2 && g_selectedIdx == i) { g_selectedIdx = -1; }
                else { g_selectedIdx = i; g_selectionType = 2; }
              }
              if (nodeOpen) ImGui::TreePop();
            }
            ImGui::TreePop();
          }
          ImGui::TreePop();
        }
      }
      ImGui::End();
    }

    // ── Inspector ──
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

    ImGuiRender();
  }

  drv->SwapBuffers();
  drv->EndFrame();
}

void EditorApp::OnPause()  { bPaused = true;  }
void EditorApp::OnResume() { bPaused = false; }
void EditorApp::OnReset()  {}
void EditorApp::LoadScene(int) {}

} // namespace t8ditor