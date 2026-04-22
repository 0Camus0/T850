#include "pch.h"
/*********************************************************
* T850 Engine — D3D12 Backend
* D3D12Heap.cpp: Descriptor heap implementation
*********************************************************/

#include <video/d3d12/D3D12Heap.h>

#ifdef OS_WINDOWS

#include <utils/Log.h>

namespace t800 {

  // ══════════════════════════════════════════════════════
  //  D3D12Heap
  // ══════════════════════════════════════════════════════

  bool D3D12Heap::Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                          uint32_t numDescriptors, bool shaderVisible) {
    m_type = type;
    m_maxDescriptors = numDescriptors;
    m_currentCount = 0;
    m_shaderVisible = shaderVisible;

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = numDescriptors;
    desc.Type = type;
    desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                               : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap));
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D12] CreateDescriptorHeap failed type=%d hr=0x%08X", type, hr);
      return false;
    }
    m_incrementSize = device->GetDescriptorHandleIncrementSize(type);
    T8_LOG_INFO("[D3D12] Heap type=%d created: %u descriptors, increment=%llu, visible=%d",
                type, numDescriptors, (unsigned long long)m_incrementSize, shaderVisible);
    return true;
  }

  void D3D12Heap::Destroy() { m_heap.Reset(); m_currentCount = 0; }

  D3D12_CPU_DESCRIPTOR_HANDLE D3D12Heap::GetCPUStart() const { return m_heap->GetCPUDescriptorHandleForHeapStart(); }
  D3D12_GPU_DESCRIPTOR_HANDLE D3D12Heap::GetGPUStart() const { return m_heap->GetGPUDescriptorHandleForHeapStart(); }

  D3D12_CPU_DESCRIPTOR_HANDLE D3D12Heap::AllocateCPU() {
    auto h = GetCPUAt(m_currentCount); m_currentCount++; return h;
  }
  D3D12_GPU_DESCRIPTOR_HANDLE D3D12Heap::AllocateGPU() {
    return GetGPUAt(m_currentCount - 1); // pair with AllocateCPU
  }

  D3D12_CPU_DESCRIPTOR_HANDLE D3D12Heap::GetCPUAt(uint64_t i) const {
    auto h = GetCPUStart(); h.ptr += i * m_incrementSize; return h;
  }
  D3D12_GPU_DESCRIPTOR_HANDLE D3D12Heap::GetGPUAt(uint64_t i) const {
    auto h = GetGPUStart(); h.ptr += i * m_incrementSize; return h;
  }

} // namespace t800

#endif // OS_WINDOWS
