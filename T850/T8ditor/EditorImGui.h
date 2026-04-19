/*********************************************************
* T8ditor — ImGui integration layer.
*
* Initialises/shuts down Dear ImGui with the correct
* per-API renderer backend (D3D11, D3D12, OpenGL ES) and
* the SDL3 platform backend.  Provides the per-frame
* NewFrame / RenderDrawData calls that EditorApp hooks
* into its draw loop.
*
* Also owns the top-level menu bar and any modal dialogs
* (file-open, file-save, import).
*********************************************************/

#ifndef T8DITOR_EDITORIMGUI_H
#define T8DITOR_EDITORIMGUI_H

#include <string>

struct SDL_Window;

namespace t800 {
  class RootFramework;
  class BaseDriver;
}

namespace t8ditor {

  // ── Lifecycle (called from EditorApp) ──────────────
  bool ImGuiInit(t800::RootFramework* fw);
  void ImGuiShutdown();
  void ImGuiNewFrame();
  void ImGuiRender();          // calls ImGui::Render() + backend RenderDrawData

  // ── Menu bar ───────────────────────────────────────
  struct MenuAction {
    bool wantsImportX  = false;   // File > Import (.x)
    bool wantsLoadScene = false;  // File > Load Scene
    bool wantsSaveScene = false;  // File > Save Scene
    bool wantsExit     = false;   // File > Exit
  };

  // Draw the main menu bar. Returns actions triggered this frame.
  MenuAction ImGuiDrawMenuBar();

  // ── Mouse wheel (captured via SDL event watcher) ────
  // Returns accumulated wheel delta since last call, then resets.
  float ImGuiConsumeWheelDelta();

  // ── File dialogs ───────────────────────────────────
  // Opens a native Windows file dialog. Returns empty string on cancel.
  std::string OpenFileDialog(const wchar_t* filter, const wchar_t* title);

} // namespace t8ditor

#endif // T8DITOR_EDITORIMGUI_H
