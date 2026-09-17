#include <pch.h>
#include <video/d3d11/D3D11Compute.h>
#include <video/d3d11/D3D11Driver.h>
#include <video/d3d11/D3D11Texture.h>

#ifdef OS_WINDOWS

#include <utils/Log.h>
#include <utils/ShaderPermutationDump.h>

#include <algorithm>
#include <cstring>
#include <sstream>

namespace t850 {

  extern Device* T8Device;
  extern DeviceContext* T8DeviceContext;

  namespace {
    std::string BuildSource(const ComputePipelineDesc& desc) {
      std::ostringstream source;
      for (const std::string& define : desc.defines)
        if (!define.empty()) source << "#define " << define << '\n';
      source << desc.source;
      return source.str();
    }

    ID3D11Device* GetDevice() {
      return reinterpret_cast<ID3D11Device*>(T8Device->GetAPIObject());
    }

    ID3D11DeviceContext* GetContext() {
      return reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    }
  }

  bool D3D11ComputePipeline::Create(ID3D11Device* device, const ComputePipelineDesc& desc) {
    if (!device || desc.source.empty() || desc.entryPoint.empty())
      return false;

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    const std::string source = BuildSource(desc);
    Microsoft::WRL::ComPtr<ID3DBlob> blob;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompile(source.data(), source.size(), desc.debugName.c_str(), nullptr, nullptr,
                            desc.entryPoint.c_str(), "cs_5_0", flags, 0, &blob, &errors);
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D11][Compute] Shader compile failed for '%s': %s", desc.debugName.c_str(),
                   errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error");
      return false;
    }
    hr = device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader);
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D11][Compute] CreateComputeShader failed hr=0x%08X", hr);
      return false;
    }

    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
    if (FAILED(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&reflection))))
      return false;
    D3D11_SHADER_DESC shaderDesc = {};
    if (FAILED(reflection->GetDesc(&shaderDesc))) return false;
    UINT tx = 0, ty = 0, tz = 0;
    reflection->GetThreadGroupSize(&tx, &ty, &tz);
    for (UINT i = 0; i < shaderDesc.BoundResources; ++i) {
      D3D11_SHADER_INPUT_BIND_DESC binding = {};
      if (FAILED(reflection->GetResourceBindingDesc(i, &binding)) || binding.BindCount != 1)
        return false;
      switch (binding.Type) {
        case D3D_SIT_CBUFFER: {
          ID3D11ShaderReflectionConstantBuffer* cb = reflection->GetConstantBufferByName(binding.Name);
          D3D11_SHADER_BUFFER_DESC cbDesc = {};
          if (!cb || FAILED(cb->GetDesc(&cbDesc)) || cbDesc.Size == 0 || (cbDesc.Size % 4) != 0)
            return false;
          constantWordCounts[binding.BindPoint] = cbDesc.Size / 4;
          break;
        }
        case D3D_SIT_STRUCTURED:
        case D3D_SIT_BYTEADDRESS: bufferSrvs.insert(binding.BindPoint); break;
        case D3D_SIT_UAV_RWSTRUCTURED:
        case D3D_SIT_UAV_RWBYTEADDRESS: bufferUavs.insert(binding.BindPoint); break;
        case D3D_SIT_TEXTURE: textureSrvs.insert(binding.BindPoint); break;
        case D3D_SIT_UAV_RWTYPED: textureUavs.insert(binding.BindPoint); break;
        case D3D_SIT_SAMPLER: samplers.insert(binding.BindPoint); break;
        default: return false;
      }
    }

    if (!desc.debugName.empty())
      shader->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(desc.debugName.size()), desc.debugName.data());
    ShaderPermutationDump::RecordCompute(desc.debugName, desc.entryPoint, desc.permutationName, desc.defines);
    T8_LOG_INFO("[D3D11][Compute] Pipeline '%s' created (threads=%ux%ux%u constants=%zu bufferSRVs=%zu bufferUAVs=%zu)",
                desc.debugName.c_str(), tx, ty, tz, constantWordCounts.size(), bufferSrvs.size(), bufferUavs.size());
    return true;
  }

  bool D3D11ComputeBuffer::Create(ID3D11Device* device, const ComputeBufferDesc& desc, const void* initialData) {
    if (!device || desc.byteWidth == 0 || desc.structureStride == 0 ||
        (desc.byteWidth % desc.structureStride) != 0) return false;
    descriptor = desc;
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = desc.byteWidth;
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = desc.access == ComputeBufferAccess::ReadWrite
      ? D3D11_BIND_UNORDERED_ACCESS : D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bufferDesc.StructureByteStride = desc.structureStride;
    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = initialData;
    if (FAILED(device->CreateBuffer(&bufferDesc, initialData ? &data : nullptr, &buffer))) return false;

    const UINT elements = desc.byteWidth / desc.structureStride;
    if (desc.access == ComputeBufferAccess::ReadWrite) {
      D3D11_UNORDERED_ACCESS_VIEW_DESC view = {};
      view.Format = DXGI_FORMAT_UNKNOWN;
      view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
      view.Buffer.NumElements = elements;
      if (FAILED(device->CreateUnorderedAccessView(buffer.Get(), &view, &uav))) return false;
    } else {
      D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
      view.Format = DXGI_FORMAT_UNKNOWN;
      view.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
      view.BufferEx.NumElements = elements;
      if (FAILED(device->CreateShaderResourceView(buffer.Get(), &view, &srv))) return false;
    }
    if (!desc.debugName.empty())
      buffer->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(desc.debugName.size()), desc.debugName.data());
    return true;
  }

  std::unique_ptr<ComputePipeline> D3DXDriver::CreateComputePipeline(const ComputePipelineDesc& desc) {
    auto pipeline = std::make_unique<D3D11ComputePipeline>();
    return pipeline->Create(GetDevice(), desc) ? std::move(pipeline) : nullptr;
  }

  std::unique_ptr<ComputeBuffer> D3DXDriver::CreateComputeBuffer(const ComputeBufferDesc& desc, const void* initialData) {
    auto buffer = std::make_unique<D3D11ComputeBuffer>();
    return buffer->Create(GetDevice(), desc, initialData) ? std::move(buffer) : nullptr;
  }

  bool D3DXDriver::DispatchCompute(ComputePipeline& pipelineBase,
                                   const std::vector<ComputeBindingDesc>& bindings,
                                   uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    auto* pipeline = dynamic_cast<D3D11ComputePipeline*>(&pipelineBase);
    ID3D11Device* device = GetDevice();
    ID3D11DeviceContext* context = GetContext();
    if (!pipeline || !device || !context || !groupCountX || !groupCountY || !groupCountZ ||
        groupCountX > 65535 || groupCountY > 65535 || groupCountZ > 65535) return false;

    ID3D11ShaderResourceView* nullGraphicsSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    context->VSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullGraphicsSRVs);
    context->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullGraphicsSRVs);
    context->GSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullGraphicsSRVs);
    context->OMSetRenderTargets(0, nullptr, nullptr);

    std::unordered_set<uint32_t> constants, bufferSrvs, bufferUavs;
    std::unordered_set<uint32_t> textureSrvs, textureUavs, samplers;
    std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>> transientConstants;
    for (const ComputeBindingDesc& binding : bindings) {
      switch (binding.type) {
        case ComputeBindingType::Constants32: {
          auto expected = pipeline->constantWordCounts.find(binding.shaderRegister);
          if (expected == pipeline->constantWordCounts.end() || !binding.constants ||
              expected->second != binding.constantCount || !constants.insert(binding.shaderRegister).second) return false;
          D3D11_BUFFER_DESC desc = {};
          desc.ByteWidth = static_cast<UINT>((binding.constantCount * 4u + 15u) & ~15u);
          desc.Usage = D3D11_USAGE_IMMUTABLE;
          desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
          std::vector<uint32_t> paddedConstants(desc.ByteWidth / sizeof(uint32_t), 0u);
          std::copy_n(binding.constants, binding.constantCount, paddedConstants.data());
          D3D11_SUBRESOURCE_DATA data = {};
          data.pSysMem = paddedConstants.data();
          Microsoft::WRL::ComPtr<ID3D11Buffer> cb;
          if (FAILED(device->CreateBuffer(&desc, &data, &cb))) return false;
          ID3D11Buffer* raw = cb.Get();
          context->CSSetConstantBuffers(binding.shaderRegister, 1, &raw);
          transientConstants.push_back(std::move(cb));
          break;
        }
        case ComputeBindingType::ReadOnlyBuffer: {
          auto* buffer = dynamic_cast<D3D11ComputeBuffer*>(binding.buffer);
          if (!buffer || !buffer->srv || !pipeline->bufferSrvs.count(binding.shaderRegister) ||
              !bufferSrvs.insert(binding.shaderRegister).second) return false;
          ID3D11ShaderResourceView* raw = buffer->srv.Get();
          context->CSSetShaderResources(binding.shaderRegister, 1, &raw);
          break;
        }
        case ComputeBindingType::ReadWriteBuffer: {
          auto* buffer = dynamic_cast<D3D11ComputeBuffer*>(binding.buffer);
          if (!buffer || !buffer->uav || !pipeline->bufferUavs.count(binding.shaderRegister) ||
              !bufferUavs.insert(binding.shaderRegister).second) return false;
          ID3D11UnorderedAccessView* raw = buffer->uav.Get();
          context->CSSetUnorderedAccessViews(binding.shaderRegister, 1, &raw, nullptr);
          break;
        }
        case ComputeBindingType::ReadOnlyTexture: {
          auto* texture = dynamic_cast<D3DXTexture*>(binding.texture);
          if (!texture || !texture->pSRVTex || !pipeline->textureSrvs.count(binding.shaderRegister) ||
              !textureSrvs.insert(binding.shaderRegister).second) return false;
          ID3D11ShaderResourceView* raw = texture->pSRVTex.Get();
          context->CSSetShaderResources(binding.shaderRegister, 1, &raw);
          break;
        }
        case ComputeBindingType::ReadWriteTexture: {
          auto* texture = dynamic_cast<D3DXTexture*>(binding.texture);
          if (!texture || !texture->pUAVTex || !pipeline->textureUavs.count(binding.shaderRegister) ||
              !textureUavs.insert(binding.shaderRegister).second) return false;
          ID3D11UnorderedAccessView* raw = texture->pUAVTex.Get();
          context->CSSetUnorderedAccessViews(binding.shaderRegister, 1, &raw, nullptr);
          break;
        }
        case ComputeBindingType::Sampler: {
          auto* texture = dynamic_cast<D3DXTexture*>(binding.texture);
          if (!texture || !texture->pSampler || !pipeline->samplers.count(binding.shaderRegister) ||
              !samplers.insert(binding.shaderRegister).second) return false;
          ID3D11SamplerState* raw = texture->pSampler.Get();
          context->CSSetSamplers(binding.shaderRegister, 1, &raw);
          break;
        }
      }
    }
    if (constants.size() != pipeline->constantWordCounts.size() ||
        bufferSrvs.size() != pipeline->bufferSrvs.size() || bufferUavs.size() != pipeline->bufferUavs.size() ||
        textureSrvs.size() != pipeline->textureSrvs.size() || textureUavs.size() != pipeline->textureUavs.size() ||
        samplers.size() != pipeline->samplers.size()) return false;

    context->CSSetShader(pipeline->shader.Get(), nullptr, 0);
    context->Dispatch(groupCountX, groupCountY, groupCountZ);
    ID3D11Buffer* nullCBs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    ID3D11ShaderResourceView* nullSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    ID3D11UnorderedAccessView* nullUAVs[D3D11_PS_CS_UAV_REGISTER_COUNT] = {};
    ID3D11SamplerState* nullSamplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
    context->CSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, nullCBs);
    context->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRVs);
    context->CSSetUnorderedAccessViews(0, D3D11_PS_CS_UAV_REGISTER_COUNT, nullUAVs, nullptr);
    context->CSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, nullSamplers);
    context->CSSetShader(nullptr, nullptr, 0);
    // Compute requires all OM targets unbound. Restore the normal frame output
    // for a following graphics pass, including the final backbuffer quad.
    PopRT();
    return true;
  }

  bool D3DXDriver::ReadComputeBuffer(ComputeBuffer& bufferBase, void* destination, size_t byteCount) {
    auto* buffer = dynamic_cast<D3D11ComputeBuffer*>(&bufferBase);
    ID3D11Device* device = GetDevice();
    ID3D11DeviceContext* context = GetContext();
    if (!buffer || !buffer->buffer || !destination || !byteCount || byteCount > buffer->descriptor.byteWidth)
      return false;
    D3D11_BUFFER_DESC source = {};
    buffer->buffer->GetDesc(&source);
    source.BindFlags = 0;
    source.MiscFlags = 0;
    source.Usage = D3D11_USAGE_STAGING;
    source.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
    if (FAILED(device->CreateBuffer(&source, nullptr, &staging))) return false;
    context->CopyResource(staging.Get(), buffer->buffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    std::memcpy(destination, mapped.pData, byteCount);
    context->Unmap(staging.Get(), 0);
    return true;
  }

} // namespace t850

#endif
