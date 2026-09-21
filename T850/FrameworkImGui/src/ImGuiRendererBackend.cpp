#include <pch.h>

#include <imgui/ImGuiRendererBackend.h>
#include <utils/Log.h>

namespace t850 {

std::unique_ptr<ImGuiRendererBackend> CreateImGuiRendererBackend(GraphicsApi::E api) {
  switch (api) {
#ifndef OS_WEB
  case GraphicsApi::D3D11: return CreateImGuiD3D11Backend();
  case GraphicsApi::D3D12: return CreateImGuiD3D12Backend();
  case GraphicsApi::OPENGL: return CreateImGuiOpenGLBackend();
  case GraphicsApi::VULKAN: return CreateImGuiVulkanBackend();
#endif
#if (defined(_WIN32) && (defined(_M_X64) || defined(_M_ARM64))) || defined(__EMSCRIPTEN__)
  case GraphicsApi::WEBGPU: return CreateImGuiWebGPUBackend();
#endif
  default:
    T8_LOG_ERROR("[ImGuiSystem] Unsupported graphics API %d", static_cast<int>(api));
    return nullptr;
  }
}

} // namespace t850
