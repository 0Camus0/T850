#ifndef T800_D3D12SHADER_H
#define T800_D3D12SHADER_H

#include <Config.h>
#include <video/BaseDriver.h>

#ifdef OS_WINDOWS

#include <d3d12.h>
#include <D3Dcompiler.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#include <unordered_map>
#include <string>
#include <vector>

namespace t850 {

  class D3D12Shader : public ShaderBase {
  public:
    bool CreateShaderAPI(std::string src_vs, std::string src_fs,
                         const std::string& vs_name = "", const std::string& fs_name = "") override;
    void Set(const DeviceContext& deviceContext) override;
    void DestroyAPIShader() override;

    ComPtr<ID3DBlob>            VS_blob;
    ComPtr<ID3DBlob>            FS_blob;
    ComPtr<ID3D12RootSignature> pRootSignature;
    std::vector<D3D12_INPUT_ELEMENT_DESC> VertexDecl;
    int vertexStride = 0;

    // Root parameter slot indices (resolved during reflection)
    int cbvSlot = -1;      // root param index for constant buffer b0
    int samplerSlot = -1;  // root param index for sampler s0
    // SRV slots: root param index for each texture register t0..tN
    std::unordered_map<int, int> srvSlots; // register -> root param index
    std::unordered_map<int, int> samplerSlots; // register -> root param index

  private:
    bool BuildRootSignature(ID3D12Device* device, ID3D12ShaderReflection* vsReflect,
                            ID3D12ShaderReflection* fsReflect);
    // Storage for semantic name strings (must outlive VertexDecl)
    std::vector<std::string> m_semanticNames;
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_D3D12SHADER_H
