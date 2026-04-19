/*********************************************************
* T8ditor — EditorApp implementation. See header.
*********************************************************/

#include "EditorApp.h"

#include <core/Core.h>
#include <video/BaseDriver.h>
#include <utils/InputManager.h>
#include <utils/Log.h>
#include <utils/xMaths.h>

#include <T8_descriptors.h>

#include <cmath>
#include <filesystem>

#ifdef OS_WINDOWS
#include <core/windows/Win32Framework.h>
#endif

namespace t8ditor {

namespace {
  // Set by main.cpp before EditorApp is constructed.
  std::string g_startupMeshPath;
}

void SetStartupMeshPath(const std::string& p) {
  g_startupMeshPath = p;
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
  const int  w = (int)desc.width;
  const int  h = (int)desc.height;

  m_camera.Init(w, h, /*fovDeg=*/50.0f);
  m_camera.SetTarget(XVECTOR3(0.0f, 0.0f, 0.0f));
  m_camera.Frame();

  if (!m_lines.Create()) {
    T8_LOG_ERROR("[T8ditor] EditorLineRenderer::Create failed — grid/gizmo will be inert");
  }
  m_grid.Create(/*halfExtent=*/10, /*spacing=*/1.0f);
  m_gizmo.Create();

  // ── Set up lit rendering pipeline ──
  // Camera-attached directional light (like 3ds Max viewport headlamp)
  m_sceneProps.AddCamera(&m_camera.GetCameraMutable());
  m_sceneProps.AddDirectionalLight(
    XVECTOR3(0.0f, -1.0f, 0.0f),   // direction (updated per-frame to follow camera)
    XVECTOR3(1.0f, 1.0f, 1.0f),    // white
    1.5f,                           // intensity
    true                            // enabled
  );
  m_sceneProps.ActiveLights = 1;
  m_sceneProps.AmbientColor = XVECTOR3(0.15f, 0.15f, 0.15f);

  XMatIdentity(m_vp);
  m_primMgr.Init();
  m_primMgr.SetVP(&m_vp);
  m_primMgr.SetSceneProps(&m_sceneProps);

  // Forward pass key for direct drawing (no deferred, no shadows)
  // (created locally in OnDraw each frame)

  // Optional mesh load
  if (!g_startupMeshPath.empty()) {
    ImportMesh(g_startupMeshPath);
  } else {
    T8_LOG_INFO("[T8ditor] No --mesh supplied; opening empty scene");
  }

  m_assetsCreated = true;

  // Initialise ImGui after the driver is fully up.
  m_imguiReady = ImGuiInit(pFramework);
  if (!m_imguiReady)
    T8_LOG_ERROR("[T8ditor] ImGui init failed — editor panels will be unavailable");

  T8_LOG_INFO("[T8ditor] CreateAssets done (viewport %dx%d)", w, h);
}

void EditorApp::ImportMesh(const std::string& path) {
  // Check file exists before attempting to load
  if (!std::filesystem::exists(path)) {
    T8_LOG_ERROR("[T8ditor] Mesh file not found: %s", path.c_str());
    return;
  }

  // Destroy previous lit mesh instance (wireframe too)
  m_mesh.Destroy();
  m_meshPrimId = -1;

  // Load through the Framework's full pipeline (textures, materials, shaders)
  int id = m_primMgr.CreateMesh(path.c_str());
  if (id < 0) {
    T8_LOG_ERROR("[T8ditor] Failed to load mesh: %s", path.c_str());
    return;
  }
  m_meshPrimId = id;
  m_meshInst.CreateInstance(m_primMgr.GetPrimitive(id), &m_vp);
  m_meshInst.Update();

  // SetSceneProps must be called after CreateMesh — CreateMesh adds a new
  // primitive to the vector, and SetSceneProps only covers existing entries.
  m_primMgr.SetSceneProps(&m_sceneProps);

  // Also load wireframe for overlay toggle
  m_mesh.Load(path);

  // Frame the camera on the mesh
  m_camera.SetTarget(m_mesh.LocalCenter());
  m_camera.Frame();

  T8_LOG_INFO("[T8ditor] Loaded lit mesh: %s", path.c_str());
}

void EditorApp::LoadAssets() {
  // AppBase declares this pure-virtual but the framework loop never calls it.
}

void EditorApp::DestroyAssets() {
  if (m_imguiReady) {
    ImGuiShutdown();
    m_imguiReady = false;
  }
  m_primMgr.DestroyPrimitives();
  m_meshPrimId = -1;
  m_mesh.Destroy();
  m_gizmo.Destroy();
  m_grid.Destroy();
  m_lines.Destroy();
  m_assetsCreated = false;
  T8_LOG_INFO("[T8ditor] DestroyAssets");
}

void EditorApp::OnUpdate() {
  m_dtTimer.Update();
  m_dtSecs = m_dtTimer.GetDTSecs();
  if (m_firstFrame) {
    m_dtSecs = 1.0f / 60.0f;
    m_firstFrame = false;
  }
  m_sceneProps.FrameDeltaSec = m_dtSecs;

  OnInput();
  OnDraw();
}

void EditorApp::OnInput() {
  // Mode toggles (W/E/R) — single-press semantics so holding doesn't cycle.
  if (IManager.PressedOnceKey(T800K_w)) m_gizmo.SetMode(GizmoMode::Translate);
  if (IManager.PressedOnceKey(T800K_e)) m_gizmo.SetMode(GizmoMode::Rotate);
  if (IManager.PressedOnceKey(T800K_r)) m_gizmo.SetMode(GizmoMode::Scale);

  // Z — reset camera to default position
  if (IManager.PressedOnceKey(T800K_z))
    m_camera.ResetToDefault();

  // Consume wheel delta accumulated by the SDL event watcher
  float wheel = ImGuiConsumeWheelDelta();
  m_camera.Update(m_dtSecs, IManager, wheel);
  ProcessSelectionInput();
}

void EditorApp::ProcessSelectionInput() {
  if (!m_mesh.IsLoaded()) return;

  // Step rate. Linear motion is in world units/sec; rotation in rad/sec;
  // scale step is multiplicative per second. Tied directly to dt rather
  // than camera distance because the user manipulates with the keyboard
  // and an absolute rate is more predictable than a distance-relative one.
  const float linRate = 5.0f * m_dtSecs;
  const float rotRate = 1.5f * m_dtSecs;
  const float sclStep = 1.0f + 0.5f * m_dtSecs; // multiplicative

  XVECTOR3& pos = m_mesh.Position();
  XVECTOR3& eul = m_mesh.EulerRadians();
  XVECTOR3& scl = m_mesh.Scale();

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

void EditorApp::OnDraw() {
  if (!pFramework || !pFramework->pVideoDriver) return;

  t800::BaseDriver* drv = pFramework->pVideoDriver;
  drv->BeginFrame();
  drv->Clear();

  if (m_assetsCreated) {
    const ::Camera& cam = m_camera.GetCamera();
    m_vp = cam.VP;

    // Update headlamp: directional light points from camera eye toward target
    if (!m_sceneProps.Lights.empty()) {
      XVECTOR3 look = cam.Look;
      XVECTOR3 eye  = cam.Eye;
      XVECTOR3 dir(look.x - eye.x, look.y - eye.y, look.z - eye.z);
      float len = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
      if (len > 0.0001f) { dir.x /= len; dir.y /= len; dir.z /= len; }
      m_sceneProps.Lights[0].Direction = dir;
      m_sceneProps.Lights[0].Position  = eye;
    }

    // ── Lit/textured mesh ──
    if (m_meshPrimId >= 0) {
      // Apply selection transform to the instance
      const XVECTOR3& pos = m_mesh.Position();
      const XVECTOR3& eul = m_mesh.EulerRadians();
      const XVECTOR3& scl = m_mesh.Scale();
      m_meshInst.TranslateAbsolute(pos.x, pos.y, pos.z);
      m_meshInst.RotateXAbsolute(eul.x);
      m_meshInst.RotateYAbsolute(eul.y);
      m_meshInst.RotateZAbsolute(eul.z);
      m_meshInst.ScaleAbsolute(scl.x, scl.y, scl.z);
      t800::ShaderKey fwdKey(0);
      fwdKey.setPass(t800::PassType::FORWARD);
      m_meshInst.SetGlobalKey(fwdKey);
      m_meshInst.Update();
      m_meshInst.Draw();
    }

    // ── Editor overlays (grid, gizmo) ──
    if (m_lines.IsReady()) {
      const XMATRIX44 vp = cam.VP;
      m_grid.Draw(m_lines, vp);

      if (m_mesh.IsLoaded()) {
        XMATRIX44 selWorld;
        const XVECTOR3& p = m_mesh.Position();
        XMatTranslation(selWorld, p.x, p.y, p.z);
        m_gizmo.Draw(m_lines, vp, selWorld);
      } else {
        XMATRIX44 origin;
        XMatIdentity(origin);
        m_gizmo.Draw(m_lines, vp, origin);
      }
    }
  }

  // ── ImGui overlay ──
  if (m_imguiReady) {
    ImGuiNewFrame();

    MenuAction menuAction = ImGuiDrawMenuBar();

    // Handle menu actions
    if (menuAction.wantsExit) {
      auto* w32fw = static_cast<t800::Win32Framework*>(pFramework);
      w32fw->m_alive = false;
    }
    if (menuAction.wantsImportX) {
      std::string path = OpenFileDialog(
        L"DirectX Mesh (*.x)\0*.x\0All Files (*.*)\0*.*\0",
        L"Import .x Mesh");
      if (!path.empty()) {
        ImportMesh(path);
      }
    }

    ImGuiRender();
  }

  drv->SwapBuffers();
  drv->EndFrame();
}

void EditorApp::OnPause()  { bPaused = true;  }
void EditorApp::OnResume() { bPaused = false; }
void EditorApp::OnReset()  {}

void EditorApp::LoadScene(int /*id*/) {
  // Editor opens scenes through asset-browser UI (not by index). No-op for now.
}

} // namespace t8ditor
