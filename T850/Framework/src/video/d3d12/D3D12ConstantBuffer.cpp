#include <pch.h>
/*********************************************************
* T850 Engine — D3D12 Backend
* D3D12ConstantBuffer.cpp: Constant buffer implementation
*********************************************************/

#include <video/d3d12/D3D12ConstantBuffer.h>
#include <video/d3d12/D3D12DeviceContext.h>
#include <video/d3d12/D3D12Device.h>
#include <video/d3d12/D3D12Driver.h>
#include <video/d3d12/D3D12Shader.h>

#ifdef OS_WINDOWS

#include <utils/Log.h>
#include <debug/RenderTrace.h>

namespace t850 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  static D3D12Driver* GetD3D12Driver() { return static_cast<D3D12Driver*>(g_pBaseDriver); }
  static ID3D12Device* GetNativeDevice() { return static_cast<D3D12Device*>(T8Device)->GetNativeDevice(); }

  // ══════════════════════════════════════════════════════
  //  D3D12 Buffers — Constant Buffer
  // ══════════════════════════════════════════════════════

  void* D3D12ConstantBuffer::GetAPIObject() const { return (void*)m_buffer.Get(); }
  void** D3D12ConstantBuffer::GetAPIObjectReference() const { return nullptr; }

  void D3D12ConstantBuffer::Create(const Device& device, BufferDesc desc, void* initialData) {
    descriptor = desc;
    ID3D12Device* dev = GetNativeDevice();
    auto* driver = GetD3D12Driver();

    m_alignedSize = (desc.byteWidth + 255) & ~255;

    D3D12_HEAP_PROPERTIES heapProps = {}; heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = m_alignedSize;
    bufDesc.Height = 1; bufDesc.DepthOrArraySize = 1; bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = dev->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&m_buffer));
    if (FAILED(hr)) { T8_LOG_ERROR("[D3D12] CB create failed hr=0x%08X size=%d", hr, m_alignedSize); return; }

    m_buffer->Map(0, nullptr, &m_mappedData);

    if (initialData) {
      sysMemCpy.assign((char*)initialData, (char*)initialData + desc.byteWidth);
      memcpy(m_mappedData, initialData, desc.byteWidth);
    }

    // Create CBV in shader-visible heap
    m_cpuHandle = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateCPU();
    m_gpuHandle = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateGPU();

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = m_buffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = m_alignedSize;
    dev->CreateConstantBufferView(&cbvDesc, m_cpuHandle);

    T8_LOG_DEBUG("[D3D12] CB created: %d bytes (aligned=%d)", desc.byteWidth, m_alignedSize);
  }

  void D3D12ConstantBuffer::Set(const DeviceContext& deviceContext, unsigned int slot) {
    const_cast<DeviceContext*>(&deviceContext)->actualConstantBuffer = (ConstantBuffer*)this;
    auto* cmdList = static_cast<const D3D12DeviceContext*>(&deviceContext)->GetCommandList();
    auto* shader = static_cast<D3D12Shader*>(deviceContext.actualShaderSet);
    int rootParam = -1;
    if (shader) {
      auto it = shader->cbvSlots.find((int)slot);
      if (it != shader->cbvSlots.end()) rootParam = it->second;
    }
    if (shader && rootParam >= 0 && !sysMemCpy.empty()) {
      auto* driver = GetD3D12Driver();
      // Allocate ring buffer space for this draw's CB data
      D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = driver->AllocateCBData(sysMemCpy.data(), (UINT)sysMemCpy.size());
      // Bind inline root CBV — no descriptor table / CreateConstantBufferView needed
      cmdList->SetGraphicsRootConstantBufferView(rootParam, gpuAddr);
      T8_LOG_TRACE("[D3D12] CB::Set slot=%u rootParam=%d gpuVA=0x%llX dataSize=%d",
                   slot, rootParam, gpuAddr, (int)sysMemCpy.size());
#ifdef T850_RENDER_TRACE
      if (T8_TRACE_ACTIVE()) {
        int bufId = g_renderTracer->EnsureBufferId(this, "cbuffer");
        // Record the upload (offset = ring address low bits as a surrogate),
        // then both Request and Commit at the same site (D3D12 binds are
        // synchronous — the inline root CBV is committed immediately).
        g_renderTracer->EvUpdateCBuffer(bufId, sysMemCpy.data(),
                                        (uint32_t)sysMemCpy.size(),
                                        (uint32_t)(gpuAddr & 0xFFFFFFFFu));
        g_renderTracer->EvBindCBufferRequest(bufId);
        g_renderTracer->EvBindCBufferCommit((int)slot, bufId);
      }
#endif
    }
  }

  void D3D12ConstantBuffer::UpdateFromSystemCopy(const DeviceContext&) {
    if (m_mappedData && !sysMemCpy.empty()) memcpy(m_mappedData, sysMemCpy.data(), sysMemCpy.size());
  }
  void D3D12ConstantBuffer::UpdateFromBuffer(const DeviceContext&, const void* buffer) {
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    if (m_mappedData) memcpy(m_mappedData, buffer, descriptor.byteWidth);
  }
  void D3D12ConstantBuffer::release() {
    if (m_mappedData) { m_buffer->Unmap(0, nullptr); m_mappedData = nullptr; }
    m_buffer.Reset(); sysMemCpy.clear(); delete this;
  }

} // namespace t850

#endif // OS_WINDOWS
