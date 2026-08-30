#pragma once

#include <core/Core.h>
#include <imgui.h>

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
};

std::unique_ptr<ImGuiRendererBackend> CreateImGuiRendererBackend(GraphicsApi::E api);

std::unique_ptr<ImGuiRendererBackend> CreateImGuiD3D11Backend();
std::unique_ptr<ImGuiRendererBackend> CreateImGuiD3D12Backend();
std::unique_ptr<ImGuiRendererBackend> CreateImGuiOpenGLBackend();
std::unique_ptr<ImGuiRendererBackend> CreateImGuiVulkanBackend();

} // namespace t850
