/*********************************************************
* T850 Engine — D3D12 Backend
*
* D3D12IndexBuffer.h: Index buffer implementation
*********************************************************/

#ifndef T800_D3D12INDEXBUFFER_H
#define T800_D3D12INDEXBUFFER_H

#include <Config.h>
#include <video/BaseDriver.h>

#ifdef OS_WINDOWS

#include <d3d12.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace t850 {

  class D3D12Device;

  // ══════════════════════════════════════════════════════
  //  D3D12 Index Buffer
  // ══════════════════════════════════════════════════════
  class D3D12IndexBuffer : public IndexBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;
    void Set(const DeviceContext& deviceContext, const unsigned offset, IndexBufferFormat::E format = IndexBufferFormat::R32) override;
    void UpdateFromSystemCopy(const DeviceContext& deviceContext) override;
    void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) override;
    void release() override;
  private:
    friend class D3D12Device;
    void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) override;
    ComPtr<ID3D12Resource> m_buffer;
    D3D12_INDEX_BUFFER_VIEW m_view = {};
    void* m_mappedData = nullptr;
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_D3D12INDEXBUFFER_H
