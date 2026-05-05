#include <pch.h>
/*********************************************************
* T850 Engine — D3D12 Backend
* D3D12Texture.cpp: Texture upload, SRV creation, binding
*********************************************************/

#include <video/d3d12/D3D12Driver.h>

#ifdef OS_WINDOWS

#include <utils/Log.h>
#include <debug/RenderTrace.h>

namespace t850 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  static D3D12Driver* GetD3D12Driver() { return static_cast<D3D12Driver*>(g_pBaseDriver); }
  static ID3D12Device* GetNativeDevice() { return static_cast<D3D12Device*>(T8Device)->GetNativeDevice(); }

  namespace {
    UINT CalculateFullMipCount(UINT width, UINT height) {
      UINT levels = 1;
      while (width > 1 || height > 1) {
        width = width > 1 ? (width >> 1) : 1;
        height = height > 1 ? (height >> 1) : 1;
        ++levels;
      }
      return levels;
    }

    void GenerateMipChain8(const unsigned char* src, UINT width, UINT height, UINT faceCount,
                           UINT bytesPerPixel, std::vector<unsigned char>& outData) {
      const UINT mipCount = CalculateFullMipCount(width, height);
      size_t totalBytes = 0;
      for (UINT face = 0; face < faceCount; ++face) {
        UINT mipWidth = width;
        UINT mipHeight = height;
        for (UINT mip = 0; mip < mipCount; ++mip) {
          totalBytes += static_cast<size_t>(mipWidth) * mipHeight * bytesPerPixel;
          mipWidth = mipWidth > 1 ? (mipWidth >> 1) : 1;
          mipHeight = mipHeight > 1 ? (mipHeight >> 1) : 1;
        }
      }

      outData.resize(totalBytes);
      size_t dstOffset = 0;
      const size_t baseFaceBytes = static_cast<size_t>(width) * height * bytesPerPixel;

      for (UINT face = 0; face < faceCount; ++face) {
        UINT prevWidth = width;
        UINT prevHeight = height;
        const unsigned char* prev = src + static_cast<size_t>(face) * baseFaceBytes;
        size_t prevBytes = baseFaceBytes;

        memcpy(outData.data() + dstOffset, prev, prevBytes);
        size_t prevOffset = dstOffset;
        dstOffset += prevBytes;

        for (UINT mip = 1; mip < mipCount; ++mip) {
          UINT mipWidth = prevWidth > 1 ? (prevWidth >> 1) : 1;
          UINT mipHeight = prevHeight > 1 ? (prevHeight >> 1) : 1;
          unsigned char* dst = outData.data() + dstOffset;
          const unsigned char* srcMip = outData.data() + prevOffset;

          for (UINT y = 0; y < mipHeight; ++y) {
            for (UINT x = 0; x < mipWidth; ++x) {
              UINT sx0 = x * 2;
              UINT sy0 = y * 2;
              UINT sx1 = (sx0 + 1 < prevWidth) ? sx0 + 1 : sx0;
              UINT sy1 = (sy0 + 1 < prevHeight) ? sy0 + 1 : sy0;
              for (UINT c = 0; c < bytesPerPixel; ++c) {
                UINT accum = 0;
                accum += srcMip[(static_cast<size_t>(sy0) * prevWidth + sx0) * bytesPerPixel + c];
                accum += srcMip[(static_cast<size_t>(sy0) * prevWidth + sx1) * bytesPerPixel + c];
                accum += srcMip[(static_cast<size_t>(sy1) * prevWidth + sx0) * bytesPerPixel + c];
                accum += srcMip[(static_cast<size_t>(sy1) * prevWidth + sx1) * bytesPerPixel + c];
                dst[(static_cast<size_t>(y) * mipWidth + x) * bytesPerPixel + c] = static_cast<unsigned char>((accum + 2) / 4);
              }
            }
          }

          prevOffset = dstOffset;
          prevWidth = mipWidth;
          prevHeight = mipHeight;
          dstOffset += static_cast<size_t>(mipWidth) * mipHeight * bytesPerPixel;
        }
      }
    }
  }

  // ══════════════════════════════════════════════════════
  //  D3D12Texture — Uncompressed upload
  // ══════════════════════════════════════════════════════

  void D3D12Texture::LoadAPITexture(DeviceContext* context, unsigned char* buffer) {
    ID3D12Device* device = GetNativeDevice();
    auto* driver = GetD3D12Driver();

    DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    int bytesPerPixel = 4;
    if (this->props & TextBasicFormat::CH_ALPHA) { fmt = DXGI_FORMAT_R8_UNORM; bytesPerPixel = 1; }
    if (cil_props & CIL_HALF_FLOAT) { fmt = DXGI_FORMAT_R16G16B16A16_FLOAT; bytesPerPixel = 8; }

    bool isCube = (cil_props & CIL_CUBE_MAP) != 0;
    UINT arraySize = isCube ? 6 : 1;
    bool hasSourceMips = mipmaps > 1;
    UINT mipCount = hasSourceMips ? mipmaps : 1;
    std::vector<unsigned char> generatedMips;
    if (!hasSourceMips && !(cil_props & CIL_HALF_FLOAT) && buffer) {
      mipCount = CalculateFullMipCount(this->x, this->y);
      if (mipCount > 1) {
        GenerateMipChain8(buffer, this->x, this->y, arraySize, static_cast<UINT>(bytesPerPixel), generatedMips);
        buffer = generatedMips.data();
      }
    }

    // Create texture resource
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = this->x;
    texDesc.Height = this->y;
    texDesc.DepthOrArraySize = arraySize;
    texDesc.MipLevels = static_cast<UINT16>(mipCount);
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
    UINT totalSubresources = arraySize * mipCount;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(totalSubresources);
    std::vector<UINT> numRows(totalSubresources);
    std::vector<UINT64> rowSizes(totalSubresources);
    device->GetCopyableFootprints(&texDesc, 0, totalSubresources, 0, footprints.data(), numRows.data(), rowSizes.data(), &uploadSize);

    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = uploadSize;
    uploadDesc.Height = 1; uploadDesc.DepthOrArraySize = 1; uploadDesc.MipLevels = 1;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES uploadHeap = {}; uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    ComPtr<ID3D12Resource> uploadBuf;
    HRESULT hrUp = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuf));
    if (FAILED(hrUp)) { T8_LOG_ERROR("[D3D12] Texture upload buffer creation failed hr=0x%08X", hrUp); this->id = (unsigned)-1; return; }

    // Map and copy
    void* mapped = nullptr;
    uploadBuf->Map(0, nullptr, &mapped);
    size_t sourceOffset = 0;
    for (UINT face = 0; face < arraySize; face++) {
      UINT mipWidth = this->x;
      UINT mipHeight = this->y;
      for (UINT mip = 0; mip < mipCount; ++mip) {
        UINT subresource = mip + face * mipCount;
        UINT srcPitch = mipWidth * bytesPerPixel;
        auto& fp = footprints[subresource];
        unsigned char* src = buffer + sourceOffset;
        unsigned char* dst = (unsigned char*)mapped + fp.Offset;
        for (UINT row = 0; row < numRows[subresource]; row++) {
          memcpy(dst + row * fp.Footprint.RowPitch, src + row * srcPitch,
                 (size_t)(srcPitch < fp.Footprint.RowPitch ? srcPitch : fp.Footprint.RowPitch));
        }
        sourceOffset += static_cast<size_t>(srcPitch) * mipHeight;
        mipWidth >>= 1; if (mipWidth < 1) mipWidth = 1;
        mipHeight >>= 1; if (mipHeight < 1) mipHeight = 1;
      }
    }
    uploadBuf->Unmap(0, nullptr);

    driver->UploadTextureSubresources(pTexResource.Get(), uploadBuf.Get(), footprints.data(), totalSubresources,
                                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // Create SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = fmt;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (isCube) {
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
      srvDesc.TextureCube.MipLevels = mipCount;
    } else {
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srvDesc.Texture2D.MipLevels = mipCount;
    }

    srvCPU = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateCPU();
    srvGPU = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateGPU();
    device->CreateShaderResourceView(pTexResource.Get(), &srvDesc, srvCPU);

    static int texId = 0;
    this->id = texId++;

    this->mipmaps = mipCount;
    T8_LOG_DEBUG("[D3D12] Texture created: '%s' -> slot %d (%ux%u, fmt=%d mips=%u%s)",
                 filepath.c_str(), this->id, this->x, this->y, fmt, mipCount, isCube ? ", cube" : "");
    if (isCube) {
      T8_LOG_INFO("[D3D12] Cubemap image created: %ux%u x6 faces, fmt=%d mips=%u", this->x, this->y, (int)fmt, mipCount);
    }
    T8_LOG_INFO("[D3D12] LoadAPITexture OK (%ux%u ch=%u fmt=%d)", this->x, this->y, m_channels, (int)fmt);
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
    HRESULT hrUp = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuf));
    if (FAILED(hrUp)) { T8_LOG_ERROR("[D3D12] Compressed tex upload buffer failed hr=0x%08X", hrUp); this->id = (unsigned)-1; return; }

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

    driver->UploadTextureSubresources(pTexResource.Get(), uploadBuf.Get(), footprints.data(), totalSubs,
                                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

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
    T8_LOG_INFO("[D3D12] Compressed texture OK: %ux%u fmt=%d mips=%u faces=%u cube=%d",
                this->x, this->y, (int)fmt, mipCount, numFaces, (int)isCube);
  }

  void D3D12Texture::DestroyAPITexture() { pTexResource.Reset(); }

  void D3D12Texture::SetTextureParams() {
    ID3D12Device* device = GetNativeDevice();
    auto* driver = GetD3D12Driver();

    D3D12_SAMPLER_DESC sd = {};

    // Filter
    sd.Filter = D3D12_FILTER_ANISOTROPIC;
    sd.MaxAnisotropy = 16;

    if ((cil_props & CIL_CUBE_MAP) && !(params & (TextBasicParams::NEAREST_FILTER | TextBasicParams::LINEAR_FILTER | TextBasicParams::CLAMP_TO_BORDER))) {
      sd.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
      sd.MaxAnisotropy = 1;
    }

    if (params & TextBasicParams::NEAREST_FILTER) {
      sd.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
      sd.MaxAnisotropy = 1;
    } else if (params & TextBasicParams::LINEAR_FILTER) {
      sd.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
      sd.MaxAnisotropy = 1;
    }

    // Address mode — default CLAMP
    sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    if (params & TextBasicParams::TILED) {
      sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    }

    if (params & TextBasicParams::CLAMP_TO_BORDER) {
      sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
      sd.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
      sd.MaxAnisotropy = 1;
      sd.BorderColor[0] = 1.0f;
      sd.BorderColor[1] = 1.0f;
      sd.BorderColor[2] = 1.0f;
      sd.BorderColor[3] = 1.0f;
    }

    sd.MinLOD = 0.0f;
    sd.MaxLOD = (params & (TextBasicParams::NEAREST_FILTER | TextBasicParams::LINEAR_FILTER)) ? 0.0f : D3D12_FLOAT32_MAX;
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
    T8_LOG_TRACE("[D3D12] Texture::Set slot=%u name='%s' file='%s' srvGPU=0x%llX", slot, shaderTextureName.c_str(), filepath.c_str(), srvGPU.ptr);
    auto* cmdList = static_cast<const D3D12DeviceContext*>(&deviceContext)->GetCommandList();
    auto* shader = static_cast<D3D12Shader*>(deviceContext.actualShaderSet);
    if (!shader) return;

    auto it = shader->srvSlots.find(slot);
    if (it != shader->srvSlots.end()) {
      cmdList->SetGraphicsRootDescriptorTable(it->second, srvGPU);
    }
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int texId = g_renderTracer->LookupTextureId(this);
      // D3D12 binds are synchronous: emit both Request (engine intent) and
      // Commit (what the API actually got) at the same site so trace diffs
      // line up with Vulkan's split request/commit pair.
      g_renderTracer->EvBindTextureRequest(slot, texId, shaderTextureName, "ps");
      // Use the SRV GPU descriptor handle low 32 bits as a viewId surrogate
      // (stable per-process, unique per srvGPU range) — same diff strategy
      // as Vulkan's view pointer surrogate.
      int viewId    = (int)(srvGPU.ptr & 0xFFFFFFFFu);
      // Replace the descriptor-handle pointer surrogate with a logical
      // sampler signature so D3D12's id matches GL/D3D11/Vulkan when the
      // sampler params are equivalent. (The handle was unique-per-process
      // and never registered, making cross-API diff useless.)
      int samplerId = g_renderTracer->RegisterSampler(
        RenderTracer::MakeSamplerSigD3D12(params, (cil_props & CIL_CUBE_MAP) != 0));
      g_renderTracer->EvBindTextureCommit(slot, texId, viewId, samplerId, shaderTextureName, "ps");
    }
#endif
  }

  void D3D12Texture::SetVS(const DeviceContext& deviceContext, unsigned int slot, std::string shaderTextureName) {
    T8_LOG_TRACE("[D3D12] Texture::SetVS slot=%u name='%s' file='%s' srvGPU=0x%llX", slot, shaderTextureName.c_str(), filepath.c_str(), srvGPU.ptr);
    auto* cmdList = static_cast<const D3D12DeviceContext*>(&deviceContext)->GetCommandList();
    auto* shader = static_cast<D3D12Shader*>(deviceContext.actualShaderSet);
    if (!shader) return;

    auto it = shader->srvSlots.find(slot);
    if (it != shader->srvSlots.end()) {
      cmdList->SetGraphicsRootDescriptorTable(it->second, srvGPU);
    }
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int texId = g_renderTracer->LookupTextureId(this);
      g_renderTracer->EvBindTextureRequest(slot, texId, shaderTextureName, "vs");
      int viewId    = (int)(srvGPU.ptr & 0xFFFFFFFFu);
      int samplerId = g_renderTracer->RegisterSampler(
        RenderTracer::MakeSamplerSigD3D12(params, (cil_props & CIL_CUBE_MAP) != 0));
      g_renderTracer->EvBindTextureCommit(slot, texId, viewId, samplerId, shaderTextureName, "vs");
    }
#endif
  }

  void D3D12Texture::SetSampler(const DeviceContext& deviceContext, unsigned int slot) {
    if (!hasSampler) return;
    auto* cmdList = static_cast<const D3D12DeviceContext*>(&deviceContext)->GetCommandList();
    auto* shader = static_cast<D3D12Shader*>(deviceContext.actualShaderSet);
    if (shader) {
      auto it = shader->samplerSlots.find(slot);
      if (it == shader->samplerSlots.end()) return;
      T8_LOG_TRACE("[D3D12] Texture::SetSampler slot=%u file='%s'", slot, filepath.c_str());
      cmdList->SetGraphicsRootDescriptorTable(it->second, samplerGPU);
    }
  }

  void D3D12Texture::UpdateFloatData(const DeviceContext& deviceContext, int w, int h, const float* data) {
    if (!pTexResource || !m_uploadBuffer || !data) return;
    auto* cmdList = static_cast<const D3D12DeviceContext*>(&deviceContext)->GetCommandList();
    auto* device = reinterpret_cast<ID3D12Device*>(T8Device->GetAPIObject());

    // Get texture footprint for row pitch
    D3D12_RESOURCE_DESC texDesc = pTexResource->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
    UINT numRows; UINT64 rowSize;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, &layout, &numRows, &rowSize, nullptr);

    // Copy data to upload buffer (respecting row pitch alignment)
    void* mapped = nullptr;
    m_uploadBuffer->Map(0, nullptr, &mapped);
    for (UINT r = 0; r < numRows; r++) {
      memcpy((uint8_t*)mapped + layout.Offset + r * layout.Footprint.RowPitch,
             (const uint8_t*)data + r * w * 16, w * 16);
    }
    m_uploadBuffer->Unmap(0, nullptr);

    const D3D12_RESOURCE_STATES shaderReadState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    // Barrier: SRV → COPY_DEST
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = pTexResource.Get();
    barrier.Transition.StateBefore = shaderReadState;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    cmdList->ResourceBarrier(1, &barrier);

    // Copy upload buffer to texture
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    dst.pResource = pTexResource.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = m_uploadBuffer.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = layout;
    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    // Barrier: COPY_DEST → SRV
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = shaderReadState;
    cmdList->ResourceBarrier(1, &barrier);
  }

} // namespace t850

#endif // OS_WINDOWS
