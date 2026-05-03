/*********************************************************
* T850 Engine — D3D12 Backend
*
* D3D12ConstantBuffer.h: Constant buffer implementation
*********************************************************/

#ifndef T800_D3D12CONSTANTBUFFER_H
#define T800_D3D12CONSTANTBUFFER_H

#include <Config.h>
#include <video/BaseDriver.h>

#ifdef OS_WINDOWS

#include <d3d12.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace t850 {

  class D3D12Device;

  // ══════════════════════════════════════════════════════
  //  D3D12 Constant Buffer
  // ══════════════════════════════════════════════════════
  class D3D12ConstantBuffer : public ConstantBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;
    void Set(const DeviceContext& deviceContext, unsigned int slot = 0) override;
    void UpdateFromSystemCopy(const DeviceContext& deviceContext) override;
    void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) override;
    void release() override;
  private:
    friend class D3D12Device;
    void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) override;
    ComPtr<ID3D12Resource> m_buffer;
    D3D12_CPU_DESCRIPTOR_HANDLE m_cpuHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_gpuHandle = {};
    void* m_mappedData = nullptr;
    uint32_t m_alignedSize = 0;
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_D3D12CONSTANTBUFFER_H
