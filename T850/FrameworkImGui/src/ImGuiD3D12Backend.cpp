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
    initInfo.UserData = &srvHeap;
    initInfo.SrvDescriptorAllocFn =
      [](ImGui_ImplDX12_InitInfo* info,
         D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
         D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
        auto* heap = static_cast<D3D12Heap*>(info ? info->UserData : nullptr);
        if (!heap || !outCpu || !outGpu) {
          if (outCpu) *outCpu = D3D12_CPU_DESCRIPTOR_HANDLE{0};
          if (outGpu) *outGpu = D3D12_GPU_DESCRIPTOR_HANDLE{0};
          return;
        }
        *outCpu = heap->AllocateCPU();
        *outGpu = heap->AllocateGPU();
      };
    initInfo.SrvDescriptorFreeFn =
      [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE,
         D3D12_GPU_DESCRIPTOR_HANDLE) {};

    m_rendererInitialized = ImGui_ImplDX12_Init(&initInfo);
    if (!m_rendererInitialized) Shutdown();
    return m_rendererInitialized;
  }

  void Shutdown() override {
    if (m_rendererInitialized) ImGui_ImplDX12_Shutdown();
    if (m_platformInitialized) ImGui_ImplSDL3_Shutdown();
    m_driver = nullptr;
    m_srvHeap = nullptr;
    m_rendererInitialized = false;
    m_platformInitialized = false;
  }

  void NewFrame() override {
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplSDL3_NewFrame();
  }

  void RenderDrawData(ImDrawData* drawData) override {
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
  D3D12Driver* m_driver = nullptr;
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
