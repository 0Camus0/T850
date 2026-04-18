/*********************************************************
* T850 Engine — D3D12 Backend
* D3D12Texture.cpp: Texture upload, SRV creation, binding
*********************************************************/

#include <video/d3d12/D3D12Driver.h>

#ifdef OS_WINDOWS

#include <utils/Log.h>

namespace t800 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  static D3D12Driver* GetD3D12Driver() { return static_cast<D3D12Driver*>(g_pBaseDriver); }
  static ID3D12Device* GetNativeDevice() { return static_cast<D3D12Device*>(T8Device)->GetNativeDevice(); }

  // ══════════════════════════════════════════════════════
  //  D3D12Texture — Uncompressed upload
  // ══════════════════════════════════════════════════════

  void D3D12Texture::LoadAPITexture(DeviceContext* context, unsigned char* buffer) {
    ID3D12Device* device = GetNativeDevice();
    auto* driver = GetD3D12Driver();

    DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    int bytesPerPixel = 4;
    if (this->props & TEXT_BASIC_FORMAT::CH_ALPHA) { fmt = DXGI_FORMAT_R8_UNORM; bytesPerPixel = 1; }
    if (cil_props & CIL_HALF_FLOAT) { fmt = DXGI_FORMAT_R16G16B16A16_FLOAT; bytesPerPixel = 8; }

    bool isCube = (cil_props & CIL_CUBE_MAP) != 0;
    UINT arraySize = isCube ? 6 : 1;

    // Create texture resource
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = this->x;
    texDesc.Height = this->y;
    texDesc.DepthOrArraySize = arraySize;
    texDesc.MipLevels = 1;
    texDesc.Format = fmt;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProps = {}; heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    HRESULT hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                  IID_PPV_ARGS(&pTexResource));
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D12] Texture CreateCommittedResource failed hr=0x%08X (%ux%u)", hr, this->x, this->y);
      this->id = (unsigned)-1; return;
    }

    // Upload via staging buffer
    UINT64 uploadSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[6];
    UINT numRows[6]; UINT64 rowSizes[6];
    device->GetCopyableFootprints(&texDesc, 0, arraySize, 0, footprints, numRows, rowSizes, &uploadSize);

    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = uploadSize;
    uploadDesc.Height = 1; uploadDesc.DepthOrArraySize = 1; uploadDesc.MipLevels = 1;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES uploadHeap = {}; uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    ComPtr<ID3D12Resource> uploadBuf;
    device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuf));

    // Map and copy
    void* mapped = nullptr;
    uploadBuf->Map(0, nullptr, &mapped);
    UINT srcPitch = this->x * bytesPerPixel;
    for (UINT face = 0; face < arraySize; face++) {
      auto& fp = footprints[face];
      unsigned char* src = buffer + face * (this->x * this->y * bytesPerPixel);
      unsigned char* dst = (unsigned char*)mapped + fp.Offset;
      for (UINT row = 0; row < numRows[face]; row++) {
        memcpy(dst + row * fp.Footprint.RowPitch, src + row * srcPitch,
               (size_t)(srcPitch < fp.Footprint.RowPitch ? srcPitch : fp.Footprint.RowPitch));
      }
    }
    uploadBuf->Unmap(0, nullptr);

    // Copy via temp command list
    ComPtr<ID3D12CommandAllocator> tmpAlloc;
    ComPtr<ID3D12GraphicsCommandList> tmpList;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tmpAlloc));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, tmpAlloc.Get(), nullptr, IID_PPV_ARGS(&tmpList));

    for (UINT face = 0; face < arraySize; face++) {
      D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
      dst.pResource = pTexResource.Get();
      dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      dst.SubresourceIndex = face;
      src.pResource = uploadBuf.Get();
      src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      src.PlacedFootprint = footprints[face];
      tmpList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }

    // Barrier: COPY_DEST -> PIXEL_SHADER_RESOURCE
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = pTexResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    tmpList->ResourceBarrier(1, &barrier);

    tmpList->Close();
    ID3D12CommandList* lists[] = { tmpList.Get() };
    driver->GetCmdQueue()->ExecuteCommandLists(1, lists);

    // Fence wait for upload
    ComPtr<ID3D12Fence> tmpFence;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&tmpFence));
    HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    driver->GetCmdQueue()->Signal(tmpFence.Get(), 1);
    if (tmpFence->GetCompletedValue() < 1) {
      tmpFence->SetEventOnCompletion(1, evt);
      WaitForSingleObject(evt, INFINITE);
    }
    CloseHandle(evt);

    // Create SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = fmt;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (isCube) {
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
      srvDesc.TextureCube.MipLevels = 1;
    } else {
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srvDesc.Texture2D.MipLevels = 1;
    }

    srvCPU = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateCPU();
    srvGPU = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateGPU();
    device->CreateShaderResourceView(pTexResource.Get(), &srvDesc, srvCPU);

    static int texId = 0;
    this->id = texId++;

    T8_LOG_DEBUG("[D3D12] Texture created: '%s' -> slot %d (%ux%u, fmt=%d%s)",
                 filepath.c_str(), this->id, this->x, this->y, fmt, isCube ? ", cube" : "");
  }

  // ══════════════════════════════════════════════════════
  //  D3D12Texture — Compressed upload (BC1/BC2/BC3)
  // ══════════════════════════════════════════════════════

  void D3D12Texture::LoadAPITextureCompressed(unsigned char* buffer) {
    ID3D12Device* device = GetNativeDevice();
    auto* driver = GetD3D12Driver();

    DXGI_FORMAT fmt = DXGI_FORMAT_BC1_UNORM;
    int blockSize = 8;
    if (cil_props & CIL_DXT3) { fmt = DXGI_FORMAT_BC2_UNORM; blockSize = 16; }
    else if (cil_props & CIL_DXT5) { fmt = DXGI_FORMAT_BC3_UNORM; blockSize = 16; }

    bool isCube = (cil_props & CIL_CUBE_MAP) != 0;
    UINT numFaces = isCube ? 6 : 1;
    UINT mipCount = (mipmaps > 0) ? mipmaps : 1;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = this->x; texDesc.Height = this->y;
    texDesc.DepthOrArraySize = numFaces;
    texDesc.MipLevels = mipCount;
    texDesc.Format = fmt;
    texDesc.SampleDesc.Count = 1;

    D3D12_HEAP_PROPERTIES heapProps = {}; heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    HRESULT hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                  IID_PPV_ARGS(&pTexResource));
    if (FAILED(hr)) { this->id = (unsigned)-1; T8_LOG_ERROR("[D3D12] Compressed tex create failed"); return; }

    UINT totalSubs = numFaces * mipCount;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(totalSubs);
    std::vector<UINT> numRows(totalSubs);
    std::vector<UINT64> rowSizes(totalSubs);
    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(&texDesc, 0, totalSubs, 0, footprints.data(), numRows.data(), rowSizes.data(), &uploadSize);

    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = uploadSize;
    uploadDesc.Height = 1; uploadDesc.DepthOrArraySize = 1; uploadDesc.MipLevels = 1;
    uploadDesc.SampleDesc.Count = 1; uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES uploadHeap = {}; uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    ComPtr<ID3D12Resource> uploadBuf;
    device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuf));

    void* mapped = nullptr;
    uploadBuf->Map(0, nullptr, &mapped);
    unsigned char* pData = buffer;
    for (UINT face = 0; face < numFaces; face++) {
      int w = this->x, h = this->y;
      for (UINT mip = 0; mip < mipCount; mip++) {
        UINT sub = face * mipCount + mip;
        int wBlocks = (w + 3) / 4; if (wBlocks < 1) wBlocks = 1;
        int hBlocks = (h + 3) / 4; if (hBlocks < 1) hBlocks = 1;
        UINT srcPitch = wBlocks * blockSize;
        auto& fp = footprints[sub];
        unsigned char* dst = (unsigned char*)mapped + fp.Offset;
        for (int row = 0; row < hBlocks; row++) {
          memcpy(dst + row * fp.Footprint.RowPitch, pData + row * srcPitch, srcPitch);
        }
        pData += wBlocks * hBlocks * blockSize;
        w >>= 1; if (w < 1) w = 1;
        h >>= 1; if (h < 1) h = 1;
      }
    }
    uploadBuf->Unmap(0, nullptr);

    // Copy + transition
    ComPtr<ID3D12CommandAllocator> tmpAlloc;
    ComPtr<ID3D12GraphicsCommandList> tmpList;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tmpAlloc));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, tmpAlloc.Get(), nullptr, IID_PPV_ARGS(&tmpList));

    for (UINT sub = 0; sub < totalSubs; sub++) {
      D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
      dst.pResource = pTexResource.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = sub;
      src.pResource = uploadBuf.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = footprints[sub];
      tmpList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = pTexResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    tmpList->ResourceBarrier(1, &barrier);
    tmpList->Close();

    ID3D12CommandList* lists[] = { tmpList.Get() };
    driver->GetCmdQueue()->ExecuteCommandLists(1, lists);
    ComPtr<ID3D12Fence> tmpFence;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&tmpFence));
    HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    driver->GetCmdQueue()->Signal(tmpFence.Get(), 1);
    if (tmpFence->GetCompletedValue() < 1) { tmpFence->SetEventOnCompletion(1, evt); WaitForSingleObject(evt, INFINITE); }
    CloseHandle(evt);

    // SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = fmt;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (isCube) { srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE; srvDesc.TextureCube.MipLevels = mipCount; }
    else { srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srvDesc.Texture2D.MipLevels = mipCount; }

    srvCPU = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateCPU();
    srvGPU = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateGPU();
    device->CreateShaderResourceView(pTexResource.Get(), &srvDesc, srvCPU);

    static int texId = 0;
    this->id = texId++;
    T8_LOG_DEBUG("[D3D12] Compressed texture created: '%s' -> slot %d (%ux%u, fmt=%d, mips=%u)",
                 filepath.c_str(), this->id, this->x, this->y, fmt, mipCount);
  }

  void D3D12Texture::DestroyAPITexture() { pTexResource.Reset(); }

  void D3D12Texture::SetTextureParams() {
    ID3D12Device* device = GetNativeDevice();
    auto* driver = GetD3D12Driver();

    D3D12_SAMPLER_DESC sd = {};

    // Filter
    sd.Filter = D3D12_FILTER_ANISOTROPIC;
    sd.MaxAnisotropy = 16;

    if (params & TEXT_BASIC_PARAMS::NEAREST_FILTER) {
      sd.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
      sd.MaxAnisotropy = 1;
    } else if (params & TEXT_BASIC_PARAMS::LINEAR_FILTER) {
      sd.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
      sd.MaxAnisotropy = 1;
    }

    // Address mode — default CLAMP
    sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    if (params & TEXT_BASIC_PARAMS::TILED) {
      sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    }

    if (params & TEXT_BASIC_PARAMS::CLAMP_TO_BORDER) {
      sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
      sd.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
      sd.MaxAnisotropy = 1;
    }

    sd.MinLOD = 0.0f;
    sd.MaxLOD = (params & (TEXT_BASIC_PARAMS::NEAREST_FILTER | TEXT_BASIC_PARAMS::LINEAR_FILTER)) ? 0.0f : D3D12_FLOAT32_MAX;
    sd.MipLODBias = 0.0f;
    sd.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;

    // Allocate sampler in the sampler heap
    D3D12_CPU_DESCRIPTOR_HANDLE cpuH = driver->GetHeap(D3D12Heap::SAMPLER).AllocateCPU();
    samplerGPU = driver->GetHeap(D3D12Heap::SAMPLER).AllocateGPU();
    device->CreateSampler(&sd, cpuH);
    hasSampler = true;

    T8_LOG_DEBUG("[D3D12] Sampler created for '%s': filter=%d addr=%d",
                 filepath.c_str(), sd.Filter, sd.AddressU);
  }

  void D3D12Texture::GetFormatBpp(unsigned int&, unsigned int&, unsigned int&) {}

  // ══════════════════════════════════════════════════════
  //  D3D12Texture — Bind to shader slot
  // ══════════════════════════════════════════════════════

  void D3D12Texture::Set(const DeviceContext& deviceContext, unsigned int slot, std::string shaderTextureName) {
    T8_LOG_TRACE("[D3D12] Texture::Set slot=%u name='%s' file='%s'", slot, shaderTextureName.c_str(), filepath.c_str());
    auto* cmdList = static_cast<const D3D12DeviceContext*>(&deviceContext)->GetCommandList();
    auto* shader = static_cast<D3D12Shader*>(deviceContext.actualShaderSet);
    if (!shader) return;

    auto it = shader->srvSlots.find(slot);
    if (it != shader->srvSlots.end()) {
      cmdList->SetGraphicsRootDescriptorTable(it->second, srvGPU);
    }
  }

  void D3D12Texture::SetSampler(const DeviceContext& deviceContext, unsigned int slot) {
    if (!hasSampler) return;
    auto* cmdList = static_cast<const D3D12DeviceContext*>(&deviceContext)->GetCommandList();
    auto* shader = static_cast<D3D12Shader*>(deviceContext.actualShaderSet);
    if (shader && shader->samplerSlot >= 0) {
      T8_LOG_TRACE("[D3D12] Texture::SetSampler slot=%u file='%s'", slot, filepath.c_str());
      cmdList->SetGraphicsRootDescriptorTable(shader->samplerSlot, samplerGPU);
    }
  }

} // namespace t800

#endif // OS_WINDOWS
