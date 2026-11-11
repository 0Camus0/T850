#include <pch.h>
#include <debug/RuntimeTelemetry.h>
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

    // One place maps a compute storage format to a DXGI format, so the texture and the
    // UAV whose format the binding check compares can never disagree.
    DXGI_FORMAT ToDxgiStorageFormat(ComputeStorageFormat format) {
      switch (format) {
        case ComputeStorageFormat::Rgba8Unorm:  return DXGI_FORMAT_R8G8B8A8_UNORM;
        case ComputeStorageFormat::Rgba16Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case ComputeStorageFormat::R32Float:    return DXGI_FORMAT_R32_FLOAT;
        default:                                return DXGI_FORMAT_UNKNOWN;
      }
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
    T8_TELEMETRY_ADD("shader.cache.uncached", 1);
    HRESULT hr = T8_TELEMETRY_CALL("shader.compile", D3DCompile(source.data(), source.size(), desc.debugName.c_str(), nullptr, nullptr,
                desc.entryPoint.c_str(), "cs_5_0", flags, 0, &blob, &errors));
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D11][Compute] Shader compile failed for '%s': %s", desc.debugName.c_str(),
                   errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error");
      return false;
    }
    hr = T8_TELEMETRY_CALL("shader.module.create", device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader));
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
    if (!tx || !ty || !tz) {
      T8_LOG_ERROR("[D3D11][Compute] Shader '%s' has an invalid thread-group size",
                   desc.debugName.c_str());
      return false;
    }
    threadGroupSize = {tx, ty, tz};
    std::vector<ComputeBindingLayoutDesc> reflected;
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
      ComputeBindingLayoutDesc resource;
      resource.shaderRegister = binding.BindPoint;
      switch (binding.Type) {
        case D3D_SIT_CBUFFER:
          resource.type = ComputeBindingType::Constants32;
          resource.constantCount = constantWordCounts.at(binding.BindPoint);
          break;
        case D3D_SIT_STRUCTURED: case D3D_SIT_BYTEADDRESS: resource.type = ComputeBindingType::ReadOnlyBuffer; break;
        case D3D_SIT_UAV_RWSTRUCTURED: case D3D_SIT_UAV_RWBYTEADDRESS: resource.type = ComputeBindingType::ReadWriteBuffer; break;
        case D3D_SIT_TEXTURE: resource.type = ComputeBindingType::ReadOnlyTexture; break;
        case D3D_SIT_UAV_RWTYPED: resource.type = ComputeBindingType::ReadWriteTexture; break;
        case D3D_SIT_SAMPLER: resource.type = ComputeBindingType::Sampler; break;
        default: return false;
      }
      if ((resource.type == ComputeBindingType::ReadOnlyTexture || resource.type == ComputeBindingType::ReadWriteTexture)) {
        if (binding.Dimension == D3D_SRV_DIMENSION_TEXTURE3D) {
          resource.dimension = ComputeTextureDimension::Tex3D;
        } else if (binding.Dimension == D3D_SRV_DIMENSION_TEXTURECUBE &&
                   resource.type == ComputeBindingType::ReadOnlyTexture) {
          // An environment cube binds through the texture's own SRV, like a 2D texture.
          resource.dimension = ComputeTextureDimension::Cube;
        } else if (binding.Dimension != D3D_SRV_DIMENSION_TEXTURE2D) {
          return false;
        }
      }
      reflected.push_back(resource);
    }
    if (!SetValidatedLayout(desc, reflected, false)) return false;

    if (!desc.debugName.empty())
      shader->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(desc.debugName.size()), desc.debugName.data());
    ShaderPermutationDump::RecordCompute(desc.debugName, desc.entryPoint, desc.permutationName, desc.defines);
    std::string uavList;
    for (UINT slot : textureUavs) { uavList += std::to_string(slot); uavList += ','; }
    T8_LOG_INFO("[D3D11][Compute] Pipeline '%s' created (threads=%ux%ux%u constants=%zu bufferSRVs=%zu bufferUAVs=%zu textureSRVs=%zu textureUAVs=%zu[%s] samplers=%zu)",
                desc.debugName.c_str(), tx, ty, tz, constantWordCounts.size(), bufferSrvs.size(), bufferUavs.size(),
                textureSrvs.size(), textureUavs.size(), uavList.c_str(), samplers.size());
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
      ? D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE : D3D11_BIND_SHADER_RESOURCE;
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
    }
    {
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

  bool D3D11ComputeTexture3D::Create(ID3D11Device* device, const ComputeTexture3DDesc& desc, const void* initialData) {
    if (!device || desc.width == 0 || desc.height == 0 || desc.depth == 0) return false;
    if (desc.format == ComputeStorageFormat::Unspecified) return false;
    // Volume contents are produced on the GPU, so CPU seeding is not supported.
    if (initialData) return false;
    descriptor = desc;

    const DXGI_FORMAT format = ToDxgiStorageFormat(desc.format);
    if (format == DXGI_FORMAT_UNKNOWN) {
      T8_LOG_ERROR("[D3D11][Compute] Volume '%s' has an unsupported storage format", desc.debugName.c_str());
      return false;
    }
    D3D11_TEXTURE3D_DESC textureDesc = {};
    textureDesc.Width = desc.width;
    textureDesc.Height = desc.height;
    textureDesc.Depth = desc.depth;
    textureDesc.MipLevels = 1;
    textureDesc.Format = format;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.CPUAccessFlags = 0;
    textureDesc.MiscFlags = 0;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE |
      (desc.access == ComputeBufferAccess::ReadWrite ? D3D11_BIND_UNORDERED_ACCESS : 0);
    if (FAILED(device->CreateTexture3D(&textureDesc, nullptr, &texture))) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
    srvDesc.Texture3D.MipLevels = 1;
    srvDesc.Texture3D.MostDetailedMip = 0;
    if (FAILED(device->CreateShaderResourceView(texture.Get(), &srvDesc, &srv))) return false;

    if (desc.access == ComputeBufferAccess::ReadWrite) {
      D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
      uavDesc.Format = format;
      uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
      uavDesc.Texture3D.MipSlice = 0;
      uavDesc.Texture3D.FirstWSlice = 0;
      uavDesc.Texture3D.WSize = desc.depth;
      if (FAILED(device->CreateUnorderedAccessView(texture.Get(), &uavDesc, &uav))) return false;
    }
    if (!desc.debugName.empty())
      texture->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(desc.debugName.size()), desc.debugName.data());
    return true;
  }

  std::unique_ptr<ComputeTexture3D> D3DXDriver::CreateComputeTexture3D(const ComputeTexture3DDesc& desc,
                                                                       const void* initialData) {
    auto volume = std::make_unique<D3D11ComputeTexture3D>();
    return volume->Create(GetDevice(), desc, initialData) ? std::move(volume) : nullptr;
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
    if (!pipeline || !pipeline->ValidateBindings(bindings) || !device || !context || !groupCountX || !groupCountY || !groupCountZ ||
        groupCountX > 65535 || groupCountY > 65535 || groupCountZ > 65535) return false;

    struct ResolvedBinding {
      const ComputeBindingDesc* binding = nullptr;
      D3D11ComputeBuffer* buffer = nullptr;
      D3DXTexture* texture = nullptr;
      D3D11ComputeTexture3D* texture3D = nullptr;
      Microsoft::WRL::ComPtr<ID3D11Buffer> constants;
      Microsoft::WRL::ComPtr<ID3D11Buffer> inlineBuffer;
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> inlineSrv;
    };
    std::unordered_set<uint32_t> constants, bufferSrvs, bufferUavs;
    std::unordered_set<uint32_t> textureSrvs, textureUavs, samplers;
    std::vector<ResolvedBinding> resolved;
    resolved.reserve(bindings.size());
    for (const ComputeBindingDesc& binding : bindings) {
      const ComputeBindingLayoutDesc* layout = nullptr;
      for (const auto& candidate : pipeline->bindingLayout)
        if (candidate.type == binding.type && candidate.shaderRegister == binding.shaderRegister) layout = &candidate;
      const bool volume = layout && layout->dimension == ComputeTextureDimension::Tex3D;
      if (binding.type == ComputeBindingType::ReadWriteTexture || binding.type == ComputeBindingType::ReadOnlyTexture) {
        if (volume) {
          const auto* texture3D = dynamic_cast<D3D11ComputeTexture3D*>(binding.texture3D);
          if (!texture3D || !texture3D->srv) return false;
          if (binding.type == ComputeBindingType::ReadWriteTexture && !texture3D->uav) return false;
        } else {
          const auto* texture = dynamic_cast<D3DXTexture*>(binding.texture);
          if (!texture || !texture->pSRVTex) return false;
          D3D11_SHADER_RESOURCE_VIEW_DESC view{};
          texture->pSRVTex->GetDesc(&view);
          if (layout && layout->dimension == ComputeTextureDimension::Cube) {
            // An environment cube binds through its own TEXTURECUBE SRV. Read-only only.
            if (binding.type != ComputeBindingType::ReadOnlyTexture ||
                view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURECUBE) {
              T8_LOG_ERROR("[D3D11][Compute] Cube t%u does not match: type=%d view=%d",
                           binding.shaderRegister, static_cast<int>(binding.type),
                           static_cast<int>(view.ViewDimension));
              return false;
            }
          } else if (view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D) return false;
          if (binding.type == ComputeBindingType::ReadWriteTexture) {
            if (!texture->pUAVTex) return false;
            D3D11_UNORDERED_ACCESS_VIEW_DESC output{};
            texture->pUAVTex->GetDesc(&output);
            for (const auto& candidate : pipeline->bindingLayout)
              if (candidate.type == binding.type && candidate.shaderRegister == binding.shaderRegister &&
                  output.Format != ToDxgiStorageFormat(candidate.storageFormat)) return false;
          }
        }
      }
      ResolvedBinding item;
      item.binding = &binding;
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
          if (FAILED(device->CreateBuffer(&desc, &data, &item.constants))) return false;
          break;
        }
        case ComputeBindingType::ReadOnlyBuffer: {
          item.buffer = dynamic_cast<D3D11ComputeBuffer*>(binding.buffer);
          if (!pipeline->bufferSrvs.count(binding.shaderRegister) ||
              !bufferSrvs.insert(binding.shaderRegister).second) return false;
          if (item.buffer) {
            if (!item.buffer->srv) return false;
            break;
          }
          if (!binding.inlineData || !binding.inlineBytes || !binding.inlineStride) return false;
          D3D11_BUFFER_DESC desc = {};
          desc.ByteWidth = binding.inlineBytes;
          desc.Usage = D3D11_USAGE_IMMUTABLE;
          desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
          desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
          desc.StructureByteStride = binding.inlineStride;
          D3D11_SUBRESOURCE_DATA data = {};
          data.pSysMem = binding.inlineData;
          if (FAILED(device->CreateBuffer(&desc, &data, &item.inlineBuffer))) return false;
          D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
          view.Format = DXGI_FORMAT_UNKNOWN;
          view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
          view.Buffer.FirstElement = 0;
          view.Buffer.NumElements = binding.inlineBytes / binding.inlineStride;
          if (FAILED(device->CreateShaderResourceView(item.inlineBuffer.Get(), &view, &item.inlineSrv))) return false;
          break;
        }
        case ComputeBindingType::ReadWriteBuffer: {
          item.buffer = dynamic_cast<D3D11ComputeBuffer*>(binding.buffer);
          if (!item.buffer || !item.buffer->uav || !pipeline->bufferUavs.count(binding.shaderRegister) ||
              !bufferUavs.insert(binding.shaderRegister).second) return false;
          break;
        }
        case ComputeBindingType::ReadOnlyTexture: {
          if (volume) {
            item.texture3D = dynamic_cast<D3D11ComputeTexture3D*>(binding.texture3D);
            if (!item.texture3D || !item.texture3D->srv || !pipeline->textureSrvs.count(binding.shaderRegister) ||
                !textureSrvs.insert(binding.shaderRegister).second) return false;
          } else {
            item.texture = dynamic_cast<D3DXTexture*>(binding.texture);
            if (!item.texture || !item.texture->pSRVTex || !pipeline->textureSrvs.count(binding.shaderRegister) ||
                !textureSrvs.insert(binding.shaderRegister).second) return false;
          }
          break;
        }
        case ComputeBindingType::ReadWriteTexture: {
          if (volume) {
            item.texture3D = dynamic_cast<D3D11ComputeTexture3D*>(binding.texture3D);
            if (!item.texture3D || !item.texture3D->uav || !pipeline->textureUavs.count(binding.shaderRegister) ||
                !textureUavs.insert(binding.shaderRegister).second) return false;
          } else {
            item.texture = dynamic_cast<D3DXTexture*>(binding.texture);
            if (!item.texture || !item.texture->pUAVTex || !pipeline->textureUavs.count(binding.shaderRegister) ||
                !textureUavs.insert(binding.shaderRegister).second) return false;
          }
          break;
        }
        case ComputeBindingType::Sampler: {
          item.texture = dynamic_cast<D3DXTexture*>(binding.texture);
          if (!item.texture || !item.texture->pSampler || !pipeline->samplers.count(binding.shaderRegister) ||
              !samplers.insert(binding.shaderRegister).second) return false;
          break;
        }
      }
      resolved.push_back(std::move(item));
    }
    if (constants.size() != pipeline->constantWordCounts.size() ||
        bufferSrvs.size() != pipeline->bufferSrvs.size() || bufferUavs.size() != pipeline->bufferUavs.size() ||
        textureSrvs.size() != pipeline->textureSrvs.size() || textureUavs.size() != pipeline->textureUavs.size() ||
        samplers.size() != pipeline->samplers.size()) return false;

    ID3D11ShaderResourceView* nullGraphicsSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    context->VSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullGraphicsSRVs);
    context->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullGraphicsSRVs);
    context->GSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullGraphicsSRVs);
    context->OMSetRenderTargets(0, nullptr, nullptr);

    for (const ResolvedBinding& item : resolved) {
      const ComputeBindingDesc& binding = *item.binding;
      switch (binding.type) {
        case ComputeBindingType::Constants32: {
          ID3D11Buffer* raw = item.constants.Get();
          context->CSSetConstantBuffers(binding.shaderRegister, 1, &raw);
          break;
        }
        case ComputeBindingType::ReadOnlyBuffer: {
          ID3D11ShaderResourceView* raw = item.buffer ? item.buffer->srv.Get() : item.inlineSrv.Get();
          context->CSSetShaderResources(binding.shaderRegister, 1, &raw);
          break;
        }
        case ComputeBindingType::ReadWriteBuffer: {
          ID3D11UnorderedAccessView* raw = item.buffer->uav.Get();
          context->CSSetUnorderedAccessViews(binding.shaderRegister, 1, &raw, nullptr);
          break;
        }
        case ComputeBindingType::ReadOnlyTexture: {
          ID3D11ShaderResourceView* raw = item.texture3D ? item.texture3D->srv.Get() : item.texture->pSRVTex.Get();
          context->CSSetShaderResources(binding.shaderRegister, 1, &raw);
          break;
        }
        case ComputeBindingType::ReadWriteTexture: {
          ID3D11UnorderedAccessView* raw = item.texture3D ? item.texture3D->uav.Get() : item.texture->pUAVTex.Get();
          context->CSSetUnorderedAccessViews(binding.shaderRegister, 1, &raw, nullptr);
          break;
        }
        case ComputeBindingType::Sampler: {
          ID3D11SamplerState* raw = item.texture->pSampler.Get();
          context->CSSetSamplers(binding.shaderRegister, 1, &raw);
          break;
        }
      }
    }

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
    if (!buffer || !buffer->buffer || !destination || !byteCount || byteCount % 4 || byteCount > buffer->descriptor.byteWidth)
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
