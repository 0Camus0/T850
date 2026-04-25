#include <pch.h>
/*********************************************************
* T850 Engine — D3D12 Backend
* D3D12Device.cpp: Device implementation
*********************************************************/

#include <video/d3d12/D3D12Device.h>
#include <video/d3d12/D3D12VertexBuffer.h>
#include <video/d3d12/D3D12IndexBuffer.h>
#include <video/d3d12/D3D12ConstantBuffer.h>
#include <video/d3d12/D3D12Shader.h>
#include <video/d3d12/D3D12Texture.h>
#include <video/d3d12/D3D12RT.h>
#include <video/d3d12/D3D12Driver.h>

#ifdef OS_WINDOWS

#include <utils/Log.h>

namespace t850 {

  // ══════════════════════════════════════════════════════
  //  D3D12Device
  // ══════════════════════════════════════════════════════

  void* D3D12Device::GetAPIObject() const { return (void*)m_device.Get(); }
  void** D3D12Device::GetAPIObjectReference() const { return nullptr; }
  void D3D12Device::release() { m_device.Reset(); }

  Buffer* D3D12Device::CreateBuffer(BufferType::E bufferType, BufferDesc desc, void* initialData) {
    T8_LOG_DEBUG("[D3D12] CreateBuffer type=%d size=%d", bufferType, desc.byteWidth);
    Buffer* buf = nullptr;
    switch (bufferType) {
      case BufferType::VERTEX:   buf = new D3D12VertexBuffer;   break;
      case BufferType::INDEX:    buf = new D3D12IndexBuffer;    break;
      case BufferType::CONSTANT: buf = new D3D12ConstantBuffer; break;
    }
    if (buf) buf->Create(*this, desc, initialData);
    return buf;
  }

  ShaderBase* D3D12Device::CreateShader(std::string src_vs, std::string src_fs, ShaderKey key,
                                         const std::string& vs_name, const std::string& fs_name) {
    D3D12Shader* sh = new D3D12Shader();
    if (!sh->CreateShader(src_vs, src_fs, key, vs_name, fs_name)) {
      delete sh;
      return nullptr;
    }
    return sh;
  }

  Texture* D3D12Device::CreateTexture(std::string path) {
    D3D12Texture* tex = new D3D12Texture;
    tex->LoadTexture(path.c_str());
    return tex;
  }

  Texture* D3D12Device::CreateTextureFromMemory(const unsigned char* buff, int w, int h, int channels, std::string name) {
    D3D12Texture* tex = new D3D12Texture;
    tex->LoadFromMemory(buff, w, h, channels);
    return tex;
  }

  Texture* D3D12Device::CreateCubeMap(const unsigned char* buff, int w, int h) {
    D3D12Texture* tex = new D3D12Texture;
    tex->CreateCubeMap(buff, w, h);
    return tex;
  }

  Texture* D3D12Device::CreateFloatTexture(int w, int h, const float* data) {
    auto* driver = static_cast<D3D12Driver*>(g_pBaseDriver);
    auto* device = reinterpret_cast<ID3D12Device*>(GetAPIObject());
    D3D12Texture* tex = new D3D12Texture;

    // Create GPU texture (DEFAULT heap)
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = w;
    texDesc.Height = h;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES defaultHeap = { D3D12_HEAP_TYPE_DEFAULT };
    HRESULT hr = device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE,
        &texDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
        IID_PPV_ARGS(tex->pTexResource.GetAddressOf()));
    if (FAILED(hr)) { T8_LOG_ERROR("[D3D12] CreateFloatTexture failed (resource)"); delete tex; return nullptr; }

    // Create persistent UPLOAD buffer for per-frame updates
    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadSize);
    D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = uploadSize;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE,
        &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(tex->m_uploadBuffer.GetAddressOf()));
    if (FAILED(hr)) { T8_LOG_ERROR("[D3D12] CreateFloatTexture failed (upload)"); delete tex; return nullptr; }

    // Create SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    tex->srvCPU = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateCPU();
    tex->srvGPU = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateGPU();
    device->CreateShaderResourceView(tex->pTexResource.Get(), &srvDesc, tex->srvCPU);

    // Create sampler (NEAREST, no interpolation)
    D3D12_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampDesc.AddressU = sampDesc.AddressV = sampDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.MaxLOD = 0;
    auto sampCPU = driver->GetHeap(D3D12Heap::SAMPLER).AllocateCPU();
    tex->samplerGPU = driver->GetHeap(D3D12Heap::SAMPLER).AllocateGPU();
    device->CreateSampler(&sampDesc, sampCPU);
    tex->hasSampler = true;

    tex->x = w;
    tex->y = h;

    // Upload initial data if provided
    if (data) {
      // Use a temporary command list for initial upload
      ComPtr<ID3D12CommandAllocator> alloc;
      ComPtr<ID3D12GraphicsCommandList> cmd;
      device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
      device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmd));

      // Barrier: SRV → COPY_DEST
      D3D12_RESOURCE_BARRIER barrier = {};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Transition.pResource = tex->pTexResource.Get();
      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
      barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
      cmd->ResourceBarrier(1, &barrier);

      // Copy data to upload buffer
      void* mapped = nullptr;
      tex->m_uploadBuffer->Map(0, nullptr, &mapped);
      D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
      UINT numRows; UINT64 rowSize;
      device->GetCopyableFootprints(&texDesc, 0, 1, 0, &layout, &numRows, &rowSize, nullptr);
      for (UINT r = 0; r < numRows; r++) {
        memcpy((uint8_t*)mapped + layout.Offset + r * layout.Footprint.RowPitch,
               (const uint8_t*)data + r * w * 16, w * 16);
      }
      tex->m_uploadBuffer->Unmap(0, nullptr);

      D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
      dst.pResource = tex->pTexResource.Get();
      dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      src.pResource = tex->m_uploadBuffer.Get();
      src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      src.PlacedFootprint = layout;
      cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

      // Barrier: COPY_DEST → SRV
      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
      barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
      cmd->ResourceBarrier(1, &barrier);

      cmd->Close();
      ID3D12CommandList* lists[] = { cmd.Get() };
      driver->GetCmdQueue()->ExecuteCommandLists(1, lists);
      driver->WaitForGPU();
    }

    T8_LOG_INFO("[D3D12] CreateFloatTexture: %dx%d RGBA32F", w, h);
    return tex;
  }

  BaseRT* D3D12Device::CreateRT(int nrt, int cf, int df, int w, int h, bool genMips) {
    D3D12RT* rt = new D3D12RT;
    if (rt->LoadRT(nrt, cf, df, w, h, genMips)) return rt;
    delete rt;
    return nullptr;
  }

} // namespace t850

#endif // OS_WINDOWS
