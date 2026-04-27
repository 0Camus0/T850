#include <pch.h>
/*********************************************************
 * T850 Engine — D3D12 Ray Tracing
 * D3D12RTPipeline.cpp: DXR state object + SBT creation
 *
 * Shaders are compiled via DXC (dxcompiler.dll) loaded
 * dynamically at runtime.  If DXC is not present the
 * pipeline creation fails gracefully and RT is disabled.
 *********************************************************/

#include <video/d3d12/D3D12Driver.h>

#ifdef OS_WINDOWS

#include <utils/Log.h>
#include <fstream>
#include <sstream>
#include <cstring>

// DXC COM interfaces — loaded dynamically so the engine can link
// against d3d12.lib without requiring dxcompiler.lib at link time.
#include <dxcapi.h>
#pragma comment(lib, "dxcompiler.lib")

namespace t850 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  static ID3D12Device5* GetNativeDevice5() {
    return static_cast<D3D12Device*>(T8Device)->GetNativeDevice();
  }
  static D3D12Driver* GetDriver() { return static_cast<D3D12Driver*>(g_pBaseDriver); }

  // ─────────────────────────────────────────────────────
  //  Helper: read a file into a string
  // ─────────────────────────────────────────────────────
  static std::string ReadFile(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
  }

  // ─────────────────────────────────────────────────────
  //  Compile HLSL → DXIL library blob using DXC
  // ─────────────────────────────────────────────────────
  bool D3D12RTPipeline::CompileRTLibrary(const char* hlslPath, ComPtr<ID3DBlob>& outBlob) {
    std::string src = ReadFile(hlslPath);
    if (src.empty()) {
      T8_LOG_ERROR("[D3D12RT] Cannot read shader file: %s", hlslPath);
      return false;
    }

    ComPtr<IDxcUtils> utils;
    ComPtr<IDxcCompiler3> compiler;
    HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D12RT] DxcCreateInstance(Utils) failed hr=0x%08X — is dxcompiler.dll present?", hr);
      return false;
    }
    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D12RT] DxcCreateInstance(Compiler) failed hr=0x%08X", hr);
      return false;
    }

    ComPtr<IDxcBlobEncoding> sourceBlob;
    utils->CreateBlobFromPinned(src.data(), (UINT32)src.size(), CP_UTF8, &sourceBlob);

    // Compile flags for ray tracing library target
    LPCWSTR args[] = {
      L"-T", L"lib_6_3",
      L"-HV", L"2021",
      L"-Zpc",             // column-major matrices (matches HLSL convention)
    };

    DxcBuffer srcBuf;
    srcBuf.Ptr = sourceBlob->GetBufferPointer();
    srcBuf.Size = sourceBlob->GetBufferSize();
    srcBuf.Encoding = CP_UTF8;

    ComPtr<IDxcResult> result;
    hr = compiler->Compile(&srcBuf, args, _countof(args), nullptr, IID_PPV_ARGS(&result));

    ComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    if (errors && errors->GetStringLength() > 0) {
      T8_LOG_ERROR("[D3D12RT] DXC errors for '%s':\n%s", hlslPath, errors->GetStringPointer());
    }

    HRESULT compileStatus;
    result->GetStatus(&compileStatus);
    if (FAILED(compileStatus)) {
      T8_LOG_ERROR("[D3D12RT] DXC compile failed for '%s' hr=0x%08X", hlslPath, compileStatus);
      return false;
    }

    ComPtr<IDxcBlob> dxilBlob;
    result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&dxilBlob), nullptr);
    if (!dxilBlob) {
      T8_LOG_ERROR("[D3D12RT] DXC produced no object blob for '%s'", hlslPath);
      return false;
    }

    // Wrap IDxcBlob into ID3DBlob via CreateBlob helper (copy)
    hr = D3DCreateBlob(dxilBlob->GetBufferSize(), &outBlob);
    if (FAILED(hr)) return false;
    std::memcpy(outBlob->GetBufferPointer(), dxilBlob->GetBufferPointer(), dxilBlob->GetBufferSize());

    T8_LOG_INFO("[D3D12RT] Compiled '%s' (%zu bytes DXIL)", hlslPath, dxilBlob->GetBufferSize());
    return true;
  }

  // ─────────────────────────────────────────────────────
  //  Global root signature: TLAS SRV, output UAV, G-buffer SRVs, CB
  // ─────────────────────────────────────────────────────
  bool D3D12RTPipeline::BuildRootSignature(ID3D12Device5* device) {
    // Descriptor ranges
    D3D12_DESCRIPTOR_RANGE ranges[4] = {};
    // t0: TLAS SRV
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1; ranges[0].BaseShaderRegister = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    // t1-t3: G-buffer SRVs
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[1].NumDescriptors = 3; ranges[1].BaseShaderRegister = 1;
    ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    // u0: output UAV
    ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[2].NumDescriptors = 1; ranges[2].BaseShaderRegister = 0;
    ranges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    // s0: sampler
    ranges[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    ranges[3].NumDescriptors = 1; ranges[3].BaseShaderRegister = 0;
    ranges[3].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[4] = {};
    // TLAS SRV table
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &ranges[0];
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    // G-buffer SRV table
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &ranges[1];
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    // UAV table
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &ranges[2];
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    // Inline CBV (b0)
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[3].Descriptor.ShaderRegister = 0;
    params[3].Descriptor.RegisterSpace  = 0;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 4;
    rsDesc.pParameters   = params;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE; // No IA for RT

    ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D12RT] SerializeRootSignature failed: %s",
                   errBlob ? (char*)errBlob->GetBufferPointer() : "?");
      return false;
    }
    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
                                      IID_PPV_ARGS(&globalRootSig));
    if (FAILED(hr)) { T8_LOG_ERROR("[D3D12RT] CreateRootSignature failed hr=0x%08X", hr); return false; }
    return true;
  }

  // ─────────────────────────────────────────────────────
  //  Build Shader Binding Table
  // ─────────────────────────────────────────────────────
  bool D3D12RTPipeline::BuildSBT(ID3D12StateObjectProperties* props) {
    ID3D12Device5* device = GetNativeDevice5();

    // SBT layout: [rayGen | miss | hitGroup]
    // Each record = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES (32) padded to 64 bytes
    constexpr UINT kRecordSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES; // 32 bytes
    constexpr UINT kAligned    = 64; // round up to 64-byte alignment
    UINT bufferSize = kAligned * 3;

    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = bufferSize; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                   D3D12_RESOURCE_STATE_GENERIC_READ,
                                                   nullptr, IID_PPV_ARGS(&sbt.buffer));
    if (FAILED(hr)) { T8_LOG_ERROR("[D3D12RT] SBT buffer alloc failed hr=0x%08X", hr); return false; }
    sbt.buffer->Map(0, nullptr, &sbt.mapped);

    auto copyIdentifier = [&](void* dest, const wchar_t* name) {
      void* id = props->GetShaderIdentifier(name);
      if (!id) { T8_LOG_ERROR("[D3D12RT] Shader identifier '%ls' not found", name); return false; }
      std::memcpy(dest, id, kRecordSize);
      return true;
    };

    char* p = static_cast<char*>(sbt.mapped);
    if (!copyIdentifier(p,           L"RayGenShader"))   return false;
    if (!copyIdentifier(p + kAligned, L"MissShader"))     return false;
    if (!copyIdentifier(p + kAligned * 2, L"HitGroup"))  return false;

    D3D12_GPU_VIRTUAL_ADDRESS base = sbt.buffer->GetGPUVirtualAddress();
    sbt.rayGenVA   = base;
    sbt.missVA     = base + kAligned;
    sbt.hitGroupVA = base + kAligned * 2;
    sbt.stride     = kAligned;
    return true;
  }

  // ─────────────────────────────────────────────────────
  //  Create the DXR state object
  // ─────────────────────────────────────────────────────
  bool D3D12RTPipeline::Create(const char* raygenSrc, const char* missSrc, const char* closestHitSrc) {
    ID3D12Device5* device = GetNativeDevice5();
    if (!device) { T8_LOG_ERROR("[D3D12RT] No ID3D12Device5 — RT not supported"); return false; }

    // Compile shader libraries
    ComPtr<ID3DBlob> raygenBlob, missBlob, hitBlob;
    if (!CompileRTLibrary(raygenSrc,     raygenBlob))   return false;
    if (!CompileRTLibrary(missSrc,       missBlob))     return false;
    if (!CompileRTLibrary(closestHitSrc, hitBlob))      return false;

    if (!BuildRootSignature(device)) return false;

    // Assemble state object subobjects
    std::vector<D3D12_STATE_SUBOBJECT> subobjects;
    subobjects.reserve(16);

    // 1. DXIL library — raygen
    D3D12_DXIL_LIBRARY_DESC raygenLib = {};
    raygenLib.DXILLibrary = { raygenBlob->GetBufferPointer(), raygenBlob->GetBufferSize() };
    D3D12_EXPORT_DESC raygenExport = { L"RayGenShader", nullptr, D3D12_EXPORT_FLAG_NONE };
    raygenLib.NumExports = 1; raygenLib.pExports = &raygenExport;
    subobjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &raygenLib });

    // 2. DXIL library — miss
    D3D12_DXIL_LIBRARY_DESC missLib = {};
    missLib.DXILLibrary = { missBlob->GetBufferPointer(), missBlob->GetBufferSize() };
    D3D12_EXPORT_DESC missExport = { L"MissShader", nullptr, D3D12_EXPORT_FLAG_NONE };
    missLib.NumExports = 1; missLib.pExports = &missExport;
    subobjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &missLib });

    // 3. DXIL library — closest hit
    D3D12_DXIL_LIBRARY_DESC hitLib = {};
    hitLib.DXILLibrary = { hitBlob->GetBufferPointer(), hitBlob->GetBufferSize() };
    D3D12_EXPORT_DESC hitExport = { L"ClosestHitShader", nullptr, D3D12_EXPORT_FLAG_NONE };
    hitLib.NumExports = 1; hitLib.pExports = &hitExport;
    subobjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &hitLib });

    // 4. Hit group
    D3D12_HIT_GROUP_DESC hitGroup = {};
    hitGroup.HitGroupExport     = L"HitGroup";
    hitGroup.Type               = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hitGroup.ClosestHitShaderImport = L"ClosestHitShader";
    subobjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hitGroup });

    // 5. Shader config (payload size)
    D3D12_RAYTRACING_SHADER_CONFIG shaderCfg = {};
    shaderCfg.MaxPayloadSizeInBytes   = 32; // enough for reflection payload (float3 + float)
    shaderCfg.MaxAttributeSizeInBytes = D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;
    subobjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shaderCfg });

    // 6. Pipeline config (recursion depth)
    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineCfg = {};
    pipelineCfg.MaxTraceRecursionDepth = 2;
    subobjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipelineCfg });

    // 7. Global root signature
    D3D12_GLOBAL_ROOT_SIGNATURE globalRS = {};
    globalRS.pGlobalRootSignature = globalRootSig.Get();
    subobjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &globalRS });

    D3D12_STATE_OBJECT_DESC soDesc = {};
    soDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    soDesc.NumSubobjects = (UINT)subobjects.size();
    soDesc.pSubobjects   = subobjects.data();

    HRESULT hr = device->CreateStateObject(&soDesc, IID_PPV_ARGS(&stateObject));
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D12RT] CreateStateObject failed hr=0x%08X", hr);
      return false;
    }

    ComPtr<ID3D12StateObjectProperties> props;
    stateObject->QueryInterface(IID_PPV_ARGS(&props));
    if (!BuildSBT(props.Get())) return false;

    valid = true;
    T8_LOG_INFO("[D3D12RT] RT pipeline created: raygen='%s'", raygenSrc);
    return true;
  }

  void D3D12RTPipeline::Destroy() {
    if (sbt.mapped && sbt.buffer) {
      sbt.buffer->Unmap(0, nullptr);
      sbt.mapped = nullptr;
    }
    sbt.buffer.Reset();
    stateObject.Reset();
    globalRootSig.Reset();
    valid = false;
  }

} // namespace t850

#endif // OS_WINDOWS
