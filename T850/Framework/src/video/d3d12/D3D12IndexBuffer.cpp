#include "pch.h"
/*********************************************************
* T850 Engine — D3D12 Backend
* D3D12IndexBuffer.cpp: Index buffer implementation
*********************************************************/

#include <video/d3d12/D3D12IndexBuffer.h>
#include <video/d3d12/D3D12DeviceContext.h>
#include <video/d3d12/D3D12Device.h>
#include <video/d3d12/D3D12Driver.h>

#ifdef OS_WINDOWS

#include <utils/Log.h>

namespace t800 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  static D3D12Driver* GetD3D12Driver() { return static_cast<D3D12Driver*>(g_pBaseDriver); }
  static ID3D12Device* GetNativeDevice() { return static_cast<D3D12Device*>(T8Device)->GetNativeDevice(); }

  // ══════════════════════════════════════════════════════
  //  D3D12 Buffers — Index Buffer
  // ══════════════════════════════════════════════════════

  void* D3D12IndexBuffer::GetAPIObject() const { return (void*)m_buffer.Get(); }
  void** D3D12IndexBuffer::GetAPIObjectReference() const { return nullptr; }

  void D3D12IndexBuffer::Create(const Device& device, BufferDesc desc, void* initialData) {
    descriptor = desc;
    ID3D12Device* dev = GetNativeDevice();
    auto* driver = GetD3D12Driver();

    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = desc.byteWidth;
    bufDesc.Height = 1; bufDesc.DepthOrArraySize = 1; bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (initialData && desc.usage == T8_BUFFER_USAGE::DEFAULT) {
      // Static buffer: create in GPU-local DEFAULT heap, upload via staging
      D3D12_HEAP_PROPERTIES defaultHeap = {}; defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
      HRESULT hr = dev->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&m_buffer));
      if (FAILED(hr)) { T8_LOG_ERROR("[D3D12] IB create failed hr=0x%08X", hr); return; }

      D3D12_HEAP_PROPERTIES uploadHeap = {}; uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
      ComPtr<ID3D12Resource> staging;
      hr = dev->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                    IID_PPV_ARGS(&staging));
      if (FAILED(hr) || !staging) { T8_LOG_ERROR("[D3D12] IB staging create failed hr=0x%08X", hr); return; }
      void* mapped = nullptr;
      hr = staging->Map(0, nullptr, &mapped);
      if (FAILED(hr) || !mapped) { T8_LOG_ERROR("[D3D12] IB staging map failed hr=0x%08X", hr); return; }
      memcpy(mapped, initialData, desc.byteWidth);
      staging->Unmap(0, nullptr);

      ComPtr<ID3D12CommandAllocator> tmpAlloc;
      ComPtr<ID3D12GraphicsCommandList> tmpList;
      dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tmpAlloc));
      dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, tmpAlloc.Get(), nullptr, IID_PPV_ARGS(&tmpList));
      tmpList->CopyResource(m_buffer.Get(), staging.Get());
      D3D12_RESOURCE_BARRIER barrier = {};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Transition.pResource = m_buffer.Get();
      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
      barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
      barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      tmpList->ResourceBarrier(1, &barrier);
      tmpList->Close();
      ID3D12CommandList* lists[] = { tmpList.Get() };
      driver->GetCmdQueue()->ExecuteCommandLists(1, lists);

      ComPtr<ID3D12Fence> tmpFence;
      dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&tmpFence));
      HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
      driver->GetCmdQueue()->Signal(tmpFence.Get(), 1);
      if (tmpFence->GetCompletedValue() < 1) {
        tmpFence->SetEventOnCompletion(1, evt);
        WaitForSingleObject(evt, INFINITE);
      }
      CloseHandle(evt);

      sysMemCpy.assign((char*)initialData, (char*)initialData + desc.byteWidth);
      m_mappedData = nullptr;
    } else {
      D3D12_HEAP_PROPERTIES uploadHeap = {}; uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
      HRESULT hr = dev->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                 IID_PPV_ARGS(&m_buffer));
      if (FAILED(hr)) { T8_LOG_ERROR("[D3D12] IB create failed hr=0x%08X", hr); return; }
      m_buffer->Map(0, nullptr, &m_mappedData);
      if (initialData) {
        sysMemCpy.assign((char*)initialData, (char*)initialData + desc.byteWidth);
        memcpy(m_mappedData, initialData, desc.byteWidth);
      }
    }

    m_view.BufferLocation = m_buffer->GetGPUVirtualAddress();
    m_view.SizeInBytes = desc.byteWidth;
    m_view.Format = DXGI_FORMAT_R32_UINT;
    T8_LOG_DEBUG("[D3D12] IB created: %d bytes%s", desc.byteWidth,
                 m_mappedData ? " (upload)" : " (default)");
  }

  void D3D12IndexBuffer::Set(const DeviceContext& deviceContext, const unsigned offset, T8_IB_FORMAR::E format) {
    const_cast<DeviceContext*>(&deviceContext)->actualIndexBuffer = (IndexBuffer*)this;
    m_view.Format = (format == T8_IB_FORMAR::R16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
    auto* cmdList = static_cast<const D3D12DeviceContext*>(&deviceContext)->GetCommandList();
    cmdList->IASetIndexBuffer(&m_view);
  }

  void D3D12IndexBuffer::UpdateFromSystemCopy(const DeviceContext&) {
    if (m_mappedData && !sysMemCpy.empty()) memcpy(m_mappedData, sysMemCpy.data(), sysMemCpy.size());
  }
  void D3D12IndexBuffer::UpdateFromBuffer(const DeviceContext&, const void* buffer) {
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    if (m_mappedData) memcpy(m_mappedData, buffer, descriptor.byteWidth);
  }
  void D3D12IndexBuffer::release() {
    if (m_mappedData) { m_buffer->Unmap(0, nullptr); m_mappedData = nullptr; }
    m_buffer.Reset(); sysMemCpy.clear(); delete this;
  }

} // namespace t800

#endif // OS_WINDOWS
