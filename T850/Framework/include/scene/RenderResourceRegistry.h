#ifndef T850_RENDER_RESOURCE_REGISTRY_H
#define T850_RENDER_RESOURCE_REGISTRY_H

#include <Descriptors.h>
#include <string>
#include <unordered_map>

namespace t850 {

  class PrimitiveBase;
  class ShaderBase;
  class Texture;

  struct RenderMeshHandle {
    std::string key;
    PrimitiveBase* primitive = nullptr;
    int primitiveId = -1;
  };

  struct RenderTextureHandle {
    std::string key;
    Texture* texture = nullptr;
    int textureId = -1;
  };

  struct RenderShaderHandle {
    ShaderKey key;
    ShaderBase* shader = nullptr;
  };

  class RenderResourceRegistry {
  public:
    void Clear();

    RenderMeshHandle RegisterMesh(std::string key, PrimitiveBase* primitive, int primitiveId = -1);
    const RenderMeshHandle* FindMesh(const std::string& key) const;

    RenderTextureHandle RegisterTexture(std::string key, Texture* texture, int textureId = -1);
    const RenderTextureHandle* FindTexture(const std::string& key) const;

    RenderShaderHandle RegisterShader(ShaderKey key, ShaderBase* shader);
    const RenderShaderHandle* FindShader(ShaderKey key) const;

  private:
    std::unordered_map<std::string, RenderMeshHandle> m_meshes;
    std::unordered_map<std::string, RenderTextureHandle> m_textures;
    std::unordered_map<unsigned long long, RenderShaderHandle> m_shaders;
  };

} // namespace t850

#endif
