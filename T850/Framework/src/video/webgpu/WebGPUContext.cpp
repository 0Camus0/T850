#include <video/webgpu/WebGPUContext.h>
#include <debug/RuntimeTelemetry.h>
#include <core/Config.h>
#ifndef __EMSCRIPTEN__
#include <utils/ResourceLocator.h>
#include <utils/ShaderDiskCache.h>
#endif

#if (defined(_WIN32) && (defined(_M_X64) || defined(_M_ARM64))) || defined(__EMSCRIPTEN__)
#include <utils/Log.h>
#ifdef __EMSCRIPTEN__
#include <SDL3/SDL.h>
#else
#include <T850DawnShaderConfig.h>
#include <dawn/native/D3DBackend.h>
#include <Windows.h>
#endif
#include <atomic>
#include <algorithm>
#ifndef __EMSCRIPTEN__
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#endif
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace t850::webgpu {
namespace {
std::atomic<uint64_t> g_frameAttempts{0};
std::atomic<bool> g_forcedDeviceLossInjected{false};
std::atomic<uint64_t> g_configuredDeviceLossFrame{0};

bool ShouldInjectDeviceLoss() {
  static const uint64_t environmentFrame = [] {
#ifdef __EMSCRIPTEN__
    return uint64_t{0};
#else
    const char* value = std::getenv("T850_WEBGPU_FORCE_DEVICE_LOSS_FRAME");
    if (!value || !*value) return uint64_t{0};
    char* end = nullptr;
    const auto parsed = std::strtoull(value, &end, 10);
    return end && *end == '\0' ? parsed : uint64_t{0};
#endif
  }();
  const uint64_t configuredFrame = g_configuredDeviceLossFrame.load(std::memory_order_acquire);
  const uint64_t frame = configuredFrame ? configuredFrame : environmentFrame;
  return frame && g_frameAttempts.fetch_add(1, std::memory_order_relaxed) + 1 == frame;
}

std::string Message(wgpu::StringView text) {
  if (!text.data) return {};
  return text.length == WGPU_STRLEN ? std::string(text.data) : std::string(text.data, text.length);
}
void Require(bool success, const char* message) {
  if (!success) throw std::runtime_error(std::string("[WebGPU] ") + message);
}
void Wait(const wgpu::Instance& instance, wgpu::Future future) {
#ifdef __EMSCRIPTEN__
  Require(instance.WaitAny(future, UINT64_MAX) == wgpu::WaitStatus::Success, "Browser GPU operation failed");
#else
  Require(instance.WaitAny(future, 30'000'000'000ULL) == wgpu::WaitStatus::Success, "GPU operation timed out");
#endif
}
}

void ConfigureDeviceLossTestFrame(uint64_t frame) {
  g_frameAttempts.store(0, std::memory_order_release);
  g_forcedDeviceLossInjected.store(false, std::memory_order_release);
  g_configuredDeviceLossFrame.store(frame, std::memory_order_release);
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

#ifndef __EMSCRIPTEN__
struct WebGPUContext::PersistentCache {
  static constexpr const char* kCachePath = "Shaders/.t8shadercache/dawn-native";

  PersistentCache() {
    const char* mode = std::getenv("T850_WEBGPU_DAWN_CACHE");
    m_enabled = !mode || _stricmp(mode, "off") != 0;
    if (!m_enabled) return;
    m_root = ResourceLocator::Instance().ResolveCachePath(kCachePath);
    if (mode && _stricmp(mode, "reset") == 0) {
      std::error_code error;
      std::filesystem::remove_all(m_root, error);
      if (error) {
        T8_LOG_ERROR("[WebGPU] Dawn persistent cache reset failed: %s", error.message().c_str());
        m_enabled = false;
        return;
      }
    }
    std::error_code error;
    std::filesystem::create_directories(m_root, error);
    if (error) {
      T8_LOG_ERROR("[WebGPU] Dawn persistent cache unavailable: %s", error.message().c_str());
      m_enabled = false;
    }
  }

  bool IsEnabled() const { return m_enabled; }

  static size_t Load(const void* key, size_t keySize, void* value, size_t valueSize, void* userdata) {
    return static_cast<PersistentCache*>(userdata)->Load(key, keySize, value, valueSize);
  }

  static void Store(const void* key, size_t keySize, const void* value, size_t valueSize, void* userdata) {
    static_cast<PersistentCache*>(userdata)->Store(key, keySize, value, valueSize);
  }

  void Report() {
    if (!m_enabled) {
      T8_LOG_INFO("[WebGPU] Dawn persistent cache disabled");
      return;
    }
    T8_LOG_INFO("[WebGPU] Dawn persistent cache: hits=%llu misses=%llu stores=%llu loadBytes=%llu storeBytes=%llu lookupMs=%.4f storeMs=%.4f",
      static_cast<unsigned long long>(m_hits), static_cast<unsigned long long>(m_misses),
      static_cast<unsigned long long>(m_stores), static_cast<unsigned long long>(m_loadBytes),
      static_cast<unsigned long long>(m_storeBytes), m_lookupMilliseconds, m_storeMilliseconds);
    if (g_config.flags.compileShaders) {
      std::cout << "[DawnPersistentCacheProfile] hits=" << m_hits << " misses=" << m_misses
                << " stores=" << m_stores << " loadBytes=" << m_loadBytes
                << " storeBytes=" << m_storeBytes << " lookupMs=" << m_lookupMilliseconds
                << " storeMs=" << m_storeMilliseconds << std::endl;
    }
  }

private:
  std::filesystem::path Path(const void* key, size_t keySize) const {
    const std::string bytes(static_cast<const char*>(key), keySize);
    return m_root / (ShaderDiskCache::ContentHash(bytes) + ".blob");
  }

  size_t Load(const void* key, size_t keySize, void* value, size_t valueSize) {
    if (!m_enabled || !key || keySize == 0) return 0;
    const auto started = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::filesystem::path path = Path(key, keySize);
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
      if (!value) ++m_misses;
      m_lookupMilliseconds += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
      return 0;
    }
    const std::streamsize size = file.tellg();
    if (size <= 0 || static_cast<uint64_t>(size) > SIZE_MAX) return 0;
    if (!value || valueSize == 0) {
      ++m_hits;
      m_lookupMilliseconds += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
      return static_cast<size_t>(size);
    }
    if (valueSize < static_cast<size_t>(size)) return 0;
    file.seekg(0, std::ios::beg);
    file.read(static_cast<char*>(value), size);
    if (!file.good()) return 0;
    m_loadBytes += static_cast<uint64_t>(size);
    m_lookupMilliseconds += std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - started).count();
    return static_cast<size_t>(size);
  }

  void Store(const void* key, size_t keySize, const void* value, size_t valueSize) {
    if (!m_enabled || !key || keySize == 0 || !value || valueSize == 0) return;
    const auto started = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto bytes = std::span<const unsigned char>(static_cast<const unsigned char*>(value), valueSize);
    if (!ResourceLocator::Instance().WriteBinaryAtomic(Path(key, keySize).string(), bytes)) return;
    ++m_stores;
    m_storeBytes += valueSize;
    m_storeMilliseconds += std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - started).count();
  }

  bool m_enabled = false;
  std::filesystem::path m_root;
  std::mutex m_mutex;
  uint64_t m_hits = 0;
  uint64_t m_misses = 0;
  uint64_t m_stores = 0;
  uint64_t m_loadBytes = 0;
  uint64_t m_storeBytes = 0;
  double m_lookupMilliseconds = 0.0;
  double m_storeMilliseconds = 0.0;
};
#endif

WebGPUContext::WebGPUContext() : m_health(std::make_shared<Health>()) {}
WebGPUContext::~WebGPUContext() {
  try { Shutdown(); }
  catch (const std::exception& error) { T8_LOG_ERROR("[WebGPU] teardown: %s", error.what()); }
}

void WebGPUContext::CheckHealth() const {
  std::string diagnostic;
  if (GetHealthError(diagnostic)) throw std::runtime_error("[WebGPU] " + diagnostic);
}

bool WebGPUContext::GetHealthError(std::string& diagnostic) const {
  std::lock_guard<std::mutex> lock(m_health->mutex);
  diagnostic = m_health->error;
  return !diagnostic.empty();
}

void WebGPUContext::Initialize(void* hwnd, uint32_t newWidth, uint32_t newHeight) {
  Require(!instance, "Context is already initialized");
#ifdef __EMSCRIPTEN__
  Require(hwnd != nullptr, "An SDL canvas window is required");
#else
  Require(hwnd && IsWindow(static_cast<HWND>(hwnd)), "A valid HWND is required");
#endif
  m_health = std::make_shared<Health>();
  const wgpu::InstanceFeatureName feature = wgpu::InstanceFeatureName::TimedWaitAny;
  wgpu::InstanceDescriptor instanceDesc{};
  instanceDesc.requiredFeatureCount = 1;
  instanceDesc.requiredFeatures = &feature;
  instance = wgpu::CreateInstance(&instanceDesc);
  Require(static_cast<bool>(instance), "Instance creation failed");
#ifdef __EMSCRIPTEN__
  const auto properties = SDL_GetWindowProperties(static_cast<SDL_Window*>(hwnd));
  std::string selector = SDL_GetStringProperty(properties, SDL_PROP_WINDOW_EMSCRIPTEN_CANVAS_ID_STRING, "canvas");
  if (!selector.starts_with('#')) selector.insert(selector.begin(), '#');
  wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector nativeSurface{};
  nativeSurface.selector = selector.c_str();
#else
  wgpu::SurfaceSourceWindowsHWND nativeSurface{};
  nativeSurface.hwnd = hwnd;
  nativeSurface.hinstance = GetModuleHandle(nullptr);
#endif
  wgpu::SurfaceDescriptor surfaceDesc{};
  surfaceDesc.nextInChain = &nativeSurface;
  surface = instance.CreateSurface(&surfaceDesc);
  Require(static_cast<bool>(surface), "Surface creation failed");
  wgpu::RequestAdapterOptions options{};
#ifndef __EMSCRIPTEN__
  options.backendType = wgpu::BackendType::D3D12;
#endif
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
#ifdef __EMSCRIPTEN__
  Require(adapter.GetInfo(&info) == wgpu::Status::Success, "Browser adapter information unavailable");
  T8_LOG_INFO("[WebGPU] api=webgpu provider=browser adapter=%s", Message(info.device).c_str());
#else
  Require(adapter.GetInfo(&info) == wgpu::Status::Success && info.backendType == wgpu::BackendType::D3D12
    && info.adapterType != wgpu::AdapterType::CPU, "Hardware D3D12 adapter required; fallback rejected");
  auto nativeAdapter = dawn::native::d3d::GetDXGIAdapter(adapter.Get());
  DXGI_ADAPTER_DESC nativeInfo{};
  Require(nativeAdapter && SUCCEEDED(nativeAdapter->GetDesc(&nativeInfo)), "Adapter LUID unavailable");
  adapterLuid = static_cast<uint64_t>(static_cast<uint32_t>(nativeInfo.AdapterLuid.HighPart)) << 32 | nativeInfo.AdapterLuid.LowPart;
  T8_LOG_INFO("[WebGPU] adapter LUID=%llu", static_cast<unsigned long long>(adapterLuid));
  T8_LOG_INFO("[WebGPU] api=webgpu provider=Dawn backend=D3D12 adapter=%s vendor=%u device=%u compiler=%s",
    Message(info.device).c_str(), info.vendorID, info.deviceID, T850_DAWN_SHADER_ABI);
  #endif
  wgpu::DeviceDescriptor deviceDesc{};
  wgpu::Limits adapterLimits{};
  Require(adapter.GetLimits(&adapterLimits) == wgpu::Status::Success, "Adapter limits unavailable");
  wgpu::Limits requiredLimits{};
  requiredLimits.maxColorAttachments = adapterLimits.maxColorAttachments;
  requiredLimits.maxColorAttachmentBytesPerSample = adapterLimits.maxColorAttachmentBytesPerSample;
  deviceDesc.requiredLimits = &requiredLimits;
#if T850_ENABLE_GPU_PROFILING && !defined(__EMSCRIPTEN__)
  const char* gpuProfilingToggles[] = {"allow_unsafe_apis"};
  wgpu::DawnTogglesDescriptor gpuProfilingToggleDescriptor{};
  if (g_config.profileGpu) {
    gpuProfilingToggleDescriptor.enabledToggleCount = std::size(gpuProfilingToggles);
    gpuProfilingToggleDescriptor.enabledToggles = gpuProfilingToggles;
    deviceDesc.nextInChain = &gpuProfilingToggleDescriptor;
  }
#endif
#ifndef __EMSCRIPTEN__
  m_persistentCache = std::make_unique<PersistentCache>();
  wgpu::DawnCacheDeviceDescriptor cacheDescriptor{};
  if (m_persistentCache->IsEnabled()) {
    cacheDescriptor.nextInChain = deviceDesc.nextInChain;
    cacheDescriptor.isolationKey = "t850-dawn-d3d12-v1";
    cacheDescriptor.loadDataFunction = &PersistentCache::Load;
    cacheDescriptor.storeDataFunction = &PersistentCache::Store;
    cacheDescriptor.functionUserdata = m_persistentCache.get();
    deviceDesc.nextInChain = &cacheDescriptor;
  }
#endif
  T8_LOG_INFO("[WebGPU] color attachment limits: count=%u bytesPerSample=%u",
    requiredLimits.maxColorAttachments, requiredLimits.maxColorAttachmentBytesPerSample);
  std::vector<wgpu::FeatureName> features;
  for (const auto optional : {wgpu::FeatureName::Float32Filterable, wgpu::FeatureName::TextureCompressionBC}) {
    if (adapter.HasFeature(optional)) features.push_back(optional);
  }
#if T850_ENABLE_GPU_PROFILING
  if (g_config.profileGpu) {
    Require(adapter.HasFeature(wgpu::FeatureName::TimestampQuery),
            "GPU timestamp profiling requested but timestamp-query is unavailable");
    features.push_back(wgpu::FeatureName::TimestampQuery);
  }
#endif
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
  T8_LOG_INFO("[WebGPU] Device optional features: BC=%d float32-filterable=%d",
    device.HasFeature(wgpu::FeatureName::TextureCompressionBC) ? 1 : 0,
    device.HasFeature(wgpu::FeatureName::Float32Filterable) ? 1 : 0);
#if T850_ENABLE_GPU_PROFILING
  T8_LOG_INFO("[WebGPU] timestamp-query requested=%d active=%d",
    g_config.profileGpu ? 1 : 0,
    device.HasFeature(wgpu::FeatureName::TimestampQuery) ? 1 : 0);
#endif
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
  m_directReadback = (capabilities.usages & wgpu::TextureUsage::CopySrc) != wgpu::TextureUsage::None;
  configuration.usage = wgpu::TextureUsage::RenderAttachment;
  if (m_directReadback) configuration.usage |= wgpu::TextureUsage::CopySrc;
  configuration.presentMode = wgpu::PresentMode::Fifo;
#ifndef __EMSCRIPTEN__
  for (size_t index = 0; index < capabilities.presentModeCount; ++index) {
    if (capabilities.presentModes[index] == wgpu::PresentMode::Immediate)
      configuration.presentMode = wgpu::PresentMode::Immediate;
  }
#endif
  T8_LOG_INFO("[WebGPU] presentation=%s", configuration.presentMode == wgpu::PresentMode::Immediate ? "immediate" : "fifo");
  configuration.alphaMode = capabilities.alphaModes[0];
  Resize(newWidth, newHeight);
}

void WebGPUContext::Resize(uint32_t newWidth, uint32_t newHeight) {
  Require(!commands, "Cannot resize during a frame");
  WaitForGPU();
  backbuffer = nullptr;
  m_presentTarget = nullptr;
  if (m_captureTarget) m_captureTarget.Destroy();
  if (depth) depth.Destroy();
  m_captureTarget = nullptr;
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
  if (!m_directReadback) {
    wgpu::TextureDescriptor colorDesc{};
    colorDesc.size = {width, height, 1};
    colorDesc.format = configuration.format;
    colorDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc;
    m_captureTarget = device.CreateTexture(&colorDesc);
    if (!m_presentPipeline) {
      wgpu::ShaderSourceWGSL source{};
      source.code = R"(
@group(0) @binding(0) var inputTexture: texture_2d<f32>;
@vertex fn VS(@builtin(vertex_index) vertexIndex: u32) -> @builtin(position) vec4f {
  let positions = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));
  return vec4f(positions[vertexIndex], 0, 1);
}
@fragment fn FS(@builtin(position) position: vec4f) -> @location(0) vec4f {
  return textureLoad(inputTexture, vec2i(position.xy), 0);
}
)";
      wgpu::ShaderModuleDescriptor moduleDesc{};
      moduleDesc.nextInChain = &source;
      const auto module = T8_TELEMETRY_CALL("shader.module.create", device.CreateShaderModule(&moduleDesc));
      wgpu::ColorTargetState target{};
      target.format = configuration.format;
      wgpu::FragmentState fragment{};
      fragment.module = module;
      fragment.entryPoint = "FS";
      fragment.targetCount = 1;
      fragment.targets = &target;
      wgpu::RenderPipelineDescriptor pipeline{};
      pipeline.vertex.module = module;
      pipeline.vertex.entryPoint = "VS";
      pipeline.fragment = &fragment;
      m_presentPipeline = T8_TELEMETRY_CALL("pipeline.create.graphics", device.CreateRenderPipeline(&pipeline));
    }
  }
  m_configured = true;
  CheckHealth();
}

bool WebGPUContext::BeginFrame(bool swapchain) {
  T8_TELEMETRY_SCOPE("webgpu.begin_frame");
  if (ShouldInjectDeviceLoss() && !g_forcedDeviceLossInjected.exchange(true, std::memory_order_acq_rel)) {
    m_health->Fail("Injected device loss for recovery validation");
    device.Destroy();
  }
  CheckHealth();
  Require(!commands, "Frame is already active");
  instance.ProcessEvents();
  CollectCompletedBuffers(m_submissions.size() >= 4);
  if (swapchain) {
    if (!m_configured) return false;
    if (m_captureTarget) {
      backbuffer = m_captureTarget;
    } else {
    wgpu::SurfaceTexture acquired{};
    {
      T8_TELEMETRY_SCOPE("webgpu.surface_acquire");
      surface.GetCurrentTexture(&acquired);
    }
    if (acquired.status == wgpu::SurfaceGetCurrentTextureStatus::Timeout) return false;
    if (acquired.status == wgpu::SurfaceGetCurrentTextureStatus::Outdated || acquired.status == wgpu::SurfaceGetCurrentTextureStatus::Lost) {
      acquired.texture = nullptr;
      Resize(width, height);
      surface.GetCurrentTexture(&acquired);
    }
    Require(acquired.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal
      || acquired.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal, "Surface acquisition failed");
    m_presentTarget = acquired.texture;
    backbuffer = m_directReadback ? acquired.texture : m_captureTarget;
    Require(static_cast<bool>(backbuffer), "Surface returned no texture");
    }
  }
  commands = device.CreateCommandEncoder();
  return true;
}

void WebGPUContext::Submit(bool present) {
  Require(static_cast<bool>(commands), "No frame to submit");
  if (present && m_captureTarget) {
    wgpu::SurfaceTexture acquired{};
    surface.GetCurrentTexture(&acquired);
    Require(acquired.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal ||
      acquired.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal, "Presentation surface acquisition failed");
    m_presentTarget = acquired.texture;
    wgpu::BindGroupEntry entry{};
    entry.binding = 0;
    entry.textureView = m_captureTarget.CreateView();
    wgpu::BindGroupDescriptor bindings{};
    bindings.layout = m_presentPipeline.GetBindGroupLayout(0);
    bindings.entryCount = 1;
    bindings.entries = &entry;
    const auto group = device.CreateBindGroup(&bindings);
    wgpu::RenderPassColorAttachment color{};
    color.view = m_presentTarget.CreateView();
    color.loadOp = wgpu::LoadOp::Clear;
    color.storeOp = wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor passDesc{};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &color;
    const auto pass = commands.BeginRenderPass(&passDesc);
    pass.SetPipeline(m_presentPipeline);
    pass.SetBindGroup(0, group);
    pass.Draw(3);
    pass.End();
  }
  auto command = commands.Finish();
  commands = nullptr;
  SubmitCommands(command);
  if (present) {
    T8_TELEMETRY_SCOPE("gpu.present");
    Require(static_cast<bool>(backbuffer), "Present requires a swapchain frame");
#ifndef __EMSCRIPTEN__
    Require(surface.Present() == wgpu::Status::Success, "Presentation failed");
#endif
  }
  backbuffer = nullptr;
  m_presentTarget = nullptr;
  instance.ProcessEvents();
  CheckHealth();
}

void WebGPUContext::WaitForGPU() {
  if (!queue) return;
  T8_TELEMETRY_SCOPE("gpu.gpu_wait");
  auto completed = std::make_shared<bool>(false);
  Wait(instance, queue.OnSubmittedWorkDone(wgpu::CallbackMode::WaitAnyOnly,
    [completed](wgpu::QueueWorkDoneStatus status, wgpu::StringView) { *completed = status == wgpu::QueueWorkDoneStatus::Success; }));
  Require(*completed, "Queue completion failed");
  CollectCompletedBuffers();
  CheckHealth();
}

wgpu::Buffer WebGPUContext::AcquireBuffer(const wgpu::BufferDescriptor& descriptor) {
  const BufferKey key{descriptor.size, descriptor.usage};
  auto available = m_freeBuffers.find(key);
  if (available != m_freeBuffers.end() && !available->second.empty()) {
    auto buffer = std::move(available->second.back());
    available->second.pop_back();
    m_freeBufferBytes -= descriptor.size;
    if (available->second.empty()) m_freeBuffers.erase(available);
    if (RuntimeTelemetry::IsFrameActive()) T8_TELEMETRY_ADD("webgpu.buffer_reuses", 1);
    T8_TELEMETRY_ADD("gpu.pool.hits", 1);
    return buffer;
  }
  if (descriptor.size <= maximumPoolBytes) {
    while (m_freeBufferBytes > maximumPoolBytes - descriptor.size) {
      auto unused = std::prev(m_freeBuffers.end());
      unused->second.back().Destroy();
      unused->second.pop_back();
      m_freeBufferBytes -= unused->first.first;
      if (unused->second.empty()) m_freeBuffers.erase(unused);
      if (RuntimeTelemetry::IsFrameActive()) T8_TELEMETRY_ADD("webgpu.buffer_evictions", 1);
      T8_TELEMETRY_ADD("gpu.pool.evictions", 1);
    }
  }
  if (RuntimeTelemetry::IsFrameActive()) T8_TELEMETRY_ADD("webgpu.buffer_allocations", 1);
  T8_TELEMETRY_ADD("gpu.pool.misses", 1);
  RuntimeTelemetry::RecordStaging(descriptor.usage & wgpu::BufferUsage::Uniform
    ? RuntimeTelemetry::UploadResource::Uniform : descriptor.usage & wgpu::BufferUsage::Index
    ? RuntimeTelemetry::UploadResource::Index : RuntimeTelemetry::UploadResource::Vertex, 0, 1);
  return device.CreateBuffer(&descriptor);
}

wgpu::Buffer WebGPUContext::UploadUniform(const void* data, uint64_t size, uint64_t& offset) {
  T8_TELEMETRY_ADD("webgpu.uniform_snapshot.calls", 1);
  Require(commands && data && size, "Uniform snapshot requires active commands and data");
  const uint64_t alignedSize = (size + 255) & ~uint64_t(255);
  if (m_uniformUploads.empty() || m_uniformUploads.back().data.size() + alignedSize > m_uniformUploads.back().capacity) {
    wgpu::BufferDescriptor descriptor{};
    descriptor.size = (std::max)(uint64_t(1024 * 1024), alignedSize);
    descriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    m_uniformUploads.push_back({AcquireBuffer(descriptor), {}, descriptor.size});
    m_uniformUploads.back().data.reserve(descriptor.size);
  }
  auto& upload = m_uniformUploads.back();
  offset = upload.data.size();
  upload.data.resize(offset + alignedSize);
  std::memcpy(upload.data.data() + offset, data, size);
  RuntimeTelemetry::RecordStaging(RuntimeTelemetry::UploadResource::Uniform, size);
  if (RuntimeTelemetry::IsFrameActive()) {
    T8_TELEMETRY_ADD("webgpu.uniform_snapshots", 1);
    T8_TELEMETRY_ADD("webgpu.uniform_bytes", size);
  }
  return upload.buffer;
}

void WebGPUContext::Retire(wgpu::Buffer buffer, uint64_t size, wgpu::BufferUsage usage) {
  if (buffer) m_retiredBuffers.push_back({std::move(buffer), {size, usage}});
}

void WebGPUContext::CollectCompletedBuffers(bool waitForOldest) {
  T8_TELEMETRY_SCOPE("webgpu.collect_completed");
  while (!m_submissions.empty()) {
    auto& submission = m_submissions.front();
    if (waitForOldest) {
      T8_TELEMETRY_SCOPE("gpu.gpu_wait");
      Wait(instance, submission.completion);
      waitForOldest = false;
    } else {
      const auto status = instance.WaitAny(submission.completion, 0);
      if (status == wgpu::WaitStatus::TimedOut) break;
      Require(status == wgpu::WaitStatus::Success, "Queue completion polling failed");
    }
    CheckHealth();
    for (auto& retired : submission.buffers) {
      if (retired.key.first <= maximumPoolBytes - m_freeBufferBytes) {
        m_freeBufferBytes += retired.key.first;
        m_freeBuffers[retired.key].push_back(std::move(retired.buffer));
      } else {
        retired.buffer.Destroy();
        T8_TELEMETRY_ADD("gpu.pool.evictions", 1);
      }
    }
    m_submissions.pop_front();
  }
  if (RuntimeTelemetry::IsFrameActive()) {
    T8_TELEMETRY_SET("webgpu.pending_submissions", m_submissions.size());
    T8_TELEMETRY_SET("webgpu.free_buffer_bytes", m_freeBufferBytes);
  }
}

void WebGPUContext::Retire(wgpu::Texture texture) {
  if (texture) m_retiredTextures.push_back(std::move(texture));
}

void WebGPUContext::SubmitCommands(const wgpu::CommandBuffer& command) {
  T8_TELEMETRY_ADD("gpu.pool.hits", 0);
  T8_TELEMETRY_ADD("gpu.pool.misses", 0);
  T8_TELEMETRY_ADD("gpu.pool.evictions", 0);
  {
    T8_TELEMETRY_ADD("webgpu.uniform_upload.calls", 1);
    for (auto& upload : m_uniformUploads) {
      queue.WriteBuffer(upload.buffer, 0, upload.data.data(), upload.data.size());
      RuntimeTelemetry::RecordStaging(RuntimeTelemetry::UploadResource::Uniform, upload.data.size());
      Retire(std::move(upload.buffer), upload.capacity, wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);
      if (RuntimeTelemetry::IsFrameActive()) T8_TELEMETRY_ADD("webgpu.uniform_upload_calls", 1);
    }
    m_uniformUploads.clear();
    ++m_uniformEpoch;
  }
  {
    T8_TELEMETRY_SCOPE("gpu.submit");
    queue.Submit(1, &command);
  }
  T8_TELEMETRY_SCOPE("webgpu.resource_retirement");
  if (RuntimeTelemetry::IsFrameActive()) {
    T8_TELEMETRY_ADD("webgpu.retired_buffers", m_retiredBuffers.size());
    T8_TELEMETRY_ADD("webgpu.retired_textures", m_retiredTextures.size());
  }
  const auto health = m_health;
  const auto completion = queue.OnSubmittedWorkDone(wgpu::CallbackMode::WaitAnyOnly,
    [health](wgpu::QueueWorkDoneStatus status, wgpu::StringView) {
      if (status != wgpu::QueueWorkDoneStatus::Success) health->Fail("Queue submission failed");
    });
  m_submissions.push_back({completion, std::move(m_retiredBuffers)});
  for (const auto& texture : m_retiredTextures) texture.Destroy();
  m_retiredBuffers.clear();
  m_retiredTextures.clear();
}

void WebGPUContext::Shutdown() {
  commands = nullptr;
  std::exception_ptr failure;
  if (queue) {
    try { WaitForGPU(); }
    catch (...) { failure = std::current_exception(); }
  }
  backbuffer = nullptr;
  for (const auto& entry : m_freeBuffers) for (const auto& buffer : entry.second) buffer.Destroy();
  m_freeBuffers.clear();
  m_freeBufferBytes = 0;
  m_submissions.clear();
  for (const auto& upload : m_uniformUploads) upload.buffer.Destroy();
  m_uniformUploads.clear();
  ++m_uniformEpoch;
  m_presentTarget = nullptr;
  m_captureTarget = nullptr;
  m_presentPipeline = nullptr;
  depth = nullptr;
  if (surface && m_configured) surface.Unconfigure();
  m_configured = false;
  configuration = {};
  surface = nullptr;
  queue = nullptr;
  m_retiredBuffers.clear();
  m_retiredTextures.clear();
  if (device) device.Destroy();
  device = nullptr;
  adapter = nullptr;
  if (instance) instance.ProcessEvents();
#ifndef __EMSCRIPTEN__
  if (m_persistentCache) m_persistentCache->Report();
  m_persistentCache.reset();
#endif
  instance = nullptr;
  if (failure) std::rethrow_exception(failure);
  CheckHealth();
}
}
#endif