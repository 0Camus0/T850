#include <pch.h>

#include <imgui/ImGuiRendererBackend.h>

#if defined(OS_WINDOWS) && __has_include(<imgui_impl_dx12.h>)
#include <core/Core.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL.h>
#include <video/d3d12/D3D12Device.h>
#include <video/d3d12/D3D12Driver.h>
#include <video/d3d12/D3D12Texture.h>

#include <unordered_map>
#endif

namespace t850 {

#if defined(OS_WINDOWS) && __has_include(<imgui_impl_dx12.h>)
extern Device* T8Device;

namespace {

bool IsSingleChannelFormat(DXGI_FORMAT format) {
  return format == DXGI_FORMAT_R8_UNORM ||
         format == DXGI_FORMAT_R16_FLOAT ||
         format == DXGI_FORMAT_R32_FLOAT;
}

UINT OpaquePreviewMapping(DXGI_FORMAT format) {
  const int green = IsSingleChannelFormat(format)
    ? D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0
    : D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1;
  const int blue = IsSingleChannelFormat(format)
    ? D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0
    : D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2;
  return D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
    D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0,
    green,
    blue,
    D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1);
}

class ImGuiD3D12Backend final : public ImGuiRendererBackend {
public:
  ~ImGuiD3D12Backend() override { Shutdown(); }

  bool Init(RootFramework* framework, void* nativeWindow) override {
    auto* window = static_cast<SDL_Window*>(nativeWindow);
    if (!framework || !framework->pVideoDriver ||
        framework->pVideoDriver->m_currentAPI != GraphicsApi::D3D12 ||
        !T8Device || !window ||
        !ImGui_ImplSDL3_InitForD3D(window)) return false;
    m_platformInitialized = true;

    m_driver = static_cast<D3D12Driver*>(framework->pVideoDriver);
    ID3D12Device* device = static_cast<D3D12Device*>(T8Device)->GetNativeDevice();
    auto& srvHeap = m_driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE);
    m_srvHeap = srvHeap.GetHeap();

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = device;
    initInfo.CommandQueue = m_driver->GetCmdQueue();
    initInfo.NumFramesInFlight = D3D12Driver::kBackBufferCount;
    initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    initInfo.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    initInfo.SrvDescriptorHeap = m_srvHeap;
    initInfo.UserData = this;
    initInfo.SrvDescriptorAllocFn =
      [](ImGui_ImplDX12_InitInfo* info,
         D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
         D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
        auto* backend = static_cast<ImGuiD3D12Backend*>(info ? info->UserData : nullptr);
        if (!backend || !outCpu || !outGpu) {
          if (outCpu) *outCpu = D3D12_CPU_DESCRIPTOR_HANDLE{0};
          if (outGpu) *outGpu = D3D12_GPU_DESCRIPTOR_HANDLE{0};
          return;
        }
        if (!backend->m_freeTextureDescriptors.empty()) {
          const auto descriptor = backend->m_freeTextureDescriptors.back();
          backend->m_freeTextureDescriptors.pop_back();
          *outCpu = descriptor.first;
          *outGpu = descriptor.second;
        } else {
          auto& heap = backend->m_driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE);
          *outCpu = heap.AllocateCPU();
          *outGpu = heap.AllocateGPU();
        }
      };
    initInfo.SrvDescriptorFreeFn =
      [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu,
        D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
        auto* backend = static_cast<ImGuiD3D12Backend*>(info->UserData);
        if (cpu.ptr && gpu.ptr) backend->m_freeTextureDescriptors.emplace_back(cpu, gpu);
      };

    m_initInfo = initInfo;
    m_rendererInitialized = InitRenderer(m_driver->GetRenderTargetLayout());
    if (!m_rendererInitialized) Shutdown();
    return m_rendererInitialized;
  }

  void Shutdown() override {
    if (m_rendererInitialized) {
      m_driver->WaitForGPU();
      ImGui_ImplDX12_Shutdown();
    }
    if (m_platformInitialized) ImGui_ImplSDL3_Shutdown();
    m_driver = nullptr;
    m_srvHeap = nullptr;
    m_rendererInitialized = false;
    m_platformInitialized = false;
    m_freeTextureDescriptors.clear();
    m_targetLayout = {};
    m_targetReady = false;
  }

  void NewFrame() override {
    const auto layout = m_driver->GetRenderTargetLayout();
    m_targetReady = ValidateOverlayLayout(*m_driver, layout);
    if (m_targetReady && !layout.HasCompatibleAttachments(m_targetLayout)) {
      m_driver->WaitForGPU();
      if (m_rendererInitialized) ImGui_ImplDX12_Shutdown();
      m_rendererInitialized = InitRenderer(layout);
      if (!m_rendererInitialized) T8_LOG_ERROR("[ImGui] Failed to configure D3D12 overlay target");
    }
    if (m_targetReady && m_rendererInitialized) m_targetLayout = layout;
    if (m_rendererInitialized) ImGui_ImplDX12_NewFrame();
    ImGui_ImplSDL3_NewFrame();
  }

  void RenderDrawData(ImDrawData* drawData) override {
    if (!m_rendererInitialized || !m_targetReady || !ValidateOverlayDraw(*m_driver, m_targetLayout)) return;
    ID3D12GraphicsCommandList* commandList = m_driver->GetCmdList();
    ID3D12DescriptorHeap* heaps[] = {m_srvHeap};
    commandList->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(drawData, commandList);
  }

  ImTextureID GetTextureID(Texture* texture, ImGuiTextureMode mode) override {
    auto* d3dTexture = static_cast<D3D12Texture*>(texture);
    if (!d3dTexture->pTexResource) return (ImTextureID)nullptr;
    if (mode == ImGuiTextureMode::Native)
      return static_cast<ImTextureID>(d3dTexture->srvGPU.ptr);

    auto found = m_opaqueTextureIDs.find(texture);
    if (found != m_opaqueTextureIDs.end()) return found->second;

    auto& srvHeap = m_driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE);
    const D3D12_CPU_DESCRIPTOR_HANDLE srvCPU = srvHeap.AllocateCPU();
    const D3D12_GPU_DESCRIPTOR_HANDLE srvGPU = srvHeap.AllocateGPU();
    const D3D12_RESOURCE_DESC resourceDesc = d3dTexture->pTexResource->GetDesc();
    DXGI_FORMAT srvFormat = resourceDesc.Format;
    if (resourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) {
      if (srvFormat == DXGI_FORMAT_R32_TYPELESS) srvFormat = DXGI_FORMAT_R32_FLOAT;
      if (srvFormat == DXGI_FORMAT_R16_TYPELESS) srvFormat = DXGI_FORMAT_R16_FLOAT;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = srvFormat;
    srvDesc.Shader4ComponentMapping = OpaquePreviewMapping(srvFormat);
    if (resourceDesc.DepthOrArraySize == 6 &&
        (resourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) {
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
      srvDesc.TextureCube.MipLevels = 1;
    } else {
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srvDesc.Texture2D.MipLevels = 1;
    }

    Microsoft::WRL::ComPtr<ID3D12Device> device;
    if (FAILED(d3dTexture->pTexResource->GetDevice(IID_PPV_ARGS(device.GetAddressOf()))))
      return static_cast<ImTextureID>(d3dTexture->srvGPU.ptr);
    device->CreateShaderResourceView(d3dTexture->pTexResource.Get(), &srvDesc, srvCPU);
    const ImTextureID textureID = static_cast<ImTextureID>(srvGPU.ptr);
    m_opaqueTextureIDs[texture] = textureID;
    return textureID;
  }

  void PruneTextureIDs(const std::unordered_set<Texture*>& liveTextures) override {
    for (auto iterator = m_opaqueTextureIDs.begin(); iterator != m_opaqueTextureIDs.end();) {
      if (!liveTextures.contains(iterator->first)) iterator = m_opaqueTextureIDs.erase(iterator);
      else ++iterator;
    }
  }

  void ReleaseTextureIDs() override { m_opaqueTextureIDs.clear(); }

private:
  bool InitRenderer(const RenderTargetLayout& layout) {
    if (!ValidateOverlayLayout(*m_driver, layout)) return false;
    switch (layout.colorFormats[0]) {
    case BaseRT::RGBA8: m_initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM; break;
    case BaseRT::BGRA8: m_initInfo.RTVFormat = DXGI_FORMAT_B8G8R8A8_UNORM; break;
    case BaseRT::RGBA16F: m_initInfo.RTVFormat = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
    case BaseRT::RGBA32F: m_initInfo.RTVFormat = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
    case BaseRT::R8: m_initInfo.RTVFormat = DXGI_FORMAT_R8_UNORM; break;
    case BaseRT::F16: m_initInfo.RTVFormat = DXGI_FORMAT_R16_FLOAT; break;
    case BaseRT::F32: m_initInfo.RTVFormat = DXGI_FORMAT_R32_FLOAT; break;
    default: return false;
    }
    m_initInfo.DSVFormat = layout.depthFormat == BaseRT::NOTHING ? DXGI_FORMAT_UNKNOWN
      : layout.depthFormat == BaseRT::FD16 ? DXGI_FORMAT_D16_UNORM : DXGI_FORMAT_D32_FLOAT;
    if (!ImGui_ImplDX12_Init(&m_initInfo)) return false;
    m_targetLayout = layout;
    return true;
  }
  D3D12Driver* m_driver = nullptr;
  ImGui_ImplDX12_InitInfo m_initInfo{};
  std::vector<std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE>> m_freeTextureDescriptors;
  RenderTargetLayout m_targetLayout;
  bool m_targetReady = false;
  ID3D12DescriptorHeap* m_srvHeap = nullptr;
  bool m_platformInitialized = false;
  bool m_rendererInitialized = false;
  std::unordered_map<Texture*, ImTextureID> m_opaqueTextureIDs;
};

} // namespace
#endif

std::unique_ptr<ImGuiRendererBackend> CreateImGuiD3D12Backend() {
#if defined(OS_WINDOWS) && __has_include(<imgui_impl_dx12.h>)
  return std::make_unique<ImGuiD3D12Backend>();
#else
  return nullptr;
#endif
}

} // namespace t850
