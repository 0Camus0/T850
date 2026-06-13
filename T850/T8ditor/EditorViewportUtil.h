/*********************************************************
 * T8ditor — shared hosted-viewport helpers.
 *********************************************************/

#ifndef T8DITOR_EDITOR_VIEWPORT_UTIL_H
#define T8DITOR_EDITOR_VIEWPORT_UTIL_H

#include <imgui.h>
#include <scene/RenderViewport.h>
#include <utils/xMaths.h>

class Camera;

namespace t850 {
  class BaseDriver;
  class BaseRT;
  class Texture;
}

namespace t8ditor {

struct EditorViewportSize {
  int width = 0;
  int height = 0;
  ImVec2 imSize = ImVec2(0.0f, 0.0f);
};

EditorViewportSize EditorViewportDesiredSize(const ImVec2& available,
                                             int minWidth = 64,
                                             int minHeight = 64);
bool EditorViewportResizeHeld();
bool EditorViewportShouldResize(t850::RenderViewport& viewport,
                                int desiredWidth,
                                int desiredHeight,
                                const t850::RenderViewportDesc& desc = t850::RenderViewportDesc{});

t850::BaseRT* EditorRenderTarget(t850::BaseDriver* driver, int handle);
bool EditorRenderTargetReady(const t850::BaseRT* rt,
                             int minColorTextures,
                             bool requireDepthTexture);
t850::Texture* EditorRenderTargetColor(t850::BaseRT* rt, int colorIndex = 0);

bool DrawEditorViewportTexture(t850::BaseDriver* driver,
                               t850::Texture* texture,
                               const ImVec2& imageMin,
                               const ImVec2& imageSize,
                               const char* inputId,
                               const char* unavailableText,
                               bool* outInputActive = nullptr);

void ConfigureEditorOrbitCamera(::Camera& camera,
                                const XVECTOR3& target,
                                float yaw,
                                float& pitch,
                                float distance,
                                float fovRadians,
                                float aspect,
                                float nearPlane,
                                float farPlane,
                                float minPitch,
                                float maxPitch);

} // namespace t8ditor

#endif // T8DITOR_EDITOR_VIEWPORT_UTIL_H
