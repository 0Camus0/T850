#include <video/webgpu/WebGPUContext.h>
#include <Windows.h>
#include <array>
#include <iostream>
#include <stdexcept>

int main() {
  const auto application = GetModuleHandle(nullptr);
  WNDCLASSW windowClass{};
  windowClass.lpfnWndProc = DefWindowProcW;
  windowClass.hInstance = application;
  windowClass.lpszClassName = L"T850DawnSurfaceProbe";
  if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;
  HWND window = CreateWindowW(windowClass.lpszClassName, L"T850 Dawn Surface Test", WS_OVERLAPPEDWINDOW,
    CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, nullptr, nullptr, application, nullptr);
  if (!window) return 1;
  ShowWindow(window, SW_SHOWNOACTIVATE);
  int result = 0;
  try {
    for (unsigned iteration = 0; iteration < 3; ++iteration) {
      t850::webgpu::WebGPUContext context;
      context.Initialize(window, 640, 480);
      for (const auto dimensions : {std::array<uint32_t, 2>{640, 480}, {321, 239}, {800, 600}}) {
        context.Resize(dimensions[0], dimensions[1]);
        if (!context.BeginFrame(true)) throw std::runtime_error("Unexpected suspended surface");
        wgpu::RenderPassColorAttachment color{};
        color.view = context.backbuffer.CreateView();
        color.loadOp = wgpu::LoadOp::Clear;
        color.storeOp = wgpu::StoreOp::Store;
        color.clearValue = {0.08, 0.35, 0.7, 1.0};
        wgpu::RenderPassDescriptor descriptor{};
        descriptor.colorAttachmentCount = 1;
        descriptor.colorAttachments = &color;
        auto pass = context.commands.BeginRenderPass(&descriptor);
        pass.End();
        pass = nullptr;
        color.view = nullptr;
        context.Submit(true);
        context.WaitForGPU();
        MSG message{};
        while (PeekMessageW(&message, window, 0, 0, PM_REMOVE)) {
          TranslateMessage(&message);
          DispatchMessageW(&message);
        }
      }
      context.Resize(0, 0);
      if (context.BeginFrame(true)) throw std::runtime_error("Zero-size surface was acquired");
      context.Resize(640, 480);
      if (!context.BeginFrame(false)) throw std::runtime_error("Offscreen frame failed");
      context.Submit(false);
      context.WaitForGPU();
      context.Shutdown();
      context.CheckHealth();
    }
    std::cout << "Dawn surface lifecycle PASS: hardware, clear/present, resize, suspend/resume, offscreen submission, teardown\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    result = 1;
  }
  DestroyWindow(window);
  UnregisterClassW(windowClass.lpszClassName, application);
  return result;
}