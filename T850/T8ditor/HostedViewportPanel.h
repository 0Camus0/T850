/*********************************************************
 * T8ditor — hosted editor window/viewport state.
 *********************************************************/

#ifndef T8DITOR_HOSTED_VIEWPORT_PANEL_H
#define T8DITOR_HOSTED_VIEWPORT_PANEL_H

#include <imgui.h>
#include <scene/RenderViewport.h>

namespace t850 { class Texture; }

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

class HostedRenderViewport {
public:
  bool Ensure(t850::BaseDriver* driver,
              int width,
              int height,
              const t850::RenderViewportDesc& desc = t850::RenderViewportDesc{});
  bool ShouldResize(int desiredWidth,
                    int desiredHeight,
                    const t850::RenderViewportDesc& desc = t850::RenderViewportDesc{});
  void Destroy(t850::BaseDriver* driver);

  int Handle() const { return m_target.Handle(); }
  int Width() const { return m_target.Width(); }
  int Height() const { return m_target.Height(); }
  bool IsValid() const { return m_target.IsValid(); }
  t850::RenderViewport& Target() { return m_target; }
  const t850::RenderViewport& Target() const { return m_target; }

  void ResetImageRect();
  void SetImageRect(const ImVec2& imageMin, const ImVec2& imageSize);
  ImVec2 ImageMin() const { return ImVec2(m_imageMinX, m_imageMinY); }
  ImVec2 ImageSize() const { return ImVec2(m_imageSizeX, m_imageSizeY); }
  float ImageMinX() const { return m_imageMinX; }
  float ImageMinY() const { return m_imageMinY; }
  float ImageSizeX() const { return m_imageSizeX; }
  float ImageSizeY() const { return m_imageSizeY; }
  bool HasImageRect() const { return m_imageSizeX > 0.0f && m_imageSizeY > 0.0f; }
  bool Contains(float screenX, float screenY) const;
  int LocalX(float screenX) const;
  int LocalY(float screenY) const;

  bool InputActive() const { return m_inputActive; }
  void SetInputActive(bool active) { m_inputActive = active; }
  bool DrawTexture(t850::BaseDriver* driver,
                   t850::Texture* texture,
                   const ImVec2& imageMin,
                   const ImVec2& imageSize,
                   const char* inputId,
                   const char* unavailableText);

private:
  t850::RenderViewport m_target;
  float m_imageMinX = 0.0f;
  float m_imageMinY = 0.0f;
  float m_imageSizeX = 0.0f;
  float m_imageSizeY = 0.0f;
  bool m_inputActive = false;
};

} // namespace t8ditor

#endif // T8DITOR_HOSTED_VIEWPORT_PANEL_H
