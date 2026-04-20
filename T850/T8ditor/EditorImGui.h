/*********************************************************
* T8ditor — ImGui integration layer.
*
* Initialises/shuts down Dear ImGui with the correct
* per-API renderer backend (D3D11, D3D12, OpenGL ES) and
* the SDL3 platform backend.  Provides the per-frame
* NewFrame / RenderDrawData calls that EditorApp hooks
* into its draw loop.
*
* Also owns the top-level menu bar, tool panels (hierarchy,
* inspector, console), and modal dialogs.
*********************************************************/

#ifndef T8DITOR_EDITORIMGUI_H
#define T8DITOR_EDITORIMGUI_H

#include <string>
#include <utils/xMaths.h>

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

  // Persistent panel visibility — pass references from EditorApp.
  struct PanelVisibility {
    bool showHierarchy  = true;
    bool showInspector  = true;
    bool showConsole    = true;
    bool showWireframe  = false;
    bool showSkybox     = true;
  };

  // Draw the main menu bar. Returns actions triggered this frame.
  // `panels` is read/written for the View menu checkboxes.
  MenuAction ImGuiDrawMenuBar(PanelVisibility& panels);

  // ── Toolbar ─────────────────────────────────────────
  // Draws a horizontal button bar just below the menu bar.
  // `currentMode` is the active gizmo mode; returns the (possibly new) mode.
  // `addCamera`/`addLight` are set to the type to add (0=persp/dir, 1=ortho/omni, -1=none).
  int ImGuiDrawToolbar(int currentMode, int& addCamera, int& addLight);

  // ── Panels ─────────────────────────────────────────
  // Hierarchy panel: lists scene objects. Returns true if user clicked
  // a mesh entry (sets `selected` accordingly).
  bool ImGuiDrawHierarchyPanel(const char* meshName, bool hasMesh, bool& selected);

  // Inspector panel: T/R/S sliders for the selection.
  // `eulerDeg` is in degrees for display; caller converts to/from radians.
  void ImGuiDrawInspectorPanel(XVECTOR3& pos, XVECTOR3& eulerDeg,
                               XVECTOR3& scale, bool hasMesh);

  // Console panel: scrollable log viewer.
  void ImGuiDrawConsolePanel();

  // ── Log capture ────────────────────────────────────
  void ImGuiLogCaptureStart();
  void ImGuiLogCaptureStop();

  // ── ImGuizmo interactive gizmo ───────────────────────
  // Call once per frame before Manipulate.
  void ImGuizmoBeginFrame(int vpX, int vpY, int vpW, int vpH, bool ortho);

  // Draw an interactive gizmo for the given object matrix.
  // `view` and `proj` are the camera's View and Projection matrices (float[16]).
  // `worldMatrix` is the object's world matrix (float[16]) — modified in-place.
  // `operation`: 0=Translate, 1=Rotate, 2=Scale (matches GizmoMode enum).
  // Returns true if the user manipulated the gizmo this frame.
  bool ImGuizmoManipulate(const float* view, const float* proj,
                          int operation, float* worldMatrix);

  // ── Mouse wheel (captured via SDL event watcher) ────
  // Returns accumulated wheel delta since last call, then resets.
  float ImGuiConsumeWheelDelta();

  // ── File dialogs ───────────────────────────────────
  // Opens a native Windows file dialog. Returns empty string on cancel.
  std::string OpenFileDialog(const wchar_t* filter, const wchar_t* title);

} // namespace t8ditor

#endif // T8DITOR_EDITORIMGUI_H
