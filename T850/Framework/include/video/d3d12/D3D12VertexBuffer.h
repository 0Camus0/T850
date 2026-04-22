/*********************************************************
* T850 Engine — D3D12 Backend
*
* D3D12VertexBuffer.h: Vertex buffer implementation
*********************************************************/

#ifndef T800_D3D12VERTEXBUFFER_H
#define T800_D3D12VERTEXBUFFER_H

#include <Config.h>
#include <video/BaseDriver.h>

#ifdef OS_WINDOWS

#include <d3d12.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace t800 {

  class D3D12Device;

  // ══════════════════════════════════════════════════════
  //  D3D12 Vertex Buffer
  // ══════════════════════════════════════════════════════
  class D3D12VertexBuffer : public VertexBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;
    void Set(const DeviceContext& deviceContext, const unsigned stride, const unsigned offset) override;
    void UpdateFromSystemCopy(const DeviceContext& deviceContext) override;
    void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) override;
    void release() override;
  private:
    friend class D3D12Device;
    void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) override;
    ComPtr<ID3D12Resource> m_buffer;
    D3D12_VERTEX_BUFFER_VIEW m_view = {};
    void* m_mappedData = nullptr;
  };

} // namespace t800

#endif // OS_WINDOWS
#endif // T800_D3D12VERTEXBUFFER_H
