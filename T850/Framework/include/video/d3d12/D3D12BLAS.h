/*********************************************************
 * T850 Engine — D3D12 Ray Tracing
 *
 * D3D12BLAS.h: Bottom-level acceleration structure (DXR)
 *********************************************************/

#ifndef T850_D3D12BLAS_H
#define T850_D3D12BLAS_H

#include <Config.h>
#include <video/AccelStructure.h>

#ifdef OS_WINDOWS

#include <d3d12.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace t850 {

  class D3D12VertexBuffer;
  class D3D12IndexBuffer;

  // ══════════════════════════════════════════════════════
  //  D3D12BLAS — one triangle-geometry BLAS per mesh subset
  // ══════════════════════════════════════════════════════
  class D3D12BLAS : public BLAS {
  public:
    // vertexGPUVA / indexGPUVA: GPU virtual addresses of the existing
    // vertex and index buffers (no copy — referenced in place).
    uint64_t vertexGPUVA   = 0;
    uint64_t indexGPUVA    = 0;
    uint32_t vertexCount   = 0;
    uint32_t indexCount    = 0;
    uint32_t vertexStride  = 0;
    bool     is32BitIndex  = true;  // true → DXGI_FORMAT_R32_UINT

    void Build(bool allowUpdate = false) override;
    void Refit() override;
    void Destroy() override;
    uint64_t GetGPUAddress() const override;

  private:
    ComPtr<ID3D12Resource> m_resultBuffer;   // ACCELERATION_STRUCTURE resource
    ComPtr<ID3D12Resource> m_scratchBuffer;  // temporary scratch space
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS m_buildFlags = {};
    uint64_t m_resultGPUVA = 0;
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T850_D3D12BLAS_H
