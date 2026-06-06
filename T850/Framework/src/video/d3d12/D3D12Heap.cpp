#include <pch.h>
/*********************************************************
* T850 Engine — D3D12 Backend
* D3D12Heap.cpp: Descriptor heap implementation
*********************************************************/

#include <video/d3d12/D3D12Heap.h>

#ifdef OS_WINDOWS

#include <utils/Log.h>

namespace t850 {

  // ══════════════════════════════════════════════════════
  //  D3D12Heap
  // ══════════════════════════════════════════════════════

  bool D3D12Heap::Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                          uint32_t numDescriptors, bool shaderVisible) {
    m_type = type;
    m_maxDescriptors = numDescriptors;
    m_currentCount = 0;
    m_lastAllocatedIndex = UINT64_MAX;
    m_freeList.clear();
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

  void D3D12Heap::Destroy() {
    m_heap.Reset();
    m_currentCount = 0;
    m_lastAllocatedIndex = UINT64_MAX;
    m_freeList.clear();
  }

  D3D12_CPU_DESCRIPTOR_HANDLE D3D12Heap::GetCPUStart() const { return m_heap->GetCPUDescriptorHandleForHeapStart(); }
  D3D12_GPU_DESCRIPTOR_HANDLE D3D12Heap::GetGPUStart() const { return m_heap->GetGPUDescriptorHandleForHeapStart(); }

  D3D12_CPU_DESCRIPTOR_HANDLE D3D12Heap::AllocateCPU() {
    if (!m_freeList.empty()) {
      const uint64_t index = m_freeList.back();
      m_freeList.pop_back();
      m_lastAllocatedIndex = index;
      return GetCPUAt(index);
    }
    if (m_currentCount >= m_maxDescriptors) {
      T8_LOG_ERROR("[D3D12] Heap AllocateCPU overflow: count=%u max=%u type=%d",
                   m_currentCount, m_maxDescriptors, m_type);
      m_lastAllocatedIndex = UINT64_MAX;
      return D3D12_CPU_DESCRIPTOR_HANDLE{ 0 };
    }
    const uint64_t index = m_currentCount++;
    m_lastAllocatedIndex = index;
    return GetCPUAt(index);
  }
  D3D12_GPU_DESCRIPTOR_HANDLE D3D12Heap::AllocateGPU() {
    if (m_lastAllocatedIndex == UINT64_MAX) {
      T8_LOG_ERROR("[D3D12] Heap AllocateGPU called without preceding AllocateCPU type=%d", m_type);
      return D3D12_GPU_DESCRIPTOR_HANDLE{ 0 };
    }
    return GetGPUAt(m_lastAllocatedIndex); // pair with AllocateCPU
  }

  void D3D12Heap::FreeCPU(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    if (!handle.ptr || !m_heap) return;
    if (m_type != D3D12_DESCRIPTOR_HEAP_TYPE_RTV &&
        m_type != D3D12_DESCRIPTOR_HEAP_TYPE_DSV) {
      T8_LOG_ERROR("[D3D12] Heap FreeCPU only supports RTV/DSV descriptors (type=%d)", m_type);
      return;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE start = GetCPUStart();
    if (handle.ptr < start.ptr || m_incrementSize == 0) {
      T8_LOG_ERROR("[D3D12] Heap FreeCPU invalid handle type=%d", m_type);
      return;
    }
    const SIZE_T offset = handle.ptr - start.ptr;
    if ((offset % static_cast<SIZE_T>(m_incrementSize)) != 0) {
      T8_LOG_ERROR("[D3D12] Heap FreeCPU unaligned handle type=%d", m_type);
      return;
    }
    const uint64_t index = offset / static_cast<SIZE_T>(m_incrementSize);
    if (index >= m_currentCount || index >= m_maxDescriptors) {
      T8_LOG_ERROR("[D3D12] Heap FreeCPU out-of-range index=%llu count=%llu max=%llu type=%d",
                   static_cast<unsigned long long>(index),
                   static_cast<unsigned long long>(m_currentCount),
                   static_cast<unsigned long long>(m_maxDescriptors),
                   m_type);
      return;
    }
    m_freeList.push_back(index);
  }

  D3D12_CPU_DESCRIPTOR_HANDLE D3D12Heap::GetCPUAt(uint64_t i) const {
    auto h = GetCPUStart(); h.ptr += static_cast<SIZE_T>(i) * static_cast<SIZE_T>(m_incrementSize); return h;
  }
  D3D12_GPU_DESCRIPTOR_HANDLE D3D12Heap::GetGPUAt(uint64_t i) const {
    auto h = GetGPUStart(); h.ptr += static_cast<UINT64>(i) * static_cast<UINT64>(m_incrementSize); return h;
  }

} // namespace t850

#endif // OS_WINDOWS
