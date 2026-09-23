#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <video/webgpu/WebGPUDriver.h>

#if (defined(_WIN32) && (defined(_M_X64) || defined(_M_ARM64))) || defined(__EMSCRIPTEN__)
#include <video/webgpu/WebGPUContext.h>
#include <core/Config.h>
#include <debug/RuntimeTelemetry.h>
#include <debug/GpuTimestampProfiler.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>
#include <utils/ShaderPermutationDump.h>
#include <utils/TextureMipmaps.h>
#include <SDL3/SDL.h>
#ifndef __EMSCRIPTEN__
#include <DirectXPackedVector.h>
#endif
#include <exception>
#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <map>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <tuple>

namespace t850 {
extern Device* T8Device;
extern DeviceContext* T8DeviceContext;
namespace {
void Require(bool success, const std::string& message) {
  if (!success) {
    T8_LOG_ERROR("[WebGPU] %s", message.c_str());
    throw std::runtime_error("[WebGPU] " + message);
  }
}
void Require(bool success, const char* message) {
  if (!success) Require(false, std::string(message));
}
struct WebGPUBufferData {
  wgpu::Buffer gpu;
  uint64_t size = 0;
  uint64_t offset = 0;
  uint64_t uniformEpoch = 0;
  const std::vector<char>* uniformShadow = nullptr;
};
class WebGPUTexture;
class WebGPUShader;
class WebGPUDevice;
class WebGPUDeviceContext;
class WebGPURT;
}

struct WebGPUDriverState {
  webgpu::WebGPUContext context;
  void* hwnd = nullptr;
  WebGPUDriver* driver = nullptr;
  std::unique_ptr<WebGPUDevice> device;
  std::unique_ptr<WebGPUDeviceContext> deviceContext;
  webgpu::ShaderFlow flow = webgpu::ShaderFlow::Auto;
  wgpu::RenderPassEncoder pass;
  std::vector<wgpu::Texture> targetColors;
  std::vector<wgpu::TextureFormat> targetFormats;
  wgpu::Texture targetDepth;
  WebGPUTexture* targetDepthResource = nullptr;
  wgpu::RenderPipeline depthSamplingCopyPipeline;
  uint32_t targetWidth = 0;
  uint32_t targetHeight = 0;
  std::array<float, 4> viewport{};
  std::array<uint32_t, 4> scissor{};
  bool active = false;
  bool offscreen = false;
  bool clearPending = false;
  wgpu::Color clearColor{0, 0, 0, 1};
  BaseDriver::BlendStates blend = BaseDriver::BLEND_OPAQUE;
  BaseDriver::DepthStencilStates depth = BaseDriver::READ_WRITE;
  wgpu::CullMode cull = wgpu::CullMode::None;
  wgpu::PrimitiveTopology topology = wgpu::PrimitiveTopology::TriangleList;
  wgpu::IndexFormat indexFormat = wgpu::IndexFormat::Uint32;
  unsigned stride = 0;
  unsigned vertexOffset = 0;
  unsigned indexOffset = 0;
  WebGPUBufferData* vertex = nullptr;
  WebGPUBufferData* index = nullptr;
  std::array<WebGPUBufferData*, 16> constants{};
  std::array<WebGPUTexture*, 32> textures{};
  std::array<WebGPUTexture*, 32> samplers{};
  WebGPUShader* shader = nullptr;
  unsigned draws = 0;
  uint64_t gpuProfileSubmissionSerial = 0;
  void EndPass() { if (pass) { pass.End(); pass = nullptr; } }
  void BeginPass();
  void SetTarget(std::vector<wgpu::Texture> colors, wgpu::Texture depthTexture, uint32_t width, uint32_t height, bool clear, WebGPUTexture* depthResource = nullptr);
  bool IsAttachment(const wgpu::Texture& texture) const {
    if (texture.Get() == targetDepth.Get()) return true;
    return std::any_of(targetColors.begin(), targetColors.end(), [&](const auto& color) { return color.Get() == texture.Get(); });
  }
  void Draw(unsigned count, unsigned firstIndex, unsigned firstVertex);
  std::vector<float> ReadColor(WebGPUTexture& texture);
  void SaveColor(WebGPUTexture& texture, const std::string& path);
  void ResetBindings();
};

namespace {
template<class Base, wgpu::BufferUsage Usage>
class WebGPUBuffer final : public Base, public WebGPUBufferData {
public:
  explicit WebGPUBuffer(WebGPUDriverState& state) : m_state(state) {}
  ~WebGPUBuffer() override {
    if constexpr (Usage != wgpu::BufferUsage::Uniform)
      m_state.context.Retire(std::move(gpu), (size + 3) & ~uint64_t(3), Usage | wgpu::BufferUsage::CopyDst);
  }
  void* GetAPIObject() const override { return gpu.Get(); }
  void** GetAPIObjectReference() const override { return nullptr; }
  void Create(const Device&, BufferDesc desc, void* initialData) override {
    T8_UPLOAD_SCOPE(UploadResourceType(), initialData ? desc.byteWidth : 0, 0);
    Require(desc.byteWidth > 0, "Buffer size must be positive");
    this->descriptor = desc;
    this->sysMemCpy.resize(desc.byteWidth);
    if (initialData) std::memcpy(this->sysMemCpy.data(), initialData, desc.byteWidth);
    Upload();
  }
  void UpdateFromSystemCopy(const DeviceContext&) override {
    T8_UPLOAD_SCOPE(UploadResourceType(), this->sysMemCpy.size(), 0);
    Upload();
  }
  void UpdateFromBuffer(const DeviceContext&, const void* data) override {
    T8_UPLOAD_SCOPE(UploadResourceType(), data ? this->descriptor.byteWidth : 0, 0);
    Require(data != nullptr, "Buffer update requires data");
    this->sysMemCpy.assign(static_cast<const char*>(data), static_cast<const char*>(data) + this->descriptor.byteWidth);
    Upload();
  }
  void release() override {
    if (m_state.vertex == this) m_state.vertex = nullptr;
    if (m_state.index == this) m_state.index = nullptr;
    for (auto& constant : m_state.constants) if (constant == this) constant = nullptr;
    delete this;
  }
  void Set(const DeviceContext& context, unsigned stride, unsigned offset) {
    m_state.vertex = this;
    m_state.stride = stride;
    m_state.vertexOffset = offset;
    const_cast<DeviceContext&>(context).actualVertexBuffer = dynamic_cast<VertexBuffer*>(this);
  }
  void Set(const DeviceContext& context, unsigned offset, IndexBufferFormat::E format) {
    m_state.index = this;
    m_state.indexOffset = offset;
    m_state.indexFormat = format == IndexBufferFormat::R16 ? wgpu::IndexFormat::Uint16 : wgpu::IndexFormat::Uint32;
    const_cast<DeviceContext&>(context).actualIndexBuffer = dynamic_cast<IndexBuffer*>(this);
  }
  void Set(const DeviceContext& context, unsigned slot = 0) {
    Require(slot < m_state.constants.size(), "Uniform slot exceeds driver limit");
    m_state.constants[slot] = this;
    const_cast<DeviceContext&>(context).actualConstantBuffer = dynamic_cast<ConstantBuffer*>(this);
  }
private:
  static constexpr RuntimeTelemetry::UploadResource UploadResourceType() {
    if constexpr (Usage == wgpu::BufferUsage::Uniform) return RuntimeTelemetry::UploadResource::Uniform;
    if constexpr (Usage == wgpu::BufferUsage::Vertex) return RuntimeTelemetry::UploadResource::Vertex;
    return RuntimeTelemetry::UploadResource::Index;
  }
  void Upload() {
    T8_TELEMETRY_ADD("webgpu.buffer_upload.calls", 1);
    if (RuntimeTelemetry::IsFrameActive()) {
      T8_TELEMETRY_ADD("webgpu.buffer_uploads", 1);
      T8_TELEMETRY_ADD("webgpu.buffer_upload_bytes", this->sysMemCpy.size());
    }
    Require(this->sysMemCpy.size() == static_cast<size_t>(this->descriptor.byteWidth), "Buffer shadow size mismatch");
    if constexpr (Usage == wgpu::BufferUsage::Uniform) {
      size = this->sysMemCpy.size();
      uniformShadow = &this->sysMemCpy;
      uniformEpoch = 0;
      return;
    }
    m_state.context.Retire(std::move(gpu), (size + 3) & ~uint64_t(3), Usage | wgpu::BufferUsage::CopyDst);
    size = this->sysMemCpy.size();
    wgpu::BufferDescriptor desc{};
    desc.size = (size + 3) & ~uint64_t(3);
    desc.usage = Usage | wgpu::BufferUsage::CopyDst;
    {
      T8_TELEMETRY_ADD("webgpu.create_buffer.calls", 1);
      gpu = m_state.context.AcquireBuffer(desc);
    }
    std::vector<char> aligned(desc.size);
    RuntimeTelemetry::RecordStaging(UploadResourceType(), desc.size);
    std::memcpy(aligned.data(), this->sysMemCpy.data(), size);
    {
      T8_TELEMETRY_ADD("webgpu.write_buffer.calls", 1);
      m_state.context.queue.WriteBuffer(gpu, 0, aligned.data(), aligned.size());
    }
  }
  WebGPUDriverState& m_state;
};

class WebGPUTexture final : public Texture {
public:
  explicit WebGPUTexture(WebGPUDriverState& state) : state(state) {}
  wgpu::Texture gpu;
  wgpu::TextureView view;
  wgpu::Sampler sampler;
  wgpu::Texture depthSamplingCopy;
  wgpu::TextureView depthSamplingView;
  bool depthSamplingDirty = true;
  bool unfilterableFloat = false;
  wgpu::TextureFormat format = wgpu::TextureFormat::RGBA8Unorm;
  WebGPUDriverState& state;
  bool RequiresFilteredDepth() const { return format == wgpu::TextureFormat::Depth32Float && state.context.device.HasFeature(wgpu::FeatureName::Float32Filterable) && (!(params & NEAREST_FILTER) || (params & CLAMP_TO_BORDER)); }
  bool RequiresNonFilteringBinding() const { return unfilterableFloat || (format == wgpu::TextureFormat::Depth32Float && !RequiresFilteredDepth()); }
  wgpu::TextureView SampleView() {
    if (!RequiresFilteredDepth()) return view;
    if (!depthSamplingCopy) {
      wgpu::TextureDescriptor descriptor{};
      descriptor.size = {x, y, 1};
      descriptor.format = wgpu::TextureFormat::R32Float;
      descriptor.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
      depthSamplingCopy = state.context.device.CreateTexture(&descriptor);
      depthSamplingView = depthSamplingCopy.CreateView();
    }
    if (!depthSamplingDirty) return depthSamplingView;
    state.EndPass();
    if (!state.depthSamplingCopyPipeline) {
      wgpu::ShaderSourceWGSL source{};
      source.code = R"(
@group(0) @binding(0) var depthSource: texture_depth_2d;
@vertex fn vertexMain(@builtin(vertex_index) index: u32) -> @builtin(position) vec4<f32> {
  let positions = array<vec2<f32>, 3>(vec2<f32>(-1, -1), vec2<f32>(3, -1), vec2<f32>(-1, 3));
  return vec4<f32>(positions[index], 0, 1);
}
@fragment fn fragmentMain(@builtin(position) position: vec4<f32>) -> @location(0) f32 {
  return textureLoad(depthSource, vec2<i32>(position.xy), 0);
}
)";
      wgpu::ShaderModuleDescriptor moduleDescriptor{};
      moduleDescriptor.nextInChain = &source;
      auto module = T8_TELEMETRY_CALL("shader.module.create", state.context.device.CreateShaderModule(&moduleDescriptor));
      wgpu::ColorTargetState color{};
      color.format = wgpu::TextureFormat::R32Float;
      wgpu::FragmentState fragment{};
      fragment.module = module; fragment.entryPoint = "fragmentMain"; fragment.targetCount = 1; fragment.targets = &color;
      wgpu::RenderPipelineDescriptor pipeline{};
      pipeline.vertex.module = module; pipeline.vertex.entryPoint = "vertexMain";
      pipeline.fragment = &fragment;
      state.depthSamplingCopyPipeline = T8_TELEMETRY_CALL("pipeline.create.graphics", state.context.device.CreateRenderPipeline(&pipeline));
    }
    wgpu::BindGroupEntry entry{};
    entry.binding = 0; entry.textureView = view;
    wgpu::BindGroupDescriptor binding{};
    binding.layout = state.depthSamplingCopyPipeline.GetBindGroupLayout(0);
    binding.entryCount = 1; binding.entries = &entry;
    auto group = state.context.device.CreateBindGroup(&binding);
    wgpu::RenderPassColorAttachment color{};
    color.view = depthSamplingView; color.loadOp = wgpu::LoadOp::Clear; color.storeOp = wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor passDescriptor{};
    passDescriptor.colorAttachmentCount = 1; passDescriptor.colorAttachments = &color;
    auto copy = state.context.commands.BeginRenderPass(&passDescriptor);
    copy.SetPipeline(state.depthSamplingCopyPipeline);
    copy.SetBindGroup(0, group);
    copy.Draw(3);
    copy.End();
    state.context.CheckHealth();
    depthSamplingDirty = false;
    return depthSamplingView;
  }
  void Allocate(int width, int height, wgpu::TextureFormat textureFormat, wgpu::TextureUsage usage, unsigned levels = 1, unsigned layers = 1) {
    Require(width > 0 && height > 0, "Texture dimensions must be positive");
    Require(layers == 1 || (layers == 6 && width == height), "Invalid cube texture dimensions");
    x = width; y = height; mipmaps = levels;
    format = textureFormat;
    unfilterableFloat = (format == wgpu::TextureFormat::R32Float || format == wgpu::TextureFormat::RGBA32Float) &&
      !state.context.device.HasFeature(wgpu::FeatureName::Float32Filterable);
    wgpu::TextureDescriptor desc{};
    desc.size = {x, y, layers};
    desc.mipLevelCount = levels;
    desc.format = format;
    desc.usage = usage;
    state.context.Retire(std::move(gpu));
    gpu = state.context.device.CreateTexture(&desc);
    wgpu::TextureViewDescriptor viewDesc{};
    viewDesc.dimension = layers == 6 ? wgpu::TextureViewDimension::Cube : wgpu::TextureViewDimension::e2D;
    view = gpu.CreateView(&viewDesc);
    SetTextureParams();
  }
  void Upload(const unsigned char* data, int width, int height, wgpu::TextureFormat textureFormat,
              unsigned bytesPerBlock, unsigned levels, unsigned layers, unsigned blockSize = 1) {
    Allocate(width, height, textureFormat, wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst, levels, layers);
    RuntimeTelemetry::RecordStaging(RuntimeTelemetry::UploadResource::Texture, 0, 1);
    size_t offset = 0;
    for (unsigned face = 0; face < layers; ++face) {
      for (unsigned mip = 0; mip < levels; ++mip) {
        const unsigned mipWidth = std::max(1u, x >> mip);
        const unsigned mipHeight = std::max(1u, y >> mip);
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = ((mipWidth + blockSize - 1) / blockSize) * bytesPerBlock;
        layout.rowsPerImage = (mipHeight + blockSize - 1) / blockSize;
        const size_t bytes = static_cast<size_t>(layout.bytesPerRow) * layout.rowsPerImage;
        std::vector<unsigned char> zeros;
        if (!data) zeros.resize(bytes);
        wgpu::TexelCopyTextureInfo destination{};
        destination.texture = gpu;
        destination.mipLevel = mip;
        destination.origin.z = face;
        wgpu::Extent3D extent{((mipWidth + blockSize - 1) / blockSize) * blockSize,
                              ((mipHeight + blockSize - 1) / blockSize) * blockSize, 1};
        state.context.queue.WriteTexture(&destination, data ? data + offset : zeros.data(), bytes, &layout, &extent);
        offset += bytes;
      }
    }
    state.context.CheckHealth();
  }
  void LoadAPITexture(DeviceContext*, unsigned char* data) override {
    T8_UPLOAD_SOURCE(RuntimeTelemetry::CurrentUploadSource() == RuntimeTelemetry::UploadSource::Streaming
      ? RuntimeTelemetry::UploadSource::Streaming : RuntimeTelemetry::UploadSource::AssetLoad);
    T8_UPLOAD_SCOPE(RuntimeTelemetry::UploadResource::Texture, data ? UploadByteSize() : 0, 0);
    const unsigned layers = (cil_props & CIL_CUBE_MAP) ? 6 : 1;
    unsigned levels = std::max(1u, mipmaps);
    if (cil_props & CIL_HALF_FLOAT) {
      Upload(data, x, y, wgpu::TextureFormat::RGBA16Float, 8, levels, layers);
      return;
    }
    Require(m_channels == 1 || m_channels == 3 || m_channels == 4, "Unsupported texture channels");
    size_t pixels = 0;
    for (unsigned mip = 0; mip < levels; ++mip) pixels += static_cast<size_t>(std::max(1u, x >> mip)) * std::max(1u, y >> mip) * layers;
    std::vector<uint8_t> rgba;
    if (m_channels == 3) {
      rgba.resize(pixels * 4, 255);
      RuntimeTelemetry::RecordStaging(RuntimeTelemetry::UploadResource::Texture, rgba.size());
      for (size_t pixel = 0; pixel < pixels; ++pixel) std::memcpy(rgba.data() + pixel * 4, data + pixel * 3, 3);
      data = rgba.data();
    }
    std::vector<unsigned char> generatedMips;
    if (levels == 1) {
      levels = CalculateFullMipCount(x, y);
      if (levels > 1) {
        GenerateMipChain8(data, x, y, layers, m_channels == 1 ? 1 : 4, generatedMips);
        data = generatedMips.data();
      }
    }
    Upload(data, x, y, m_channels == 1 ? wgpu::TextureFormat::R8Unorm : wgpu::TextureFormat::RGBA8Unorm,
           m_channels == 1 ? 1 : 4, levels, layers);
  }
  void LoadAPITextureCompressed(unsigned char* data) override {
    T8_UPLOAD_SOURCE(RuntimeTelemetry::CurrentUploadSource() == RuntimeTelemetry::UploadSource::Streaming
      ? RuntimeTelemetry::UploadSource::Streaming : RuntimeTelemetry::UploadSource::AssetLoad);
    T8_UPLOAD_SCOPE(RuntimeTelemetry::UploadResource::Texture, data ? UploadByteSize() : 0, 0);
    if (!state.context.device.HasFeature(wgpu::FeatureName::TextureCompressionBC)) {
      std::vector<unsigned char> rgba;
      const unsigned levels = std::max(1u, mipmaps);
      const unsigned faces = (cil_props & CIL_CUBE_MAP) ? 6 : 1;
      unsigned firstMip = 0;
      unsigned uploadWidth = x, uploadHeight = y;
      while (faces == 6 && firstMip + 1 < levels && firstMip < 31 && (uploadWidth > 512 || uploadHeight > 512)) {
        ++firstMip;
        uploadWidth = std::max(1u, uploadWidth >> 1);
        uploadHeight = std::max(1u, uploadHeight >> 1);
      }
      Require(DecompressDXTToRGBA(data, size, x, y, levels, faces, cil_props, rgba, firstMip), "Unsupported or invalid BC texture payload");
      T8_LOG_INFO("[WebGPU] BC unavailable; decoded %ux%u mips=%u faces=%u to RGBA8 (source=%ux%u firstMip=%u bytes=%zu)",
        uploadWidth, uploadHeight, levels - firstMip, faces, x, y, firstMip, rgba.size());
      cil_props = (cil_props & CIL_CUBE_MAP) | CIL_RGBA | CIL_RAW;
      props = TextBasicFormat::CH_RGBA;
      m_channels = 4;
      size = static_cast<unsigned int>(rgba.size());
      Upload(rgba.data(), uploadWidth, uploadHeight, wgpu::TextureFormat::RGBA8Unorm, 4, levels - firstMip, faces);
      return;
    }
    const auto textureFormat = (cil_props & CIL_DXT3) ? wgpu::TextureFormat::BC2RGBAUnorm
      : (cil_props & CIL_DXT5) ? wgpu::TextureFormat::BC3RGBAUnorm : wgpu::TextureFormat::BC1RGBAUnorm;
    Upload(data, x, y, textureFormat, textureFormat == wgpu::TextureFormat::BC1RGBAUnorm ? 8 : 16,
           std::max(1u, mipmaps), (cil_props & CIL_CUBE_MAP) ? 6 : 1, 4);
  }
  void UpdateFloatData(const DeviceContext&, int width, int height, const float* data) override {
    T8_UPLOAD_SCOPE(RuntimeTelemetry::UploadResource::Texture,
      data && width > 0 && height > 0 ? static_cast<uint64_t>(width) * height * 16 : 0, 0);
    Require(data && gpu && format == wgpu::TextureFormat::RGBA32Float && !(cil_props & CIL_CUBE_MAP)
      && mipmaps == 1 && width == static_cast<int>(x) && height == static_cast<int>(y), "Float texture update requires matching RGBA32F 2D data");
    Upload(reinterpret_cast<const unsigned char*>(data), width, height, format, 16, 1, 1);
  }
  void DestroyAPITexture() override {
    if (state.targetDepthResource == this) state.targetDepthResource = nullptr;
    for (auto& bound : state.textures) if (bound == this) bound = nullptr;
    for (auto& bound : state.samplers) if (bound == this) bound = nullptr;
    state.context.Retire(std::move(gpu));
    state.context.Retire(std::move(depthSamplingCopy));
    gpu = nullptr; view = nullptr; sampler = nullptr; depthSamplingCopy = nullptr; depthSamplingView = nullptr;
  }
  void SetTextureParams() override {
    wgpu::SamplerDescriptor desc{};
    desc.addressModeU = desc.addressModeV = (params & TILED) ? wgpu::AddressMode::Repeat : wgpu::AddressMode::ClampToEdge;
    desc.magFilter = desc.minFilter = (params & NEAREST_FILTER) && !(params & CLAMP_TO_BORDER) ? wgpu::FilterMode::Nearest : wgpu::FilterMode::Linear;
    desc.mipmapFilter = (params & (NEAREST_FILTER | LINEAR_FILTER)) && !(params & CLAMP_TO_BORDER) ? wgpu::MipmapFilterMode::Nearest : wgpu::MipmapFilterMode::Linear;
    desc.lodMaxClamp = (params & (NEAREST_FILTER | LINEAR_FILTER)) ? 0.0f : static_cast<float>(std::max(1u, mipmaps) - 1);
    desc.maxAnisotropy = !(params & (NEAREST_FILTER | LINEAR_FILTER | CLAMP_TO_BORDER)) && !(cil_props & CIL_CUBE_MAP) ? 16 : 1;
    if (RequiresNonFilteringBinding()) {
      desc.magFilter = desc.minFilter = wgpu::FilterMode::Nearest;
      desc.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
      desc.maxAnisotropy = 1;
    }
    sampler = state.context.device.CreateSampler(&desc);
  }
  void GetFormatBpp(unsigned int&, unsigned int& nativeFormat, unsigned int& bpp) override { nativeFormat = 0; bpp = 4; }
  void Set(const DeviceContext&, unsigned slot, std::string) override {
    Require(slot < state.textures.size(), "Texture slot exceeds driver limit");
    Require(!state.IsAttachment(gpu), "Cannot sample an active attachment");
    state.textures[slot] = this;
  }
  void SetSampler(const DeviceContext&, unsigned slot = 0) override {
    Require(slot < state.samplers.size(), "Sampler slot exceeds driver limit");
    state.samplers[slot] = this;
  }
};

#ifndef __EMSCRIPTEN__
bool RecordShaderPreparation(bool prepared, const webgpu::ShaderArtifact& artifact, const webgpu::ShaderFlowReport& report) {
#if T850_ENABLE_PROFILING
  static const auto translation = RuntimeTelemetry::RegisterScope("shader.translate.hlsl_spirv_wgsl");
  static const auto wgsl = RuntimeTelemetry::RegisterScope("shader.prepare.wgsl");
  const auto record = [&](bool cacheHit, double milliseconds, webgpu::ShaderSourceLanguage language, bool succeeded) {
    if (cacheHit) { T8_TELEMETRY_ADD("shader.cache.hits", 1); }
    else if (succeeded || milliseconds > 0) { T8_TELEMETRY_ADD("shader.cache.misses", 1); }
    if (!succeeded) { T8_TELEMETRY_ADD("shader.preparation.failures", 1); }
    if (milliseconds > 0)
      RuntimeTelemetry::RecordScope(language == webgpu::ShaderSourceLanguage::Wgsl ? wgsl : translation, milliseconds);
  };
  if (report.attempts.empty())
    record(artifact.cacheHit, artifact.translationMilliseconds, webgpu::ShaderSourceLanguage::Hlsl, prepared);
  else for (const auto& attempt : report.attempts)
    record(attempt.cacheHit, attempt.preparationMilliseconds, attempt.sourceLanguage, attempt.succeeded);
#endif
  return prepared;
}
#endif

class WebGPUShader final : public ShaderBase {
public:
  explicit WebGPUShader(WebGPUDriverState& state) : state(state) {}
  WebGPUDriverState& state;
  webgpu::ShaderArtifact vertex;
  webgpu::ShaderArtifact fragment;
  wgpu::ShaderModule vertexModule;
  wgpu::ShaderModule fragmentModule;
  struct ResourceLayout {
    wgpu::BindGroupLayout bindings;
    wgpu::PipelineLayout pipeline;
    wgpu::BindGroup group;
    std::vector<wgpu::BindGroupEntry> entries;
  };
  std::vector<wgpu::BindGroupLayoutEntry> bindingEntries;
  std::map<uint64_t, ResourceLayout> resourceLayouts;
  std::vector<webgpu::ShaderBinding> resources;
  using PipelineKey = std::tuple<uint64_t, unsigned, std::vector<wgpu::TextureFormat>, bool,
    wgpu::PrimitiveTopology, wgpu::IndexFormat, BaseDriver::BlendStates, BaseDriver::DepthStencilStates, wgpu::CullMode>;
  std::map<PipelineKey, wgpu::RenderPipeline> pipelines;
  std::array<bool, 16> dynamicUniforms{};
  bool CreateShaderAPI(std::string vertexSource, std::string fragmentSource, const std::string& vertexName, const std::string& fragmentName) override {
    for (const bool isVertex : {true, false}) {
      webgpu::ShaderFileRequest request;
      request.name = isVertex ? vertexName : fragmentName;
      request.stage = isVertex ? webgpu::ShaderStage::Vertex : webgpu::ShaderStage::Fragment;
      request.entryPoint = isVertex ? "VS" : "FS";
      request.defines = m_sourceDefines;
      request.keyBits = key.bits;
      request.flow = state.flow;
      webgpu::ShaderFlowReport report;
      std::string diagnostic;
      auto& artifact = isVertex ? vertex : fragment;
      webgpu::ShaderRequest packagedRequest;
      packagedRequest.name = request.name;
      packagedRequest.stage = request.stage;
      packagedRequest.entryPoint = request.entryPoint;
      packagedRequest.defines = request.defines;
      packagedRequest.keyBits = request.keyBits;
      packagedRequest.source = isVertex ? vertexSource : fragmentSource;
    #ifdef __EMSCRIPTEN__
      Require(T8_TELEMETRY_CALL("shader.package.load", webgpu::ReadShaderPackage(packagedRequest, artifact, state.flow, report, diagnostic)), diagnostic);
      T8_TELEMETRY_ADD("shader.packaged.stages", 1);
    #else
      if (request.name.empty()) {
        Require(state.flow != webgpu::ShaderFlow::Wgsl, "Anonymous HLSL shader has no direct WGSL source");
        webgpu::ShaderRequest inlineRequest;
        inlineRequest.name = isVertex ? "inline-vertex" : "inline-fragment";
        inlineRequest.stage = request.stage;
        inlineRequest.entryPoint = request.entryPoint;
        inlineRequest.keyBits = key.bits;
        inlineRequest.source = isVertex ? vertexSource : fragmentSource;
        Require(RecordShaderPreparation(T8_TELEMETRY_CALL("shader.load", webgpu::LoadOrTranslateShader(inlineRequest, artifact, diagnostic)), artifact, report), diagnostic);
        T8_LOG_INFO("[WebGPU] shader=%s actual=spirv inline-HLSL hit=%d", inlineRequest.name.c_str(), artifact.cacheHit);
      } else {
        if (!std::filesystem::path(request.name).has_parent_path()) request.name = "Shaders/" + request.name;
        Require(RecordShaderPreparation(T8_TELEMETRY_CALL("shader.load", webgpu::LoadShaderFiles(request, artifact, report, diagnostic)), artifact, report), diagnostic);
        T8_LOG_INFO("[WebGPU] shader=%s flow=%s actual=%s hit=%d fallback=%d prepare_ms=%.3f",
          request.name.c_str(), webgpu::ShaderFlowName(request.flow),
          report.attempts.back().sourceLanguage == webgpu::ShaderSourceLanguage::Wgsl ? "wgsl" : "spirv",
          artifact.cacheHit, report.fallbackAttempted, report.elapsedMilliseconds);
      }
      if (!g_config.webShaderOutput.empty())
        Require(webgpu::WriteShaderPackage(packagedRequest, artifact, state.flow, report, g_config.webShaderOutput, diagnostic), diagnostic);
#endif
      if (!diagnostic.empty()) T8_LOG_INFO("[WebGPU] shader warning: %s", diagnostic.c_str());
      const bool comparisonSampler = std::any_of(artifact.bindings.begin(), artifact.bindings.end(),
        [](const auto& binding) { return binding.comparisonSampler; });
      if (!state.driver->ValidateShaderComparisonSamplers(comparisonSampler,
          request.name.empty() ? "inline" : request.name, isVertex ? "vertex" : "fragment", key.bits, diagnostic)) {
        T8_LOG_ERROR("%s", diagnostic.c_str());
        return false;
      }
      wgpu::ShaderSourceWGSL source{};
      source.code = artifact.wgsl.c_str();
      wgpu::ShaderModuleDescriptor descriptor{};
      descriptor.nextInChain = &source;
      const auto label = request.name + " key=" + std::to_string(request.keyBits);
      descriptor.label = label.c_str();
      (isVertex ? vertexModule : fragmentModule) = T8_TELEMETRY_CALL("shader.module.create", state.context.device.CreateShaderModule(&descriptor));
#ifdef __EMSCRIPTEN__
      std::string compilationError;
      const auto future = (isVertex ? vertexModule : fragmentModule).GetCompilationInfo(wgpu::CallbackMode::WaitAnyOnly,
        [&compilationError, &label](wgpu::CompilationInfoRequestStatus status, const wgpu::CompilationInfo* info) {
          if (status != wgpu::CompilationInfoRequestStatus::Success || !info) {
            compilationError = "Cannot retrieve shader compilation information: " + label;
            return;
          }
          for (size_t messageIndex = 0; messageIndex < info->messageCount; ++messageIndex) {
            const auto& message = info->messages[messageIndex];
            if (message.type != wgpu::CompilationMessageType::Error) continue;
            const auto length = message.message.length == WGPU_STRLEN ? std::strlen(message.message.data) : message.message.length;
            compilationError += label + ":" + std::to_string(message.lineNum) + ":" +
              std::to_string(message.linePos) + " " + std::string(message.message.data, length) + "\n";
          }
        });
      Require(state.context.instance.WaitAny(future, UINT64_MAX) == wgpu::WaitStatus::Success, "Shader compilation callback failed");
      Require(compilationError.empty(), compilationError);
#endif
    }
    for (const auto& input : fragment.inputs)
      Require(std::find(vertex.outputs.begin(), vertex.outputs.end(), input) != vertex.outputs.end(), "Shader stage interfaces do not match");
    std::map<uint32_t, wgpu::BindGroupLayoutEntry> entries;
    for (const bool isVertex : {true, false}) {
      for (const auto& binding : (isVertex ? vertex : fragment).bindings) {
        Require(binding.group == 0, "Current binding policy supports bind group zero only");
        auto found = std::find_if(resources.begin(), resources.end(), [&](const auto& existing) { return existing.binding == binding.binding; });
        if (found == resources.end()) resources.push_back(binding);
        else {
          Require(found->kind == binding.kind && found->dimension == binding.dimension, "Cross-stage binding type mismatch");
          found->minimumBufferSize = std::max(found->minimumBufferSize, binding.minimumBufferSize);
        }
        auto& entry = entries[binding.binding];
        entry.binding = binding.binding;
        entry.visibility |= isVertex ? wgpu::ShaderStage::Vertex : wgpu::ShaderStage::Fragment;
        if (binding.kind == webgpu::ResourceKind::UniformBuffer) {
          entry.buffer.type = wgpu::BufferBindingType::Uniform;
          entry.buffer.minBindingSize = std::max(entry.buffer.minBindingSize, binding.minimumBufferSize);
        } else if (binding.kind == webgpu::ResourceKind::Sampler) {
          entry.sampler.type = wgpu::SamplerBindingType::Filtering;
        } else {
          Require(binding.kind == webgpu::ResourceKind::SampledTexture
            && (binding.dimension == webgpu::TextureDimension::D2 || binding.dimension == webgpu::TextureDimension::Cube)
            && binding.sampledType == webgpu::ShaderComponentType::Float, "Only float 2D and cube sampled textures are supported");
          entry.texture.sampleType = wgpu::TextureSampleType::Float;
          entry.texture.viewDimension = binding.dimension == webgpu::TextureDimension::Cube ? wgpu::TextureViewDimension::Cube : wgpu::TextureViewDimension::e2D;
        }
      }
    }
    unsigned dynamicUniformCount = 0;
    for (auto& [slot, entry] : entries) {
      if (entry.buffer.type == wgpu::BufferBindingType::Uniform && slot >= 64 && slot < 80 && dynamicUniformCount < 8) {
        entry.buffer.hasDynamicOffset = true;
        dynamicUniforms[slot - 64] = true;
        ++dynamicUniformCount;
      }
      bindingEntries.push_back(entry);
    }
    std::sort(resources.begin(), resources.end(), [](const auto& left, const auto& right) { return left.binding < right.binding; });
    state.context.CheckHealth();
    return true;
  }
  uint64_t ResourceLayoutKey() const {
    uint64_t layoutKey = 0;
    for (unsigned slot = 0; slot < state.textures.size(); ++slot) {
      if (state.textures[slot] && state.textures[slot]->RequiresNonFilteringBinding()) layoutKey |= uint64_t(1) << slot;
      if (state.samplers[slot] && state.samplers[slot]->RequiresNonFilteringBinding()) layoutKey |= uint64_t(1) << (slot + 32);
    }
    return layoutKey;
  }
  ResourceLayout& GetResourceLayout(uint64_t layoutKey) {
    auto& result = resourceLayouts[layoutKey];
    if (result.pipeline) return result;
    auto nativeEntries = bindingEntries;
    for (auto& entry : nativeEntries) {
      if (entry.texture.sampleType != wgpu::TextureSampleType::BindingNotUsed && entry.binding < 32 && (layoutKey & (uint64_t(1) << entry.binding)))
        entry.texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
      if (entry.sampler.type != wgpu::SamplerBindingType::BindingNotUsed && entry.binding >= 32 && entry.binding < 64 && (layoutKey & (uint64_t(1) << entry.binding)))
        entry.sampler.type = wgpu::SamplerBindingType::NonFiltering;
    }
    wgpu::BindGroupLayoutDescriptor bindingDesc{};
    bindingDesc.entryCount = nativeEntries.size();
    bindingDesc.entries = nativeEntries.data();
    result.bindings = state.context.device.CreateBindGroupLayout(&bindingDesc);
    wgpu::PipelineLayoutDescriptor layoutDesc{};
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &result.bindings;
    result.pipeline = state.context.device.CreatePipelineLayout(&layoutDesc);
    state.context.CheckHealth();
    return result;
  }
  void Set(const DeviceContext& context) override { state.shader = this; const_cast<DeviceContext&>(context).actualShaderSet = this; }
  void DestroyAPIShader() override {
    if (state.shader == this) state.shader = nullptr;
    pipelines.clear(); resourceLayouts.clear(); bindingEntries.clear(); vertexModule = nullptr; fragmentModule = nullptr;
  }
};

class WebGPURT final : public BaseRT {
public:
  explicit WebGPURT(WebGPUDriverState& state) : state(state) {}
  WebGPUDriverState& state;
  bool LoadAPIRT() override {
    std::string diagnostic;
    if (!state.driver->ValidateRenderTarget(number_RT, color_format, depth_format, w, h, GenMips, perColorFormats, diagnostic)) {
      T8_LOG_ERROR("%s", diagnostic.c_str());
      return false;
    }
    std::vector<std::unique_ptr<WebGPUTexture>> colors;
    for (int attachment = 0; attachment < number_RT; ++attachment) {
      const int format = perColorFormats.empty() ? color_format : perColorFormats[attachment];
      wgpu::TextureFormat nativeFormat;
      switch (format) {
      case RGBA8: case RGB8: nativeFormat = wgpu::TextureFormat::RGBA8Unorm; break;
      case RGBA16F: nativeFormat = wgpu::TextureFormat::RGBA16Float; break;
      case RGBA32F: nativeFormat = wgpu::TextureFormat::RGBA32Float; break;
      case R8: nativeFormat = wgpu::TextureFormat::R8Unorm; break;
      case F16: nativeFormat = wgpu::TextureFormat::R16Float; break;
      case F32: nativeFormat = wgpu::TextureFormat::R32Float; break;
      default: return false;
      }
      auto color = std::make_unique<WebGPUTexture>(state);
      color->m_channels = format == R8 || format == F16 || format == F32 ? 1 : 4;
      color->params = CLAMP_TO_EDGE | NEAREST_FILTER;
      wgpu::TextureUsage usage = wgpu::TextureUsage::RenderAttachment |
        wgpu::TextureUsage::TextureBinding |
        wgpu::TextureUsage::CopySrc;
      if (AllowUnorderedAccess) usage |= wgpu::TextureUsage::StorageBinding;
      color->Allocate(w, h, nativeFormat, usage);
      colors.push_back(std::move(color));
    }
    std::unique_ptr<WebGPUTexture> depth;
    if (depth_format != NOTHING) {
      depth = std::make_unique<WebGPUTexture>(state);
      depth->params = CLAMP_TO_BORDER;
      depth->Allocate(w, h, wgpu::TextureFormat::Depth32Float,
        wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc);
    }
    state.context.CheckHealth();
    for (auto& color : colors) vColorTextures.push_back(color.release());
    pDepthTexture = depth.release();
    return true;
  }
  void DestroyAPIRT() override {
    for (auto* texture : vColorTextures) texture->release();
    vColorTextures.clear();
    if (pDepthTexture) { pDepthTexture->release(); pDepthTexture = nullptr; }
  }
  void Set(const DeviceContext&) override { Bind(true); }
  void SetLoad(const DeviceContext&) override { Bind(false); }
  void ChangeCubeDepthTexture(int) override {
    T8_LOG_ERROR("[UnsupportedRenderTarget] backend=webgpu feature=cube render targets");
  }
  void Bind(bool clear) const {
    std::vector<wgpu::Texture> colors;
    for (auto* color : vColorTextures) colors.push_back(static_cast<WebGPUTexture*>(color)->gpu);
    const auto depth = pDepthTexture ? static_cast<WebGPUTexture*>(pDepthTexture)->gpu : wgpu::Texture{};
    state.SetTarget(std::move(colors), depth, w, h, clear, static_cast<WebGPUTexture*>(pDepthTexture));
  }
};

bool ComputeBindingMatches(ComputeBindingType type, webgpu::ResourceKind kind) {
  switch (type) {
  case ComputeBindingType::Constants32:
    return kind == webgpu::ResourceKind::UniformBuffer;
  case ComputeBindingType::ReadOnlyBuffer:
    return kind == webgpu::ResourceKind::ReadOnlyStorageBuffer;
  case ComputeBindingType::ReadWriteBuffer:
    return kind == webgpu::ResourceKind::ReadWriteStorageBuffer;
  case ComputeBindingType::ReadOnlyTexture:
    return kind == webgpu::ResourceKind::SampledTexture;
  case ComputeBindingType::ReadWriteTexture:
    return kind == webgpu::ResourceKind::WriteOnlyStorageTexture;
  case ComputeBindingType::Sampler:
    return kind == webgpu::ResourceKind::Sampler;
  }
  return false;
}

class WebGPUComputePipeline final : public ComputePipeline {
public:
  explicit WebGPUComputePipeline(WebGPUDriverState& state) : state(state) {}
  WebGPUDriverState& state;
  std::vector<ComputeBindingLayoutDesc> bindings;
  std::map<uint32_t, webgpu::ShaderBinding> resources;
  webgpu::ShaderArtifact artifact;
  wgpu::ShaderModule module;
  wgpu::BindGroupLayout bindGroupLayout;
  wgpu::PipelineLayout pipelineLayout;
  wgpu::ComputePipeline pipeline;
  std::string entryPoint;
  std::vector<wgpu::BindGroupLayoutEntry> bindingEntries;
  struct Variant {
    wgpu::BindGroupLayout bindings;
    wgpu::PipelineLayout layout;
    wgpu::ComputePipeline pipeline;
  };
  std::map<uint64_t, Variant> variants;

  Variant& GetVariant(uint64_t nonFiltering) {
    auto& variant = variants[nonFiltering];
    if (variant.pipeline) return variant;
    auto entries = bindingEntries;
    for (auto& entry : entries) {
      if (entry.binding < 64 && (nonFiltering & (uint64_t(1) << entry.binding))) {
        if (entry.texture.sampleType != wgpu::TextureSampleType::BindingNotUsed)
          entry.texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
        if (entry.sampler.type != wgpu::SamplerBindingType::BindingNotUsed)
          entry.sampler.type = wgpu::SamplerBindingType::NonFiltering;
      }
    }
    wgpu::BindGroupLayoutDescriptor binding{};
    binding.entryCount = entries.size(); binding.entries = entries.data();
    variant.bindings = state.context.device.CreateBindGroupLayout(&binding);
    wgpu::PipelineLayoutDescriptor layout{};
    layout.bindGroupLayoutCount = 1; layout.bindGroupLayouts = &variant.bindings;
    variant.layout = state.context.device.CreatePipelineLayout(&layout);
    wgpu::ComputePipelineDescriptor descriptor{};
    descriptor.layout = variant.layout;
    descriptor.compute.module = module;
    descriptor.compute.entryPoint = entryPoint.c_str();
    variant.pipeline = T8_TELEMETRY_CALL("pipeline.create.compute", state.context.device.CreateComputePipeline(&descriptor));
    T8_TELEMETRY_ADD("gpu.pipeline_creations", 1);
    state.context.CheckHealth();
    return variant;
  }

  bool Create(const ComputePipelineDesc& desc) {
    try {
      Require(!desc.source.empty() && !desc.entryPoint.empty(), "Compute shader source and entry point are required");
      webgpu::ShaderFileRequest request;
      request.name = desc.debugName.empty() ? "Shaders/inline-compute.hlsl" : desc.debugName;
      if (!std::filesystem::path(request.name).has_parent_path()) request.name = "Shaders/" + request.name;
      request.stage = webgpu::ShaderStage::Compute;
      request.layout = webgpu::BindingLayout::ComputeV1;
      request.entryPoint = desc.entryPoint;
      std::ostringstream defines;
      for (const std::string& define : desc.defines)
        if (!define.empty()) defines << "#define " << define << '\n';
      request.defines = defines.str();
      request.flow = state.flow;
      webgpu::ShaderFlowReport report;
      std::string diagnostic;
      webgpu::ShaderRequest packagedRequest;
      packagedRequest.name = request.name;
      packagedRequest.stage = request.stage;
      packagedRequest.layout = request.layout;
      packagedRequest.entryPoint = request.entryPoint;
      packagedRequest.defines = request.defines;
      packagedRequest.source = desc.source;
    #ifdef __EMSCRIPTEN__
      Require(T8_TELEMETRY_CALL("shader.package.load", webgpu::ReadShaderPackage(packagedRequest, artifact, state.flow, report, diagnostic)), diagnostic);
      T8_TELEMETRY_ADD("shader.packaged.stages", 1);
    #else
      Require(RecordShaderPreparation(T8_TELEMETRY_CALL("shader.load", webgpu::LoadShaderFiles(request, artifact, report, diagnostic, desc.source)), artifact, report),
              diagnostic.empty() ? "Compute shader preparation failed" : diagnostic);
      if (!g_config.webShaderOutput.empty())
        Require(webgpu::WriteShaderPackage(packagedRequest, artifact, state.flow, report, g_config.webShaderOutput, diagnostic), diagnostic);
    #endif
      if (!diagnostic.empty()) T8_LOG_INFO("[WebGPU][Compute] shader warning: %s", diagnostic.c_str());
      const bool comparisonSampler = std::any_of(artifact.bindings.begin(), artifact.bindings.end(),
        [](const auto& binding) { return binding.comparisonSampler; });
      if (!state.driver->ValidateShaderComparisonSamplers(comparisonSampler, request.name, "compute", request.keyBits, diagnostic)) {
        T8_LOG_ERROR("%s", diagnostic.c_str());
        return false;
      }
      Require(!artifact.wgsl.empty(), "Compute shader artifact is empty");
      Require(artifact.workgroupSize[0] && artifact.workgroupSize[1] && artifact.workgroupSize[2],
              "Compute shader has an invalid workgroup size");
      Require(artifact.bindings.size() == desc.bindings.size(),
              "Compute binding declaration count does not match shader reflection");

      std::map<uint32_t, ComputeBindingLayoutDesc> declared;
      std::vector<ComputeBindingLayoutDesc> reflected;
      std::vector<wgpu::BindGroupLayoutEntry> layoutEntries;
      for (const ComputeBindingLayoutDesc& binding : desc.bindings) {
        Require(binding.bindingIndex < 64, "Compute binding index exceeds layout key capacity");
        Require(declared.emplace(binding.bindingIndex, binding).second,
                "Duplicate compute bind-group binding " + std::to_string(binding.bindingIndex));
      }
      for (const webgpu::ShaderBinding& resource : artifact.bindings) {
        Require(resource.group == 0, "Compute shaders support bind group zero only");
        const auto expected = declared.find(resource.binding);
        Require(expected != declared.end() && ComputeBindingMatches(expected->second.type, resource.kind),
                "Compute binding " + std::to_string(resource.binding) + " does not match its declaration");
        if (expected->second.type == ComputeBindingType::Constants32) {
          Require(expected->second.constantCount &&
                  resource.minimumBufferSize == static_cast<uint64_t>(expected->second.constantCount) * sizeof(uint32_t),
                  "Compute constant layout does not match shader reflection");
        }

        wgpu::BindGroupLayoutEntry entry{};
        entry.binding = resource.binding;
        entry.visibility = wgpu::ShaderStage::Compute;
        switch (resource.kind) {
        case webgpu::ResourceKind::UniformBuffer:
          entry.buffer.type = wgpu::BufferBindingType::Uniform;
          entry.buffer.minBindingSize = resource.minimumBufferSize;
          break;
        case webgpu::ResourceKind::ReadOnlyStorageBuffer:
          entry.buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
          entry.buffer.minBindingSize = resource.minimumBufferSize;
          break;
        case webgpu::ResourceKind::ReadWriteStorageBuffer:
          entry.buffer.type = wgpu::BufferBindingType::Storage;
          entry.buffer.minBindingSize = resource.minimumBufferSize;
          break;
        case webgpu::ResourceKind::SampledTexture:
          Require(resource.dimension == webgpu::TextureDimension::D2 &&
                  resource.sampledType == webgpu::ShaderComponentType::Float,
                  "Compute sampled textures must be float 2D textures");
          entry.texture.sampleType = wgpu::TextureSampleType::Float;
          entry.texture.viewDimension = wgpu::TextureViewDimension::e2D;
          break;
        case webgpu::ResourceKind::Sampler:
          entry.sampler.type = wgpu::SamplerBindingType::Filtering;
          break;
        case webgpu::ResourceKind::WriteOnlyStorageTexture:
          Require(resource.dimension == webgpu::TextureDimension::D2,
                  "Compute storage textures must be 2D");
          Require(resource.storageRgba8Unorm || resource.storageRgba16Float,
                  "Compute storage texture format must be rgba8unorm or rgba16float");
          entry.storageTexture.access = wgpu::StorageTextureAccess::WriteOnly;
          entry.storageTexture.format = resource.storageRgba16Float
            ? wgpu::TextureFormat::RGBA16Float : wgpu::TextureFormat::RGBA8Unorm;
          entry.storageTexture.viewDimension = wgpu::TextureViewDimension::e2D;
          break;
        case webgpu::ResourceKind::DepthTexture:
          T8_LOG_ERROR("[UnsupportedShaderFeature] backend=webgpu shader=%s stage=compute feature=depth_texture", request.name.c_str());
          return false;
        }
        resources.emplace(resource.binding, resource);
        auto reflectedBinding = expected->second;
        if (resource.kind == webgpu::ResourceKind::WriteOnlyStorageTexture)
          reflectedBinding.storageFormat = resource.storageRgba16Float
            ? ComputeStorageFormat::Rgba16Float : ComputeStorageFormat::Rgba8Unorm;
        reflected.push_back(reflectedBinding);
        layoutEntries.push_back(entry);
      }
      Require(SetValidatedLayout(desc, reflected, true), "Invalid compute binding layout");

      wgpu::ShaderSourceWGSL source{};
      source.code = artifact.wgsl.c_str();
      wgpu::ShaderModuleDescriptor moduleDescriptor{};
      moduleDescriptor.nextInChain = &source;
      module = T8_TELEMETRY_CALL("shader.module.create", state.context.device.CreateShaderModule(&moduleDescriptor));
      bindingEntries = std::move(layoutEntries);
      entryPoint = desc.entryPoint;
      auto& variant = GetVariant(0);
      bindGroupLayout = variant.bindings;
      pipelineLayout = variant.layout;
      pipeline = variant.pipeline;
      state.context.CheckHealth();
      Require(static_cast<bool>(pipeline), "Compute pipeline creation failed");
      bindings = desc.bindings;
      threadGroupSize = artifact.workgroupSize;
      ShaderPermutationDump::RecordCompute(desc.debugName, desc.entryPoint,
                                            desc.permutationName, desc.defines);
      const char* flow = report.attempts.empty() ? "spirv" :
        (report.attempts.back().sourceLanguage == webgpu::ShaderSourceLanguage::Wgsl ? "wgsl" : "spirv");
      T8_LOG_INFO("[WebGPU][Compute] Pipeline '%s' created (flow=%s threads=%ux%ux%u bindings=%zu)",
                  desc.debugName.c_str(), flow, artifact.workgroupSize[0], artifact.workgroupSize[1],
                  artifact.workgroupSize[2], bindings.size());
      return true;
    } catch (const std::exception& error) {
      T8_LOG_ERROR("[WebGPU][Compute] Pipeline '%s' failed: %s", desc.debugName.c_str(), error.what());
      return false;
    }
  }
};

class WebGPUComputeBuffer final : public ComputeBuffer {
public:
  explicit WebGPUComputeBuffer(WebGPUDriverState& state) : state(state) {}
  ~WebGPUComputeBuffer() override {
    state.context.Retire(std::move(gpu), allocationSize,
      wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst);
  }
  WebGPUDriverState& state;
  wgpu::Buffer gpu;
  uint64_t allocationSize = 0;

  bool Create(const ComputeBufferDesc& desc, const void* initialData) {
    if (!desc.byteWidth || !desc.structureStride ||
        (desc.byteWidth % desc.structureStride) != 0 ||
        (desc.byteWidth % sizeof(uint32_t)) != 0 ||
        (desc.structureStride % sizeof(uint32_t)) != 0) return false;
    descriptor = desc;
    allocationSize = desc.byteWidth;
    wgpu::BufferDescriptor bufferDescriptor{};
    bufferDescriptor.size = allocationSize;
    bufferDescriptor.usage = wgpu::BufferUsage::Storage |
                             wgpu::BufferUsage::CopySrc |
                             wgpu::BufferUsage::CopyDst;
    gpu = state.context.AcquireBuffer(bufferDescriptor);
    if (!gpu) return false;
    std::vector<unsigned char> zeros;
    if (!initialData) zeros.resize(desc.byteWidth);
    state.context.queue.WriteBuffer(gpu, 0, initialData ? initialData : zeros.data(), desc.byteWidth);
    state.context.CheckHealth();
    return true;
  }
};

class WebGPUDevice final : public Device {
public:
  explicit WebGPUDevice(WebGPUDriverState& state) : state(state) {}
  WebGPUDriverState& state;
  void* GetAPIObject() const override { return state.context.device.Get(); }
  void** GetAPIObjectReference() const override { return nullptr; }
  void release() override {}
  Buffer* CreateBuffer(BufferType::E type, BufferDesc desc, void* data) override {
    std::unique_ptr<Buffer> result;
    if (type == BufferType::VERTEX) result = std::make_unique<WebGPUBuffer<VertexBuffer, wgpu::BufferUsage::Vertex>>(state);
    else if (type == BufferType::INDEX) result = std::make_unique<WebGPUBuffer<IndexBuffer, wgpu::BufferUsage::Index>>(state);
    else if (type == BufferType::CONSTANT) result = std::make_unique<WebGPUBuffer<ConstantBuffer, wgpu::BufferUsage::Uniform>>(state);
    Require(static_cast<bool>(result), "Unsupported buffer type");
    result->Create(*this, desc, data);
    return result.release();
  }
  ShaderBase* CreateShader(std::string vertex, std::string fragment, ShaderKey key, const std::string& vertexName, const std::string& fragmentName) override {
    auto shader = std::make_unique<WebGPUShader>(state);
    if (!shader->CreateShader(vertex, fragment, key, vertexName, fragmentName)) return nullptr;
    return shader.release();
  }
  Texture* CreateTexture(std::string path) override {
    auto texture = std::make_unique<WebGPUTexture>(state);
    Require(texture->LoadTexture(path.c_str()), "Texture loading failed: " + path);
    return texture.release();
  }
  Texture* CreateTextureFromMemory(const unsigned char* data, int width, int height, int channels, std::string name) override {
    auto texture = std::make_unique<WebGPUTexture>(state);
    Require(texture->LoadFromMemory(data, width, height, channels, name.c_str()), "Texture upload failed");
    return texture.release();
  }
  Texture* CreateCubeMap(const unsigned char* data, int width, int height) override {
    auto texture = std::make_unique<WebGPUTexture>(state);
    Require(texture->CreateCubeMap(data, width, height), "Cube texture upload failed");
    return texture.release();
  }
  Texture* CreateFloatTexture(int width, int height, const float* data) override { return CreateFloat(width, height, 1, 1, data); }
  Texture* CreateFloatCubeMap(int size, int mipCount, const float* data) override { return CreateFloat(size, size, mipCount, 6, data); }
  Texture* CreateFloat(int width, int height, int mipCount, unsigned layers, const float* data) {
    T8_UPLOAD_SCOPE(RuntimeTelemetry::UploadResource::Texture,
      data && width > 0 && height > 0 && mipCount > 0
        ? RuntimeTelemetry::TextureUploadBytes(width, height, mipCount, layers, 16) : 0, 0);
    Require(width > 0 && height > 0 && mipCount > 0 && static_cast<unsigned>(mipCount) <= CalculateFullMipCount(width, height), "Invalid float texture dimensions or mip count");
    auto texture = std::make_unique<WebGPUTexture>(state);
    texture->m_channels = 4;
    texture->props = TextBasicFormat::CH_RGBA;
    texture->cil_props = layers == 6 ? CIL_CUBE_MAP : 0;
    texture->params = CLAMP_TO_EDGE | (layers == 6 ? MIPMAPS : NEAREST_FILTER);
    if (layers == 6 && !state.context.device.HasFeature(wgpu::FeatureName::Float32Filterable)) {
      size_t channels = 0;
      for (int mip = 0; mip < mipCount; ++mip) channels += static_cast<size_t>(std::max(1, width >> mip)) * std::max(1, height >> mip) * layers * 4;
      std::vector<uint16_t> half(channels);
      if (data) std::transform(data, data + channels, half.begin(), FloatToHalf);
      texture->Upload(reinterpret_cast<const unsigned char*>(half.data()), width, height, wgpu::TextureFormat::RGBA16Float, 8, mipCount, layers);
      T8_LOG_INFO("[WebGPU] Float32 filtering unavailable; uploaded float cubemap as RGBA16F");
    } else {
      texture->Upload(reinterpret_cast<const unsigned char*>(data), width, height, wgpu::TextureFormat::RGBA32Float, 16, mipCount, layers);
    }
    return texture.release();
  }
  BaseRT* CreateRT(int count, int color, int depth, int width, int height,
                   bool mips, bool allowStorage) override {
    auto target = std::make_unique<WebGPURT>(state);
    target->AllowUnorderedAccess = allowStorage;
    if (!target->LoadRT(count, color, depth, width, height, mips)) return nullptr;
    return target.release();
  }
};

class WebGPUDeviceContext final : public DeviceContext {
public:
  explicit WebGPUDeviceContext(WebGPUDriverState& state) : state(state) {
    actualConstantBuffer = nullptr; actualIndexBuffer = nullptr; actualVertexBuffer = nullptr; actualShaderSet = nullptr;
  }
  WebGPUDriverState& state;
  void* GetAPIObject() const override { return state.context.commands.Get(); }
  void** GetAPIObjectReference() const override { return nullptr; }
  void release() override { state.EndPass(); }
  void SetPrimitiveTopology(Topology::E topology) override {
    switch (topology) {
    case Topology::TRIANLE_LIST: state.topology = wgpu::PrimitiveTopology::TriangleList; break;
    case Topology::TRIANGLE_STRIP: state.topology = wgpu::PrimitiveTopology::TriangleStrip; break;
    case Topology::LINE_LIST: state.topology = wgpu::PrimitiveTopology::LineList; break;
    case Topology::LINE_STRIP: state.topology = wgpu::PrimitiveTopology::LineStrip; break;
    case Topology::POINT_LIST: state.topology = wgpu::PrimitiveTopology::PointList; break;
    default: throw std::runtime_error("[WebGPU] Unknown primitive topology");
    }
  }
  void DrawIndexed(unsigned count, unsigned firstIndex, unsigned firstVertex) override { state.Draw(count, firstIndex, firstVertex); }
};
}

void WebGPUDriverState::ResetBindings() {
  vertex = nullptr; index = nullptr; shader = nullptr;
  constants.fill(nullptr); textures.fill(nullptr); samplers.fill(nullptr);
}
void WebGPUDriverState::SetTarget(std::vector<wgpu::Texture> colors, wgpu::Texture depthTexture,
                                  uint32_t width, uint32_t height, bool clear, WebGPUTexture* depthResource) {
  Require(active, "Render target binding requires an active frame");
  EndPass();
  targetColors = std::move(colors); targetDepth = depthTexture; targetWidth = width; targetHeight = height;
  targetFormats.clear();
  for (const auto& color : targetColors) targetFormats.push_back(color.GetFormat());
  targetDepthResource = depthResource;
  viewport = {0, 0, static_cast<float>(width), static_cast<float>(height)};
  scissor = {0, 0, width, height};
  clearPending = clear;
  if (clear) {
    clearColor = {0, 0, 0, 0};
    BeginPass();
  }
}
void WebGPUDriverState::BeginPass() {
  if (pass) return;
  Require(active && (!targetColors.empty() || targetDepth), "No active render target");
  std::vector<wgpu::RenderPassColorAttachment> colors(targetColors.size());
  for (size_t attachment = 0; attachment < targetColors.size(); ++attachment) {
    colors[attachment].view = targetColors[attachment].CreateView();
    colors[attachment].loadOp = clearPending ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
    colors[attachment].storeOp = wgpu::StoreOp::Store;
    colors[attachment].clearValue = clearColor;
  }
  wgpu::RenderPassDepthStencilAttachment depthAttachment{};
  depthAttachment.view = targetDepth ? targetDepth.CreateView() : wgpu::TextureView{};
  depthAttachment.depthClearValue = 0;
  depthAttachment.depthLoadOp = clearPending ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
  depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
  wgpu::RenderPassDescriptor desc{};
  desc.colorAttachmentCount = colors.size();
  desc.colorAttachments = colors.data();
  desc.depthStencilAttachment = targetDepth ? &depthAttachment : nullptr;
  pass = context.commands.BeginRenderPass(&desc);
  pass.SetViewport(viewport[0], viewport[1], viewport[2], viewport[3], 0, 1);
  pass.SetScissorRect(scissor[0], scissor[1], scissor[2], scissor[3]);
  if (clearPending && targetDepthResource) targetDepthResource->depthSamplingDirty = true;
  clearPending = false;
}

void WebGPUDriverState::Draw(unsigned count, unsigned firstIndex, unsigned firstVertex) {
  RuntimeTelemetry::RecordDraw(count);
  T8_TELEMETRY_ADD("webgpu.draw.calls", 1);
  if (RuntimeTelemetry::IsFrameActive()) T8_TELEMETRY_ADD("webgpu.draws", 1);
  Require(active && shader && vertex && index, "Draw requires an active frame, shader, vertex and index buffers");
  BeginPass();
  const auto resourceLayoutKey = shader->ResourceLayoutKey();
  auto& resourceLayout = shader->GetResourceLayout(resourceLayoutKey);
  const WebGPUShader::PipelineKey pipelineKey{resourceLayoutKey, stride, targetFormats, static_cast<bool>(targetDepth),
    topology, indexFormat, blend, depth, cull};
  auto& pipeline = shader->pipelines[pipelineKey];
  if (!pipeline) {
    std::vector<wgpu::VertexAttribute> attributes;
    uint64_t offset = 0;
    for (const auto& input : shader->vertex.inputs) {
      Require(input.type == webgpu::ShaderComponentType::Float && input.components >= 1 && input.components <= 4, "Only float vertex attributes are currently supported");
      constexpr wgpu::VertexFormat formats[]{wgpu::VertexFormat::Float32, wgpu::VertexFormat::Float32x2, wgpu::VertexFormat::Float32x3, wgpu::VertexFormat::Float32x4};
      wgpu::VertexAttribute attribute{};
      attribute.shaderLocation = input.location; attribute.offset = offset; attribute.format = formats[input.components - 1];
      attributes.push_back(attribute);
      offset += input.components * sizeof(float);
    }
    Require(offset <= stride, "Vertex stride is smaller than the shader input layout");
    wgpu::VertexBufferLayout bufferLayout{};
    bufferLayout.arrayStride = stride; bufferLayout.attributeCount = attributes.size(); bufferLayout.attributes = attributes.data();
    std::vector<wgpu::ColorTargetState> colors(targetColors.size());
    wgpu::BlendState blendState{};
    blendState.color.operation = blendState.alpha.operation = wgpu::BlendOperation::Add;
    if (blend == BaseDriver::ALPHA_BLEND || blend == BaseDriver::NON_PREMULTIPLIED) {
      blendState.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
      blendState.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
      blendState.alpha.srcFactor = blend == BaseDriver::NON_PREMULTIPLIED ? wgpu::BlendFactor::SrcAlpha : wgpu::BlendFactor::One;
      blendState.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    } else if (blend == BaseDriver::ADDITIVE) {
      blendState.color.srcFactor = blendState.alpha.srcFactor = wgpu::BlendFactor::SrcAlpha;
      blendState.color.dstFactor = blendState.alpha.dstFactor = wgpu::BlendFactor::One;
    }
    for (size_t attachment = 0; attachment < colors.size(); ++attachment) {
      colors[attachment].format = targetFormats[attachment];
      if (blend == BaseDriver::ALPHA_BLEND || blend == BaseDriver::NON_PREMULTIPLIED || blend == BaseDriver::ADDITIVE)
        colors[attachment].blend = &blendState;
    }
    wgpu::FragmentState fragment{};
    fragment.module = shader->fragmentModule; fragment.entryPoint = "FS"; fragment.targetCount = colors.size(); fragment.targets = colors.data();
    wgpu::DepthStencilState depthState{};
    depthState.format = wgpu::TextureFormat::Depth32Float;
    depthState.depthCompare = depth == BaseDriver::NONE ? wgpu::CompareFunction::Always : wgpu::CompareFunction::GreaterEqual;
    depthState.depthWriteEnabled = depth != BaseDriver::NONE && depth != BaseDriver::READ;
    wgpu::RenderPipelineDescriptor descriptor{};
    descriptor.layout = resourceLayout.pipeline;
    descriptor.vertex.module = shader->vertexModule; descriptor.vertex.entryPoint = "VS";
    descriptor.vertex.bufferCount = 1; descriptor.vertex.buffers = &bufferLayout;
    descriptor.fragment = &fragment;
    descriptor.depthStencil = targetDepth ? &depthState : nullptr;
    descriptor.primitive.topology = topology; descriptor.primitive.cullMode = cull;
    descriptor.primitive.frontFace = wgpu::FrontFace::CW;
    if (topology == wgpu::PrimitiveTopology::TriangleStrip || topology == wgpu::PrimitiveTopology::LineStrip) descriptor.primitive.stripIndexFormat = indexFormat;
    pipeline = T8_TELEMETRY_CALL("pipeline.create.graphics", context.device.CreateRenderPipeline(&descriptor));
    T8_TELEMETRY_ADD("gpu.pipeline_creations", 1);
    context.CheckHealth();
  }
  auto& entries = resourceLayout.entries;
  bool bindingsChanged = !resourceLayout.group || entries.size() != shader->resources.size();
  entries.resize(shader->resources.size());
  std::array<uint32_t, 8> dynamicOffsets{};
  size_t dynamicOffsetCount = 0;
  {
    T8_TELEMETRY_ADD("webgpu.binding_prepare.calls", 1);
    for (size_t resourceIndex = 0; resourceIndex < shader->resources.size(); ++resourceIndex) {
      const auto& resource = shader->resources[resourceIndex];
      auto& entry = entries[resourceIndex];
      entry.binding = resource.binding;
      if (resource.kind == webgpu::ResourceKind::UniformBuffer) {
        Require(resource.binding >= 64 && resource.binding - 64 < constants.size(), "Unsupported uniform binding");
        auto* buffer = constants[resource.binding - 64];
        if (!buffer || buffer->size < resource.minimumBufferSize) {
          Require(false, "Required uniform buffer missing or too small: slot=" + std::to_string(resource.binding - 64)
            + " required=" + std::to_string(resource.minimumBufferSize)
            + " supplied=" + std::to_string(buffer ? buffer->size : 0)
            + " shaderKey=" + std::to_string(shader->key.bits));
        }
        if (buffer->uniformEpoch != context.UniformEpoch()) {
          buffer->gpu = context.UploadUniform(buffer->uniformShadow->data(), buffer->size, buffer->offset);
          buffer->uniformEpoch = context.UniformEpoch();
        }
        uint64_t offset = buffer->offset;
        if (shader->dynamicUniforms[resource.binding - 64]) {
          dynamicOffsets[dynamicOffsetCount++] = static_cast<uint32_t>(offset);
          offset = 0;
        }
        if (entry.buffer.Get() != buffer->gpu.Get() || entry.size != resource.minimumBufferSize || entry.offset != offset) {
          entry.buffer = buffer->gpu;
          entry.size = resource.minimumBufferSize;
          entry.offset = offset;
          bindingsChanged = true;
        }
      } else if (resource.kind == webgpu::ResourceKind::Sampler) {
        Require(resource.binding >= 32 && resource.binding - 32 < samplers.size(), "Unsupported sampler binding");
        const auto slot = resource.binding - 32;
        const auto* texture = samplers[slot];
        if (!texture) Require(false, "Required sampler not bound at slot " + std::to_string(slot));
        if (entry.sampler.Get() != texture->sampler.Get()) {
          entry.sampler = texture->sampler;
          bindingsChanged = true;
        }
      } else {
        Require(resource.binding < textures.size(), "Unsupported sampled texture binding");
        auto* texture = textures[resource.binding];
        if (!texture || IsAttachment(texture->gpu)) Require(false,
          "Sampled texture " + std::string(texture ? "aliases active attachment" : "missing") + " at binding "
          + std::to_string(resource.binding) + " shaderKey=" + std::to_string(shader->key.bits));
        auto view = texture->SampleView();
        if (entry.textureView.Get() != view.Get()) {
          entry.textureView = std::move(view);
          bindingsChanged = true;
        }
      }
    }
  }
  if (bindingsChanged) {
    T8_TELEMETRY_ADD("webgpu.create_bind_group.calls", 1);
    wgpu::BindGroupDescriptor group{};
    group.layout = resourceLayout.bindings; group.entryCount = entries.size(); group.entries = entries.data();
    resourceLayout.group = context.device.CreateBindGroup(&group);
    if (RuntimeTelemetry::IsFrameActive()) T8_TELEMETRY_ADD("webgpu.bind_group_allocations", 1);
    T8_TELEMETRY_ADD("gpu.bind_group_creations", 1);
  }
  BeginPass();
  {
    T8_TELEMETRY_ADD("webgpu.encoder_commands.calls", 1);
    pass.SetPipeline(pipeline);
    pass.SetBindGroup(0, resourceLayout.group, dynamicOffsetCount, dynamicOffsets.data());
    Require(vertexOffset < vertex->size && indexOffset < index->size, "Buffer offset out of range");
    pass.SetVertexBuffer(0, vertex->gpu, vertexOffset, vertex->size - vertexOffset);
    pass.SetIndexBuffer(index->gpu, indexFormat, indexOffset, index->size - indexOffset);
    pass.DrawIndexed(count, 1, firstIndex, static_cast<int32_t>(firstVertex), 0);
  }
  if (count && targetDepthResource && depth != BaseDriver::NONE && depth != BaseDriver::READ) targetDepthResource->depthSamplingDirty = true;
  ++draws;
  context.CheckHealth();
}

WebGPUDriver::WebGPUDriver() : m_state(std::make_unique<WebGPUDriverState>()) {
  m_currentAPI = GraphicsApi::WEBGPU;
  width = height = 0;
  m_state->driver = this;
}
WebGPUDriver::~WebGPUDriver() { try { DestroyDriver(); } catch (const std::exception& error) { T8_LOG_ERROR("[WebGPU] %s", error.what()); } }
WGPUDevice WebGPUDriver::NativeDevice() const { return m_state->context.device.Get(); }
unsigned WebGPUDriver::MaxRenderTargetColorAttachments() const {
  wgpu::Limits limits{};
  return m_state->context.device && m_state->context.device.GetLimits(&limits) == wgpu::Status::Success
    ? limits.maxColorAttachments : 0;
}
WGPUTextureFormat WebGPUDriver::SurfaceFormat() const { return static_cast<WGPUTextureFormat>(m_state->context.configuration.format); }
int WebGPUDriver::SurfaceColorFormat() const {
  switch (m_state->context.configuration.format) {
  case wgpu::TextureFormat::RGBA8Unorm: return BaseRT::RGBA8;
  case wgpu::TextureFormat::BGRA8Unorm: return BaseRT::BGRA8;
  default: return BaseRT::NOTHING;
  }
}
WGPURenderPassEncoder WebGPUDriver::OverlayPass() {
  if (!m_state->active) return nullptr;
  m_state->BeginPass();
  return m_state->pass.Get();
}
WGPUTextureView WebGPUDriver::TextureView(Texture* texture) const {
  const auto* native = dynamic_cast<WebGPUTexture*>(texture);
  if (!native || (native->cil_props & CIL_CUBE_MAP) || native->format == wgpu::TextureFormat::Depth32Float) return nullptr;
  return native->view.Get();
}
void WebGPUDriver::SetWindow(void* window) {
  Require(window != nullptr, "SDL window required");
#ifdef __EMSCRIPTEN__
  m_state->hwnd = window;
#else
  m_state->hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(static_cast<SDL_Window*>(window)), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#endif
}
void WebGPUDriver::SetWindowHandle(const WindowHandle& window) {
#ifndef __EMSCRIPTEN__
  if (window.kind == WindowHandle::WIN32_HWND) m_state->hwnd = window.nativeHandle;
  else
#endif
  if (window.kind == WindowHandle::SDL_WINDOW) SetWindow(window.sdlWindow);
  else throw std::runtime_error("[WebGPU] Unsupported window handle");
}
void WebGPUDriver::InitDriver() {
  m_state->context.Initialize(m_state->hwnd, width, height);
  m_state->device = std::make_unique<WebGPUDevice>(*m_state);
  m_state->deviceContext = std::make_unique<WebGPUDeviceContext>(*m_state);
  T8Device = m_state->device.get(); T8DeviceContext = m_state->deviceContext.get();
}
std::unique_ptr<ComputePipeline> WebGPUDriver::CreateComputePipeline(const ComputePipelineDesc& desc) {
  auto pipeline = std::make_unique<WebGPUComputePipeline>(*m_state);
  return pipeline->Create(desc) ? std::move(pipeline) : nullptr;
}
std::unique_ptr<ComputeBuffer> WebGPUDriver::CreateComputeBuffer(const ComputeBufferDesc& desc,
                                                                  const void* initialData) {
  auto buffer = std::make_unique<WebGPUComputeBuffer>(*m_state);
  return buffer->Create(desc, initialData) ? std::move(buffer) : nullptr;
}
bool WebGPUDriver::DispatchCompute(ComputePipeline& pipelineBase,
                                   const std::vector<ComputeBindingDesc>& runtimeBindings,
                                   uint32_t groupX, uint32_t groupY, uint32_t groupZ) {
  auto* pipeline = dynamic_cast<WebGPUComputePipeline*>(&pipelineBase);
  if (!pipeline || !pipeline->ValidateBindings(runtimeBindings) || &pipeline->state != m_state.get() || !pipeline->pipeline ||
      !groupX || !groupY || !groupZ || groupX > 65535 || groupY > 65535 || groupZ > 65535 ||
      runtimeBindings.size() != pipeline->bindings.size()) return false;
  for (const auto& binding : runtimeBindings) {
    if (binding.buffer) {
      const auto* buffer = dynamic_cast<WebGPUComputeBuffer*>(binding.buffer);
      if (!buffer || &buffer->state != m_state.get() || !buffer->gpu) return false;
    }
    if (binding.texture) {
      const auto* texture = dynamic_cast<WebGPUTexture*>(binding.texture);
      if (!texture || &texture->state != m_state.get() || !texture->gpu ||
          texture->gpu.GetDimension() != wgpu::TextureDimension::e2D || texture->gpu.GetDepthOrArrayLayers() != 1) return false;
      if (binding.type == ComputeBindingType::Sampler && !texture->sampler) return false;
      if (binding.type == ComputeBindingType::ReadWriteTexture) {
        if (!(texture->gpu.GetUsage() & wgpu::TextureUsage::StorageBinding)) return false;
        for (const auto& layout : pipeline->bindingLayout)
          if (layout.type == binding.type && layout.shaderRegister == binding.shaderRegister &&
              texture->format != (layout.storageFormat == ComputeStorageFormat::Rgba16Float
                ? wgpu::TextureFormat::RGBA16Float : wgpu::TextureFormat::RGBA8Unorm)) return false;
      }
    }
  }
  try {
    m_state->EndPass();
    struct StandaloneCommands {
      webgpu::WebGPUContext& context;
      bool standalone;
      ~StandaloneCommands() { if (standalone) context.commands = nullptr; }
    } commands{m_state->context, !m_state->active};
    if (commands.standalone) m_state->context.commands = m_state->context.device.CreateCommandEncoder();
    std::vector<bool> consumed(runtimeBindings.size(), false);
    std::vector<wgpu::BindGroupEntry> entries;
    uint64_t nonFiltering = 0;
    entries.reserve(pipeline->bindings.size());
    for (const ComputeBindingLayoutDesc& layout : pipeline->bindings) {
      const ComputeBindingDesc* runtime = nullptr;
      size_t runtimeIndex = 0;
      for (size_t index = 0; index < runtimeBindings.size(); ++index) {
        if (runtimeBindings[index].type == layout.type &&
            runtimeBindings[index].shaderRegister == layout.shaderRegister) {
          if (runtime) return false;
          runtime = &runtimeBindings[index];
          runtimeIndex = index;
        }
      }
      if (!runtime || consumed[runtimeIndex]) return false;
      consumed[runtimeIndex] = true;
      const auto reflected = pipeline->resources.find(layout.bindingIndex);
      if (reflected == pipeline->resources.end()) return false;

      wgpu::BindGroupEntry entry{};
      entry.binding = layout.bindingIndex;
      switch (layout.type) {
      case ComputeBindingType::Constants32: {
        if (!runtime->constants || runtime->constantCount != layout.constantCount) return false;
        const uint64_t byteCount = static_cast<uint64_t>(runtime->constantCount) * sizeof(uint32_t);
        entry.buffer = m_state->context.UploadUniform(runtime->constants, byteCount, entry.offset);
        entry.size = byteCount;
        break;
      }
      case ComputeBindingType::ReadOnlyBuffer:
      case ComputeBindingType::ReadWriteBuffer: {
        auto* buffer = dynamic_cast<WebGPUComputeBuffer*>(runtime->buffer);
        if (!buffer || &buffer->state != m_state.get() || !buffer->gpu ||
            (layout.type == ComputeBindingType::ReadWriteBuffer &&
             buffer->descriptor.access != ComputeBufferAccess::ReadWrite)) return false;
        entry.buffer = buffer->gpu;
        entry.size = buffer->allocationSize;
        break;
      }
      case ComputeBindingType::ReadOnlyTexture: {
        auto* texture = dynamic_cast<WebGPUTexture*>(runtime->texture);
        if (!texture || &texture->state != m_state.get() || !texture->gpu) return false;
        entry.textureView = texture->SampleView();
        if (texture->RequiresNonFilteringBinding()) nonFiltering |= uint64_t(1) << layout.bindingIndex;
        break;
      }
      case ComputeBindingType::ReadWriteTexture: {
        auto* texture = dynamic_cast<WebGPUTexture*>(runtime->texture);
        if (!texture || &texture->state != m_state.get() || !texture->gpu ||
            (reflected->second.storageRgba8Unorm && texture->format != wgpu::TextureFormat::RGBA8Unorm) ||
            (reflected->second.storageRgba16Float && texture->format != wgpu::TextureFormat::RGBA16Float)) return false;
        entry.textureView = texture->view;
        break;
      }
      case ComputeBindingType::Sampler: {
        auto* texture = dynamic_cast<WebGPUTexture*>(runtime->texture);
        if (!texture || &texture->state != m_state.get() || !texture->sampler) return false;
        entry.sampler = texture->sampler;
        if (texture->RequiresNonFilteringBinding()) nonFiltering |= uint64_t(1) << layout.bindingIndex;
        break;
      }
      }
      entries.push_back(entry);
    }
    if (std::find(consumed.begin(), consumed.end(), false) != consumed.end()) return false;

    wgpu::BindGroupDescriptor groupDescriptor{};
    auto& variant = pipeline->GetVariant(nonFiltering);
    groupDescriptor.layout = variant.bindings;
    groupDescriptor.entryCount = entries.size();
    groupDescriptor.entries = entries.data();
    auto bindGroup = m_state->context.device.CreateBindGroup(&groupDescriptor);
    if (!bindGroup) return false;

    auto pass = m_state->context.commands.BeginComputePass();
    pass.SetPipeline(variant.pipeline);
    pass.SetBindGroup(0, bindGroup);
    pass.DispatchWorkgroups(groupX, groupY, groupZ);
    pass.End();
    if (RuntimeTelemetry::IsFrameActive()) T8_TELEMETRY_ADD("webgpu.compute_dispatches", 1);
    if (commands.standalone) {
      auto command = m_state->context.commands.Finish();
      m_state->context.SubmitCommands(command);
    }
    m_state->context.CheckHealth();
    return true;
  } catch (const std::exception& error) {
    T8_LOG_ERROR("[WebGPU][Compute] Dispatch failed: %s", error.what());
    return false;
  }
}
bool WebGPUDriver::ReadComputeBuffer(ComputeBuffer& bufferBase, void* destination, size_t byteCount) {
  auto* buffer = dynamic_cast<WebGPUComputeBuffer*>(&bufferBase);
  if (!buffer || &buffer->state != m_state.get() || !buffer->gpu || !destination || !byteCount ||
      byteCount > buffer->descriptor.byteWidth || (byteCount % sizeof(uint32_t)) != 0) return false;
  try {
    m_state->EndPass();
    wgpu::BufferDescriptor descriptor{};
    descriptor.size = byteCount;
    descriptor.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    auto staging = m_state->context.device.CreateBuffer(&descriptor);
    if (!staging) return false;
    if (m_state->active) {
      m_state->context.commands.CopyBufferToBuffer(buffer->gpu, 0, staging, 0, byteCount);
      auto command = m_state->context.commands.Finish();
      m_state->context.SubmitCommands(command);
      m_state->context.commands = m_state->context.device.CreateCommandEncoder();
    } else {
      auto encoder = m_state->context.device.CreateCommandEncoder();
      encoder.CopyBufferToBuffer(buffer->gpu, 0, staging, 0, byteCount);
      auto command = encoder.Finish();
      m_state->context.SubmitCommands(command);
    }
    auto mapped = std::make_shared<bool>(false);
    const auto future = staging.MapAsync(wgpu::MapMode::Read, 0, byteCount, wgpu::CallbackMode::WaitAnyOnly,
      [mapped](wgpu::MapAsyncStatus status, wgpu::StringView) {
        *mapped = status == wgpu::MapAsyncStatus::Success;
      });
  #ifdef __EMSCRIPTEN__
    const auto waitStatus = m_state->context.instance.WaitAny(future, UINT64_MAX);
  #else
    const auto waitStatus = m_state->context.instance.WaitAny(future, 30'000'000'000ULL);
  #endif
    if (waitStatus != wgpu::WaitStatus::Success || !*mapped)
      return false;
    std::memcpy(destination, staging.GetConstMappedRange(0, byteCount), byteCount);
    staging.Unmap();
    staging.Destroy();
    m_state->context.CheckHealth();
    return true;
  } catch (const std::exception& error) {
    T8_LOG_ERROR("[WebGPU][Compute] Readback failed: %s", error.what());
    return false;
  }
}
void WebGPUDriver::CreateSurfaces() { m_state->context.Resize(width, height); }
void WebGPUDriver::DestroySurfaces() { m_state->context.Resize(0, 0); }
void WebGPUDriver::Update() { m_state->context.CheckHealth(); }
void WebGPUDriver::SetDimensions(int newWidth, int newHeight) { Require(newWidth >= 0 && newHeight >= 0, "Negative dimensions"); width = newWidth; height = newHeight; }
bool WebGPUDriver::ResizeSwapchain(int newWidth, int newHeight) {
  SetDimensions(newWidth, newHeight);
  m_state->context.Resize(width, height);
  return true;
}
bool WebGPUDriver::SuspendWindowSurface() { DestroySurfaces(); return true; }
bool WebGPUDriver::ResumeWindowSurface(void* window, int newWidth, int newHeight) {
  Require(window == m_state->hwnd, "Replacing HWND requires driver recreation");
  return ResizeSwapchain(newWidth, newHeight);
}
void WebGPUDriver::BeginFrame(FrameTargetMode target) {
  if (m_state->active) return;
  m_state->offscreen = target == FrameTargetMode::Offscreen;
  m_state->active = m_state->context.BeginFrame(!m_state->offscreen);
  m_state->draws = 0;
  m_state->ResetBindings();
  if (m_state->active && !m_state->offscreen) PopRT();
#if T850_ENABLE_GPU_PROFILING
  if (m_state->active && g_gpuTimestampProfiler) g_gpuTimestampProfiler->BeginFrame();
#endif
}
void WebGPUDriver::EndFrame() { m_state->EndPass(); }
void WebGPUDriver::CompleteFrame(FrameCompletionMode mode) {
  if (!m_state->active) return;
  EndFrame();
#if T850_ENABLE_GPU_PROFILING
  if (g_gpuTimestampProfiler) g_gpuTimestampProfiler->EndFrame();
#endif
  m_state->targetColors.clear(); m_state->targetDepth = nullptr;
  m_state->context.Submit(mode == FrameCompletionMode::Present && !m_state->offscreen && !IsOffscreenEnabled());
#if T850_ENABLE_GPU_PROFILING
  if (g_gpuTimestampProfiler) g_gpuTimestampProfiler->OnSubmitted(++m_state->gpuProfileSubmissionSerial);
#endif
  m_state->active = false;
  m_state->ResetBindings();
  if (IsOffscreenEnabled()) CompleteOffscreenFrame();
}

webgpu::WebGPUContext& WebGPUDriver::TimestampContext() { return m_state->context; }
void WebGPUDriver::SwapBuffers() { CompleteFrame(); }
void WebGPUDriver::Clear() {
  if (!m_state->active) BeginFrame();
  ClearWithColor(0, 0, 0, 1);
}
void WebGPUDriver::ClearWithColor(float red, float green, float blue, float alpha) {
  if (!m_state->active) return;
  m_state->EndPass(); m_state->clearColor = {red, green, blue, alpha}; m_state->clearPending = true; m_state->BeginPass();
}
void WebGPUDriver::ClearBackbufferWithColor(float red, float green, float blue, float alpha) {
  if (!m_state->active) BeginFrame();
  PopRT();
  ClearWithColor(red, green, blue, alpha);
}
void WebGPUDriver::WaitForGPU() { m_state->context.WaitForGPU(); }
void WebGPUDriver::FlushGPUResources() { if (m_state->active) CompleteFrame(FrameCompletionMode::SubmitNoPresent); WaitForGPU(); }
void WebGPUDriver::SetBlendState(BlendStates state) { m_state->blend = state; }
void WebGPUDriver::SetDepthStencilState(DepthStencilStates state) { m_state->depth = state; }
void WebGPUDriver::SetCullFace(FaceCulling state) {
  m_FaceCulling = state;
  m_state->cull = state == FRONT_AND_BACK ? wgpu::CullMode::None : state == FRONT_FACES ? wgpu::CullMode::Back : wgpu::CullMode::Front;
}
void WebGPUDriver::SetViewport(float x, float y, float width, float height) {
  m_state->viewport = {x, y, width, height};
  m_state->BeginPass();
  m_state->pass.SetViewport(x, y, width, height, 0, 1);
}
void WebGPUDriver::SetScissorRect(int x, int y, int width, int height) {
  Require(x >= 0 && y >= 0 && width >= 0 && height >= 0, "Invalid scissor rectangle");
  m_state->scissor = {static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
  m_state->BeginPass(); m_state->pass.SetScissorRect(x, y, width, height);
}
void WebGPUDriver::ClearPendingTextureBinding(int slot) { if (slot >= 0 && slot < 32) { m_state->textures[slot] = nullptr; m_state->samplers[slot] = nullptr; } }
void WebGPUDriver::PopRT() {
  if (m_state->offscreen) {
    m_state->EndPass();
    m_state->targetColors.clear(); m_state->targetDepth = nullptr;
    CurrentRT = -1;
    return;
  }
  if (!m_state->active) return;
  m_state->SetTarget({m_state->context.backbuffer}, m_state->context.depth, width, height, false);
  CurrentRT = -1;
}
void WebGPUDriver::SetShaderFlow(webgpu::ShaderFlow flow) { Require(m_shaders.empty(), "Select shader flow before creating shaders"); m_state->flow = flow; }
unsigned WebGPUDriver::DrawCount() const { return m_state->draws; }
uint64_t WebGPUDriver::AdapterLuid() const { return m_state->context.adapterLuid; }
void WebGPUDriver::SaveScreenshot(std::string path) {
  if (IsOffscreenEnabled()) {
    SaveRTToFile(GetActiveOffscreenRT(), 0, path);
    return;
  }
  Require(static_cast<bool>(m_state->context.backbuffer), "Screenshot requires an acquired backbuffer");
  WebGPUTexture texture(*m_state);
  texture.x = width; texture.y = height;
  texture.format = m_state->context.configuration.format;
  texture.gpu = m_state->context.backbuffer;
  m_state->SaveColor(texture, path + ".ppm");
}
std::vector<float> WebGPUDriverState::ReadColor(WebGPUTexture& texture) {
  unsigned bytesPerPixel = 0;
  unsigned channels = 4;
  bool half = false;
  bool floating = false;
  switch (texture.format) {
  case wgpu::TextureFormat::RGBA8Unorm: case wgpu::TextureFormat::BGRA8Unorm: bytesPerPixel = 4; break;
  case wgpu::TextureFormat::R8Unorm: bytesPerPixel = 1; channels = 1; break;
  case wgpu::TextureFormat::R16Float: bytesPerPixel = 2; channels = 1; half = true; break;
  case wgpu::TextureFormat::RGBA16Float: bytesPerPixel = 8; half = true; break;
  case wgpu::TextureFormat::RGBA32Float: bytesPerPixel = 16; floating = true; break;
  case wgpu::TextureFormat::R32Float: case wgpu::TextureFormat::Depth32Float: bytesPerPixel = 4; channels = 1; floating = true; break;
  default: throw std::runtime_error("[WebGPU] Unsupported texture readback format");
  }
  if (active) {
    EndPass();
    auto pending = context.commands.Finish();
    context.SubmitCommands(pending);
    context.commands = context.device.CreateCommandEncoder();
  }
  const uint32_t rowPitch = (texture.x * bytesPerPixel + 255) & ~255u;
  wgpu::BufferDescriptor descriptor{};
  descriptor.size = static_cast<uint64_t>(rowPitch) * texture.y;
  descriptor.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
  auto staging = context.device.CreateBuffer(&descriptor);
  auto encoder = context.device.CreateCommandEncoder();
  wgpu::TexelCopyTextureInfo source{};
  source.texture = texture.gpu;
  if (texture.format == wgpu::TextureFormat::Depth32Float) source.aspect = wgpu::TextureAspect::DepthOnly;
  wgpu::TexelCopyBufferInfo destination{};
  destination.buffer = staging;
  destination.layout.bytesPerRow = rowPitch;
  destination.layout.rowsPerImage = texture.y;
  wgpu::Extent3D extent{texture.x, texture.y, 1};
  encoder.CopyTextureToBuffer(&source, &destination, &extent);
  auto command = encoder.Finish();
  context.SubmitCommands(command);
  auto mapped = std::make_shared<bool>(false);
  const auto future = staging.MapAsync(wgpu::MapMode::Read, 0, descriptor.size, wgpu::CallbackMode::WaitAnyOnly,
    [mapped](wgpu::MapAsyncStatus status, wgpu::StringView) { *mapped = status == wgpu::MapAsyncStatus::Success; });
#ifdef __EMSCRIPTEN__
  Require(context.instance.WaitAny(future, UINT64_MAX) == wgpu::WaitStatus::Success && *mapped, "Browser readback failed");
#else
  Require(context.instance.WaitAny(future, 30'000'000'000ULL) == wgpu::WaitStatus::Success && *mapped, "Readback failed or timed out");
#endif
  const auto* bytes = static_cast<const uint8_t*>(staging.GetConstMappedRange());
  std::vector<float> output(static_cast<size_t>(texture.x) * texture.y * 4, 1.0f);
  for (uint32_t row = 0; row < texture.y; ++row) {
    for (uint32_t column = 0; column < texture.x; ++column) {
      const auto* pixel = bytes + static_cast<size_t>(row) * rowPitch + column * bytesPerPixel;
      auto* decoded = output.data() + (static_cast<size_t>(row) * texture.x + column) * 4;
      for (unsigned channel = 0; channel < channels; ++channel) {
        if (half) {
          uint16_t value;
          std::memcpy(&value, pixel + channel * 2, sizeof(value));
#ifdef __EMSCRIPTEN__
          decoded[channel] = HalfToFloat(value);
#else
          decoded[channel] = DirectX::PackedVector::XMConvertHalfToFloat(value);
#endif
        } else if (floating) {
          std::memcpy(decoded + channel, pixel + channel * 4, sizeof(float));
        } else decoded[channel] = pixel[channel] / 255.0f;
      }
      if (channels == 1) decoded[1] = decoded[2] = decoded[0];
      if (texture.format == wgpu::TextureFormat::BGRA8Unorm) std::swap(decoded[0], decoded[2]);
    }
  }
  staging.Unmap();
  staging.Destroy();
  context.CheckHealth();
  return output;
}
void WebGPUDriverState::SaveColor(WebGPUTexture& texture, const std::string& path) {
  const auto rgba = ReadColor(texture);
  const auto header = "P6\n" + std::to_string(texture.x) + " " + std::to_string(texture.y) + "\n255\n";
  std::vector<unsigned char> image(header.begin(), header.end());
  for (size_t offset = 0; offset < rgba.size(); offset += 4) {
    for (size_t channel = 0; channel < 3; ++channel) {
      const float value = rgba[offset + channel];
      image.push_back(static_cast<unsigned char>((std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f) * 255.0f));
    }
  }
  Require(ResourceLocator::Instance().WriteBinaryAtomic(path, image), "Capture write failed: " + path);
  T8_LOG_INFO("[WebGPU] Saved %s (%ux%u)", path.c_str(), texture.x, texture.y);
}
void WebGPUDriver::SaveRTToFile(int target, int attachment, std::string path) {
  if (target < 0 || static_cast<size_t>(target) >= RTs.size() || !RTs[target]) return;
  auto* renderTarget = RTs[target];
  if (attachment != DEPTH_ATTACHMENT && (attachment < 0 || static_cast<size_t>(attachment) >= renderTarget->vColorTextures.size())) return;
  auto* texture = static_cast<WebGPUTexture*>(attachment == DEPTH_ATTACHMENT ? renderTarget->pDepthTexture : renderTarget->vColorTextures[attachment]);
  if (!texture) return;
  m_state->SaveColor(*texture, path + ".ppm");
}
bool WebGPUDriver::ReadRTColorFloat(int target, int attachment, float outRGBA[4]) {
  if (!outRGBA || target < 0 || static_cast<size_t>(target) >= RTs.size() || !RTs[target] || attachment < 0
      || static_cast<size_t>(attachment) >= RTs[target]->vColorTextures.size()) return false;
  auto* texture = static_cast<WebGPUTexture*>(RTs[target]->vColorTextures[attachment]);
  const auto rgba = m_state->ReadColor(*texture);
  std::copy_n(rgba.begin(), 4, outRGBA);
  return true;
}
void WebGPUDriver::DestroyDriver() {
  if (!m_state->context.device) return;
  std::exception_ptr failure;
  try { FlushGPUResources(); }
  catch (...) {
    failure = std::current_exception();
    m_state->EndPass();
    m_state->context.commands = nullptr;
    m_state->active = false;
  }
  DestroyOffscreenTargets(); DestroyShaders(); DestroyRTs(); DestroyTextures(); DestroyTechniques();
  m_state->ResetBindings();
  if (T8Device == m_state->device.get()) T8Device = nullptr;
  if (T8DeviceContext == m_state->deviceContext.get()) T8DeviceContext = nullptr;
  m_state->deviceContext.reset(); m_state->device.reset();
  m_state->depthSamplingCopyPipeline = nullptr;
  try { m_state->context.Shutdown(); }
  catch (...) { if (!failure) failure = std::current_exception(); }
  if (failure) std::rethrow_exception(failure);
}
}
#endif