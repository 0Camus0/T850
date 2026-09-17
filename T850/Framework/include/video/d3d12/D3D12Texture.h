#ifndef T800_D3D12TEXTURE_H
#define T800_D3D12TEXTURE_H

#include <Config.h>
#include <video/BaseDriver.h>

#ifdef OS_WINDOWS

#include <d3d12.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace t850 {

  class D3D12Texture : public Texture {
  public:
    D3D12Texture() {}

    void LoadAPITexture(DeviceContext* context, unsigned char* buffer) override;
    void LoadAPITextureCompressed(unsigned char* buffer) override;
    void DestroyAPITexture() override;
    void SetTextureParams() override;
    void GetFormatBpp(unsigned int& props, unsigned int& format, unsigned int& bpp) override;
    void Set(const DeviceContext& deviceContext, unsigned int slot, std::string shaderTextureName) override;
    void SetVS(const DeviceContext& deviceContext, unsigned int slot, std::string shaderTextureName) override;
    void SetSampler(const DeviceContext& deviceContext, unsigned int slot = 0) override;
    void UpdateFloatData(const DeviceContext& deviceContext, int w, int h, const float* data) override;

    ComPtr<ID3D12Resource> pTexResource;
    ComPtr<ID3D12Resource> m_uploadBuffer;  // persistent upload heap for per-frame updates
    D3D12_CPU_DESCRIPTOR_HANDLE srvCPU = {};
    D3D12_GPU_DESCRIPTOR_HANDLE srvGPU = {};
    D3D12_CPU_DESCRIPTOR_HANDLE uavCPU = {};
    D3D12_GPU_DESCRIPTOR_HANDLE uavGPU = {};

    D3D12_RESOURCE_STATES GetTrackedState() const {
      return m_externalState ? *m_externalState : m_trackedState;
    }
    void SetTrackedState(D3D12_RESOURCE_STATES state) {
      if (m_externalState) *m_externalState = state;
      else m_trackedState = state;
    }
    void SetExternalState(D3D12_RESOURCE_STATES* state) { m_externalState = state; }
    void SetGraphicsReadState(D3D12_RESOURCE_STATES state) { m_graphicsReadState = state; }
    D3D12_RESOURCE_STATES GetGraphicsReadState() const { return m_graphicsReadState; }

    // Per-texture sampler (created in SetTextureParams based on texture params)
    D3D12_GPU_DESCRIPTOR_HANDLE samplerGPU = {};
    bool hasSampler = false;
    bool m_floatUpdateDisabled = false;

  private:
    D3D12_RESOURCE_STATES m_trackedState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES m_graphicsReadState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES* m_externalState = nullptr;
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_D3D12TEXTURE_H
