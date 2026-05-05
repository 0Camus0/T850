#include <pch.h>
/*********************************************************
* T850 Engine — D3D12 Backend
* D3D12VertexBuffer.cpp: Vertex buffer implementation
*********************************************************/

#include <video/d3d12/D3D12VertexBuffer.h>
#include <video/d3d12/D3D12DeviceContext.h>
#include <video/d3d12/D3D12Device.h>
#include <video/d3d12/D3D12Driver.h>

#ifdef OS_WINDOWS

#include <utils/Log.h>
#include <debug/RenderTrace.h>

namespace t850 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  static D3D12Driver* GetD3D12Driver() { return static_cast<D3D12Driver*>(g_pBaseDriver); }
  static ID3D12Device* GetNativeDevice() { return static_cast<D3D12Device*>(T8Device)->GetNativeDevice(); }

  // ══════════════════════════════════════════════════════
  //  D3D12 Buffers — Vertex Buffer
  // ══════════════════════════════════════════════════════

  void* D3D12VertexBuffer::GetAPIObject() const { return (void*)m_buffer.Get(); }
  void** D3D12VertexBuffer::GetAPIObjectReference() const { return nullptr; }

  void D3D12VertexBuffer::Create(const Device& device, BufferDesc desc, void* initialData) {
    descriptor = desc;
    ID3D12Device* dev = GetNativeDevice();
    auto* driver = GetD3D12Driver();

    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = desc.byteWidth;
    bufDesc.Height = 1; bufDesc.DepthOrArraySize = 1; bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (initialData && desc.usage == BufferUsage::DEFAULT) {
      // Static buffer: create in GPU-local DEFAULT heap, upload via staging
      D3D12_HEAP_PROPERTIES defaultHeap = {}; defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
      HRESULT hr = dev->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&m_buffer));
      if (FAILED(hr)) { T8_LOG_ERROR("[D3D12] VB create failed hr=0x%08X", hr); return; }

      driver->UploadBufferData(m_buffer.Get(), initialData, desc.byteWidth,
                               D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

      sysMemCpy.assign((char*)initialData, (char*)initialData + desc.byteWidth);
      m_mappedData = nullptr; // not persistently mapped for DEFAULT heap
    } else {
      // Dynamic buffer or no initial data: use UPLOAD heap (persistently mapped)
      D3D12_HEAP_PROPERTIES uploadHeap = {}; uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
      HRESULT hr = dev->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                 IID_PPV_ARGS(&m_buffer));
      if (FAILED(hr)) { T8_LOG_ERROR("[D3D12] VB create failed hr=0x%08X", hr); return; }
      m_buffer->Map(0, nullptr, &m_mappedData);
      if (initialData) {
        sysMemCpy.assign((char*)initialData, (char*)initialData + desc.byteWidth);
        memcpy(m_mappedData, initialData, desc.byteWidth);
      }
    }

    m_view.BufferLocation = m_buffer->GetGPUVirtualAddress();
    m_view.SizeInBytes = desc.byteWidth;
    m_view.StrideInBytes = 0;
    T8_LOG_DEBUG("[D3D12] VB created: %d bytes%s", desc.byteWidth,
                 m_mappedData ? " (upload)" : " (default)");
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE() && initialData) {
      int bufId = g_renderTracer->EnsureBufferId(this, "vb");
      g_renderTracer->RecordBufferUpdate(bufId, initialData, desc.byteWidth, "vb", "");
    }
#endif
  }

  void D3D12VertexBuffer::Set(const DeviceContext& deviceContext, const unsigned stride, const unsigned offset) {
    T8_LOG_TRACE("[D3D12] VB::Set stride=%u gpuVA=0x%llX size=%u", stride, m_view.BufferLocation, m_view.SizeInBytes);
    const_cast<DeviceContext*>(&deviceContext)->actualVertexBuffer = (VertexBuffer*)this;
    m_view.StrideInBytes = stride;
    auto* cmdList = static_cast<const D3D12DeviceContext*>(&deviceContext)->GetCommandList();
    cmdList->IASetVertexBuffers(0, 1, &m_view);
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int bufId = g_renderTracer->EnsureBufferId(this, "vb");
      g_renderTracer->EvBindVertexBufferRequest(bufId, stride, offset);
    }
#endif
  }

  void D3D12VertexBuffer::UpdateFromSystemCopy(const DeviceContext& deviceContext) {
    if (!sysMemCpy.empty()) {
      // D3D12: suballocate from the per-frame ring buffer so each draw
      // within the same command list sees its own vertex data.
      auto* driver = GetD3D12Driver();
      UINT size = (UINT)sysMemCpy.size();
      D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = driver->AllocateRingData(sysMemCpy.data(), size);
      m_view.BufferLocation = gpuAddr;
      m_view.SizeInBytes = size;
      // Rebind so the next DrawIndexed picks up the new address
      auto* cmdList = static_cast<const D3D12DeviceContext*>(&deviceContext)->GetCommandList();
      cmdList->IASetVertexBuffers(0, 1, &m_view);
#ifdef T850_RENDER_TRACE
      if (T8_TRACE_ACTIVE()) {
        int bufId = g_renderTracer->EnsureBufferId(this, "vb");
        g_renderTracer->RecordBufferUpdate(bufId, sysMemCpy.data(), size, "vb", "");
      }
#endif
    }
  }
  void D3D12VertexBuffer::UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) {
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    // D3D12: suballocate from the per-frame ring buffer so each draw
    // within the same command list sees its own vertex data.
    auto* driver = GetD3D12Driver();
    UINT size = descriptor.byteWidth;
    D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = driver->AllocateRingData(buffer, size);
    m_view.BufferLocation = gpuAddr;
    m_view.SizeInBytes = size;
    // Rebind so the next DrawIndexed picks up the new address
    auto* cmdList = static_cast<const D3D12DeviceContext*>(&deviceContext)->GetCommandList();
    cmdList->IASetVertexBuffers(0, 1, &m_view);
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int bufId = g_renderTracer->EnsureBufferId(this, "vb");
      g_renderTracer->RecordBufferUpdate(bufId, buffer, size, "vb", "");
    }
#endif
  }
  void D3D12VertexBuffer::release() {
    if (m_mappedData) { m_buffer->Unmap(0, nullptr); m_mappedData = nullptr; }
    m_buffer.Reset(); sysMemCpy.clear(); delete this;
  }

} // namespace t850

#endif // OS_WINDOWS
