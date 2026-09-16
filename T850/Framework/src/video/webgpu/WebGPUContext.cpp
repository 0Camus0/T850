#include <video/webgpu/WebGPUContext.h>

#if defined(_WIN32) && defined(_M_X64)
#include <utils/Log.h>
#include <T850DawnShaderConfig.h>
#include <dawn/native/D3DBackend.h>
#include <Windows.h>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace t850::webgpu {
namespace {
std::string Message(wgpu::StringView text) {
  if (!text.data) return {};
  return text.length == WGPU_STRLEN ? std::string(text.data) : std::string(text.data, text.length);
}
void Require(bool success, const char* message) {
  if (!success) throw std::runtime_error(std::string("[WebGPU] ") + message);
}
void Wait(const wgpu::Instance& instance, wgpu::Future future) {
  Require(instance.WaitAny(future, 30'000'000'000ULL) == wgpu::WaitStatus::Success, "GPU operation timed out");
}
}

struct WebGPUContext::Health {
  mutable std::mutex mutex;
  std::string error;
  void Fail(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex);
    if (error.empty()) error = message;
    T8_LOG_ERROR("[WebGPU] %s", message.c_str());
  }
};

WebGPUContext::WebGPUContext() : m_health(std::make_shared<Health>()) {}
WebGPUContext::~WebGPUContext() {
  try { Shutdown(); }
  catch (const std::exception& error) { T8_LOG_ERROR("[WebGPU] teardown: %s", error.what()); }
}

void WebGPUContext::CheckHealth() const {
  std::lock_guard<std::mutex> lock(m_health->mutex);
  if (!m_health->error.empty()) throw std::runtime_error("[WebGPU] " + m_health->error);
}

void WebGPUContext::Initialize(void* hwnd, uint32_t newWidth, uint32_t newHeight) {
  Require(!instance, "Context is already initialized");
  Require(hwnd && IsWindow(static_cast<HWND>(hwnd)), "A valid HWND is required");
  m_health = std::make_shared<Health>();
  const wgpu::InstanceFeatureName feature = wgpu::InstanceFeatureName::TimedWaitAny;
  wgpu::InstanceDescriptor instanceDesc{};
  instanceDesc.requiredFeatureCount = 1;
  instanceDesc.requiredFeatures = &feature;
  instance = wgpu::CreateInstance(&instanceDesc);
  Require(static_cast<bool>(instance), "Instance creation failed");
  wgpu::SurfaceSourceWindowsHWND nativeSurface{};
  nativeSurface.hwnd = hwnd;
  nativeSurface.hinstance = GetModuleHandle(nullptr);
  wgpu::SurfaceDescriptor surfaceDesc{};
  surfaceDesc.nextInChain = &nativeSurface;
  surface = instance.CreateSurface(&surfaceDesc);
  Require(static_cast<bool>(surface), "Surface creation failed");
  wgpu::RequestAdapterOptions options{};
  options.backendType = wgpu::BackendType::D3D12;
  options.featureLevel = wgpu::FeatureLevel::Core;
  options.powerPreference = wgpu::PowerPreference::HighPerformance;
  options.compatibleSurface = surface;
  auto selected = std::make_shared<wgpu::Adapter>();
  const auto health = m_health;
  Wait(instance, instance.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly,
    [selected, health](wgpu::RequestAdapterStatus status, wgpu::Adapter result, wgpu::StringView message) {
      if (status == wgpu::RequestAdapterStatus::Success) *selected = std::move(result);
      else health->Fail("Adapter request failed: " + Message(message));
    }));
  CheckHealth();
  adapter = *selected;
  Require(static_cast<bool>(adapter), "Adapter unavailable");
  wgpu::AdapterInfo info{};
  Require(adapter.GetInfo(&info) == wgpu::Status::Success && info.backendType == wgpu::BackendType::D3D12
    && info.adapterType != wgpu::AdapterType::CPU, "Hardware D3D12 adapter required; fallback rejected");
  auto nativeAdapter = dawn::native::d3d::GetDXGIAdapter(adapter.Get());
  DXGI_ADAPTER_DESC nativeInfo{};
  Require(nativeAdapter && SUCCEEDED(nativeAdapter->GetDesc(&nativeInfo)), "Adapter LUID unavailable");
  adapterLuid = static_cast<uint64_t>(static_cast<uint32_t>(nativeInfo.AdapterLuid.HighPart)) << 32 | nativeInfo.AdapterLuid.LowPart;
  T8_LOG_INFO("[WebGPU] adapter LUID=%llu", static_cast<unsigned long long>(adapterLuid));
  T8_LOG_INFO("[WebGPU] api=webgpu provider=Dawn backend=D3D12 adapter=%s vendor=%u device=%u compiler=%s",
    Message(info.device).c_str(), info.vendorID, info.deviceID, T850_DAWN_SHADER_ABI);
  wgpu::DeviceDescriptor deviceDesc{};
  wgpu::Limits adapterLimits{};
  Require(adapter.GetLimits(&adapterLimits) == wgpu::Status::Success, "Adapter limits unavailable");
  wgpu::Limits requiredLimits{};
  requiredLimits.maxColorAttachments = adapterLimits.maxColorAttachments;
  requiredLimits.maxColorAttachmentBytesPerSample = adapterLimits.maxColorAttachmentBytesPerSample;
  deviceDesc.requiredLimits = &requiredLimits;
  T8_LOG_INFO("[WebGPU] color attachment limits: count=%u bytesPerSample=%u",
    requiredLimits.maxColorAttachments, requiredLimits.maxColorAttachmentBytesPerSample);
  std::vector<wgpu::FeatureName> features;
  for (const auto optional : {wgpu::FeatureName::Float32Filterable, wgpu::FeatureName::TextureCompressionBC}) {
    if (adapter.HasFeature(optional)) features.push_back(optional);
  }
  deviceDesc.requiredFeatureCount = features.size();
  deviceDesc.requiredFeatures = features.data();
  deviceDesc.SetUncapturedErrorCallback(
    [](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message, Health* state) { state->Fail(Message(message)); }, health.get());
  deviceDesc.SetDeviceLostCallback(wgpu::CallbackMode::AllowProcessEvents,
    [health](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message) {
      if (reason != wgpu::DeviceLostReason::Destroyed && reason != wgpu::DeviceLostReason::CallbackCancelled)
        health->Fail("Device lost: " + Message(message));
    });
  auto created = std::make_shared<wgpu::Device>();
  Wait(instance, adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::WaitAnyOnly,
    [created, health](wgpu::RequestDeviceStatus status, wgpu::Device result, wgpu::StringView message) {
      if (status == wgpu::RequestDeviceStatus::Success) *created = std::move(result);
      else health->Fail("Device request failed: " + Message(message));
    }));
  CheckHealth();
  device = *created;
  Require(static_cast<bool>(device), "Device unavailable");
  queue = device.GetQueue();
  wgpu::SurfaceCapabilities capabilities{};
  Require(surface.GetCapabilities(adapter, &capabilities) == wgpu::Status::Success, "Surface capabilities unavailable");
  Require(capabilities.formatCount > 0 && capabilities.alphaModeCount > 0, "Surface has no supported format");
  configuration.device = device;
  configuration.format = capabilities.formats[0];
  for (size_t index = 0; index < capabilities.formatCount; ++index) {
    if (capabilities.formats[index] == wgpu::TextureFormat::BGRA8Unorm) configuration.format = capabilities.formats[index];
  }
  Require((capabilities.usages & wgpu::TextureUsage::CopySrc) != wgpu::TextureUsage::None, "Surface readback requires CopySrc support");
  configuration.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
  configuration.presentMode = wgpu::PresentMode::Fifo;
  configuration.alphaMode = capabilities.alphaModes[0];
  Resize(newWidth, newHeight);
}

void WebGPUContext::Resize(uint32_t newWidth, uint32_t newHeight) {
  Require(!commands, "Cannot resize during a frame");
  WaitForGPU();
  backbuffer = nullptr;
  depth = nullptr;
  if (m_configured) surface.Unconfigure();
  m_configured = false;
  width = newWidth;
  height = newHeight;
  if (!width || !height) return;
  configuration.width = width;
  configuration.height = height;
  surface.Configure(&configuration);
  wgpu::TextureDescriptor depthDesc{};
  depthDesc.size = {width, height, 1};
  depthDesc.format = wgpu::TextureFormat::Depth32Float;
  depthDesc.usage = wgpu::TextureUsage::RenderAttachment;
  depth = device.CreateTexture(&depthDesc);
  m_configured = true;
  CheckHealth();
}

bool WebGPUContext::BeginFrame(bool swapchain) {
  CheckHealth();
  Require(!commands, "Frame is already active");
  instance.ProcessEvents();
  if (swapchain) {
    if (!m_configured) return false;
    wgpu::SurfaceTexture acquired{};
    surface.GetCurrentTexture(&acquired);
    if (acquired.status == wgpu::SurfaceGetCurrentTextureStatus::Timeout) return false;
    if (acquired.status == wgpu::SurfaceGetCurrentTextureStatus::Outdated || acquired.status == wgpu::SurfaceGetCurrentTextureStatus::Lost) {
      acquired.texture = nullptr;
      Resize(width, height);
      surface.GetCurrentTexture(&acquired);
    }
    Require(acquired.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal
      || acquired.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal, "Surface acquisition failed");
    backbuffer = acquired.texture;
    Require(static_cast<bool>(backbuffer), "Surface returned no texture");
  }
  commands = device.CreateCommandEncoder();
  return true;
}

void WebGPUContext::Submit(bool present) {
  Require(static_cast<bool>(commands), "No frame to submit");
  auto command = commands.Finish();
  commands = nullptr;
  queue.Submit(1, &command);
  if (present) {
    Require(static_cast<bool>(backbuffer), "Present requires a swapchain frame");
    Require(surface.Present() == wgpu::Status::Success, "Presentation failed");
  }
  backbuffer = nullptr;
  instance.ProcessEvents();
  CheckHealth();
}

void WebGPUContext::WaitForGPU() {
  if (!queue) return;
  auto completed = std::make_shared<bool>(false);
  Wait(instance, queue.OnSubmittedWorkDone(wgpu::CallbackMode::WaitAnyOnly,
    [completed](wgpu::QueueWorkDoneStatus status, wgpu::StringView) { *completed = status == wgpu::QueueWorkDoneStatus::Success; }));
  Require(*completed, "Queue completion failed");
  CheckHealth();
}

void WebGPUContext::Shutdown() {
  commands = nullptr;
  std::exception_ptr failure;
  if (queue) {
    try { WaitForGPU(); }
    catch (...) { failure = std::current_exception(); }
  }
  backbuffer = nullptr;
  depth = nullptr;
  if (surface && m_configured) surface.Unconfigure();
  m_configured = false;
  configuration = {};
  surface = nullptr;
  queue = nullptr;
  if (device) device.Destroy();
  device = nullptr;
  adapter = nullptr;
  if (instance) instance.ProcessEvents();
  instance = nullptr;
  if (failure) std::rethrow_exception(failure);
  CheckHealth();
}
}
#endif