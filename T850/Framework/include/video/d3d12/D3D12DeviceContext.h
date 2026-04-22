/*********************************************************
* T850 Engine — D3D12 Backend
*
* D3D12DeviceContext.h: Device context implementation
*********************************************************/

#ifndef T800_D3D12DEVICECONTEXT_H
#define T800_D3D12DEVICECONTEXT_H

#include <Config.h>
#include <video/BaseDriver.h>

#ifdef OS_WINDOWS

#include <d3d12.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace t800 {

  class D3D12Driver;

  // ══════════════════════════════════════════════════════
  //  D3D12 Device Context
  // ══════════════════════════════════════════════════════
  class D3D12DeviceContext : public DeviceContext {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;
    void release() override;
    void SetPrimitiveTopology(T8_TOPOLOGY::E topology) override;
    void DrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex) override;

    ID3D12GraphicsCommandList* GetCommandList() const { return m_commandList.Get(); }

  private:
    friend class D3D12Driver;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
  };

} // namespace t800

#endif // OS_WINDOWS
#endif // T800_D3D12DEVICECONTEXT_H
