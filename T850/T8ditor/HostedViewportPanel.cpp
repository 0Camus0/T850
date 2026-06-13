/*********************************************************
 * T8ditor — hosted editor window/viewport state.
 *********************************************************/

#include "HostedViewportPanel.h"
#include "EditorInternal.h"

#include <utils/Log.h>

namespace t8ditor {

void HostedSceneWindowController::Open(bool guiVisibleOnOpen) {
  open = true;
  loaded = false;
  openRequested = true;
  closeRequested = false;
  guiVisible = guiVisibleOnOpen;
  viewportInputActive = false;
  ResetNativeWindow();
  ResetViewportRect();
}

void HostedSceneWindowController::RequestClose() {
  open = false;
  closeRequested = true;
  viewportInputActive = false;
}

void HostedSceneWindowController::Reset(bool guiVisibleDefault) {
  open = false;
  loaded = false;
  openRequested = false;
  closeRequested = false;
  guiVisible = guiVisibleDefault;
  viewportInputActive = false;
  ResetNativeWindow();
  ResetViewportRect();
}

void HostedSceneWindowController::ResetNativeWindow() {
  nativeHandle = nullptr;
  loggedNativeHandle = nullptr;
  mainViewportLogged = false;
  imguiViewportId = 0;
  dockspaceId = 0;
  dockClassId = 0;
}

void HostedSceneWindowController::ResetViewportRect() {
  viewportPosX = 0.0f;
  viewportPosY = 0.0f;
  viewportSizeX = 0.0f;
  viewportSizeY = 0.0f;
  imageMinX = 0.0f;
  imageMinY = 0.0f;
  imageSizeX = 0.0f;
  imageSizeY = 0.0f;
}

void HostedSceneWindowController::CaptureNativeViewport(ImGuiViewport* viewport, const char* title) {
  if (!viewport) {
    return;
  }

  ApplyNativeWindowChrome(viewport, title);
  nativeHandle = NativeHandleFromImGuiViewport(viewport);
  imguiViewportId = static_cast<unsigned int>(viewport->ID);
  viewportPosX = viewport->Pos.x;
  viewportPosY = viewport->Pos.y;
  viewportSizeX = viewport->Size.x;
  viewportSizeY = viewport->Size.y;

  ImGuiViewport* mainViewport = ImGui::GetMainViewport();
  const char* safeTitle = title ? title : "T8ditor";
  if (mainViewport && viewport->ID == mainViewport->ID) {
    if (!mainViewportLogged) {
      ImGuiIO& io = ImGui::GetIO();
      T8_LOG_INFO("[T8ditor] Native editor window pending title='%s': still merged with main viewport id=0x%08X hwnd=%p configFlags=0x%08X backendFlags=0x%08X",
                  safeTitle,
                  static_cast<unsigned int>(viewport->ID),
                  nativeHandle,
                  static_cast<unsigned int>(io.ConfigFlags),
                  static_cast<unsigned int>(io.BackendFlags));
      mainViewportLogged = true;
    }
  } else if (nativeHandle && nativeHandle != loggedNativeHandle) {
    T8_LOG_INFO("[T8ditor] Native editor window created title='%s' viewportId=0x%08X sdlWindow=%p hwnd=%p pos=(%.1f, %.1f) size=(%.1f, %.1f) flags=0x%08X",
                safeTitle,
                static_cast<unsigned int>(viewport->ID),
                viewport->PlatformHandle,
                nativeHandle,
                viewport->Pos.x,
                viewport->Pos.y,
                viewport->Size.x,
                viewport->Size.y,
                static_cast<unsigned int>(viewport->Flags));
    loggedNativeHandle = nativeHandle;
    mainViewportLogged = false;
  }
}

} // namespace t8ditor
