#include <video/webgpu/WebGPUShaderCompiler.h>
#include <utils/ResourceLocator.h>
#include <utils/ShaderDiskCache.h>
#include <glaze/glaze.hpp>
#include <webgpu/webgpu_cpp.h>
#include <dawn/native/D3DBackend.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include "ShaderNumerics.h"

#include <atomic>
#include <algorithm>
#include <bit>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct ProbeCacheRecord {
  uint32_t version = 1;
  std::string sourceKey;
  std::string checksum;
  std::string wgsl;
};

struct RecordedPermutation {
  std::string vertexShader;
  std::string fragmentShader;
  std::vector<std::string> defines;
};
struct RecordedInventory {
  std::map<std::string, RecordedPermutation> permutations;
};

namespace {
using namespace t850::webgpu;
std::atomic<unsigned> hardwareErrors{0};

void Require(bool value, const std::string& message) {
  if (!value) throw std::runtime_error(message);
}

std::string Message(wgpu::StringView text) {
  if (!text.data) return {};
  return text.length == WGPU_STRLEN ? std::string(text.data) : std::string(text.data, text.length);
}

void Wait(const wgpu::Instance& instance, wgpu::Future future) {
  Require(instance.WaitAny(future, 30'000'000'000ULL) == wgpu::WaitStatus::Success, "Dawn callback timed out");
}

void CheckNativeBlur(const ShaderRequest& request) {
  Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
  Microsoft::WRL::ComPtr<ID3DBlob> errors;
  const auto result = D3DCompile(request.source.data(), request.source.size(), request.name.c_str(), nullptr, nullptr,
    "CS", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &bytecode, &errors);
  Require(SUCCEEDED(result), errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "SM5 blur compile failed");
  Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
  Require(SUCCEEDED(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(&reflection))), "SM5 reflection failed");
  auto* constants = reflection->GetConstantBufferByName("BlurConstants");
  D3D11_SHADER_BUFFER_DESC buffer{};
  Require(SUCCEEDED(constants->GetDesc(&buffer)) && buffer.Size == 16, "SM5 blur constants size mismatch");
  uint32_t offset = 0;
  for (const char* name : {"width", "height", "directionX", "directionY"}) {
    D3D11_SHADER_VARIABLE_DESC variable{};
    Require(SUCCEEDED(constants->GetVariableByName(name)->GetDesc(&variable)) && variable.StartOffset == offset && variable.Size == 4,
      "SM5 blur member layout mismatch");
    offset += 4;
  }
  std::cout << "Native SM5 blur PASS: constants size=16 offsets=0,4,8,12\n";
}

void CheckHardware(const std::map<std::string, ShaderArtifact>& artifacts, bool numerics = false) {
  wgpu::InstanceFeatureName feature = wgpu::InstanceFeatureName::TimedWaitAny;
  wgpu::InstanceDescriptor instanceDesc{};
  instanceDesc.requiredFeatureCount = 1;
  instanceDesc.requiredFeatures = &feature;
  auto instance = wgpu::CreateInstance(&instanceDesc);
  Require(static_cast<bool>(instance), "Dawn instance creation failed");
  auto adapter = std::make_shared<wgpu::Adapter>();
  wgpu::RequestAdapterOptions options{};
  options.backendType = wgpu::BackendType::D3D12;
  options.featureLevel = wgpu::FeatureLevel::Core;
  options.powerPreference = wgpu::PowerPreference::HighPerformance;
  Wait(instance, instance.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly,
    [adapter](wgpu::RequestAdapterStatus status, wgpu::Adapter result, wgpu::StringView message) {
      if (status == wgpu::RequestAdapterStatus::Success) *adapter = std::move(result);
      else std::cerr << Message(message) << '\n';
    }));
  Require(static_cast<bool>(*adapter), "Hardware adapter request failed");
  wgpu::AdapterInfo info{};
  Require(adapter->GetInfo(&info) == wgpu::Status::Success && info.backendType == wgpu::BackendType::D3D12
    && info.adapterType != wgpu::AdapterType::CPU, "Hardware D3D12 required; software fallback rejected");
  std::cout << "Shader pipelines: provider=Dawn backend=D3D12 adapter=" << Message(info.device) << '\n';
  hardwareErrors = 0;
  auto device = std::make_shared<wgpu::Device>();
  wgpu::DeviceDescriptor deviceDesc{};
  deviceDesc.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message) {
    ++hardwareErrors;
    std::cerr << "Dawn error: " << Message(message) << '\n';
  });
  deviceDesc.SetDeviceLostCallback(wgpu::CallbackMode::AllowProcessEvents,
    [](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message) {
      if (reason != wgpu::DeviceLostReason::Destroyed && reason != wgpu::DeviceLostReason::CallbackCancelled) {
        ++hardwareErrors;
        std::cerr << "Device lost: " << Message(message) << '\n';
      }
    });
  Wait(instance, adapter->RequestDevice(&deviceDesc, wgpu::CallbackMode::WaitAnyOnly,
    [device](wgpu::RequestDeviceStatus status, wgpu::Device result, wgpu::StringView message) {
      if (status == wgpu::RequestDeviceStatus::Success) *device = std::move(result);
      else std::cerr << Message(message) << '\n';
    }));
  Require(static_cast<bool>(*device), "Dawn device request failed");
  {
    std::map<std::string, wgpu::ShaderModule> modules;
    for (const auto& [name, artifact] : artifacts) {
      wgpu::ShaderSourceWGSL wgsl{};
      wgsl.code = artifact.wgsl.c_str();
      wgpu::ShaderModuleDescriptor descriptor{};
      descriptor.label = name.c_str();
      descriptor.nextInChain = &wgsl;
      modules.emplace(name, device->CreateShaderModule(&descriptor));
    }
    for (const std::string family : {"tri", "Text"}) {
      const bool text = family == "Text";
      wgpu::VertexAttribute attributes[2]{};
      attributes[0].format = text ? wgpu::VertexFormat::Float32x4 : wgpu::VertexFormat::Float32x3;
      attributes[0].shaderLocation = 0;
      attributes[1].format = text ? wgpu::VertexFormat::Float32x2 : wgpu::VertexFormat::Float32x3;
      attributes[1].offset = text ? 16 : 12;
      attributes[1].shaderLocation = 1;
      wgpu::VertexBufferLayout buffer{};
      buffer.arrayStride = 24;
      buffer.attributeCount = 2;
      buffer.attributes = attributes;
      wgpu::ColorTargetState color{};
      color.format = wgpu::TextureFormat::RGBA8Unorm;
      wgpu::FragmentState fragment{};
      fragment.module = modules.at("FS_" + family + ".hlsl");
      fragment.entryPoint = "FS";
      fragment.targetCount = 1;
      fragment.targets = &color;
      wgpu::RenderPipelineDescriptor descriptor{};
      descriptor.vertex.module = modules.at("VS_" + family + ".hlsl");
      descriptor.vertex.entryPoint = "VS";
      descriptor.vertex.bufferCount = 1;
      descriptor.vertex.buffers = &buffer;
      descriptor.fragment = &fragment;
      auto accepted = std::make_shared<bool>(false);
      Wait(instance, device->CreateRenderPipelineAsync(&descriptor, wgpu::CallbackMode::WaitAnyOnly,
        [accepted](wgpu::CreatePipelineAsyncStatus status, wgpu::RenderPipeline pipeline, wgpu::StringView message) {
          *accepted = status == wgpu::CreatePipelineAsyncStatus::Success && static_cast<bool>(pipeline);
          if (!*accepted) std::cerr << Message(message) << '\n';
        }));
      Require(*accepted, family + " hardware graphics pipeline rejected");
      std::cout << family << " hardware graphics pipeline PASS\n";
    }
    wgpu::ComputePipelineDescriptor descriptor{};
    descriptor.compute.module = modules.at("CS_SeparableBlur.hlsl");
    descriptor.compute.entryPoint = "CS";
    auto accepted = std::make_shared<bool>(false);
    Wait(instance, device->CreateComputePipelineAsync(&descriptor, wgpu::CallbackMode::WaitAnyOnly,
      [accepted](wgpu::CreatePipelineAsyncStatus status, wgpu::ComputePipeline pipeline, wgpu::StringView message) {
        *accepted = status == wgpu::CreatePipelineAsyncStatus::Success && static_cast<bool>(pipeline);
        if (!*accepted) std::cerr << Message(message) << '\n';
      }));
    Require(*accepted, "Blur hardware compute pipeline rejected");
    std::cout << "Blur hardware compute pipeline PASS\n";
    if (numerics) {
      auto nativeAdapter = dawn::native::d3d::GetDXGIAdapter(adapter->Get());
      Require(static_cast<bool>(nativeAdapter), "Dawn DXGI adapter unavailable");
      DXGI_ADAPTER_DESC nativeInfo{};
      Require(SUCCEEDED(nativeAdapter->GetDesc(&nativeInfo)), "Dawn DXGI adapter description failed");
      std::cout << "Differential adapter LUID=" << nativeInfo.AdapterLuid.HighPart << ':' << nativeInfo.AdapterLuid.LowPart << '\n';
      CheckBlurNumerics(instance, *device, nativeAdapter.Get(), modules.at("CS_SeparableBlur.hlsl"));
      auto changed = artifacts.at("CS_SeparableBlur.hlsl").wgsl;
      const auto divisor = changed.find("/ 16.0");
      Require(divisor != std::string::npos, "Missing blur mutation site");
      changed.replace(divisor, 6, "/ 15.0");
      wgpu::ShaderSourceWGSL mutation{};
      mutation.code = changed.c_str();
      wgpu::ShaderModuleDescriptor mutationDesc{};
      mutationDesc.nextInChain = &mutation;
      auto mutatedModule = device->CreateShaderModule(&mutationDesc);
      bool detected = false;
      try { CheckBlurNumerics(instance, *device, nativeAdapter.Get(), mutatedModule); }
      catch (const std::runtime_error& error) {
        detected = std::string(error.what()).starts_with("Blur mismatch:");
        if (!detected) throw;
      }
      Require(detected, "Numeric tests failed to detect a changed blur divisor");
      std::cout << "Blur mutation detection PASS\n";
      CheckShaderFunctionNumerics(instance, *device, nativeAdapter.Get());
    }
  }
  device->Destroy();
  instance.ProcessEvents();
  Require(hardwareErrors.load() == 0, "Dawn reported validation errors or device loss");
}

t850::ShaderDiskCacheKey CacheKey(const ShaderRequest& request, const std::string& specialization = {}) {
  const auto signature = CompilerSignature() + ";layout=" + std::to_string(static_cast<int>(request.layout))
    + ";language=" + std::to_string(static_cast<int>(request.sourceLanguage))
    + ";specialization=" + specialization;
  const char* stage = request.stage == ShaderStage::Vertex ? "vertex" : request.stage == ShaderStage::Fragment ? "fragment" : "compute";
  return t850::ShaderDiskCache::MakeStageKey("webgpu", signature, request.keyBits, stage, request.entryPoint,
    request.name, request.defines + "\n" + request.source);
}

ShaderArtifact Compile(const ShaderRequest& request, bool expectedHit, const std::string& specialization = {}) {
  ShaderArtifact artifact;
  std::string diagnostic;
  Require(LoadOrTranslateShader(request, artifact, diagnostic, specialization), diagnostic);
  Require(artifact.cacheHit == expectedHit, request.name + ": unexpected cache state");
  Require(expectedHit ? artifact.translationMilliseconds == 0 : artifact.cacheStored, "Cache did not skip translation or store its result");
  return artifact;
}

void CheckNativeContract(const ShaderRequest& request, const ShaderArtifact& wgsl) {
  Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
  Microsoft::WRL::ComPtr<ID3DBlob> errors;
  const std::string source = request.defines + "\n" + request.source;
  const char* profile = request.stage == ShaderStage::Vertex ? "vs_5_0" : request.stage == ShaderStage::Fragment ? "ps_5_0" : "cs_5_0";
  const auto status = D3DCompile(source.data(), source.size(), request.name.c_str(), nullptr, nullptr,
    request.entryPoint.c_str(), profile, D3DCOMPILE_ENABLE_STRICTNESS, 0, &bytecode, &errors);
  Require(SUCCEEDED(status), errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "Native contract compile failed");
  Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
  Require(SUCCEEDED(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(&reflection))), "Native contract reflection failed");
  D3D11_SHADER_DESC desc{};
  Require(SUCCEEDED(reflection->GetDesc(&desc)), "Native shader description failed");
  for (UINT index = 0; index < desc.BoundResources; ++index) {
    D3D11_SHADER_INPUT_BIND_DESC binding{};
    Require(SUCCEEDED(reflection->GetResourceBindingDesc(index, &binding)), "Native resource reflection failed");
    if (binding.Type != D3D_SIT_CBUFFER) {
      const bool sampler = binding.Type == D3D_SIT_SAMPLER;
      const bool storage = binding.Type == D3D_SIT_UAV_RWTYPED;
      Require(sampler || storage || binding.Type == D3D_SIT_TEXTURE, "Unsupported native resource in parity test");
      const uint32_t slot = binding.BindPoint + (sampler ? 32 : request.stage == ShaderStage::Compute ? (storage ? 2 : 1) : 0);
      const auto kind = sampler ? ResourceKind::Sampler : storage ? ResourceKind::WriteOnlyStorageTexture : ResourceKind::SampledTexture;
      const auto actual = std::find_if(wgsl.bindings.begin(), wgsl.bindings.end(), [slot, kind](const ShaderBinding& value) {
        return value.group == 0 && value.binding == slot && value.kind == kind;
      });
      Require(actual != wgsl.bindings.end(), request.name + ": native/WGSL resource binding mismatch: " + binding.Name);
      if (!sampler) {
        TextureDimension dimension = TextureDimension::None;
        switch (binding.Dimension) {
        case D3D_SRV_DIMENSION_TEXTURE1D: dimension = TextureDimension::D1; break;
        case D3D_SRV_DIMENSION_TEXTURE2D: dimension = TextureDimension::D2; break;
        case D3D_SRV_DIMENSION_TEXTURE2DARRAY: dimension = TextureDimension::D2Array; break;
        case D3D_SRV_DIMENSION_TEXTURE3D: dimension = TextureDimension::D3; break;
        case D3D_SRV_DIMENSION_TEXTURECUBE: dimension = TextureDimension::Cube; break;
        case D3D_SRV_DIMENSION_TEXTURECUBEARRAY: dimension = TextureDimension::CubeArray; break;
        default: throw std::runtime_error("Unsupported native texture dimension");
        }
        Require(actual->dimension == dimension, request.name + ": native/WGSL texture dimension mismatch: " + binding.Name);
        const auto sampledType = binding.ReturnType == D3D_RETURN_TYPE_UINT ? ShaderComponentType::Unsigned
          : binding.ReturnType == D3D_RETURN_TYPE_SINT ? ShaderComponentType::Signed : ShaderComponentType::Float;
        Require(actual->sampledType == sampledType, request.name + ": native/WGSL texture scalar type mismatch");
      } else {
        Require(actual->comparisonSampler == ((binding.uFlags & D3D_SIF_COMPARISON_SAMPLER) != 0), "Sampler comparison mode mismatch");
      }
      continue;
    }
    const uint32_t slot = binding.BindPoint + (request.stage == ShaderStage::Compute ? 0 : 64);
    const auto actual = std::find_if(wgsl.bindings.begin(), wgsl.bindings.end(),
      [slot](const ShaderBinding& value) { return value.group == 0 && value.binding == slot && value.kind == ResourceKind::UniformBuffer; });
    Require(actual != wgsl.bindings.end(), request.name + ": missing WGSL uniform buffer");
    auto* buffer = reflection->GetConstantBufferByName(binding.Name);
    D3D11_SHADER_BUFFER_DESC bufferDesc{};
    Require(SUCCEEDED(buffer->GetDesc(&bufferDesc)) && bufferDesc.Size == actual->minimumBufferSize, request.name + ": native/WGSL buffer size mismatch");
    std::vector<uint32_t> offsets;
    for (UINT member = 0; member < bufferDesc.Variables; ++member) {
      D3D11_SHADER_VARIABLE_DESC variable{};
      Require(SUCCEEDED(buffer->GetVariableByIndex(member)->GetDesc(&variable)), "Native member reflection failed");
      offsets.push_back(variable.StartOffset);
    }
    Require(offsets == actual->uniformMemberOffsets, request.name + ": native/WGSL uniform offsets mismatch");
  }
  if (request.stage == ShaderStage::Fragment) {
    std::vector<ShaderLocation> targets;
    bool writesDepth = false;
    for (UINT index = 0; index < desc.OutputParameters; ++index) {
      D3D11_SIGNATURE_PARAMETER_DESC output{};
      Require(SUCCEEDED(reflection->GetOutputParameterDesc(index, &output)), "Native fragment output reflection failed");
      if (output.SystemValueType == D3D_NAME_DEPTH) { writesDepth = true; continue; }
      Require(output.SystemValueType == D3D_NAME_TARGET, "Unsupported fragment output semantic");
      const auto type = output.ComponentType == D3D_REGISTER_COMPONENT_UINT32 ? ShaderComponentType::Unsigned
        : output.ComponentType == D3D_REGISTER_COMPONENT_SINT32 ? ShaderComponentType::Signed : ShaderComponentType::Float;
      targets.push_back({output.SemanticIndex, static_cast<uint32_t>(std::popcount(static_cast<unsigned>(output.Mask))), type});
    }
    std::sort(targets.begin(), targets.end(), [](const auto& left, const auto& right) { return left.location < right.location; });
    Require(targets == wgsl.outputs && writesDepth == wgsl.writesDepth, request.name + ": native/WGSL fragment targets mismatch");
  }
}

void CheckVertexPermutations() {
  const char* flags[]{"USE_NORMALS", "USE_TANGENTS", "USE_BINORMALS", "USE_TEXCOORD0", "USE_TEXCOORD1", "USE_TEXCOORD2", "USE_TEXCOORD3"};
  unsigned count = 0;
  for (const std::string family : {"VS_Mesh", "VS_W"}) {
    ShaderRequest native;
    native.name = family + ".hlsl";
    native.entryPoint = "VS";
    Require(t850::ResourceLocator::Instance().ReadText(native.name, native.source), "Missing vertex source");
    ShaderRequest direct = native;
    direct.name = family + ".wgsl";
    direct.sourceLanguage = ShaderSourceLanguage::Wgsl;
    Require(t850::ResourceLocator::Instance().ReadText(direct.name, direct.source), "Missing vertex WGSL");
    for (const std::string skin : {"", "USE_SKINNING", "USE_SKINNING_QT", "USE_SKINNING_TEXTURE"}) {
      if (family == "VS_W" && skin == "USE_SKINNING_TEXTURE") continue;
      for (unsigned variant = 0; variant < (family == "VS_Mesh" ? 256u : 1u); ++variant) {
        native.defines = skin.empty() ? "" : "#define " + skin + "\n";
        for (unsigned bit = 0; bit < 7; ++bit) if (variant & (1u << bit)) native.defines += std::string("#define ") + flags[bit] + "\n";
        if (variant & 128u) native.defines += "#define SHADOW_MAP_PASS\n";
        direct.defines = native.defines;
        ShaderArtifact expected;
        ShaderArtifact actual;
        std::string diagnostic;
        Require(TranslateShader(native, expected, diagnostic), diagnostic);
        Require(TranslateShader(direct, actual, diagnostic), diagnostic);
        const auto order = [](const ShaderBinding& left, const ShaderBinding& right) { return left.binding < right.binding; };
        std::sort(expected.bindings.begin(), expected.bindings.end(), order);
        std::sort(actual.bindings.begin(), actual.bindings.end(), order);
        Require(expected.bindings == actual.bindings, family + ": vertex permutation binding mismatch\n" + native.defines);
        Require(expected.inputs == actual.inputs && expected.outputs == actual.outputs, family + ": vertex location/type mismatch\n" + native.defines);
        CheckNativeContract(native, actual);
        ++count;
      }
    }
  }
  std::cout << "Vertex permutation contracts PASS: " << count << " variants (native HLSL + translated HLSL + direct WGSL)\n";
}

void CheckRecordedPermutations() {
  std::string json;
  Require(t850::ResourceLocator::Instance().ReadText("shader_permutations.json", json), "Missing permutation corpus");
  RecordedInventory inventory;
  Require(!glz::read<glz::opts{.error_on_unknown_keys = false}>(inventory, json), "Invalid permutation corpus");
  inventory.permutations.emplace("0xF000000000000001", RecordedPermutation{"VS_Quad.hlsl", "FS_Quad.hlsl", {"CASCADE_DEBUG_PASS"}});
  inventory.permutations.emplace("0xF000000000000002", RecordedPermutation{"VS_Quad.hlsl", "FS_Quad.hlsl", {"DEPTH_PRE_PASS"}});
  inventory.permutations.emplace("0xF000000000000003", RecordedPermutation{"VS_Mesh.hlsl", "FS_Mesh.hlsl", {"SIMPLE_COLOR"}});
  uint64_t environmentKey = 0xF000000000000004ull;
  for (const std::string pass : {"DEFERRED_PASS", "DEFERRED_LDR_PASS", "DEFERRED_LIGHT_VOLUME_PASS"}) {
    for (unsigned toggles = 0; toggles < 4; ++toggles) {
      RecordedPermutation permutation{"VS_Quad.hlsl", "FS_Quad.hlsl", {pass, "USE_TEXCOORD0", "NO_ENVIRONMENT"}};
      if (toggles & 1) permutation.defines.push_back("ENABLE_SHADOWS");
      if (toggles & 2) permutation.defines.push_back("ENABLE_SSAO");
      std::ostringstream key;
      key << std::hex << environmentKey++;
      inventory.permutations.emplace(key.str(), std::move(permutation));
    }
  }
  unsigned count = 0;
  for (const auto& [key, permutation] : inventory.permutations) {
    ShaderArtifact vertex;
    ShaderArtifact translatedVertex;
    for (const std::string name : {permutation.vertexShader, permutation.fragmentShader}) {
      ShaderRequest native;
      native.name = name;
      native.stage = name.starts_with("VS") ? ShaderStage::Vertex : ShaderStage::Fragment;
      native.entryPoint = native.stage == ShaderStage::Vertex ? "VS" : "FS";
      native.keyBits = std::stoull(key, nullptr, 16);
      for (const auto& define : permutation.defines) native.defines += "#define " + define + "\n";
      Require(t850::ResourceLocator::Instance().ReadText(name, native.source), "Missing corpus HLSL");
      ShaderRequest direct = native;
      direct.name = std::filesystem::path(name).replace_extension(".wgsl").string();
      direct.sourceLanguage = ShaderSourceLanguage::Wgsl;
      Require(t850::ResourceLocator::Instance().ReadText(direct.name, direct.source), "Missing corpus WGSL: " + direct.name);
      ShaderArtifact artifact;
      ShaderArtifact translated;
      std::string diagnostic;
      Require(TranslateShader(native, translated, diagnostic), diagnostic);
      Require(TranslateShader(direct, artifact, diagnostic), diagnostic);
      if (native.defines.find("#define NO_ENVIRONMENT\n") != std::string::npos) {
        for (const auto* result : {&translated, &artifact}) {
          for (const auto& binding : result->bindings) {
            const auto slot = binding.kind == ResourceKind::Sampler ? binding.binding - 32 : binding.binding;
            Require(slot != 6 && (slot < 10 || slot > 15), "No-environment variant retained an environment resource");
          }
        }
      }
      CheckNativeContract(native, translated);
      if (native.stage == ShaderStage::Vertex) translatedVertex = translated;
      else for (const auto& input : translated.inputs) {
        Require(std::find(translatedVertex.outputs.begin(), translatedVertex.outputs.end(), input) != translatedVertex.outputs.end(),
          key + ": translated vertex/fragment location type mismatch");
      }
      if (native.stage == ShaderStage::Vertex) vertex = artifact;
      else for (const auto& input : artifact.inputs) {
        Require(std::find(vertex.outputs.begin(), vertex.outputs.end(), input) != vertex.outputs.end(),
          key + ": vertex/fragment location type mismatch");
      }
      try { CheckNativeContract(native, artifact); }
      catch (const std::exception& error) { throw std::runtime_error(key + " " + native.defines + error.what()); }
      ++count;
    }
  }
  std::cout << "Recorded permutation contracts PASS: stages=" << count << " (native HLSL + strict translated HLSL + strict direct WGSL)\n";
}

void CheckCache(const ShaderRequest& request, const ShaderArtifact& cold) {
  auto warm = Compile(request, true);
  Require(warm.wgsl == cold.wgsl, "Cold/warm WGSL mismatch");
  const auto key = CacheKey(request);
  std::vector<uint8_t> bytes;
  Require(t850::ShaderDiskCache::LoadArtifact(key, "stage.wgsl.json", bytes), "Missing cache record");
  ProbeCacheRecord record;
  Require(!glz::read_json(record, std::string(bytes.begin(), bytes.end())), "Invalid stored cache JSON");
  for (const std::string fault : {"malformed", "checksum", "stale", "invalid-wgsl"}) {
    auto broken = record;
    if (fault == "checksum") broken.wgsl += "\n";
    if (fault == "stale") broken.sourceKey = "wrong-source-key";
    if (fault == "invalid-wgsl") {
      broken.wgsl = "this is not WGSL";
      broken.checksum = t850::ShaderDiskCache::ContentHash(broken.wgsl);
    }
    std::string json;
    Require(!glz::write_json(broken, json), "Cannot encode test cache record");
    if (fault == "malformed") json = "{";
    Require(t850::ShaderDiskCache::StoreArtifact(key, "stage.wgsl.json", json.data(), json.size()), "Cannot inject cache fault");
    Require(Compile(request, false).wgsl == cold.wgsl, "Cache repair changed WGSL");
  }
  auto changed = request;
  changed.source += "\n";
  Compile(changed, false);
  changed = request;
  changed.defines = "#define CACHE_TEST 1\n";
  Compile(changed, false);
  changed = request;
  changed.name += ".other-family";
  Compile(changed, false);
  changed = request;
  changed.keyBits ^= 1;
  Compile(changed, false);
  Compile(request, false, "different-tool-or-feature-signature");
  Require(Compile(request, true).wgsl == cold.wgsl, "Signature change damaged original cache");
  changed = request;
  changed.stage = request.stage == ShaderStage::Vertex ? ShaderStage::Fragment : ShaderStage::Vertex;
  Require(CacheKey(changed).sha1 != key.sha1, "Stage identity collision");
  changed = request;
  changed.entryPoint += "_different";
  Require(CacheKey(changed).sha1 != key.sha1, "Entry-point identity collision");
  changed = request;
  changed.source = "invalid HLSL";
  ShaderArtifact failed;
  std::string diagnostic;
  Require(!LoadOrTranslateShader(changed, failed, diagnostic), "Invalid source unexpectedly compiled");
  Require(diagnostic.find(request.name) != std::string::npos && diagnostic.find(request.entryPoint) != std::string::npos
    && diagnostic.find("key=0x") != std::string::npos, "Incomplete shader diagnostic");
}

constexpr const char* shaderFiles[]{"VS_tri.hlsl", "FS_tri.hlsl", "VS_Text.hlsl", "FS_Text.hlsl", "CS_SeparableBlur.hlsl",
  "VS.hlsl", "FS.hlsl", "VS_W.hlsl", "FS_W.hlsl", "VS_EditorLine.hlsl", "FS_EditorLine.hlsl", "FS_LineFlat.hlsl",
  "FS_WireMesh.hlsl", "VS_Quad.hlsl", "FS_Quad.hlsl", "VS_Mesh.hlsl", "FS_Mesh.hlsl"};

bool RunSelectedFlow(ShaderFlow flow, const std::string& selectedFile) {
  std::vector<std::string> files;
  if (selectedFile.empty()) files.assign(std::begin(shaderFiles), std::end(shaderFiles));
  else files.push_back(selectedFile);
  bool allSucceeded = true;
  for (const auto& name : files) {
    const auto filename = std::filesystem::path(name).filename().string();
    Require(filename.starts_with("VS") || filename.starts_with("FS") || filename.starts_with("CS"), "Expected VS, FS or CS shader file prefix");
    ShaderFileRequest request;
    request.name = name;
    request.flow = flow;
    request.stage = filename.starts_with("VS") ? ShaderStage::Vertex : filename.starts_with("FS") ? ShaderStage::Fragment : ShaderStage::Compute;
    request.entryPoint = request.stage == ShaderStage::Vertex ? "VS" : request.stage == ShaderStage::Fragment ? "FS" : "CS";
    if (request.stage == ShaderStage::Compute) request.layout = BindingLayout::BlurV1;
    ShaderArtifact artifact;
    ShaderFlowReport report;
    std::string diagnostic;
    const bool success = LoadShaderFiles(request, artifact, report, diagnostic);
    const char* actual = success ? (report.attempts.back().sourceLanguage == ShaderSourceLanguage::Wgsl ? "wgsl" : "spirv") : "none";
    std::cout << name << " requested=" << ShaderFlowName(report.requestedFlow) << " actual=" << actual
      << " success=" << success << " fallback=" << report.fallbackAttempted << " cacheHit=" << artifact.cacheHit
      << " totalMs=" << report.elapsedMilliseconds << " wgslBytes=" << artifact.wgsl.size() << '\n';
    for (const auto& attempt : report.attempts) {
      std::cout << "  source=" << attempt.sourceName << " success=" << attempt.succeeded << " cacheHit=" << attempt.cacheHit
        << " preparationMs=" << attempt.preparationMilliseconds << " attemptMs=" << attempt.elapsedMilliseconds << '\n';
    }
    if (!diagnostic.empty()) std::cerr << diagnostic << '\n';
    allSucceeded &= success;
  }
  return allSucceeded;
}

bool CheckComputeV1Shaders() {
  struct ExpectedShader {
    const char* name;
    uint32_t constantWords;
    std::array<uint32_t, 3> workgroupSize;
    size_t bindingCount;
  };
  constexpr ExpectedShader shaders[]{
    {"CS_Arithmetic.hlsl", 4, {64, 1, 1}, 2},
    {"CS_Blur.hlsl", 32, {8, 8, 1}, 4},
    {"CS_Bright.hlsl", 8, {8, 8, 1}, 6},
    {"CS_GodRays.hlsl", 52, {8, 8, 1}, 6},
    {"CS_HDRComposite.hlsl", 8, {8, 8, 1}, 8},
    {"CS_TorchParticles.hlsl", 56, {8, 8, 1}, 2},
  };

  for (const ExpectedShader& expected : shaders) {
    ShaderFileRequest request;
    request.name = expected.name;
    request.stage = ShaderStage::Compute;
    request.layout = BindingLayout::ComputeV1;
    request.entryPoint = "CS";
    request.flow = ShaderFlow::Spirv;

    ShaderArtifact artifact;
    ShaderFlowReport report;
    std::string diagnostic;
    Require(LoadShaderFiles(request, artifact, report, diagnostic), diagnostic);
    const auto constants = std::find_if(artifact.bindings.begin(), artifact.bindings.end(),
      [](const ShaderBinding& binding) {
        return binding.kind == ResourceKind::UniformBuffer && binding.group == 0 && binding.binding == 0;
      });
    Require(artifact.bindings.size() == expected.bindingCount,
            std::string(expected.name) + ": unexpected ComputeV1 binding count");
    Require(constants != artifact.bindings.end() &&
            constants->minimumBufferSize == expected.constantWords * sizeof(uint32_t),
            std::string(expected.name) + ": unexpected ComputeV1 constant layout");
    Require(artifact.workgroupSize == expected.workgroupSize,
            std::string(expected.name) + ": unexpected ComputeV1 workgroup size");
  }
  std::cout << "ComputeV1 shader contracts PASS: " << std::size(shaders) << " families\n";
  return true;
}

void CheckShaderFlows() {
  auto& locator = t850::ResourceLocator::Instance();
  const auto fixtureRoot = std::filesystem::absolute(locator.GetCachePath()) / "flow-fixtures"
    / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  std::filesystem::create_directories(fixtureRoot);
  const auto stem = (fixtureRoot / "FS_flow").generic_string();
  const std::string direct = "@fragment fn FS() -> @location(0) vec4<f32> { return vec4<f32>(0.25); }\n";
  const std::string hlsl = "float4 FS() : SV_TARGET { return float4(0.75,0.75,0.75,0.75); }\n";
  const auto write = [&](const char* extension, const std::string& source) {
    Require(locator.WriteText(stem + extension, source), "Cannot write isolated flow fixture");
  };
  ShaderFileRequest request;
  request.stage = ShaderStage::Fragment;
  request.entryPoint = "FS";
  request.name = stem;
  ShaderArtifact artifact;
  ShaderFlowReport report;
  std::string diagnostic;
  const auto load = [&](bool expectedSuccess, unsigned attempts, ShaderSourceLanguage language, bool hit = false) {
    Require(LoadShaderFiles(request, artifact, report, diagnostic) == expectedSuccess, "Unexpected shader flow outcome: " + diagnostic);
    Require(report.requestedFlow == request.flow && report.attempts.size() == attempts, "Unexpected flow attempts");
    Require(report.fallbackAttempted == (attempts == 2), "Incorrect fallback report");
    if (attempts) {
      const auto& selected = report.attempts.back();
      Require(selected.sourceLanguage == language && selected.succeeded == expectedSuccess, "Incorrect actual flow report");
      Require(report.elapsedMilliseconds >= selected.elapsedMilliseconds && selected.elapsedMilliseconds >= 0, "Invalid flow timings");
      if (expectedSuccess) {
        Require(selected.cacheHit == hit && artifact.cacheHit == hit, "Flow cache isolation failed");
        Require(!hit || selected.preparationMilliseconds == 0, "Warm flow performed preparation");
      }
    }
    Require(expectedSuccess || artifact.wgsl.empty(), "Failed flow leaked a partial artifact");
  };
  write(".wgsl", direct);
  write(".hlsl", hlsl);
  Require(request.flow == ShaderFlow::Auto, "Default shader flow is not auto");
  load(true, 1, ShaderSourceLanguage::Wgsl);
  const auto directWgsl = artifact.wgsl;
  load(true, 1, ShaderSourceLanguage::Wgsl, true);
  request.flow = ShaderFlow::Spirv;
  load(true, 1, ShaderSourceLanguage::Hlsl);
  Require(artifact.wgsl != directWgsl, "Forced SPIR-V reused WGSL output");
  const auto translatedWgsl = artifact.wgsl;
  load(true, 1, ShaderSourceLanguage::Hlsl, true);
  write(".wgsl", "invalid WGSL\n");
  request.flow = ShaderFlow::Auto;
  load(true, 2, ShaderSourceLanguage::Hlsl, true);
  Require(artifact.wgsl == translatedWgsl && !report.attempts.front().diagnostic.empty()
    && diagnostic.find("fallback") != std::string::npos, "Fallback failure reason missing");
  request.flow = ShaderFlow::Wgsl;
  load(false, 1, ShaderSourceLanguage::Wgsl);
  write(".wgsl", "@vertex fn FS() -> @builtin(position) vec4<f32> { return vec4<f32>(0.0); }\n");
  request.flow = ShaderFlow::Auto;
  load(true, 2, ShaderSourceLanguage::Hlsl, true);
  write(".wgsl", "#error preprocessing failure\n");
  load(true, 2, ShaderSourceLanguage::Hlsl, true);
  write(".wgsl", "");
  load(true, 2, ShaderSourceLanguage::Hlsl, true);
  std::filesystem::remove(stem + ".wgsl");
  load(true, 2, ShaderSourceLanguage::Hlsl, true);
  write(".wgsl", direct);
  load(true, 1, ShaderSourceLanguage::Wgsl, true);
  Require(!report.fallbackAttempted && diagnostic.empty(), "Recovered WGSL retained fallback state");
  write(".hlsl", "invalid HLSL\n");
  request.flow = ShaderFlow::Spirv;
  load(false, 1, ShaderSourceLanguage::Hlsl);
  request.flow = ShaderFlow::Auto;
  load(true, 1, ShaderSourceLanguage::Wgsl, true);
  write(".wgsl", "invalid WGSL\n");
  load(false, 2, ShaderSourceLanguage::Hlsl);
  Require(diagnostic.find(stem + ".wgsl") != std::string::npos && diagnostic.find(stem + ".hlsl") != std::string::npos,
    "Both failure diagnostics must be retained");
  std::filesystem::remove(stem + ".hlsl");
  load(false, 2, ShaderSourceLanguage::Hlsl);
  request.flow = ShaderFlow::Spirv;
  load(false, 1, ShaderSourceLanguage::Hlsl);
  write(".wgsl", direct);
  request.flow = ShaderFlow::Auto;
  load(true, 1, ShaderSourceLanguage::Wgsl, true);
  std::filesystem::remove(stem + ".wgsl");
  load(false, 2, ShaderSourceLanguage::Hlsl);
  write(".wgsl", direct);
  write(".hlsl", hlsl);
  request.name = stem + ".hlsl";
  load(true, 1, ShaderSourceLanguage::Wgsl, true);
  request.flow = ShaderFlow::Spirv;
  request.name = stem + ".wgsl";
  load(true, 1, ShaderSourceLanguage::Hlsl, true);
  request.defines = "#define FLOW_VARIANT 1\n";
  load(true, 1, ShaderSourceLanguage::Hlsl);
  request.flow = ShaderFlow::Wgsl;
  load(true, 1, ShaderSourceLanguage::Wgsl);
  request.stage = ShaderStage::Compute;
  load(false, 0, ShaderSourceLanguage::Wgsl);
  request.stage = ShaderStage::Fragment;
  request.flow = static_cast<ShaderFlow>(999);
  load(false, 0, ShaderSourceLanguage::Wgsl);
  ShaderFlow parsed = ShaderFlow::Auto;
  for (const auto flow : {ShaderFlow::Auto, ShaderFlow::Wgsl, ShaderFlow::Spirv}) {
    Require(ParseShaderFlow(ShaderFlowName(flow), parsed) && parsed == flow, "Flow option round trip failed");
  }
  Require(!ParseShaderFlow("invalid", parsed) && parsed == ShaderFlow::Spirv, "Invalid option changed flow");
  std::filesystem::remove_all(fixtureRoot);
  std::cout << "Shader flow PASS: WGSL default, strict overrides, fallback/recovery, diagnostics, cache isolation\n";
}

void CheckBindings(const std::string& name, const ShaderArtifact& artifact) {
  const auto& resources = artifact.bindings;
  if (name == "CS_SeparableBlur.hlsl") {
    bool constants = false;
    bool input = false;
    bool output = false;
    for (const auto& resource : resources) {
      if (resource.group != 0) throw std::runtime_error("Unexpected compute group");
      constants |= resource.binding == 0 && resource.kind == ResourceKind::UniformBuffer && resource.minimumBufferSize == 16
        && resource.uniformMemberOffsets == std::vector<uint32_t>{0, 4, 8, 12};
      input |= resource.binding == 1 && resource.kind == ResourceKind::SampledTexture;
      output |= resource.binding == 2 && resource.kind == ResourceKind::WriteOnlyStorageTexture && resource.storageRgba16Float;
    }
    if (resources.size() != 3 || !constants || !input || !output || artifact.workgroupSize != std::array<uint32_t, 3>{8, 8, 1})
      throw std::runtime_error("Compute binding/format/workgroup contract mismatch\n" + artifact.wgsl);
    return;
  }
  if (name == "VS_tri.hlsl" || name == "FS_tri.hlsl" || name == "VS_Text.hlsl") {
    if (!resources.empty()) throw std::runtime_error("Unexpected active resource");
    return;
  }
  if (name != "FS_Text.hlsl") return;
  bool texture = false;
  bool sampler = false;
  bool uniform = false;
  for (const auto& resource : resources) {
    std::cout << "  group=" << resource.group << " binding=" << resource.binding
          << " type=" << static_cast<unsigned>(resource.kind)
          << " minimumBufferSize=" << resource.minimumBufferSize << '\n';
    if (resource.group != 0) throw std::runtime_error("Unexpected bind group");
    texture |= resource.binding == 0 && resource.kind == ResourceKind::SampledTexture;
    sampler |= resource.binding == 32 && resource.kind == ResourceKind::Sampler;
    uniform |= resource.binding == 64 && resource.kind == ResourceKind::UniformBuffer && resource.minimumBufferSize == 16;
  }
  if (resources.size() != 3 || !texture || !sampler || !uniform)
    throw std::runtime_error("Text binding layout does not match t0/s0/b0 mapping");
}
}

int main(int argc, char** argv) {
  if (argc < 3 || argc > 6) {
    std::cerr << "Usage: DawnShaderProbe <shader-directory> <cache-root> [flow [auto|wgsl|spirv] [shader-file]]\n"
      << "       DawnShaderProbe <shader-directory> <cache-root> <test|cold|warm|gpu|permutations|corpus|flow-test|compute-v1>\n";
    return 1;
  }
  int exitCode = 0;
  try {
    const std::string mode = argc >= 4 ? argv[3] : "flow";
    Require(mode == "test" || mode == "cold" || mode == "warm" || mode == "gpu" || mode == "permutations" || mode == "corpus" || mode == "flow-test" || mode == "compute-v1" || mode == "flow", "Invalid test mode");
    Require(mode == "flow" || argc <= 4, "Only flow mode accepts a flow and optional shader file");
    auto cacheRoot = std::filesystem::path(argv[2]);
    if (mode == "test" || mode == "gpu") cacheRoot /= std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    t850::ResourceLocator::Instance().SetBasePath(argv[1]);
    t850::ResourceLocator::Instance().SetCachePath(cacheRoot);
    if (mode == "flow") {
      ShaderFlow flow = ShaderFlow::Auto;
      Require(argc < 5 || ParseShaderFlow(argv[4], flow), "Invalid shader flow: expected auto, wgsl or spirv");
      return RunSelectedFlow(flow, argc == 6 ? argv[5] : "") ? 0 : 1;
    }
    if (mode == "flow-test") {
      CheckShaderFlows();
      return 0;
    }
    if (mode == "compute-v1") {
      return CheckComputeV1Shaders() ? 0 : 1;
    }
    if (mode == "permutations") {
      CheckVertexPermutations();
      return 0;
    }
    if (mode == "corpus") {
      CheckRecordedPermutations();
      return 0;
    }
    Require(t850::ShaderDiskCache::ContentHash("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d", "Cache digest known-answer failure");
    std::map<std::string, ShaderArtifact> artifacts;
    std::map<std::string, ShaderArtifact> directArtifacts;
    for (const std::string name : shaderFiles) {
      const bool vertex = name.starts_with("VS");
      const auto start = std::chrono::steady_clock::now();
      ShaderRequest request;
      request.name = name;
      Require(t850::ResourceLocator::Instance().ReadText(name, request.source), "Missing canonical shader: " + name);
      request.stage = vertex ? ShaderStage::Vertex : ShaderStage::Fragment;
      request.entryPoint = vertex ? "VS" : "FS";
      if (name.starts_with("CS_")) {
        request.stage = ShaderStage::Compute;
        request.layout = BindingLayout::BlurV1;
        request.entryPoint = "CS";
        if (mode == "test" || mode == "gpu") CheckNativeBlur(request);
      }
      auto artifact = Compile(request, mode == "warm");
      CheckBindings(name, artifact);
      if (mode == "test") CheckCache(request, artifact);
      auto directRequest = request;
      directRequest.name = std::filesystem::path(name).replace_extension(".wgsl").string();
      directRequest.sourceLanguage = ShaderSourceLanguage::Wgsl;
      Require(t850::ResourceLocator::Instance().ReadText(directRequest.name, directRequest.source), "Missing handwritten WGSL: " + directRequest.name);
      auto direct = Compile(directRequest, mode == "warm");
      CheckBindings(name, direct);
      auto expectedBindings = artifact.bindings;
      auto actualBindings = direct.bindings;
      const auto order = [](const ShaderBinding& left, const ShaderBinding& right) { return left.binding < right.binding; };
      std::sort(expectedBindings.begin(), expectedBindings.end(), order);
      std::sort(actualBindings.begin(), actualBindings.end(), order);
      Require(expectedBindings == actualBindings && artifact.workgroupSize == direct.workgroupSize, name + ": HLSL/WGSL reflection mismatch");
      if (mode == "test") CheckNativeContract(request, direct);
      if (mode == "test") CheckCache(directRequest, direct);
      directArtifacts.emplace(name, std::move(direct));
      const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      const auto& reported = directArtifacts.at(name);
      std::cout << directRequest.name << " PASS: mode=" << mode << " hit=" << reported.cacheHit << " wgslBytes=" << reported.wgsl.size()
        << " preparationMs=" << reported.translationMilliseconds
                << " elapsedMs=" << elapsed << '\n';
      artifacts.emplace(name, std::move(artifact));
    }
    if (mode == "gpu") {
      std::cout << "HLSL-derived pipelines:\n";
      CheckHardware(artifacts);
      std::cout << "Handwritten WGSL pipelines:\n";
      CheckHardware(directArtifacts, true);
    }
  } catch (const std::exception& error) {
    std::cerr << "Shader probe failed: " << error.what() << '\n';
    exitCode = 1;
  }
  return exitCode;
}