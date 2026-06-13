/*********************************************************
 * T8ditor — hosted editor window/viewport state.
 *********************************************************/

#ifndef T8DITOR_HOSTED_VIEWPORT_PANEL_H
#define T8DITOR_HOSTED_VIEWPORT_PANEL_H

#include <imgui.h>

namespace t8ditor {

class HostedSceneWindowController {
public:
  bool open = false;
  bool loaded = false;
  bool openRequested = false;
  bool closeRequested = false;
  bool guiVisible = true;
  bool viewportInputActive = false;
  void* nativeHandle = nullptr;
  void* loggedNativeHandle = nullptr;
  bool mainViewportLogged = false;
  unsigned int imguiViewportId = 0;
  unsigned int dockspaceId = 0;
  unsigned int dockClassId = 0;
  float viewportPosX = 0.0f;
  float viewportPosY = 0.0f;
  float viewportSizeX = 0.0f;
  float viewportSizeY = 0.0f;
  float imageMinX = 0.0f;
  float imageMinY = 0.0f;
  float imageSizeX = 0.0f;
  float imageSizeY = 0.0f;

  void Open(bool guiVisibleOnOpen = true);
  void RequestClose();
  void Reset(bool guiVisibleDefault = true);
  void ResetNativeWindow();
  void ResetViewportRect();
  void CaptureNativeViewport(ImGuiViewport* viewport, const char* title);
};

} // namespace t8ditor

#endif // T8DITOR_HOSTED_VIEWPORT_PANEL_H
