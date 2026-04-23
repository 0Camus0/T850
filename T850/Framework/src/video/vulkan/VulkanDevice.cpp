#include "pch.h"
/*********************************************************
 * T850 Engine — Vulkan Backend
 * VulkanDevice.cpp: Device implementation
 *********************************************************/

#include <video/vulkan/VulkanDevice.h>
#include <video/vulkan/VulkanVertexBuffer.h>
#include <video/vulkan/VulkanIndexBuffer.h>
#include <video/vulkan/VulkanConstantBuffer.h>
#include <video/vulkan/VulkanTexture.h>
#include <video/vulkan/VulkanRT.h>
#include <video/vulkan/VulkanShader.h>

#if defined(OS_WINDOWS)

#include <utils/Log.h>

namespace t800 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  // ══════════════════════════════════════════════════════
  //  VulkanDevice
  // ══════════════════════════════════════════════════════

  void* VulkanDevice::GetAPIObject() const { return (void*)m_device; }
  void** VulkanDevice::GetAPIObjectReference() const { return nullptr; }
  void VulkanDevice::release() { m_device = VK_NULL_HANDLE; }

  Buffer* VulkanDevice::CreateBuffer(T8_BUFFER_TYPE::E bufferType, BufferDesc desc, void* initialData) {
    T8_LOG_DEBUG("[Vulkan] CreateBuffer type=%d size=%d", bufferType, desc.byteWidth);
    Buffer* buf = nullptr;
    switch (bufferType) {
      case T8_BUFFER_TYPE::VERTEX:   buf = new VulkanVertexBuffer;   break;
      case T8_BUFFER_TYPE::INDEX:    buf = new VulkanIndexBuffer;    break;
      case T8_BUFFER_TYPE::CONSTANT: buf = new VulkanConstantBuffer; break;
    }
    if (buf) buf->Create(*this, desc, initialData);
    return buf;
  }

  ShaderBase* VulkanDevice::CreateShader(std::string src_vs, std::string src_fs, ShaderKey key,
                                          const std::string& vs_name, const std::string& fs_name) {
    VulkanShader* sh = new VulkanShader();
    if (!sh->CreateShader(src_vs, src_fs, key, vs_name, fs_name)) {
      delete sh;
      return nullptr;
    }
    return sh;
  }

  Texture* VulkanDevice::CreateTexture(std::string path) {
    VulkanTexture* tex = new VulkanTexture;
    tex->LoadTexture(path.c_str());
    return tex;
  }

  Texture* VulkanDevice::CreateTextureFromMemory(const unsigned char* buff, int w, int h, int channels, std::string name) {
    VulkanTexture* tex = new VulkanTexture;
    tex->LoadFromMemory(buff, w, h, channels);
    return tex;
  }

  Texture* VulkanDevice::CreateCubeMap(const unsigned char* buff, int w, int h) {
    VulkanTexture* tex = new VulkanTexture;
    tex->CreateCubeMap(buff, w, h);
    return tex;
  }

  BaseRT* VulkanDevice::CreateRT(int nrt, int cf, int df, int w, int h, bool genMips) {
    VulkanRT* rt = new VulkanRT;
    if (rt->LoadRT(nrt, cf, df, w, h, genMips)) return rt;
    delete rt;
    return nullptr;
  }

} // namespace t800

#endif // OS_WINDOWS
