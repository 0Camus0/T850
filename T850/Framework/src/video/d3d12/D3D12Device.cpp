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
#include <vector>

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

  Texture* D3D12Device::CreateFloatCubeMap(int size, int mipCount, const float* data) {
    if (size <= 0 || mipCount <= 0)
      return nullptr;

    auto* driver = static_cast<D3D12Driver*>(g_pBaseDriver);
    auto* device = reinterpret_cast<ID3D12Device*>(GetAPIObject());
    D3D12Texture* tex = new D3D12Texture;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = size;
    texDesc.Height = size;
    texDesc.DepthOrArraySize = 6;
    texDesc.MipLevels = static_cast<UINT16>(mipCount);
    texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES defaultHeap = { D3D12_HEAP_TYPE_DEFAULT };
    HRESULT hr = device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE,
        &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(tex->pTexResource.GetAddressOf()));
    if (FAILED(hr)) { T8_LOG_ERROR("[D3D12] CreateFloatCubeMap failed (resource)"); delete tex; return nullptr; }

    UINT totalSubresources = 6u * static_cast<UINT>(mipCount);
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(totalSubresources);
    std::vector<UINT> numRows(totalSubresources);
    std::vector<UINT64> rowSizes(totalSubresources);
    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(&texDesc, 0, totalSubresources, 0, footprints.data(), numRows.data(), rowSizes.data(), &uploadSize);

    D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = uploadSize;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> uploadBuf;
    hr = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE,
        &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuf));
    if (FAILED(hr)) { T8_LOG_ERROR("[D3D12] CreateFloatCubeMap failed (upload)"); delete tex; return nullptr; }

    std::vector<float> zeroData;
    const float* sourceFloats = data;
    if (!sourceFloats) {
      size_t floatCount = 0;
      int mipSize = size;
      for (int mip = 0; mip < mipCount; ++mip) {
        floatCount += size_t(mipSize) * size_t(mipSize) * 4u * 6u;
        mipSize >>= 1; if (mipSize < 1) mipSize = 1;
      }
      zeroData.assign(floatCount, 0.0f);
      sourceFloats = zeroData.data();
    }

    void* mapped = nullptr;
    uploadBuf->Map(0, nullptr, &mapped);
    const uint8_t* sourceBytes = reinterpret_cast<const uint8_t*>(sourceFloats);
    size_t sourceOffset = 0;
    for (UINT face = 0; face < 6; ++face) {
      UINT mipSize = static_cast<UINT>(size);
      for (UINT mip = 0; mip < static_cast<UINT>(mipCount); ++mip) {
        UINT subresource = mip + face * static_cast<UINT>(mipCount);
        UINT srcPitch = mipSize * 16;
        auto& fp = footprints[subresource];
        uint8_t* dst = reinterpret_cast<uint8_t*>(mapped) + fp.Offset;
        const uint8_t* src = sourceBytes + sourceOffset;
        for (UINT row = 0; row < numRows[subresource]; ++row)
          memcpy(dst + row * fp.Footprint.RowPitch, src + row * srcPitch, srcPitch);
        sourceOffset += size_t(srcPitch) * mipSize;
        mipSize >>= 1; if (mipSize < 1) mipSize = 1;
      }
    }
    uploadBuf->Unmap(0, nullptr);

    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList> cmd;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmd));

    for (UINT subresource = 0; subresource < totalSubresources; ++subresource) {
      D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
      dst.pResource = tex->pTexResource.Get();
      dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      dst.SubresourceIndex = subresource;
      src.pResource = uploadBuf.Get();
      src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      src.PlacedFootprint = footprints[subresource];
      cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = tex->pTexResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &barrier);
    cmd->Close();

    ID3D12CommandList* lists[] = { cmd.Get() };
    driver->GetCmdQueue()->ExecuteCommandLists(1, lists);
    ComPtr<ID3D12Fence> fence;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    driver->GetCmdQueue()->Signal(fence.Get(), 1);
    if (fence->GetCompletedValue() < 1) {
      fence->SetEventOnCompletion(1, evt);
      WaitForSingleObject(evt, INFINITE);
    }
    CloseHandle(evt);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.TextureCube.MipLevels = mipCount;
    srvDesc.TextureCube.MostDetailedMip = 0;
    tex->srvCPU = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateCPU();
    tex->srvGPU = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateGPU();
    device->CreateShaderResourceView(tex->pTexResource.Get(), &srvDesc, tex->srvCPU);

    tex->x = size;
    tex->y = size;
    tex->mipmaps = mipCount;
    tex->m_channels = 4;
    tex->props = TextBasicFormat::CH_RGBA;
    tex->cil_props = CIL_CUBE_MAP;
    tex->params = TextBasicParams::CLAMP_TO_EDGE | TextBasicParams::MIPMAPS;
    tex->SetTextureParams();

    T8_LOG_INFO("[D3D12] CreateFloatCubeMap: %dx%d mips=%d RGBA32F", size, size, mipCount);
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
