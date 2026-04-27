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

    // Ray tracing resource creation (requires ID3D12Device5 / DXR Tier 1.0+)
    BLAS*        CreateBLAS(VertexBuffer* vb, IndexBuffer* ib,
                             uint32_t vertexCount, uint32_t indexCount,
                             uint32_t vertexStride) override;
    TLAS*        CreateTLAS(uint32_t maxInstances) override;
    RTPipeline*  CreateRTPipeline(const char* raygenSrc, const char* missSrc,
                                   const char* closestHitSrc,
                                   ShaderKey key = ShaderKey()) override;

    // Returns the underlying ID3D12Device5 (null when RT tier < 1.0).
    ID3D12Device5* GetNativeDevice() const { return m_device.Get(); }

  private:
    friend class D3D12Driver;
    // ID3D12Device5 is a strict superset of ID3D12Device — the cast to
    // ID3D12Device* is implicit and preserves backward compatibility.
    ComPtr<ID3D12Device5> m_device;
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_D3D12DEVICE_H
