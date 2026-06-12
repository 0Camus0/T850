#include <pch.h>
#include <scene/RenderResourceRegistry.h>

namespace t850 {

  void RenderResourceRegistry::Clear() {
    m_meshes.clear();
    m_textures.clear();
    m_shaders.clear();
  }

  RenderMeshHandle RenderResourceRegistry::RegisterMesh(std::string key, PrimitiveBase* primitive, int primitiveId) {
    RenderMeshHandle handle;
    handle.key = std::move(key);
    handle.primitive = primitive;
    handle.primitiveId = primitiveId;
    if (!handle.key.empty() && primitive) {
      m_meshes[handle.key] = handle;
    }
    return handle;
  }

  const RenderMeshHandle* RenderResourceRegistry::FindMesh(const std::string& key) const {
    auto it = m_meshes.find(key);
    return it == m_meshes.end() ? nullptr : &it->second;
  }

  RenderTextureHandle RenderResourceRegistry::RegisterTexture(std::string key, Texture* texture, int textureId) {
    RenderTextureHandle handle;
    handle.key = std::move(key);
    handle.texture = texture;
    handle.textureId = textureId;
    if (!handle.key.empty() && texture) {
      m_textures[handle.key] = handle;
    }
    return handle;
  }

  const RenderTextureHandle* RenderResourceRegistry::FindTexture(const std::string& key) const {
    auto it = m_textures.find(key);
    return it == m_textures.end() ? nullptr : &it->second;
  }

  RenderShaderHandle RenderResourceRegistry::RegisterShader(ShaderKey key, ShaderBase* shader) {
    RenderShaderHandle handle;
    handle.key = key;
    handle.shader = shader;
    if (shader) {
      m_shaders[key.bits] = handle;
    }
    return handle;
  }

  const RenderShaderHandle* RenderResourceRegistry::FindShader(ShaderKey key) const {
    auto it = m_shaders.find(key.bits);
    return it == m_shaders.end() ? nullptr : &it->second;
  }

} // namespace t850
