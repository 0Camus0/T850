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

  // Optional mesh load. We log but do not fatal on failure — empty editor
  // is still useful (grid + gizmo).
  if (!g_startupMeshPath.empty()) {
    if (m_mesh.Load(g_startupMeshPath)) {
      // Center the gizmo on the mesh.
      m_camera.SetTarget(m_mesh.LocalCenter());
      m_camera.Frame();
    }
  } else {
    T8_LOG_INFO("[T8ditor] No --mesh supplied; opening empty scene");
  }

  m_assetsCreated = true;
  T8_LOG_INFO("[T8ditor] CreateAssets done (viewport %dx%d)", w, h);
}

void EditorApp::LoadAssets() {
  // AppBase declares this pure-virtual but the framework loop never calls it.
}

void EditorApp::DestroyAssets() {
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

  OnInput();
  OnDraw();
}

void EditorApp::OnInput() {
  // Mode toggles (W/E/R) — single-press semantics so holding doesn't cycle.
  if (IManager.PressedOnceKey(T800K_w)) m_gizmo.SetMode(GizmoMode::Translate);
  if (IManager.PressedOnceKey(T800K_e)) m_gizmo.SetMode(GizmoMode::Rotate);
  if (IManager.PressedOnceKey(T800K_r)) m_gizmo.SetMode(GizmoMode::Scale);

  m_camera.Update(m_dtSecs, IManager);
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

  if (m_assetsCreated && m_lines.IsReady()) {
    const XMATRIX44 vp = m_camera.GetCamera().VP;

    // Order: grid (background) -> mesh wireframe -> gizmo (always on top).
    m_grid.Draw(m_lines, vp);
    m_mesh.Draw(m_lines, vp);

    if (m_mesh.IsLoaded()) {
      // Gizmo at the mesh's transform origin (post-translation only —
      // we don't want it to inherit the mesh's scale or rotation visually).
      XMATRIX44 selWorld;
      const XVECTOR3& p = m_mesh.Position();
      XMatTranslation(selWorld, p.x, p.y, p.z);
      m_gizmo.Draw(m_lines, vp, selWorld);
    } else {
      // No selection: draw the gizmo at the world origin.
      XMATRIX44 origin;
      XMatIdentity(origin);
      m_gizmo.Draw(m_lines, vp, origin);
    }
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
