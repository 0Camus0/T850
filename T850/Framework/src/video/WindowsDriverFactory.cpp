#include <pch.h>
#include <video/WindowsDriverFactory.h>

#ifdef OS_WINDOWS
#include <video/d3d11/D3D11Driver.h>
#include <video/d3d12/D3D12Driver.h>
#include <video/gl/GLDriver.h>
#include <video/vulkan/VulkanDriver.h>
#include <video/webgpu/WebGPUDriver.h>
#include <utils/Log.h>
#include <stdexcept>

namespace t850 {
BaseDriver* CreateWindowsGraphicsDriver(GraphicsApi::E api, const std::string& shaderFlow) {
  switch (api) {
  case GraphicsApi::D3D11: return new D3DXDriver;
  case GraphicsApi::D3D12: return new D3D12Driver;
  case GraphicsApi::OPENGL: return new GLDriver;
  case GraphicsApi::VULKAN: return new VulkanDriver;
  case GraphicsApi::WEBGPU:
#if defined(_M_X64)
    {
      webgpu::ShaderFlow selectedFlow;
      if (!webgpu::ParseShaderFlow(shaderFlow, selectedFlow))
        throw std::invalid_argument("Invalid WebGPU startup shader flow");
      auto driver = std::make_unique<WebGPUDriver>();
      driver->SetShaderFlow(selectedFlow);
      T8_LOG_INFO("[WebGPU] startup shaderFlow=%s (before asset loading)", shaderFlow.c_str());
      return driver.release();
    }
#else
    throw std::runtime_error("WebGPU requires Windows x64");
#endif
  }
  throw std::runtime_error("Unknown Windows graphics API");
}
}
#endif