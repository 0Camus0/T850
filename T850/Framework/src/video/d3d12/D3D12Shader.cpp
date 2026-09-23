#include <pch.h>
#include <debug/RuntimeTelemetry.h>
/*********************************************************
* T850 Engine — D3D12 Backend
* D3D12Shader.cpp: Shader compilation, reflection, root signature
*********************************************************/

#include <video/d3d12/D3D12Driver.h>

#ifdef OS_WINDOWS

#include <utils/Log.h>
#include <utils/ShaderDiskCache.h>
#include <debug/RenderTrace.h>
#include <core/Config.h>
#if defined(_M_X64) || defined(_M_ARM64)
#include <directx-dxc/dxcapi.h>
#define T850_NATIVE_DXC_AVAILABLE 1
#else
#define T850_NATIVE_DXC_AVAILABLE 0
#endif
#include <algorithm>
#include <mutex>

namespace t850 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  static D3D12Driver* GetD3D12Driver() { return static_cast<D3D12Driver*>(g_pBaseDriver); }
  static ID3D12Device* GetNativeDevice() { return static_cast<D3D12Device*>(T8Device)->GetNativeDevice(); }

  namespace {
#if T850_NATIVE_DXC_AVAILABLE
    using DxcCreateInstanceProc = HRESULT(__stdcall*)(REFCLSID, REFIID, LPVOID*);

    struct DxcState {
      HMODULE module = nullptr;
      DxcCreateInstanceProc createInstance = nullptr;
      ComPtr<IDxcUtils> utils;
      ComPtr<IDxcCompiler3> compiler;
      std::mutex mutex;

      bool Init(std::string& diagnostic) {
        if (compiler && utils) return true;
        module = LoadLibraryW(L"dxcompiler.dll");
        if (!module) {
          diagnostic = "dxcompiler.dll is unavailable";
          return false;
        }
        createInstance = reinterpret_cast<DxcCreateInstanceProc>(
          GetProcAddress(module, "DxcCreateInstance"));
        if (!createInstance || FAILED(createInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils))) ||
            FAILED(createInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)))) {
          diagnostic = "DxcCreateInstance failed";
          return false;
        }
        return true;
      }
    };

    DxcState& GetDxcState() {
      static DxcState state;
      return state;
    }

    std::string GetDxcVersionSignature() {
      static std::once_flag versionOnce;
      static std::string version = "unavailable";
      std::call_once(versionOnce, [] {
        DxcState& state = GetDxcState();
        std::lock_guard<std::mutex> lock(state.mutex);
        std::string diagnostic;
        if (!state.Init(diagnostic)) return;
        ComPtr<IDxcVersionInfo> info;
        UINT32 major = 0, minor = 0;
        if (SUCCEEDED(state.compiler.As(&info)) && SUCCEEDED(info->GetVersion(&major, &minor)))
          version = std::to_string(major) + "." + std::to_string(minor);
      });
      return version;
    }
#else
    std::string GetDxcVersionSignature() { return "unsupported-architecture"; }
#endif

    std::string WideToUtf8(const wchar_t* text) {
      if (!text)
        return {};
      char buffer[256] = {};
      std::wcstombs(buffer, text, sizeof(buffer) - 1);
      return buffer;
    }

    bool SameLuid(const LUID& a, const LUID& b) {
      return a.HighPart == b.HighPart && a.LowPart == b.LowPart;
    }

    std::string BuildD3D12ShaderCacheDriverSignature(ID3D12Device* device) {
      std::ostringstream sig;
      sig << "d3d12;compiler=" << (UseLegacyD3D12ShaderCompiler()
        ? "legacyHLSL-fxc-sm5" : "dxc-" + GetDxcVersionSignature() + "-sm6-ieee-v1");
      if (!device)
        return sig.str();

      const LUID deviceLuid = device->GetAdapterLuid();
      ComPtr<IDXGIFactory4> factory;
      if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
          DXGI_ADAPTER_DESC1 desc = {};
          if (SUCCEEDED(adapter->GetDesc1(&desc)) && SameLuid(desc.AdapterLuid, deviceLuid)) {
            LARGE_INTEGER driverVersion = {};
            const HRESULT versionHr = adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion);
            sig << ";adapter=" << WideToUtf8(desc.Description)
                << ";vendor=" << desc.VendorId
                << ";device=" << desc.DeviceId
                << ";subsys=" << desc.SubSysId
                << ";revision=" << desc.Revision;
            if (SUCCEEDED(versionHr) && (driverVersion.HighPart != 0 || driverVersion.LowPart != 0))
              sig << ";driver=" << driverVersion.HighPart << "." << driverVersion.LowPart;
            else
              sig << ";driver=unknown";
            break;
          }
          adapter.Reset();
        }
      }
      return sig.str();
    }

    bool CreateBlobFromBytes(const std::vector<uint8_t>& bytes, ComPtr<ID3DBlob>& blob) {
      if (bytes.empty())
        return false;
      ComPtr<ID3DBlob> created;
      if (FAILED(D3DCreateBlob(bytes.size(), &created)))
        return false;
      std::memcpy(created->GetBufferPointer(), bytes.data(), bytes.size());
      blob = created;
      return true;
    }

    bool CreateDxcReflection(const void* data, size_t size,
                             ComPtr<ID3D12ShaderReflection>& reflection,
                             std::string& diagnostic) {
  #if T850_NATIVE_DXC_AVAILABLE
      DxcState& state = GetDxcState();
      std::lock_guard<std::mutex> lock(state.mutex);
      if (!state.Init(diagnostic)) return false;
      DxcBuffer buffer{};
      buffer.Ptr = data;
      buffer.Size = size;
      buffer.Encoding = DXC_CP_ACP;
      const HRESULT result = state.utils->CreateReflection(&buffer, IID_PPV_ARGS(&reflection));
      if (FAILED(result)) {
        diagnostic = "DXC reflection failed (hr=0x" + std::to_string(static_cast<unsigned>(result)) + ")";
        return false;
      }
      return true;
    #else
      (void)data; (void)size; (void)reflection;
      diagnostic = "Native DXC is packaged only for x64 and ARM64; use --shaderFlow legacyHLSL";
      return false;
    #endif
    }

    D3D_SHADER_MODEL GetHighestShaderModel(ID3D12Device* device) {
      for (const D3D_SHADER_MODEL candidate : {
             D3D_SHADER_MODEL_6_6, D3D_SHADER_MODEL_6_5, D3D_SHADER_MODEL_6_4,
             D3D_SHADER_MODEL_6_3, D3D_SHADER_MODEL_6_2, D3D_SHADER_MODEL_6_1,
             D3D_SHADER_MODEL_6_0}) {
        D3D12_FEATURE_DATA_SHADER_MODEL feature{candidate};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &feature, sizeof(feature))) &&
            feature.HighestShaderModel >= candidate)
          return candidate;
      }
      return D3D_SHADER_MODEL_5_1;
    }

    std::wstring ToWide(const std::string& value) {
      if (value.empty()) return {};
      const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
      std::wstring result(count, L'\0');
      MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count);
      return result;
    }
  }

  bool UseLegacyD3D12ShaderCompiler() {
    return g_config.webgpuShaderFlow == "legacyhlsl";
  }

  std::string GetD3D12ShaderProfile(ID3D12Device* device, D3D12ShaderStage stage) {
    const char* prefix = stage == D3D12ShaderStage::Vertex ? "vs" :
                         stage == D3D12ShaderStage::Fragment ? "ps" : "cs";
    if (UseLegacyD3D12ShaderCompiler()) return std::string(prefix) + "_5_0";
    const unsigned model = static_cast<unsigned>(GetHighestShaderModel(device));
    const unsigned major = (model >> 4u) & 0xfu;
    const unsigned minor = model & 0xfu;
    return std::string(prefix) + "_" + std::to_string(major) + "_" + std::to_string(minor);
  }

  bool CompileD3D12Shader(ID3D12Device* device,
                          const std::string& source,
                          const std::string& sourceName,
                          const std::string& entryPoint,
                          D3D12ShaderStage stage,
                          D3D12CompiledShader& output,
                          std::string& diagnostic) {
    output = {};
    output.profile = GetD3D12ShaderProfile(device, stage);
    output.legacy = UseLegacyD3D12ShaderCompiler();
    if (!output.legacy && output.profile.ends_with("_5_1")) {
      diagnostic = "DXC requires Shader Model 6 support; use --shaderFlow legacyHLSL on this adapter";
      return false;
    }
    static std::once_flag compilerIdentityLogged;
    std::call_once(compilerIdentityLogged, [&] {
      T8_LOG_INFO("[D3D12] shader compiler flow=%s profile=%s bytecode=%s",
                  output.legacy ? "legacyHLSL" : "dxc", output.profile.c_str(),
                  output.legacy ? "DXBC" : "DXIL");
    });
    if (output.legacy) {
      ComPtr<ID3DBlob> errors;
      UINT flags = stage == D3D12ShaderStage::Compute ? D3DCOMPILE_ENABLE_STRICTNESS : 0;
      if (stage == D3D12ShaderStage::Compute) {
#ifdef _DEBUG
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
      }
      const HRESULT result = D3DCompile(
        source.data(), source.size(), sourceName.empty() ? nullptr : sourceName.c_str(),
        nullptr, nullptr, entryPoint.c_str(), output.profile.c_str(), flags, 0,
        &output.bytecode, &errors);
      if (FAILED(result)) {
        diagnostic = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "FXC failed";
        return false;
      }
      const HRESULT reflectResult = D3DReflect(
        output.bytecode->GetBufferPointer(), output.bytecode->GetBufferSize(),
        IID_PPV_ARGS(&output.reflection));
      if (FAILED(reflectResult)) {
        diagnostic = "FXC reflection failed";
        return false;
      }
      return true;
    }

  #if T850_NATIVE_DXC_AVAILABLE
    DxcState& state = GetDxcState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.Init(diagnostic)) return false;
    const std::wstring entry = ToWide(entryPoint);
    const std::wstring profile = ToWide(output.profile);
    const std::wstring name = ToWide(sourceName);
    std::vector<LPCWSTR> arguments = {
      name.empty() ? L"T850Shader.hlsl" : name.c_str(),
      L"-E", entry.c_str(), L"-T", profile.c_str(),
      L"-HV", L"2018", L"-Ges", L"-Gis", L"-Zpc"
    };
#ifdef _DEBUG
    arguments.insert(arguments.end(), {L"-Zi", L"-Od", L"-Qembed_debug"});
#else
    arguments.push_back(L"-O3");
#endif
    DxcBuffer sourceBuffer{};
    sourceBuffer.Ptr = source.data();
    sourceBuffer.Size = source.size();
    sourceBuffer.Encoding = DXC_CP_UTF8;
    ComPtr<IDxcResult> result;
    HRESULT hr = state.compiler->Compile(
      &sourceBuffer, arguments.data(), static_cast<UINT32>(arguments.size()),
      nullptr, IID_PPV_ARGS(&result));
    if (FAILED(hr) || !result) {
      diagnostic = "IDxcCompiler3::Compile failed";
      return false;
    }
    ComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    HRESULT status = E_FAIL;
    result->GetStatus(&status);
    if (FAILED(status)) {
      diagnostic = errors && errors->GetStringLength() ? errors->GetStringPointer() : "DXC failed";
      return false;
    }
    ComPtr<IDxcBlob> object;
    ComPtr<IDxcBlob> reflectionData;
    if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr)) || !object ||
        FAILED(result->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&reflectionData), nullptr)) || !reflectionData) {
      diagnostic = "DXC did not produce object/reflection outputs";
      return false;
    }
    std::vector<uint8_t> objectBytes(object->GetBufferSize());
    std::memcpy(objectBytes.data(), object->GetBufferPointer(), objectBytes.size());
    std::vector<uint8_t> reflectionBytes(reflectionData->GetBufferSize());
    std::memcpy(reflectionBytes.data(), reflectionData->GetBufferPointer(), reflectionBytes.size());
    if (!CreateBlobFromBytes(objectBytes, output.bytecode) ||
        !CreateBlobFromBytes(reflectionBytes, output.reflectionData)) {
      diagnostic = "DXC output copy failed";
      return false;
    }
    DxcBuffer reflectionBuffer{};
    reflectionBuffer.Ptr = reflectionData->GetBufferPointer();
    reflectionBuffer.Size = reflectionData->GetBufferSize();
    reflectionBuffer.Encoding = DXC_CP_ACP;
    if (FAILED(state.utils->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(&output.reflection)))) {
      diagnostic = "DXC reflection failed";
      return false;
    }
    return true;
  #else
    diagnostic = "Native DXC is packaged only for x64 and ARM64; use --shaderFlow legacyHLSL";
    return false;
  #endif
  }

  bool RestoreD3D12Shader(const std::vector<uint8_t>& bytecode,
                          const std::vector<uint8_t>& reflectionData,
                          bool legacy,
                          D3D12CompiledShader& output,
                          std::string& diagnostic) {
    output = {};
    output.legacy = legacy;
    if (!CreateBlobFromBytes(bytecode, output.bytecode)) return false;
    if (legacy) {
      if (FAILED(D3DReflect(output.bytecode->GetBufferPointer(), output.bytecode->GetBufferSize(),
                            IID_PPV_ARGS(&output.reflection)))) {
        diagnostic = "Cached FXC reflection failed";
        return false;
      }
      return true;
    }
    if (!CreateBlobFromBytes(reflectionData, output.reflectionData)) return false;
    return CreateDxcReflection(output.reflectionData->GetBufferPointer(),
                               output.reflectionData->GetBufferSize(), output.reflection, diagnostic);
  }

  std::string GetD3D12ShaderCacheDriverSignature(ID3D12Device* device) {
    return BuildD3D12ShaderCacheDriverSignature(device);
  }

  // ══════════════════════════════════════════════════════
  //  D3D12Shader — Root Signature
  // ══════════════════════════════════════════════════════

  bool D3D12Shader::BuildRootSignature(ID3D12Device* device,
                                        ID3D12ShaderReflection* vsReflect,
                                        ID3D12ShaderReflection* fsReflect) {
    // Collect all bound resources from both VS and FS
    struct BoundResource { D3D12_DESCRIPTOR_RANGE_TYPE rangeType; UINT reg; UINT space; std::string name; };
    std::vector<BoundResource> resources;
    auto collectResources = [&](ID3D12ShaderReflection* reflect) {
      D3D12_SHADER_DESC sd; reflect->GetDesc(&sd);
      for (UINT i = 0; i < sd.BoundResources; i++) {
        D3D12_SHADER_INPUT_BIND_DESC bd; reflect->GetResourceBindingDesc(i, &bd);
        D3D12_DESCRIPTOR_RANGE_TYPE rt;
        switch (bd.Type) {
          case D3D_SIT_CBUFFER:    rt = D3D12_DESCRIPTOR_RANGE_TYPE_CBV; break;
          case D3D_SIT_TEXTURE:    rt = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; break;
          case D3D_SIT_SAMPLER:    rt = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER; break;
          case D3D_SIT_STRUCTURED: rt = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; break;
          default: continue;
        }
        // Dedup by register+type
        bool found = false;
        for (auto& r : resources)
          if (r.rangeType == rt && r.reg == bd.BindPoint) { found = true; break; }
        if (!found)
          resources.push_back({ rt, bd.BindPoint, bd.Space, bd.Name });
      }
    };
    collectResources(vsReflect);
    collectResources(fsReflect);

    // Sort: CBV first, then SRV, then SAMPLER
    std::sort(resources.begin(), resources.end(), [](const BoundResource& a, const BoundResource& b) {
      return a.rangeType < b.rangeType || (a.rangeType == b.rangeType && a.reg < b.reg);
    });

    T8_LOG_DEBUG("[D3D12] Root signature: %d resources", (int)resources.size());

    // Build root parameters — CBVs use inline root descriptors (no descriptor table needed),
    // SRVs and Samplers use descriptor tables.
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges(resources.size());
    std::vector<D3D12_ROOT_PARAMETER>   params(resources.size());

    for (int i = 0; i < (int)resources.size(); i++) {
      auto& r = resources[i];

      if (r.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV) {
        // Inline root CBV — binds a GPU VA directly, no descriptor table overhead
        params[i] = {};
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[i].Descriptor.ShaderRegister = r.reg;
        params[i].Descriptor.RegisterSpace  = r.space;
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
      } else {
        // SRV and Sampler — use descriptor tables
        ranges[i] = {};
        ranges[i].RangeType = r.rangeType;
        ranges[i].NumDescriptors = 1;
        ranges[i].BaseShaderRegister = r.reg;
        ranges[i].RegisterSpace = r.space;
        ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        params[i] = {};
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[i].DescriptorTable.NumDescriptorRanges = 1;
        params[i].DescriptorTable.pDescriptorRanges = &ranges[i];
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
      }

      if (r.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV) {
        cbvSlots[(int)r.reg] = i;
        if (r.reg == 0) cbvSlot = i;
      }
      if (r.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV) srvSlots[r.reg] = i;
      if (r.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER) {
        samplerSlots[r.reg] = i;
        if (r.reg == 0) samplerSlot = i;
      }

      T8_LOG_VERBOSE("[D3D12]   RootParam[%d] type=%d reg=%u name='%s'", i, r.rangeType, r.reg, r.name.c_str());
    }

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = (UINT)params.size();
    rsDesc.pParameters = params.data();
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D12] SerializeRootSignature failed: %s",
                   errBlob ? (char*)errBlob->GetBufferPointer() : "unknown");
      return false;
    }

    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
                                      IID_PPV_ARGS(&pRootSignature));
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D12] CreateRootSignature failed hr=0x%08X", hr);
      return false;
    }

    T8_LOG_DEBUG("[D3D12] Root signature created: cbvSlots=%d samplerSlot=%d srvSlots=%d",
                 (int)cbvSlots.size(), samplerSlot, (int)srvSlots.size());
    return true;
  }

  // ══════════════════════════════════════════════════════
  //  D3D12Shader — Compile & Reflect
  // ══════════════════════════════════════════════════════

  bool D3D12Shader::CreateShaderAPI(std::string src_vs, std::string src_fs,
                                     const std::string& vs_name, const std::string& fs_name) {
    ID3D12Device* device = GetNativeDevice();
    cbvSlot = -1;
    samplerSlot = -1;
    cbvSlots.clear();
    srvSlots.clear();
    samplerSlots.clear();
    const bool legacy = UseLegacyD3D12ShaderCompiler();
  #ifdef _DEBUG
    const std::string cacheApi = legacy ? "d3d12-legacy-debug" : "d3d12-debug";
  #else
    const std::string cacheApi = legacy ? "d3d12-legacy" : "d3d12";
  #endif
    const std::string driverSignature = GetD3D12ShaderCacheDriverSignature(device);
    const ShaderDiskCacheKey cacheKey = ShaderDiskCache::MakeKey(cacheApi, driverSignature, key.bits, vs_name, fs_name, src_vs, src_fs);
    const std::string objectExtension = legacy ? ".dxbc" : ".dxil";
    const std::string reflectionExtension = ".reflection";
    ComPtr<ID3D12ShaderReflection> vsReflect;
    ComPtr<ID3D12ShaderReflection> fsReflect;

    // Compile VS
    {
      std::vector<uint8_t> cachedVS, cachedReflection;
      D3D12CompiledShader compiled;
      std::string diagnostic;
      const std::string artifact = "vs" + objectExtension;
      const std::string reflectionArtifact = "vs" + reflectionExtension;
      if (ShaderDiskCache::LoadArtifact(cacheKey, artifact, cachedVS) &&
          (legacy || ShaderDiskCache::LoadArtifact(cacheKey, reflectionArtifact, cachedReflection)) &&
          RestoreD3D12Shader(cachedVS, cachedReflection, legacy, compiled, diagnostic)) {
        T8_TELEMETRY_ADD("shader.cache.hits", 1);
        T8_LOG_DEBUG("[ShaderCache][D3D12] VS hit %s", cacheKey.sha1.c_str());
      }
      else {
        T8_TELEMETRY_ADD("shader.cache.misses", 1);
        if (!T8_TELEMETRY_CALL("shader.compile", CompileD3D12Shader(
              device, src_vs, vs_name, "VS", D3D12ShaderStage::Vertex, compiled, diagnostic))) {
          T8_LOG_ERROR("[D3D12] VS compile error: %s", diagnostic.c_str());
          return false;
        }
        ShaderDiskCache::StoreArtifact(cacheKey, artifact,
          compiled.bytecode->GetBufferPointer(), compiled.bytecode->GetBufferSize());
        if (!legacy) ShaderDiskCache::StoreArtifact(cacheKey, reflectionArtifact,
          compiled.reflectionData->GetBufferPointer(), compiled.reflectionData->GetBufferSize());
        ShaderDiskCache::WriteManifest(cacheKey, driverSignature);
      }
      VS_blob = compiled.bytecode;
      vsReflect = compiled.reflection;
      T8_LOG_VERBOSE("[D3D12] VS compiled: %u bytes profile=%s flow=%s [%s]",
        (unsigned)VS_blob->GetBufferSize(), GetD3D12ShaderProfile(device, D3D12ShaderStage::Vertex).c_str(),
        legacy ? "legacyHLSL" : "dxc", vs_name.c_str());
    }

    // Compile FS
    {
      std::vector<uint8_t> cachedFS, cachedReflection;
      D3D12CompiledShader compiled;
      std::string diagnostic;
      const std::string artifact = "fs" + objectExtension;
      const std::string reflectionArtifact = "fs" + reflectionExtension;
      if (ShaderDiskCache::LoadArtifact(cacheKey, artifact, cachedFS) &&
          (legacy || ShaderDiskCache::LoadArtifact(cacheKey, reflectionArtifact, cachedReflection)) &&
          RestoreD3D12Shader(cachedFS, cachedReflection, legacy, compiled, diagnostic)) {
        T8_TELEMETRY_ADD("shader.cache.hits", 1);
        T8_LOG_DEBUG("[ShaderCache][D3D12] FS hit %s", cacheKey.sha1.c_str());
      }
      else {
        T8_TELEMETRY_ADD("shader.cache.misses", 1);
        if (!T8_TELEMETRY_CALL("shader.compile", CompileD3D12Shader(
              device, src_fs, fs_name, "FS", D3D12ShaderStage::Fragment, compiled, diagnostic))) {
          T8_LOG_ERROR("[D3D12] FS compile error: %s", diagnostic.c_str());
          return false;
        }
        ShaderDiskCache::StoreArtifact(cacheKey, artifact,
          compiled.bytecode->GetBufferPointer(), compiled.bytecode->GetBufferSize());
        if (!legacy) ShaderDiskCache::StoreArtifact(cacheKey, reflectionArtifact,
          compiled.reflectionData->GetBufferPointer(), compiled.reflectionData->GetBufferSize());
        ShaderDiskCache::WriteManifest(cacheKey, driverSignature);
      }
      FS_blob = compiled.bytecode;
      fsReflect = compiled.reflection;
      T8_LOG_VERBOSE("[D3D12] FS compiled: %u bytes profile=%s flow=%s [%s]",
        (unsigned)FS_blob->GetBufferSize(), GetD3D12ShaderProfile(device, D3D12ShaderStage::Fragment).c_str(),
        legacy ? "legacyHLSL" : "dxc", fs_name.c_str());
    }

    // Reflect VS for input layout
    D3D12_SHADER_DESC vsDesc; vsReflect->GetDesc(&vsDesc);

    int offset = 0;
    m_semanticNames.clear();
    VertexDecl.clear();
    // First pass: collect all semantic names (to avoid vector reallocation invalidating pointers)
    for (UINT i = 0; i < vsDesc.InputParameters; i++) {
      D3D12_SIGNATURE_PARAMETER_DESC pd; vsReflect->GetInputParameterDesc(i, &pd);
      m_semanticNames.push_back(pd.SemanticName);
    }
    // Second pass: build input element descs with stable pointers
    for (UINT i = 0; i < vsDesc.InputParameters; i++) {
      D3D12_SIGNATURE_PARAMETER_DESC pd; vsReflect->GetInputParameterDesc(i, &pd);

      D3D12_INPUT_ELEMENT_DESC ie = {};
      ie.SemanticName = m_semanticNames[i].c_str();
      ie.SemanticIndex = pd.SemanticIndex;
      ie.InputSlot = 0;
      ie.AlignedByteOffset = offset;
      ie.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

      if (pd.Mask == 1) {
        ie.Format = (pd.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) ? DXGI_FORMAT_R32_FLOAT
                  : (pd.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  ? DXGI_FORMAT_R32_UINT
                  : DXGI_FORMAT_R32_SINT;
        offset += 4;
      } else if (pd.Mask <= 3) {
        ie.Format = (pd.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) ? DXGI_FORMAT_R32G32_FLOAT
                  : (pd.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  ? DXGI_FORMAT_R32G32_UINT
                  : DXGI_FORMAT_R32G32_SINT;
        offset += 8;
      } else if (pd.Mask <= 7) {
        ie.Format = (pd.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) ? DXGI_FORMAT_R32G32B32_FLOAT
                  : (pd.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  ? DXGI_FORMAT_R32G32B32_UINT
                  : DXGI_FORMAT_R32G32B32_SINT;
        offset += 12;
      } else {
        ie.Format = (pd.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) ? DXGI_FORMAT_R32G32B32A32_FLOAT
                  : (pd.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  ? DXGI_FORMAT_R32G32B32A32_UINT
                  : DXGI_FORMAT_R32G32B32A32_SINT;
        offset += 16;
      }
      VertexDecl.push_back(ie);
    }
    vertexStride = offset;
    T8_LOG_VERBOSE("[D3D12] Input layout: %d elements, stride=%d", (int)VertexDecl.size(), vertexStride);

#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      // Stash the input layout for the tracer keyed by ShaderBase*; the
      // shader hasn't been registered yet (BaseDriver::CreateShader does
      // that after T8Device->CreateShader returns), so we can't use a
      // shader id here.
      std::vector<TraceShaderAttr> attrs;
      attrs.reserve(VertexDecl.size());
      for (size_t i = 0; i < VertexDecl.size(); ++i) {
        const auto& ie = VertexDecl[i];
        TraceShaderAttr a;
        a.semantic   = std::string(ie.SemanticName ? ie.SemanticName : "")
                     + (ie.SemanticIndex > 0 ? std::to_string(ie.SemanticIndex) : std::string());
        a.location   = (int)ie.SemanticIndex;
        a.input_slot = (int)ie.InputSlot;
        a.offset     = ie.AlignedByteOffset;
        switch (ie.Format) {
          case DXGI_FORMAT_R32_FLOAT:           a.format = "R32_FLOAT";          a.size_bytes = 4;  break;
          case DXGI_FORMAT_R32G32_FLOAT:        a.format = "R32G32_FLOAT";       a.size_bytes = 8;  break;
          case DXGI_FORMAT_R32G32B32_FLOAT:     a.format = "R32G32B32_FLOAT";    a.size_bytes = 12; break;
          case DXGI_FORMAT_R32G32B32A32_FLOAT:  a.format = "R32G32B32A32_FLOAT"; a.size_bytes = 16; break;
          case DXGI_FORMAT_R32_UINT:            a.format = "R32_UINT";           a.size_bytes = 4;  break;
          case DXGI_FORMAT_R32G32_UINT:         a.format = "R32G32_UINT";        a.size_bytes = 8;  break;
          case DXGI_FORMAT_R32G32B32_UINT:      a.format = "R32G32B32_UINT";     a.size_bytes = 12; break;
          case DXGI_FORMAT_R32G32B32A32_UINT:   a.format = "R32G32B32A32_UINT";  a.size_bytes = 16; break;
          case DXGI_FORMAT_R32_SINT:            a.format = "R32_SINT";           a.size_bytes = 4;  break;
          case DXGI_FORMAT_R32G32_SINT:         a.format = "R32G32_SINT";        a.size_bytes = 8;  break;
          case DXGI_FORMAT_R32G32B32_SINT:      a.format = "R32G32B32_SINT";     a.size_bytes = 12; break;
          case DXGI_FORMAT_R32G32B32A32_SINT:   a.format = "R32G32B32A32_SINT";  a.size_bytes = 16; break;
          default:                              a.format = "DXGI_FORMAT_" + std::to_string((int)ie.Format); break;
        }
        attrs.push_back(std::move(a));
      }
      g_renderTracer->RegisterShaderInputsForPtr(this, vertexStride, std::move(attrs));
    }
#endif

    // Reflect FS
    const auto validateSamplers = [&](ID3D12ShaderReflection* reflection, const std::string& name, const char* stage) {
      if (!reflection) return false;
      D3D12_SHADER_DESC descriptor{};
      reflection->GetDesc(&descriptor);
      for (UINT index = 0; index < descriptor.BoundResources; ++index) {
        D3D12_SHADER_INPUT_BIND_DESC binding{};
        reflection->GetResourceBindingDesc(index, &binding);
        std::string diagnostic;
        if (!g_pBaseDriver->ValidateShaderComparisonSamplers(
              binding.Type == D3D_SIT_SAMPLER && (binding.uFlags & D3D_SIF_COMPARISON_SAMPLER),
              name.empty() ? "inline" : name, stage, key.bits, diagnostic)) {
          T8_LOG_ERROR("%s", diagnostic.c_str());
          return false;
        }
      }
      return true;
    };
    if (!validateSamplers(vsReflect.Get(), vs_name, "vertex") ||
        !validateSamplers(fsReflect.Get(), fs_name, "fragment")) return false;

    // Build root signature from reflection
    if (!BuildRootSignature(device, vsReflect.Get(), fsReflect.Get())) return false;

#ifdef T8_DUMP_SHADER_REFLECTION
    // Dump D3D12 reflection as reference for validating SPIR-V reflection
    T8_LOG_INFO("[D3D12_REFL] === key=0x%016llX VS='%s' FS='%s' ===", static_cast<unsigned long long>(key.bits), vs_name.c_str(), fs_name.c_str());
    T8_LOG_INFO("[D3D12_REFL] VS Inputs (%u):", vsDesc.InputParameters);
    for (UINT i = 0; i < vsDesc.InputParameters; i++) {
      D3D12_SIGNATURE_PARAMETER_DESC pd; vsReflect->GetInputParameterDesc(i, &pd);
      int components = 0;
      if (pd.Mask == 1) components = 1;
      else if (pd.Mask <= 3) components = 2;
      else if (pd.Mask <= 7) components = 3;
      else components = 4;
      T8_LOG_INFO("[D3D12_REFL]   [%u] %s%u  components=%d  offset=%d",
                  i, pd.SemanticName, pd.SemanticIndex, components,
                  VertexDecl[i].AlignedByteOffset);
    }
    T8_LOG_INFO("[D3D12_REFL] VS stride=%d", vertexStride);

    T8_LOG_INFO("[D3D12_REFL] VS Resources (%u):", vsDesc.BoundResources);
    for (UINT i = 0; i < vsDesc.BoundResources; i++) {
      D3D12_SHADER_INPUT_BIND_DESC bd; vsReflect->GetResourceBindingDesc(i, &bd);
      T8_LOG_INFO("[D3D12_REFL]   [%u] '%s' type=%d bindPoint=%u bindCount=%u space=%u",
                  i, bd.Name, bd.Type, bd.BindPoint, bd.BindCount, bd.Space);
    }

    D3D12_SHADER_DESC fsDesc2; fsReflect->GetDesc(&fsDesc2);
    T8_LOG_INFO("[D3D12_REFL] FS Resources (%u):", fsDesc2.BoundResources);
    for (UINT i = 0; i < fsDesc2.BoundResources; i++) {
      D3D12_SHADER_INPUT_BIND_DESC bd; fsReflect->GetResourceBindingDesc(i, &bd);
      T8_LOG_INFO("[D3D12_REFL]   [%u] '%s' type=%d bindPoint=%u bindCount=%u space=%u",
                  i, bd.Name, bd.Type, bd.BindPoint, bd.BindCount, bd.Space);
    }
#endif

    T8_LOG_INFO("[D3D12] Shader created: key=0x%016llX stride=%d rootParams: cbv=%d sampler=%d srvs=%d",
          static_cast<unsigned long long>(key.bits), vertexStride, cbvSlot, samplerSlot, (int)srvSlots.size());
    return true;
  }

  // ══════════════════════════════════════════════════════
  //  D3D12Shader — Set (bind shader + PSO)
  // ══════════════════════════════════════════════════════

  void D3D12Shader::Set(const DeviceContext& deviceContext) {
    T8_LOG_TRACE("[D3D12] Shader::Set key=0x%016llX", static_cast<unsigned long long>(key.bits));
    const_cast<DeviceContext*>(&deviceContext)->actualShaderSet = (ShaderBase*)this;

    auto* cmdList = static_cast<const D3D12DeviceContext*>(&deviceContext)->GetCommandList();
    auto* driver = GetD3D12Driver();

    ID3D12DescriptorHeap* heaps[] = {
      driver->m_heaps[D3D12Heap::CBV_SRV_UAV_VISIBLE].GetHeap(),
      driver->m_heaps[D3D12Heap::SAMPLER].GetHeap()
    };
    cmdList->SetDescriptorHeaps(2, heaps);

    // Determine current RT configuration for PSO
    uint8_t numRTVs = 1;
    DXGI_FORMAT rtvFormats[8] = {};
    for (auto& fmt : rtvFormats) fmt = DXGI_FORMAT_UNKNOWN;
    rtvFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT dsvFmt = DXGI_FORMAT_D32_FLOAT;

    int curRT = driver->CurrentRT;
    if (curRT >= 0 && curRT < (int)driver->RTs.size()) {
      D3D12RT* rt = static_cast<D3D12RT*>(driver->RTs[curRT]);
      numRTVs = (uint8_t)(rt->number_RT > 0 ? rt->number_RT : 0);
      for (int i = 0; i < rt->number_RT && i < 8; ++i)
        rtvFormats[i] = (i < (int)rt->vColorFormats.size()) ? rt->vColorFormats[i] : rt->colorFormat;
    }

    // Get or create PSO for current state
    ID3D12PipelineState* pso = driver->GetOrCreatePSO(this, numRTVs, rtvFormats, dsvFmt);

    // Skip redundant root signature and PSO binds
    ID3D12RootSignature* rootSig = pRootSignature.Get();
    if (rootSig != driver->m_lastRootSig) {
      cmdList->SetGraphicsRootSignature(rootSig);
      driver->m_lastRootSig = rootSig;
    }
    if (pso && pso != driver->m_lastPSO) {
      cmdList->SetPipelineState(pso);
      driver->m_lastPSO = pso;
    }

    // Bind default sampler as a fallback for draws that never bind a
    // texture sampler. Textured draws rebind the texture's own sampler
    // atomically with its SRV inside Texture::Set/SetVS (which run after
    // this call), so per-texture samplers (e.g. the voxel atlas NEAREST
    // sampler) always win over the aniso default.
    for (const auto& samplerBinding : samplerSlots) {
      cmdList->SetGraphicsRootDescriptorTable(samplerBinding.second, driver->GetDefaultSamplerGPU());
    }
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int shId = g_renderTracer->LookupShaderId(this);
      g_renderTracer->EvBindShader(shId, key.bits);
      g_renderTracer->EvBindPSO((int)(uintptr_t)pso);
    }
#endif
  }

  void D3D12Shader::DestroyAPIShader() {
    VS_blob.Reset(); FS_blob.Reset();
    pRootSignature.Reset();
    VertexDecl.clear(); m_semanticNames.clear();
    srvSlots.clear(); samplerSlots.clear();
  }

} // namespace t850

#endif // OS_WINDOWS
