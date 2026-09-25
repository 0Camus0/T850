#include <pch.h>
#include <core/Config.h>
#include <debug/RuntimeTelemetry.h>
#include <video/d3d12/D3D12Compute.h>
#include <video/d3d12/D3D12Device.h>
#include <video/d3d12/D3D12Driver.h>
#include <video/d3d12/D3D12Shader.h>
#include <video/d3d12/D3D12PipelineLibrary.h>
#include <video/d3d12/D3D12ShaderCacheSession.h>
#include <video/d3d12/D3D12Texture.h>

#ifdef OS_WINDOWS

#include <utils/Log.h>
#include <utils/ShaderDiskCache.h>
#include <utils/ShaderPermutationDump.h>

#include <algorithm>
#include <cstring>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace t850 {

  extern Device* T8Device;
  extern DeviceContext* T8DeviceContext;

  namespace {
    bool BuildComputeSource(const ComputePipelineDesc& desc, std::string& source) {
      std::ostringstream prefix;
      for (const std::string& define : desc.defines) {
        if (define.empty())
          continue;
        if (define.find('\n') != std::string::npos || define.find('\r') != std::string::npos) {
          T8_LOG_ERROR("[D3D12][Compute] Invalid multiline define in permutation '%s'",
                       desc.permutationName.c_str());
          return false;
        }
        prefix << "#define " << define << '\n';
      }
      if (!desc.defines.empty())
        prefix << '\n';
      prefix << desc.source;
      source = prefix.str();
      return true;
    }

    int FindRootIndex(const std::unordered_map<uint32_t, int>& indices, uint32_t shaderRegister) {
      const auto found = indices.find(shaderRegister);
      return found == indices.end() ? -1 : found->second;
    }

    void TransitionBuffer(ID3D12GraphicsCommandList* commandList,
                          D3D12ComputeBuffer& buffer,
                          D3D12_RESOURCE_STATES nextState) {
      if (buffer.GetState() == nextState)
        return;

      D3D12_RESOURCE_BARRIER barrier = {};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Transition.pResource = buffer.GetResource();
      barrier.Transition.StateBefore = buffer.GetState();
      barrier.Transition.StateAfter = nextState;
      barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      commandList->ResourceBarrier(1, &barrier);
      buffer.SetState(nextState);
    }

    void TransitionTexture(ID3D12GraphicsCommandList* commandList,
                           D3D12Texture& texture,
                           D3D12_RESOURCE_STATES nextState) {
      if (!texture.pTexResource || texture.GetTrackedState() == nextState)
        return;

      D3D12_RESOURCE_BARRIER barrier = {};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Transition.pResource = texture.pTexResource.Get();
      barrier.Transition.StateBefore = texture.GetTrackedState();
      barrier.Transition.StateAfter = nextState;
      barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      commandList->ResourceBarrier(1, &barrier);
      texture.SetTrackedState(nextState);
    }
  }

  int D3D12ComputePipeline::GetConstantRootIndex(uint32_t shaderRegister) const {
    return FindRootIndex(m_constantRootIndices, shaderRegister);
  }

  int D3D12ComputePipeline::GetBufferSrvRootIndex(uint32_t shaderRegister) const {
    return FindRootIndex(m_bufferSrvRootIndices, shaderRegister);
  }

  int D3D12ComputePipeline::GetBufferUavRootIndex(uint32_t shaderRegister) const {
    return FindRootIndex(m_bufferUavRootIndices, shaderRegister);
  }

  int D3D12ComputePipeline::GetTextureSrvRootIndex(uint32_t shaderRegister) const {
    return FindRootIndex(m_textureSrvRootIndices, shaderRegister);
  }

  int D3D12ComputePipeline::GetTextureUavRootIndex(uint32_t shaderRegister) const {
    return FindRootIndex(m_textureUavRootIndices, shaderRegister);
  }

  int D3D12ComputePipeline::GetSamplerRootIndex(uint32_t shaderRegister) const {
    return FindRootIndex(m_samplerRootIndices, shaderRegister);
  }

  uint32_t D3D12ComputePipeline::GetConstantWordCount(uint32_t shaderRegister) const {
    const auto found = m_constantWordCounts.find(shaderRegister);
    return found == m_constantWordCounts.end() ? 0u : found->second;
  }

  bool D3D12ComputePipeline::Create(ID3D12Device* device, D3D12PipelineLibrary* pipelineLibrary,
                                    D3D12ShaderCacheSession* cacheSession, const ComputePipelineDesc& desc) {
    if (!device || desc.source.empty() || desc.entryPoint.empty()) {
      T8_LOG_ERROR("[D3D12][Compute] Invalid pipeline descriptor");
      return false;
    }

    std::string compiledSource;
    if (!BuildComputeSource(desc, compiledSource))
      return false;

    const std::string driverSignature = GetD3D12ShaderCacheDriverSignature(device);
    const bool legacy = UseLegacyD3D12ShaderCompiler();
  #ifdef _DEBUG
    const std::string cacheApi = legacy ? "d3d12-legacy-debug" : "d3d12-debug";
  #else
    const std::string cacheApi = legacy ? "d3d12-legacy" : "d3d12";
  #endif
    const std::string shaderProfile = GetD3D12ShaderProfile(device, D3D12ShaderStage::Compute);
    const std::string cacheProfile = shaderProfile + ";flow=" + (legacy ? "legacyHLSL" : "dxc");
    const ShaderDiskCacheKey cacheKey = ShaderDiskCache::MakeComputeKey(
      cacheApi,
      driverSignature,
      desc.debugName,
      desc.entryPoint,
      cacheProfile,
      compiledSource);

    const std::string artifact = legacy ? "cs.dxbc" : "cs.dxil";
    const std::string reflectionArtifact = "cs.reflection";
    std::vector<uint8_t> cachedShader, cachedReflection;
    D3D12CompiledShader compiled;
    std::string diagnostic;
    if (ShaderDiskCache::LoadArtifact(cacheKey, artifact, cachedShader) &&
        (legacy || ShaderDiskCache::LoadArtifact(cacheKey, reflectionArtifact, cachedReflection)) &&
        RestoreD3D12Shader(cachedShader, cachedReflection, legacy, compiled, diagnostic)) {
      T8_TELEMETRY_ADD("shader.cache.hits", 1);
      T8_LOG_DEBUG("[ShaderCache][D3D12] CS hit %s", cacheKey.sha1.c_str());
    } else {
      T8_TELEMETRY_ADD("shader.cache.misses", 1);
      const auto compileStarted = std::chrono::steady_clock::now();
      if (!T8_TELEMETRY_CALL("shader.compile", CompileD3D12Shader(
            device, compiledSource, desc.debugName, desc.entryPoint,
            D3D12ShaderStage::Compute, compiled, diagnostic))) {
        T8_LOG_ERROR("[D3D12][Compute] Shader compile failed for '%s': %s",
                     desc.debugName.c_str(), diagnostic.c_str());
        return false;
      }
      const double compileMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - compileStarted).count();
      if (g_config.flags.compileShaders) {
        T8_LOG_INFO("[ShaderCompileProfile] backend=d3d12 flow=%s stage=compute shader=\"%s\" entry=%s permutation=\"%s\" cache=miss elapsedMs=%.6f",
          legacy ? "legacyHLSL" : "dxc", desc.debugName.c_str(), desc.entryPoint.c_str(),
          desc.permutationName.c_str(), compileMs);
      }
      ShaderDiskCache::StoreArtifact(
        cacheKey, artifact, compiled.bytecode->GetBufferPointer(), compiled.bytecode->GetBufferSize());
      if (!legacy) ShaderDiskCache::StoreArtifact(
        cacheKey, reflectionArtifact, compiled.reflectionData->GetBufferPointer(), compiled.reflectionData->GetBufferSize());
      ShaderDiskCache::WriteManifest(cacheKey, driverSignature);
      T8_LOG_DEBUG("[ShaderCache][D3D12] CS stored %s", cacheKey.sha1.c_str());
    }
    m_shaderBlob = compiled.bytecode;
    Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection = compiled.reflection;

    D3D12_SHADER_DESC shaderDesc = {};
    if (FAILED(reflection->GetDesc(&shaderDesc))) {
      T8_LOG_ERROR("[D3D12][Compute] Could not inspect shader resources");
      return false;
    }
    UINT threadGroupX = 0;
    UINT threadGroupY = 0;
    UINT threadGroupZ = 0;
    reflection->GetThreadGroupSize(&threadGroupX, &threadGroupY, &threadGroupZ);
    if (!threadGroupX || !threadGroupY || !threadGroupZ) {
      T8_LOG_ERROR("[D3D12][Compute] Shader '%s' has an invalid thread-group size",
                   desc.debugName.c_str());
      return false;
    }
    threadGroupSize = {threadGroupX, threadGroupY, threadGroupZ};

    std::vector<D3D12_ROOT_PARAMETER> parameters;
    std::vector<ComputeBindingLayoutDesc> reflected;
    parameters.reserve(shaderDesc.BoundResources);
    std::vector<D3D12_DESCRIPTOR_RANGE> descriptorRanges;
    descriptorRanges.reserve(shaderDesc.BoundResources);
    for (UINT resourceIndex = 0; resourceIndex < shaderDesc.BoundResources; ++resourceIndex) {
      D3D12_SHADER_INPUT_BIND_DESC binding = {};
      if (FAILED(reflection->GetResourceBindingDesc(resourceIndex, &binding))) {
        T8_LOG_ERROR("[D3D12][Compute] Could not inspect resource %u", resourceIndex);
        return false;
      }
      if (binding.BindCount != 1) {
        T8_LOG_ERROR("[D3D12][Compute] Resource arrays are not supported yet: '%s' count=%u",
                     binding.Name, binding.BindCount);
        return false;
      }
      if (binding.Space != 0) {
        T8_LOG_ERROR("[D3D12][Compute] Register spaces are not supported yet: '%s' space=%u",
                     binding.Name, binding.Space);
        return false;
      }

      D3D12_ROOT_PARAMETER parameter = {};
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
      const int rootIndex = static_cast<int>(parameters.size());

      switch (binding.Type) {
        case D3D_SIT_CBUFFER: {
          ID3D12ShaderReflectionConstantBuffer* constantBuffer =
            reflection->GetConstantBufferByName(binding.Name);
          D3D12_SHADER_BUFFER_DESC constantDesc = {};
          if (!constantBuffer || FAILED(constantBuffer->GetDesc(&constantDesc)) ||
              constantDesc.Size == 0 || (constantDesc.Size % sizeof(uint32_t)) != 0) {
            T8_LOG_ERROR("[D3D12][Compute] Invalid constant buffer layout for '%s'", binding.Name);
            return false;
          }
          const uint32_t wordCount = constantDesc.Size / sizeof(uint32_t);
          if (wordCount > 64) {
            T8_LOG_ERROR("[D3D12][Compute] Root constants exceed 64 DWORDs for '%s'", binding.Name);
            return false;
          }
          parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
          parameter.Constants.ShaderRegister = binding.BindPoint;
          parameter.Constants.RegisterSpace = binding.Space;
          parameter.Constants.Num32BitValues = wordCount;
          m_constantRootIndices[binding.BindPoint] = rootIndex;
          m_constantWordCounts[binding.BindPoint] = wordCount;
          T8_LOG_INFO("[D3D12][Compute] Root parameter %d: constants b%u (%u DWORDs)",
                      rootIndex, binding.BindPoint, wordCount);
          break;
        }
        case D3D_SIT_STRUCTURED:
        case D3D_SIT_BYTEADDRESS:
          parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
          parameter.Descriptor.ShaderRegister = binding.BindPoint;
          parameter.Descriptor.RegisterSpace = binding.Space;
          m_bufferSrvRootIndices[binding.BindPoint] = rootIndex;
          T8_LOG_INFO("[D3D12][Compute] Root parameter %d: structured SRV t%u",
                      rootIndex, binding.BindPoint);
          break;
        case D3D_SIT_UAV_RWSTRUCTURED:
        case D3D_SIT_UAV_RWBYTEADDRESS:
          parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
          parameter.Descriptor.ShaderRegister = binding.BindPoint;
          parameter.Descriptor.RegisterSpace = binding.Space;
          m_bufferUavRootIndices[binding.BindPoint] = rootIndex;
          T8_LOG_INFO("[D3D12][Compute] Root parameter %d: structured UAV u%u",
                      rootIndex, binding.BindPoint);
          break;
        case D3D_SIT_TEXTURE:
        case D3D_SIT_UAV_RWTYPED:
        case D3D_SIT_SAMPLER: {
          D3D12_DESCRIPTOR_RANGE range = {};
          range.NumDescriptors = 1;
          range.BaseShaderRegister = binding.BindPoint;
          range.RegisterSpace = binding.Space;
          range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
          if (binding.Type == D3D_SIT_TEXTURE) {
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            m_textureSrvRootIndices[binding.BindPoint] = rootIndex;
          } else if (binding.Type == D3D_SIT_UAV_RWTYPED) {
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            m_textureUavRootIndices[binding.BindPoint] = rootIndex;
          } else {
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            m_samplerRootIndices[binding.BindPoint] = rootIndex;
          }
          descriptorRanges.push_back(range);
          parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
          parameter.DescriptorTable.NumDescriptorRanges = 1;
          parameter.DescriptorTable.pDescriptorRanges = &descriptorRanges.back();
          T8_LOG_INFO("[D3D12][Compute] Root parameter %d: descriptor type=%d register=%u",
                      rootIndex, static_cast<int>(range.RangeType), binding.BindPoint);
          break;
        }
        default:
          T8_LOG_ERROR("[D3D12][Compute] Unsupported resource '%s' type=%d",
                       binding.Name, static_cast<int>(binding.Type));
          return false;
      }
      parameters.push_back(parameter);
      ComputeBindingLayoutDesc resource;
      resource.shaderRegister = binding.BindPoint;
      switch (binding.Type) {
        case D3D_SIT_CBUFFER:
          resource.type = ComputeBindingType::Constants32;
          resource.constantCount = m_constantWordCounts.at(binding.BindPoint);
          break;
        case D3D_SIT_STRUCTURED: case D3D_SIT_BYTEADDRESS: resource.type = ComputeBindingType::ReadOnlyBuffer; break;
        case D3D_SIT_UAV_RWSTRUCTURED: case D3D_SIT_UAV_RWBYTEADDRESS: resource.type = ComputeBindingType::ReadWriteBuffer; break;
        case D3D_SIT_TEXTURE: resource.type = ComputeBindingType::ReadOnlyTexture; break;
        case D3D_SIT_UAV_RWTYPED: resource.type = ComputeBindingType::ReadWriteTexture; break;
        case D3D_SIT_SAMPLER: resource.type = ComputeBindingType::Sampler; break;
        default: return false;
      }
      if ((resource.type == ComputeBindingType::ReadOnlyTexture || resource.type == ComputeBindingType::ReadWriteTexture) &&
          binding.Dimension != D3D_SRV_DIMENSION_TEXTURE2D) return false;
      reflected.push_back(resource);
    }
    if (!SetValidatedLayout(desc, reflected, false)) return false;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = static_cast<UINT>(parameters.size());
    rootDesc.pParameters = parameters.empty() ? nullptr : parameters.data();
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> signatureErrors;
    const HRESULT serializeHr = D3D12SerializeRootSignature(
      &rootDesc,
      D3D_ROOT_SIGNATURE_VERSION_1,
      &signature,
      &signatureErrors);
    if (FAILED(serializeHr)) {
      T8_LOG_ERROR("[D3D12][Compute] Root signature serialization failed (hr=0x%08X): %s",
                   static_cast<unsigned>(serializeHr),
                   signatureErrors ? static_cast<const char*>(signatureErrors->GetBufferPointer()) : "unknown error");
      return false;
    }

    const HRESULT rootHr = device->CreateRootSignature(
      0,
      signature->GetBufferPointer(),
      signature->GetBufferSize(),
      IID_PPV_ARGS(&m_rootSignature));
    if (FAILED(rootHr)) {
      T8_LOG_ERROR("[D3D12][Compute] Root signature creation failed (hr=0x%08X)",
                   static_cast<unsigned>(rootHr));
      return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc = {};
    pipelineDesc.pRootSignature = m_rootSignature.Get();
    pipelineDesc.CS.pShaderBytecode = m_shaderBlob->GetBufferPointer();
    pipelineDesc.CS.BytecodeLength = m_shaderBlob->GetBufferSize();
    const std::string persistentKey = "t850-compute-pso-v1:" + cacheKey.sha1;
    const std::span<const uint8_t> persistentKeyBytes(
      reinterpret_cast<const uint8_t*>(persistentKey.data()), persistentKey.size());
    if (pipelineLibrary && pipelineLibrary->IsEnabled()) {
      if (!pipelineLibrary->LoadCompute(persistentKeyBytes, pipelineDesc, m_pipelineState)) {
        const HRESULT pipelineHr = T8_TELEMETRY_CALL("pipeline.create.compute", device->CreateComputePipelineState(
          &pipelineDesc, IID_PPV_ARGS(&m_pipelineState)));
        if (FAILED(pipelineHr)) {
          T8_LOG_ERROR("[D3D12][Compute] Pipeline creation failed (hr=0x%08X)", static_cast<unsigned>(pipelineHr));
          return false;
        }
        pipelineLibrary->Store(persistentKeyBytes, m_pipelineState.Get());
      }
    } else {
      std::vector<uint8_t> cachedPipeline;
      bool restoredPipeline = cacheSession && cacheSession->Find(
        persistentKeyBytes, cachedPipeline);
      if (restoredPipeline) pipelineDesc.CachedPSO = {cachedPipeline.data(), cachedPipeline.size()};
      HRESULT pipelineHr = T8_TELEMETRY_CALL("pipeline.create.compute", device->CreateComputePipelineState(
        &pipelineDesc, IID_PPV_ARGS(&m_pipelineState)));
      if (FAILED(pipelineHr) && restoredPipeline) {
        cacheSession->RecordRejected();
        pipelineDesc.CachedPSO = {};
        restoredPipeline = false;
        pipelineHr = T8_TELEMETRY_CALL("pipeline.create.compute", device->CreateComputePipelineState(
          &pipelineDesc, IID_PPV_ARGS(&m_pipelineState)));
      }
      if (FAILED(pipelineHr)) {
        T8_LOG_ERROR("[D3D12][Compute] Pipeline creation failed (hr=0x%08X)",
                     static_cast<unsigned>(pipelineHr));
        return false;
      }
      if (cacheSession && !restoredPipeline) {
        cacheSession->Store(persistentKeyBytes, m_pipelineState.Get());
      }
    }

    if (!desc.debugName.empty()) {
      const std::wstring debugName(desc.debugName.begin(), desc.debugName.end());
      const std::wstring rootName = debugName + L" Root Signature";
      const std::wstring pipelineName = debugName + L" Compute PSO";
      m_rootSignature->SetName(rootName.c_str());
      m_pipelineState->SetName(pipelineName.c_str());
    }

    ShaderPermutationDump::RecordCompute(
      desc.debugName, desc.entryPoint, desc.permutationName, desc.defines);

    T8_LOG_INFO("[D3D12][Compute] Pipeline '%s' created (permutation=%s profile=%s flow=%s threads=%ux%ux%u constants=%zu bufferSRVs=%zu bufferUAVs=%zu textureSRVs=%zu textureUAVs=%zu samplers=%zu)",
                desc.debugName.c_str(),
                desc.permutationName.c_str(),
          shaderProfile.c_str(), legacy ? "legacyHLSL" : "dxc",
                threadGroupX, threadGroupY, threadGroupZ,
                m_constantRootIndices.size(),
          m_bufferSrvRootIndices.size(),
          m_bufferUavRootIndices.size(),
          m_textureSrvRootIndices.size(),
          m_textureUavRootIndices.size(),
          m_samplerRootIndices.size());
    return true;
  }

  bool D3D12ComputeBuffer::Create(ID3D12Device* device,
                                  D3D12Driver* driver,
                                  const ComputeBufferDesc& desc,
                                  const void* initialData) {
    if (!device || !driver || desc.byteWidth == 0 || desc.structureStride == 0 ||
        (desc.byteWidth % desc.structureStride) != 0) {
      T8_LOG_ERROR("[D3D12][Compute] Invalid buffer descriptor for '%s'", desc.debugName.c_str());
      return false;
    }

    descriptor = desc;
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = desc.byteWidth;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = desc.access == ComputeBufferAccess::ReadWrite
      ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
      : D3D12_RESOURCE_FLAG_NONE;

    const D3D12_RESOURCE_STATES finalState = desc.access == ComputeBufferAccess::ReadWrite
      ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
      : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    // D3D12 buffers are effectively created in COMMON. Initial uploads rely on
    // implicit promotion to COPY_DEST; otherwise DispatchCompute transitions
    // from COMMON to the reflected SRV/UAV state before first use.
    const D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    const HRESULT createHr = device->CreateCommittedResource(
      &heap,
      D3D12_HEAP_FLAG_NONE,
      &resourceDesc,
      initialState,
      nullptr,
      IID_PPV_ARGS(&m_resource));
    if (FAILED(createHr)) {
      T8_LOG_ERROR("[D3D12][Compute] Buffer '%s' creation failed (hr=0x%08X)",
                   desc.debugName.c_str(), static_cast<unsigned>(createHr));
      return false;
    }

    if (!desc.debugName.empty()) {
      const std::wstring debugName(desc.debugName.begin(), desc.debugName.end());
      m_resource->SetName(debugName.c_str());
    }

    m_state = initialState;
    if (initialData) {
      driver->UploadBufferData(m_resource.Get(), initialData, desc.byteWidth, finalState);
      m_state = finalState;
    }

    T8_LOG_INFO("[D3D12][Compute] Buffer '%s' created (%u bytes, stride=%u, access=%s)",
                desc.debugName.c_str(), desc.byteWidth, desc.structureStride,
                desc.access == ComputeBufferAccess::ReadWrite ? "read-write" : "read-only");
    return true;
  }

  std::unique_ptr<ComputePipeline> D3D12Driver::CreateComputePipeline(const ComputePipelineDesc& desc) {
    auto pipeline = std::make_unique<D3D12ComputePipeline>();
    ID3D12Device* device = static_cast<D3D12Device*>(T8Device)->GetNativeDevice();
    if (!pipeline->Create(device, &m_pipelineLibrary, &m_shaderCacheSession, desc))
      return {};
    return pipeline;
  }

  std::unique_ptr<ComputeBuffer> D3D12Driver::CreateComputeBuffer(const ComputeBufferDesc& desc,
                                                                  const void* initialData) {
    auto buffer = std::make_unique<D3D12ComputeBuffer>();
    ID3D12Device* device = static_cast<D3D12Device*>(T8Device)->GetNativeDevice();
    if (!buffer->Create(device, this, desc, initialData))
      return {};
    return buffer;
  }

  bool D3D12Driver::DispatchCompute(ComputePipeline& pipelineBase,
                                    const std::vector<ComputeBindingDesc>& bindings,
                                    uint32_t groupCountX,
                                    uint32_t groupCountY,
                                    uint32_t groupCountZ) {
    auto* pipeline = dynamic_cast<D3D12ComputePipeline*>(&pipelineBase);
    if (!pipeline || !pipeline->ValidateBindings(bindings) || groupCountX == 0 || groupCountY == 0 || groupCountZ == 0) {
      T8_LOG_ERROR("[D3D12][Compute] Invalid pipeline or zero dispatch extent");
      return false;
    }
    constexpr uint32_t maxDispatchGroupsPerDimension = 65535;
    if (groupCountX > maxDispatchGroupsPerDimension ||
        groupCountY > maxDispatchGroupsPerDimension ||
        groupCountZ > maxDispatchGroupsPerDimension) {
      T8_LOG_ERROR("[D3D12][Compute] Dispatch extent exceeds D3D12 limits: %u x %u x %u",
                   groupCountX, groupCountY, groupCountZ);
      return false;
    }
    if (CurrentRT >= 0) {
      T8_LOG_ERROR("[D3D12][Compute] Dispatch requested while render target %d is active", CurrentRT);
      return false;
    }

    struct ResolvedBinding {
      const ComputeBindingDesc* binding = nullptr;
      D3D12ComputeBuffer* buffer = nullptr;
      D3D12Texture* texture = nullptr;
      int rootIndex = -1;
    };
    std::vector<ResolvedBinding> resolved;
    resolved.reserve(bindings.size());
    std::unordered_set<uint32_t> boundConstants;
    std::unordered_set<uint32_t> boundBufferSrvs;
    std::unordered_set<uint32_t> boundBufferUavs;
    std::unordered_set<uint32_t> boundTextureSrvs;
    std::unordered_set<uint32_t> boundTextureUavs;
    std::unordered_set<uint32_t> boundSamplers;

    for (const ComputeBindingDesc& binding : bindings) {
      if (binding.type == ComputeBindingType::ReadWriteTexture || binding.type == ComputeBindingType::ReadOnlyTexture) {
        const auto* texture = dynamic_cast<D3D12Texture*>(binding.texture);
        if (!texture || !texture->pTexResource) return false;
        const auto view = texture->pTexResource->GetDesc();
        if (view.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || view.DepthOrArraySize != 1 || view.SampleDesc.Count != 1)
          return false;
        if (binding.type == ComputeBindingType::ReadWriteTexture)
          for (const auto& layout : pipeline->bindingLayout)
            if (layout.type == binding.type && layout.shaderRegister == binding.shaderRegister &&
                view.Format != (layout.storageFormat == ComputeStorageFormat::Rgba16Float
                  ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM)) return false;
      }
      ResolvedBinding item;
      item.binding = &binding;
      switch (binding.type) {
        case ComputeBindingType::Constants32:
          item.rootIndex = pipeline->GetConstantRootIndex(binding.shaderRegister);
          if (!binding.constants || binding.constantCount == 0 ||
              binding.constantCount != pipeline->GetConstantWordCount(binding.shaderRegister)) {
            T8_LOG_ERROR("[D3D12][Compute] Constants b%u do not match reflected layout",
                         binding.shaderRegister);
            return false;
          }
          if (!boundConstants.insert(binding.shaderRegister).second) {
            T8_LOG_ERROR("[D3D12][Compute] Duplicate constants binding b%u", binding.shaderRegister);
            return false;
          }
          break;
        case ComputeBindingType::ReadOnlyBuffer:
          item.rootIndex = pipeline->GetBufferSrvRootIndex(binding.shaderRegister);
          item.buffer = dynamic_cast<D3D12ComputeBuffer*>(binding.buffer);
          if (!item.buffer) {
            T8_LOG_ERROR("[D3D12][Compute] Invalid read-only buffer t%u", binding.shaderRegister);
            return false;
          }
          if (!boundBufferSrvs.insert(binding.shaderRegister).second) {
            T8_LOG_ERROR("[D3D12][Compute] Duplicate read-only binding t%u", binding.shaderRegister);
            return false;
          }
          break;
        case ComputeBindingType::ReadWriteBuffer:
          item.rootIndex = pipeline->GetBufferUavRootIndex(binding.shaderRegister);
          item.buffer = dynamic_cast<D3D12ComputeBuffer*>(binding.buffer);
          if (!item.buffer || item.buffer->descriptor.access != ComputeBufferAccess::ReadWrite) {
            T8_LOG_ERROR("[D3D12][Compute] Invalid read-write buffer u%u", binding.shaderRegister);
            return false;
          }
          if (!boundBufferUavs.insert(binding.shaderRegister).second) {
            T8_LOG_ERROR("[D3D12][Compute] Duplicate read-write binding u%u", binding.shaderRegister);
            return false;
          }
          break;
        case ComputeBindingType::ReadOnlyTexture:
          item.rootIndex = pipeline->GetTextureSrvRootIndex(binding.shaderRegister);
          item.texture = dynamic_cast<D3D12Texture*>(binding.texture);
          if (!item.texture || !item.texture->pTexResource || !item.texture->srvGPU.ptr) {
            T8_LOG_ERROR("[D3D12][Compute] Invalid sampled texture t%u", binding.shaderRegister);
            return false;
          }
          if (!boundTextureSrvs.insert(binding.shaderRegister).second) {
            T8_LOG_ERROR("[D3D12][Compute] Duplicate sampled texture t%u", binding.shaderRegister);
            return false;
          }
          break;
        case ComputeBindingType::ReadWriteTexture:
          item.rootIndex = pipeline->GetTextureUavRootIndex(binding.shaderRegister);
          item.texture = dynamic_cast<D3D12Texture*>(binding.texture);
          if (!item.texture || !item.texture->pTexResource || !item.texture->uavGPU.ptr) {
            T8_LOG_ERROR("[D3D12][Compute] Invalid storage texture u%u", binding.shaderRegister);
            return false;
          }
          if (!boundTextureUavs.insert(binding.shaderRegister).second) {
            T8_LOG_ERROR("[D3D12][Compute] Duplicate storage texture u%u", binding.shaderRegister);
            return false;
          }
          break;
        case ComputeBindingType::Sampler:
          item.rootIndex = pipeline->GetSamplerRootIndex(binding.shaderRegister);
          item.texture = dynamic_cast<D3D12Texture*>(binding.texture);
          if (!item.texture || !item.texture->hasSampler || !item.texture->samplerGPU.ptr) {
            T8_LOG_ERROR("[D3D12][Compute] Invalid sampler s%u", binding.shaderRegister);
            return false;
          }
          if (!boundSamplers.insert(binding.shaderRegister).second) {
            T8_LOG_ERROR("[D3D12][Compute] Duplicate sampler s%u", binding.shaderRegister);
            return false;
          }
          break;
      }
      if (item.rootIndex < 0) {
        T8_LOG_ERROR("[D3D12][Compute] Shader register %u is not present in the pipeline",
                     binding.shaderRegister);
        return false;
      }
      resolved.push_back(item);
    }

    if (boundConstants.size() != pipeline->m_constantRootIndices.size() ||
        boundBufferSrvs.size() != pipeline->m_bufferSrvRootIndices.size() ||
        boundBufferUavs.size() != pipeline->m_bufferUavRootIndices.size() ||
        boundTextureSrvs.size() != pipeline->m_textureSrvRootIndices.size() ||
        boundTextureUavs.size() != pipeline->m_textureUavRootIndices.size() ||
        boundSamplers.size() != pipeline->m_samplerRootIndices.size()) {
      T8_LOG_ERROR("[D3D12][Compute] Dispatch bindings are incomplete (b=%zu/%zu buffer-t=%zu/%zu buffer-u=%zu/%zu texture-t=%zu/%zu texture-u=%zu/%zu s=%zu/%zu)",
                   boundConstants.size(), pipeline->m_constantRootIndices.size(),
                   boundBufferSrvs.size(), pipeline->m_bufferSrvRootIndices.size(),
                   boundBufferUavs.size(), pipeline->m_bufferUavRootIndices.size(),
                   boundTextureSrvs.size(), pipeline->m_textureSrvRootIndices.size(),
                   boundTextureUavs.size(), pipeline->m_textureUavRootIndices.size(),
                   boundSamplers.size(), pipeline->m_samplerRootIndices.size());
      return false;
    }

    BeginFrame(FrameTargetMode::Offscreen);
    ID3D12GraphicsCommandList* commandList = GetCmdList();
    ID3D12DescriptorHeap* heaps[] = {
      m_heaps[D3D12Heap::CBV_SRV_UAV_VISIBLE].GetHeap(),
      m_heaps[D3D12Heap::SAMPLER].GetHeap()
    };
    commandList->SetDescriptorHeaps(2, heaps);
    commandList->SetComputeRootSignature(pipeline->GetRootSignature());
    commandList->SetPipelineState(pipeline->GetPipelineState());
    auto& keepAlive = m_computeKeepAlive[m_currentBackBuffer];
    keepAlive.try_emplace(pipeline->GetPipelineState(), pipeline->GetPipelineState());
    keepAlive.try_emplace(pipeline->GetRootSignature(), pipeline->GetRootSignature());

    std::unordered_set<ID3D12Resource*> writtenResources;
    for (const ResolvedBinding& item : resolved) {
      if (item.buffer) keepAlive.try_emplace(item.buffer->GetResource(), item.buffer->GetResource());
      if (item.texture) keepAlive.try_emplace(item.texture->pTexResource.Get(), item.texture->pTexResource.Get());
      const ComputeBindingDesc& binding = *item.binding;
      switch (binding.type) {
        case ComputeBindingType::Constants32:
          commandList->SetComputeRoot32BitConstants(
            static_cast<UINT>(item.rootIndex),
            binding.constantCount,
            binding.constants,
            0);
          break;
        case ComputeBindingType::ReadOnlyBuffer:
          TransitionBuffer(commandList, *item.buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
          commandList->SetComputeRootShaderResourceView(
            static_cast<UINT>(item.rootIndex),
            item.buffer->GetResource()->GetGPUVirtualAddress());
          break;
        case ComputeBindingType::ReadWriteBuffer:
          TransitionBuffer(commandList, *item.buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
          commandList->SetComputeRootUnorderedAccessView(
            static_cast<UINT>(item.rootIndex),
            item.buffer->GetResource()->GetGPUVirtualAddress());
          writtenResources.insert(item.buffer->GetResource());
          break;
        case ComputeBindingType::ReadOnlyTexture:
          TransitionTexture(commandList, *item.texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
          commandList->SetComputeRootDescriptorTable(
            static_cast<UINT>(item.rootIndex), item.texture->srvGPU);
          break;
        case ComputeBindingType::ReadWriteTexture:
          TransitionTexture(commandList, *item.texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
          commandList->SetComputeRootDescriptorTable(
            static_cast<UINT>(item.rootIndex), item.texture->uavGPU);
          writtenResources.insert(item.texture->pTexResource.Get());
          break;
        case ComputeBindingType::Sampler:
          commandList->SetComputeRootDescriptorTable(
            static_cast<UINT>(item.rootIndex), item.texture->samplerGPU);
          break;
      }
    }

    commandList->Dispatch(groupCountX, groupCountY, groupCountZ);
    for (ID3D12Resource* resource : writtenResources) {
      D3D12_RESOURCE_BARRIER barrier = {};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
      barrier.UAV.pResource = resource;
      commandList->ResourceBarrier(1, &barrier);
    }
    for (const ResolvedBinding& item : resolved) {
      if (item.texture && item.binding->type != ComputeBindingType::Sampler)
        TransitionTexture(commandList, *item.texture, item.texture->GetGraphicsReadState());
    }

    // Graphics state must be rebound after a compute pipeline/root signature.
    m_lastPSO = nullptr;
    m_lastRootSig = nullptr;
    T8DeviceContext->actualShaderSet = nullptr;
    T8DeviceContext->actualConstantBuffer = nullptr;
    T8_LOG_TRACE("[D3D12][Compute] Dispatched %u x %u x %u workgroups",
           groupCountX, groupCountY, groupCountZ);
    return true;
  }

  bool D3D12Driver::ReadComputeBuffer(ComputeBuffer& bufferBase,
                                      void* destination,
                                      size_t byteCount) {
    auto* buffer = dynamic_cast<D3D12ComputeBuffer*>(&bufferBase);
    if (!buffer || !destination || byteCount == 0 || byteCount % 4 || byteCount > buffer->descriptor.byteWidth) {
      T8_LOG_ERROR("[D3D12][Compute] Invalid readback request");
      return false;
    }
    ID3D12Device* device = static_cast<D3D12Device*>(T8Device)->GetNativeDevice();
    D3D12_HEAP_PROPERTIES readbackHeap = {};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readbackDesc = {};
    readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = byteCount;
    readbackDesc.Height = 1;
    readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1;
    readbackDesc.SampleDesc.Count = 1;
    readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    const HRESULT createHr = device->CreateCommittedResource(
      &readbackHeap,
      D3D12_HEAP_FLAG_NONE,
      &readbackDesc,
      D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr,
      IID_PPV_ARGS(&readback));
    if (FAILED(createHr)) {
      T8_LOG_ERROR("[D3D12][Compute] Readback buffer creation failed (hr=0x%08X)",
                   static_cast<unsigned>(createHr));
      return false;
    }

    const bool frameOpen = m_frameStarted;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> standalone;
    if (!frameOpen && (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
        FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&standalone)))))
      return false;
    ID3D12GraphicsCommandList* commandList = frameOpen ? GetCmdList() : standalone.Get();
    const D3D12_RESOURCE_STATES previousState = buffer->GetState();
    TransitionBuffer(commandList, *buffer, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->CopyBufferRegion(readback.Get(), 0, buffer->GetResource(), 0, byteCount);
    TransitionBuffer(commandList, *buffer, previousState);

    if (FAILED(commandList->Close())) return false;
    ID3D12CommandList* lists[] = {commandList};
    m_commandQueue->ExecuteCommandLists(1, lists);
    WaitForGPU();
    if (frameOpen) {
      if (FAILED(commandList->Reset(m_commandAllocators[m_currentBackBuffer].Get(), nullptr))) return false;
      ID3D12DescriptorHeap* heaps[] = {
        m_heaps[D3D12Heap::CBV_SRV_UAV_VISIBLE].GetHeap(), m_heaps[D3D12Heap::SAMPLER].GetHeap()
      };
      commandList->SetDescriptorHeaps(2, heaps);
      m_lastPSO = nullptr;
      m_lastRootSig = nullptr;
      T8DeviceContext->actualShaderSet = nullptr;
      T8DeviceContext->actualConstantBuffer = nullptr;
      T8DeviceContext->actualIndexBuffer = nullptr;
      T8DeviceContext->actualVertexBuffer = nullptr;
      const auto viewport = m_viewport;
      const auto scissor = m_scissorRect;
      if (CurrentRT >= 0) PushRTLoad(CurrentRT);
      else PopRT();
      m_viewport = viewport;
      m_scissorRect = scissor;
      commandList->RSSetViewports(1, &m_viewport);
      commandList->RSSetScissorRects(1, &m_scissorRect);
    }

    D3D12_RANGE readRange = { 0, byteCount };
    void* mapped = nullptr;
    const HRESULT mapHr = readback->Map(0, &readRange, &mapped);
    if (FAILED(mapHr) || !mapped) {
      T8_LOG_ERROR("[D3D12][Compute] Readback mapping failed (hr=0x%08X)",
                   static_cast<unsigned>(mapHr));
      return false;
    }
    std::memcpy(destination, mapped, byteCount);
    const D3D12_RANGE writtenRange = { 0, 0 };
    readback->Unmap(0, &writtenRange);
    return true;
  }

} // namespace t850

#endif // OS_WINDOWS
