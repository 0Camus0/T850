#include <pch.h>

#include <imgui/ImGuiRendererBackend.h>
#include <utils/Log.h>

namespace t850 {

std::unique_ptr<ImGuiRendererBackend> CreateImGuiRendererBackend(GraphicsApi::E api) {
  switch (api) {
  case GraphicsApi::D3D11: return CreateImGuiD3D11Backend();
  case GraphicsApi::D3D12: return CreateImGuiD3D12Backend();
  case GraphicsApi::OPENGL: return CreateImGuiOpenGLBackend();
  case GraphicsApi::VULKAN: return CreateImGuiVulkanBackend();
  default:
    T8_LOG_ERROR("[ImGuiSystem] Unsupported graphics API %d", static_cast<int>(api));
    return nullptr;
  }
}

} // namespace t850
