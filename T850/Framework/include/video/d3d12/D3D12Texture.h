#ifndef T800_D3D12TEXTURE_H
#define T800_D3D12TEXTURE_H

#include <Config.h>
#include <video/BaseDriver.h>

#ifdef OS_WINDOWS

#include <d3d12.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace t800 {

  class D3D12Texture : public Texture {
  public:
    D3D12Texture() {}

    void LoadAPITexture(DeviceContext* context, unsigned char* buffer) override;
    void LoadAPITextureCompressed(unsigned char* buffer) override;
    void DestroyAPITexture() override;
    void SetTextureParams() override;
    void GetFormatBpp(unsigned int& props, unsigned int& format, unsigned int& bpp) override;
    void Set(const DeviceContext& deviceContext, unsigned int slot, std::string shaderTextureName) override;
    void SetSampler(const DeviceContext& deviceContext, unsigned int slot = 0) override;

    ComPtr<ID3D12Resource> pTexResource;
    D3D12_CPU_DESCRIPTOR_HANDLE srvCPU = {};
    D3D12_GPU_DESCRIPTOR_HANDLE srvGPU = {};

    // Per-texture sampler (created in SetTextureParams based on texture params)
    D3D12_GPU_DESCRIPTOR_HANDLE samplerGPU = {};
    bool hasSampler = false;
  };

} // namespace t800

#endif // OS_WINDOWS
#endif // T800_D3D12TEXTURE_H
