#include <video/webgpu/WebGPUContext.h>
#include <debug/RuntimeTelemetry.h>

#if (defined(_WIN32) && defined(_M_X64)) || defined(__EMSCRIPTEN__)
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
#include <cstring>
#include <exception>
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
#ifdef __EMSCRIPTEN__
  Require(instance.WaitAny(future, UINT64_MAX) == wgpu::WaitStatus::Success, "Browser GPU operation failed");
#else
  Require(instance.WaitAny(future, 30'000'000'000ULL) == wgpu::WaitStatus::Success, "GPU operation timed out");
#endif
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
  T8_LOG_INFO("[WebGPU] Device optional features: BC=%d float32-filterable=%d",
    device.HasFeature(wgpu::FeatureName::TextureCompressionBC) ? 1 : 0,
    device.HasFeature(wgpu::FeatureName::Float32Filterable) ? 1 : 0);
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
  instance = nullptr;
  if (failure) std::rethrow_exception(failure);
  CheckHealth();
}
}
#endif