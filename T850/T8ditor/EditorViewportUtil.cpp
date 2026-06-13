/*********************************************************
 * T8ditor — shared hosted-viewport helpers.
 *********************************************************/

#include "EditorViewportUtil.h"
#include "EditorImGui.h"

#include <utils/Camera.h>
#include <video/BaseDriver.h>

#include <algorithm>
#include <cmath>

namespace t8ditor {

EditorViewportSize EditorViewportDesiredSize(const ImVec2& available,
                                             int minWidth,
                                             int minHeight) {
  EditorViewportSize size;
  size.width = (std::max)(minWidth, static_cast<int>(std::floor(available.x)));
  size.height = (std::max)(minHeight, static_cast<int>(std::floor(available.y)));
  size.imSize = ImVec2(static_cast<float>(size.width), static_cast<float>(size.height));
  return size;
}

bool EditorViewportResizeHeld() {
  return ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
         ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
         ImGui::IsMouseDown(ImGuiMouseButton_Middle);
}

bool EditorViewportShouldResize(t850::RenderViewport& viewport,
                                int desiredWidth,
                                int desiredHeight,
                                const t850::RenderViewportDesc& desc) {
  return viewport.ShouldResize(desiredWidth, desiredHeight, EditorViewportResizeHeld(), desc);
}

t850::BaseRT* EditorRenderTarget(t850::BaseDriver* driver, int handle) {
  if (!driver || handle < 0 || handle >= static_cast<int>(driver->RTs.size())) {
    return nullptr;
  }
  return driver->RTs[handle];
}

bool EditorRenderTargetReady(const t850::BaseRT* rt,
                             int minColorTextures,
                             bool requireDepthTexture) {
  if (!rt || static_cast<int>(rt->vColorTextures.size()) < minColorTextures) {
    return false;
  }
  for (int colorIndex = 0; colorIndex < minColorTextures; ++colorIndex) {
    if (!rt->vColorTextures[static_cast<std::size_t>(colorIndex)]) {
      return false;
    }
  }
  return !requireDepthTexture || rt->pDepthTexture != nullptr;
}

t850::Texture* EditorRenderTargetColor(t850::BaseRT* rt, int colorIndex) {
  if (!rt || colorIndex < 0 || colorIndex >= static_cast<int>(rt->vColorTextures.size())) {
    return nullptr;
  }
  return rt->vColorTextures[static_cast<std::size_t>(colorIndex)];
}

bool DrawEditorViewportTexture(t850::BaseDriver* driver,
                               t850::Texture* texture,
                               const ImVec2& imageMin,
                               const ImVec2& imageSize,
                               const char* inputId,
                               const char* unavailableText,
                               bool* outInputActive) {
  ImTextureID image = ImGuiTextureID(driver, texture);
  if (!image) {
    ImGui::TextDisabled("%s", unavailableText ? unavailableText : "Viewport texture is not available for ImGui.");
    if (outInputActive) {
      *outInputActive = false;
    }
    return false;
  }

  const ImVec2 imageMax(imageMin.x + imageSize.x, imageMin.y + imageSize.y);
  const bool flipV = driver && driver->NeedsVFlip();
  const ImVec2 uv0(0.0f, flipV ? 1.0f : 0.0f);
  const ImVec2 uv1(1.0f, flipV ? 0.0f : 1.0f);
  ImGui::GetWindowDrawList()->AddImage(image, imageMin, imageMax, uv0, uv1);
  if (inputId && inputId[0] != '\0') {
    ImGui::InvisibleButton(
        inputId,
        imageSize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    if (outInputActive) {
      *outInputActive = ImGui::IsItemHovered() || ImGui::IsItemActive();
    }
  } else if (outInputActive) {
    *outInputActive = false;
  }
  return true;
}

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
                                float maxPitch) {
  pitch = std::clamp(pitch, minPitch, maxPitch);
  const float cp = std::cos(pitch);
  const XVECTOR3 eye(
      target.x + std::sin(yaw) * cp * distance,
      target.y + std::sin(pitch) * distance,
      target.z + std::cos(yaw) * cp * distance,
      1.0f);
  camera.InitPerspective(eye, fovRadians, aspect, nearPlane, farPlane);
  camera.Eye = eye;
  camera.SetLookAt(target);
  camera.Update(0.0f);
}

} // namespace t8ditor
