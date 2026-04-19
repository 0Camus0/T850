/*********************************************************
* Copyright (C) 2017 Daniel Enriquez (camus_mm@hotmail.com)
* All Rights Reserved
*
* You may use, distribute and modify this code under the
* following terms:
* ** Do not claim that you wrote this software
* ** A mention would be appreciated but not needed
* ** I do not and will not provide support, this software is "as is"
* ** Enjoy, learn and share.
*********************************************************/

#include "EditorApp.h"

#include <core/Core.h>
#include <video/BaseDriver.h>
#include <utils/Log.h>

namespace t8ditor {

  void EditorApp::InitVars() {
    m_dtTimer.Init();
    m_dtTimer.Update();
    m_dtSecs = 0.0f;
    T8_LOG_INFO("T8ditor: EditorApp::InitVars (Phase 1 stub)");
  }

  void EditorApp::CreateAssets() {
    // No assets in the stub; the editor UI lands in a follow-up.
    T8_LOG_INFO("T8ditor: EditorApp::CreateAssets (no-op stub)");
  }

  void EditorApp::LoadAssets() {
    // Pure-virtual on AppBase but never invoked by the framework loop.
    // Provided as a no-op to satisfy the interface.
  }

  void EditorApp::DestroyAssets() {
    T8_LOG_INFO("T8ditor: EditorApp::DestroyAssets (no-op stub)");
  }

  void EditorApp::OnUpdate() {
    m_dtTimer.Update();
    m_dtSecs = m_dtTimer.GetDTSecs();

    OnInput();
    OnDraw();
  }

  void EditorApp::OnDraw() {
    // Minimal frame: clear + present so the window is visible. The viewport
    // panel + scene draw arrive with the ImGui integration in Phase 1+.
    if (!pFramework || !pFramework->pVideoDriver)
      return;

    t800::BaseDriver* drv = pFramework->pVideoDriver;
    drv->BeginFrame();
    drv->Clear();
    drv->SwapBuffers();
    drv->EndFrame();
  }

  void EditorApp::OnInput() {
    // Editor input handling is wired up alongside the ImGui backend.
  }

  void EditorApp::OnPause()  { bPaused = true;  }
  void EditorApp::OnResume() { bPaused = false; }
  void EditorApp::OnReset()  {}

  void EditorApp::LoadScene(int /*id*/) {
    // The editor opens scenes through asset-browser UI, not by index.
    // Left empty for the stub.
  }

} // namespace t8ditor
