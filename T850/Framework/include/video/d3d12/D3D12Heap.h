/*********************************************************
* T850 Engine — D3D12 Backend
*
* D3D12Heap.h: Descriptor heap — linear allocator
*********************************************************/

#ifndef T800_D3D12HEAP_H
#define T800_D3D12HEAP_H

#include <Config.h>

#ifdef OS_WINDOWS

#include <d3d12.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace t850 {

  // ══════════════════════════════════════════════════════
  //  D3D12 Descriptor Heap — linear allocator
  // ══════════════════════════════════════════════════════
  class D3D12Heap {
  public:
    enum Type {
      CBV_SRV_UAV_VISIBLE = 0,
      CBV_SRV_UAV_NOT_VISIBLE,
      SAMPLER,
      RTV,
      DSV,
      MAX
    };

    bool Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                uint32_t numDescriptors, bool shaderVisible);
    void Destroy();

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUStart() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUStart() const;
    D3D12_CPU_DESCRIPTOR_HANDLE AllocateCPU();
    D3D12_GPU_DESCRIPTOR_HANDLE AllocateGPU();
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUAt(uint64_t index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUAt(uint64_t index) const;
    uint64_t GetCurrentIndex() const { return m_currentCount; }
    void Increment() { m_currentCount++; }
    ID3D12DescriptorHeap* GetHeap() const { return m_heap.Get(); }

  private:
    ComPtr<ID3D12DescriptorHeap> m_heap;
    D3D12_DESCRIPTOR_HEAP_TYPE   m_type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    uint64_t m_maxDescriptors = 0;
    uint64_t m_currentCount   = 0;
    uint64_t m_incrementSize  = 0;
    bool     m_shaderVisible  = false;
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_D3D12HEAP_H
