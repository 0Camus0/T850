/*********************************************************
* T850 Engine — D3D12 Compute Support
*********************************************************/

#ifndef T800_D3D12COMPUTE_H
#define T800_D3D12COMPUTE_H

#include <Config.h>
#include <video/BaseDriver.h>

#ifdef OS_WINDOWS

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <unordered_map>

namespace t850 {

  class D3D12Driver;
  class D3D12ShaderCacheSession;

  class D3D12ComputePipeline final : public ComputePipeline {
  public:
    bool Create(ID3D12Device* device, D3D12ShaderCacheSession* cacheSession, const ComputePipelineDesc& desc);

    ID3D12RootSignature* GetRootSignature() const { return m_rootSignature.Get(); }
    ID3D12PipelineState* GetPipelineState() const { return m_pipelineState.Get(); }
    int GetConstantRootIndex(uint32_t shaderRegister) const;
    int GetBufferSrvRootIndex(uint32_t shaderRegister) const;
    int GetBufferUavRootIndex(uint32_t shaderRegister) const;
    int GetTextureSrvRootIndex(uint32_t shaderRegister) const;
    int GetTextureUavRootIndex(uint32_t shaderRegister) const;
    int GetSamplerRootIndex(uint32_t shaderRegister) const;
    uint32_t GetConstantWordCount(uint32_t shaderRegister) const;

  private:
    friend class D3D12Driver;
    Microsoft::WRL::ComPtr<ID3DBlob> m_shaderBlob;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;
    std::unordered_map<uint32_t, int> m_constantRootIndices;
    std::unordered_map<uint32_t, int> m_bufferSrvRootIndices;
    std::unordered_map<uint32_t, int> m_bufferUavRootIndices;
    std::unordered_map<uint32_t, int> m_textureSrvRootIndices;
    std::unordered_map<uint32_t, int> m_textureUavRootIndices;
    std::unordered_map<uint32_t, int> m_samplerRootIndices;
    std::unordered_map<uint32_t, uint32_t> m_constantWordCounts;
  };

  class D3D12ComputeBuffer final : public ComputeBuffer {
  public:
    bool Create(ID3D12Device* device,
                D3D12Driver* driver,
                const ComputeBufferDesc& desc,
                const void* initialData);

    ID3D12Resource* GetResource() const { return m_resource.Get(); }
    D3D12_RESOURCE_STATES GetState() const { return m_state; }
    void SetState(D3D12_RESOURCE_STATES state) { m_state = state; }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> m_resource;
    D3D12_RESOURCE_STATES m_state = D3D12_RESOURCE_STATE_COMMON;
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_D3D12COMPUTE_H
