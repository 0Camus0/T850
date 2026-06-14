/*********************************************************
 * T8ditor — hosted editor window/viewport state.
 *********************************************************/

#include "HostedViewportPanel.h"
#include "EditorInternal.h"
#include "EditorViewportUtil.h"

#include <utils/Log.h>

#include <cmath>

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

bool HostedRenderViewport::Ensure(t850::BaseDriver* driver,
                                  int width,
                                  int height,
                                  const t850::RenderViewportDesc& desc) {
  return m_target.Ensure(driver, width, height, desc);
}

bool HostedRenderViewport::ShouldResize(int desiredWidth,
                                        int desiredHeight,
                                        const t850::RenderViewportDesc& desc) {
  return EditorViewportShouldResize(m_target, desiredWidth, desiredHeight, desc);
}

void HostedRenderViewport::Destroy(t850::BaseDriver* driver) {
  m_target.Destroy(driver);
  ResetImageRect();
  m_inputActive = false;
}

void HostedRenderViewport::ResetImageRect() {
  m_imageMinX = 0.0f;
  m_imageMinY = 0.0f;
  m_imageSizeX = 0.0f;
  m_imageSizeY = 0.0f;
}

void HostedRenderViewport::SetImageRect(const ImVec2& imageMin, const ImVec2& imageSize) {
  m_imageMinX = imageMin.x;
  m_imageMinY = imageMin.y;
  m_imageSizeX = imageSize.x;
  m_imageSizeY = imageSize.y;
}

bool HostedRenderViewport::Contains(float screenX, float screenY) const {
  return HasImageRect() &&
         screenX >= m_imageMinX &&
         screenY >= m_imageMinY &&
         screenX < m_imageMinX + m_imageSizeX &&
         screenY < m_imageMinY + m_imageSizeY;
}

int HostedRenderViewport::LocalX(float screenX) const {
  return static_cast<int>(std::lround(screenX - m_imageMinX));
}

int HostedRenderViewport::LocalY(float screenY) const {
  return static_cast<int>(std::lround(screenY - m_imageMinY));
}

bool HostedRenderViewport::DrawTexture(t850::BaseDriver* driver,
                                       t850::Texture* texture,
                                       const ImVec2& imageMin,
                                       const ImVec2& imageSize,
                                       const char* inputId,
                                       const char* unavailableText) {
  SetImageRect(imageMin, imageSize);
  return DrawEditorViewportTexture(
      driver,
      texture,
      imageMin,
      imageSize,
      inputId,
      unavailableText,
      &m_inputActive);
}

} // namespace t8ditor
