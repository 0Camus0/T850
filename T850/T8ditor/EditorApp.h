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

// EditorApp — minimal AppBase subclass that hosts the T8ditor.exe shell.
//
// This is intentionally a stub for Phase 1 of the editor roadmap (see
// EDITOR.md). It opens the same window the runtime uses, links Framework.lib,
// and runs through Win32Framework/LinuxFramework, but does not yet pull in
// ImGui or render a scene. Phases 1+ layer the editor UI on top.

#ifndef T8DITOR_EDITORAPP_H
#define T8DITOR_EDITORAPP_H

#include <core/Core.h>
#include <utils/Timer.h>

namespace t8ditor {

  class EditorApp : public t800::AppBase {
  public:
    EditorApp() : AppBase() {}

    // AppBase contract.
    void InitVars() override;
    void CreateAssets() override;
    void LoadAssets() override;
    void DestroyAssets() override;

    void OnUpdate() override;
    void OnDraw() override;
    void OnInput() override;

    void OnPause() override;
    void OnResume() override;
    void OnReset() override;

    void LoadScene(int id) override;

  private:
    Timer m_dtTimer;
    float m_dtSecs = 0.0f;
  };

} // namespace t8ditor

#endif // T8DITOR_EDITORAPP_H
