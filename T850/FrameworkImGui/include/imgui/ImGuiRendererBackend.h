#pragma once

#include <core/Core.h>
#include <imgui.h>
#include <utils/Log.h>

#include <memory>
#include <unordered_set>

struct ImDrawData;

namespace t850 {

class RootFramework;
class Texture;

enum class ImGuiTextureMode {
  Native,
  OpaquePreview
};

class ImGuiRendererBackend {
public:
  virtual ~ImGuiRendererBackend() = default;

  virtual bool Init(RootFramework* framework, void* nativeWindow) = 0;
  virtual void Shutdown() = 0;
  virtual void NewFrame() = 0;
  virtual void RenderDrawData(ImDrawData* drawData) = 0;
  virtual bool ShouldDeferPlatformWindowsUpdate() const { return false; }
  virtual bool SetNativeWindow(void* nativeWindow) { return nativeWindow != nullptr; }
  virtual bool HandlePlatformInput(void* event) { (void)event; return false; }
  virtual ImTextureID GetTextureID(Texture* texture, ImGuiTextureMode mode) = 0;
  virtual void PruneTextureIDs(const std::unordered_set<Texture*>& liveTextures) {
    (void)liveTextures;
  }
  virtual void ReleaseTextureIDs() {}
  virtual bool RequiresOpaquePreviewBlend() const { return false; }
protected:
  static bool ValidateOverlayLayout(const BaseDriver& driver, const RenderTargetLayout& layout) {
    if (layout.colorCount == 1 && layout.colorFormats[0] != BaseRT::NOTHING && layout.sampleCount == 1)
      return true;
    T8_LOG_ERROR("[ImGui] Unsupported overlay target on %s: colors=%u samples=%u",
                 driver.ApiTag(), layout.colorCount, layout.sampleCount);
    return false;
  }
  static bool ValidateOverlayDraw(const BaseDriver& driver, const RenderTargetLayout& prepared) {
    if (driver.GetRenderTargetLayout() == prepared) return true;
    T8_LOG_ERROR("[ImGui] Overlay target changed after NewFrame on %s", driver.ApiTag());
    return false;
  }
};

std::unique_ptr<ImGuiRendererBackend> CreateImGuiRendererBackend(GraphicsApi::E api);

std::unique_ptr<ImGuiRendererBackend> CreateImGuiD3D11Backend();
std::unique_ptr<ImGuiRendererBackend> CreateImGuiD3D12Backend();
std::unique_ptr<ImGuiRendererBackend> CreateImGuiOpenGLBackend();
std::unique_ptr<ImGuiRendererBackend> CreateImGuiVulkanBackend();
#if (defined(_WIN32) && defined(_M_X64)) || defined(__EMSCRIPTEN__)
std::unique_ptr<ImGuiRendererBackend> CreateImGuiWebGPUBackend();
#endif

} // namespace t850
