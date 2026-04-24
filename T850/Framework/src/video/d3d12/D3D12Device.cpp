#include "pch.h"
/*********************************************************
* T850 Engine — D3D12 Backend
* D3D12Device.cpp: Device implementation
*********************************************************/

#include <video/d3d12/D3D12Device.h>
#include <video/d3d12/D3D12VertexBuffer.h>
#include <video/d3d12/D3D12IndexBuffer.h>
#include <video/d3d12/D3D12ConstantBuffer.h>
#include <video/d3d12/D3D12Shader.h>
#include <video/d3d12/D3D12Texture.h>
#include <video/d3d12/D3D12RT.h>

#ifdef OS_WINDOWS

#include <utils/Log.h>

namespace t800 {

  // ══════════════════════════════════════════════════════
  //  D3D12Device
  // ══════════════════════════════════════════════════════

  void* D3D12Device::GetAPIObject() const { return (void*)m_device.Get(); }
  void** D3D12Device::GetAPIObjectReference() const { return nullptr; }
  void D3D12Device::release() { m_device.Reset(); }

  Buffer* D3D12Device::CreateBuffer(T8_BUFFER_TYPE::E bufferType, BufferDesc desc, void* initialData) {
    T8_LOG_DEBUG("[D3D12] CreateBuffer type=%d size=%d", bufferType, desc.byteWidth);
    Buffer* buf = nullptr;
    switch (bufferType) {
      case T8_BUFFER_TYPE::VERTEX:   buf = new D3D12VertexBuffer;   break;
      case T8_BUFFER_TYPE::INDEX:    buf = new D3D12IndexBuffer;    break;
      case T8_BUFFER_TYPE::CONSTANT: buf = new D3D12ConstantBuffer; break;
    }
    if (buf) buf->Create(*this, desc, initialData);
    return buf;
  }

  ShaderBase* D3D12Device::CreateShader(std::string src_vs, std::string src_fs, ShaderKey key,
                                         const std::string& vs_name, const std::string& fs_name) {
    D3D12Shader* sh = new D3D12Shader();
    if (!sh->CreateShader(src_vs, src_fs, key, vs_name, fs_name)) {
      delete sh;
      return nullptr;
    }
    return sh;
  }

  Texture* D3D12Device::CreateTexture(std::string path) {
    D3D12Texture* tex = new D3D12Texture;
    tex->LoadTexture(path.c_str());
    return tex;
  }

  Texture* D3D12Device::CreateTextureFromMemory(const unsigned char* buff, int w, int h, int channels, std::string name) {
    D3D12Texture* tex = new D3D12Texture;
    tex->LoadFromMemory(buff, w, h, channels);
    return tex;
  }

  Texture* D3D12Device::CreateCubeMap(const unsigned char* buff, int w, int h) {
    D3D12Texture* tex = new D3D12Texture;
    tex->CreateCubeMap(buff, w, h);
    return tex;
  }

  Texture* D3D12Device::CreateFloatTexture(int w, int h, const float* data) {
    // TODO: Phase 2 — D3D12 upload heap + SRV creation
    T8_LOG_ERROR("[D3D12] CreateFloatTexture not yet implemented");
    return nullptr;
  }

  BaseRT* D3D12Device::CreateRT(int nrt, int cf, int df, int w, int h, bool genMips) {
    D3D12RT* rt = new D3D12RT;
    if (rt->LoadRT(nrt, cf, df, w, h, genMips)) return rt;
    delete rt;
    return nullptr;
  }

} // namespace t800

#endif // OS_WINDOWS
