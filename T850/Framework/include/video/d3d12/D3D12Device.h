/*********************************************************
* T850 Engine — D3D12 Backend
*
* D3D12Device.h: Device implementation
*********************************************************/

#ifndef T800_D3D12DEVICE_H
#define T800_D3D12DEVICE_H

#include <Config.h>
#include <video/BaseDriver.h>

#ifdef OS_WINDOWS

#include <d3d12.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#include <string>

namespace t850 {

  class D3D12Driver;

  // ══════════════════════════════════════════════════════
  //  D3D12 Device
  // ══════════════════════════════════════════════════════
  class D3D12Device : public Device {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;
    void release() override;

    Buffer*     CreateBuffer(BufferType::E bufferType, BufferDesc desc, void* initialData = nullptr) override;
    ShaderBase* CreateShader(std::string src_vs, std::string src_fs, ShaderKey key = ShaderKey(),
                             const std::string& vs_name = "", const std::string& fs_name = "") override;
    Texture*    CreateTexture(std::string path) override;
    Texture*    CreateTextureFromMemory(const unsigned char* buff, int w, int h, int channels, std::string name) override;
    Texture*    CreateCubeMap(const unsigned char* buff, int w, int h) override;
    Texture*    CreateFloatTexture(int w, int h, const float* data = nullptr) override;
    BaseRT*     CreateRT(int nrt, int cf, int df, int w, int h, bool genMips = false) override;

    ID3D12Device* GetNativeDevice() const { return m_device.Get(); }

  private:
    friend class D3D12Driver;
    ComPtr<ID3D12Device> m_device;
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_D3D12DEVICE_H
