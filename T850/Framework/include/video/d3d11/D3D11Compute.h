#ifndef T800_D3D11COMPUTE_H
#define T800_D3D11COMPUTE_H

#include <Config.h>
#include <video/BaseDriver.h>

#ifdef OS_WINDOWS

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <unordered_map>
#include <unordered_set>

namespace t850 {

  class D3D11ComputePipeline final : public ComputePipeline {
  public:
    bool Create(ID3D11Device* device, const ComputePipelineDesc& desc);

    Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader;
    std::unordered_map<uint32_t, uint32_t> constantWordCounts;
    std::unordered_set<uint32_t> bufferSrvs;
    std::unordered_set<uint32_t> bufferUavs;
    std::unordered_set<uint32_t> textureSrvs;
    std::unordered_set<uint32_t> textureUavs;
    std::unordered_set<uint32_t> samplers;
  };

  class D3D11ComputeBuffer final : public ComputeBuffer {
  public:
    bool Create(ID3D11Device* device, const ComputeBufferDesc& desc, const void* initialData);

    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
  };

} // namespace t850

#endif
#endif
