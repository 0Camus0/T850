/*********************************************************
 * T850 Engine — D3D12 Ray Tracing
 *
 * D3D12TLAS.h: Top-level acceleration structure (DXR)
 *********************************************************/

#ifndef T850_D3D12TLAS_H
#define T850_D3D12TLAS_H

#include <Config.h>
#include <video/AccelStructure.h>

#ifdef OS_WINDOWS

#include <d3d12.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#include <cstdint>

namespace t850 {

  // ══════════════════════════════════════════════════════
  //  D3D12TLAS — scene top-level acceleration structure
  // ══════════════════════════════════════════════════════
  class D3D12TLAS : public TLAS {
  public:
    explicit D3D12TLAS(uint32_t maxInstances) : m_maxInstances(maxInstances) {}

    void Build(const RTInstanceDesc* instances, uint32_t instanceCount) override;
    void Destroy() override;
    uint64_t GetGPUAddress() const override;
    uint64_t GetSRVDescriptorIndex() const override { return m_srvHeapIndex; }

  private:
    uint32_t               m_maxInstances = 0;
    ComPtr<ID3D12Resource> m_resultBuffer;     // ACCELERATION_STRUCTURE resource
    ComPtr<ID3D12Resource> m_scratchBuffer;    // per-frame scratch
    ComPtr<ID3D12Resource> m_instanceBuffer;   // upload heap: D3D12_RAYTRACING_INSTANCE_DESC[]
    void*                  m_instanceMapped = nullptr;
    uint64_t               m_resultGPUVA = 0;
    uint64_t               m_srvHeapIndex = 0; // index in CBV_SRV_UAV_VISIBLE heap
    bool                   m_initialized = false;

    void EnsureBuffers(uint32_t instanceCount);
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T850_D3D12TLAS_H
