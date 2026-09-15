#include "ShaderNumerics.h"
#include <video/webgpu/WebGPUShaderCompiler.h>
#include <utils/ResourceLocator.h>
#include <utils/ShaderPreprocessor.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <DirectXPackedVector.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
using DirectX::PackedVector::XMConvertFloatToHalf;
using DirectX::PackedVector::XMConvertHalfToFloat;

void Require(bool success, const char* message) {
  if (!success) throw std::runtime_error(message);
}

std::vector<float> NativeBlur(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11ComputeShader* shader,
                             const std::array<int32_t, 4>& constants, const std::vector<float>& input,
                             UINT constantSlot = 0, UINT textureSlot = 0, std::span<const float> matrices = {}) {
  const UINT width = constants[0];
  const UINT height = constants[1];
  const UINT outputWidth = (width + 7) / 8 * 8;
  const UINT outputHeight = (height + 7) / 8 * 8;
  D3D11_TEXTURE2D_DESC textureDesc{};
  textureDesc.Width = width;
  textureDesc.Height = height;
  textureDesc.MipLevels = 1;
  textureDesc.ArraySize = 1;
  textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  textureDesc.SampleDesc.Count = 1;
  textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA data{input.data(), width * 16, 0};
  ComPtr<ID3D11Texture2D> source;
  Require(SUCCEEDED(device->CreateTexture2D(&textureDesc, &data, &source)), "D3D11 input texture failed");
  ComPtr<ID3D11ShaderResourceView> inputView;
  Require(SUCCEEDED(device->CreateShaderResourceView(source.Get(), nullptr, &inputView)), "D3D11 input view failed");
  textureDesc.Width = outputWidth;
  textureDesc.Height = outputHeight;
  textureDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  textureDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  std::vector<uint16_t> sentinel(outputWidth * outputHeight * 4, XMConvertFloatToHalf(-999.0f));
  data = {sentinel.data(), outputWidth * 8, 0};
  ComPtr<ID3D11Texture2D> destination;
  Require(SUCCEEDED(device->CreateTexture2D(&textureDesc, &data, &destination)), "D3D11 output texture failed");
  ComPtr<ID3D11UnorderedAccessView> outputView;
  Require(SUCCEEDED(device->CreateUnorderedAccessView(destination.Get(), nullptr, &outputView)), "D3D11 output view failed");
  D3D11_BUFFER_DESC bufferDesc{};
  std::vector<uint8_t> uniformData(16 + matrices.size_bytes());
  std::memcpy(uniformData.data(), constants.data(), 16);
  if (!matrices.empty()) std::memcpy(uniformData.data() + 16, matrices.data(), matrices.size_bytes());
  bufferDesc.ByteWidth = static_cast<UINT>(uniformData.size());
  bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  data = {uniformData.data(), 0, 0};
  ComPtr<ID3D11Buffer> constantBuffer;
  Require(SUCCEEDED(device->CreateBuffer(&bufferDesc, &data, &constantBuffer)), "D3D11 constants failed");
  context->CSSetShader(shader, nullptr, 0);
  context->CSSetConstantBuffers(constantSlot, 1, constantBuffer.GetAddressOf());
  context->CSSetShaderResources(textureSlot, 1, inputView.GetAddressOf());
  context->CSSetUnorderedAccessViews(0, 1, outputView.GetAddressOf(), nullptr);
  context->Dispatch(outputWidth / 8, outputHeight / 8, 1);
  context->ClearState();
  textureDesc.BindFlags = 0;
  textureDesc.Usage = D3D11_USAGE_STAGING;
  textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  Require(SUCCEEDED(device->CreateTexture2D(&textureDesc, nullptr, &staging)), "D3D11 staging failed");
  context->CopyResource(staging.Get(), destination.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  Require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "D3D11 readback failed");
  std::vector<float> result(outputWidth * outputHeight * 4);
  for (UINT row = 0; row < outputHeight; ++row) {
    const auto* values = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapped.pData) + row * mapped.RowPitch);
    for (UINT component = 0; component < outputWidth * 4; ++component)
      result[row * outputWidth * 4 + component] = XMConvertHalfToFloat(values[component]);
  }
  context->Unmap(staging.Get(), 0);
  return result;
}

std::vector<float> DawnBlur(const wgpu::Instance& instance, const wgpu::Device& device,
                           const wgpu::ComputePipeline& pipeline, const std::array<int32_t, 4>& constants,
                           const std::vector<float>& input, uint32_t groupIndex = 0, std::span<const float> matrices = {}) {
  const uint32_t width = constants[0];
  const uint32_t height = constants[1];
  const uint32_t outputWidth = (width + 7) / 8 * 8;
  const uint32_t outputHeight = (height + 7) / 8 * 8;
  wgpu::TextureDescriptor textureDesc{};
  textureDesc.size = {width, height, 1};
  textureDesc.format = wgpu::TextureFormat::RGBA32Float;
  textureDesc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
  auto source = device.CreateTexture(&textureDesc);
  auto queue = device.GetQueue();
  wgpu::TexelCopyTextureInfo sourceCopy{};
  sourceCopy.texture = source;
  wgpu::TexelCopyBufferLayout inputLayout{};
  inputLayout.bytesPerRow = width * 16;
  inputLayout.rowsPerImage = height;
  queue.WriteTexture(&sourceCopy, input.data(), input.size() * sizeof(float), &inputLayout, &textureDesc.size);
  textureDesc.size = {outputWidth, outputHeight, 1};
  textureDesc.format = wgpu::TextureFormat::RGBA16Float;
  textureDesc.usage = wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::CopyDst;
  auto destination = device.CreateTexture(&textureDesc);
  std::vector<uint16_t> sentinel(outputWidth * outputHeight * 4, XMConvertFloatToHalf(-999.0f));
  wgpu::TexelCopyTextureInfo outputCopy{};
  outputCopy.texture = destination;
  inputLayout.bytesPerRow = outputWidth * 8;
  inputLayout.rowsPerImage = outputHeight;
  queue.WriteTexture(&outputCopy, sentinel.data(), sentinel.size() * sizeof(uint16_t), &inputLayout, &textureDesc.size);
  wgpu::BufferDescriptor bufferDesc{};
  bufferDesc.size = 16 + matrices.size_bytes();
  bufferDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  auto constantBuffer = device.CreateBuffer(&bufferDesc);
  queue.WriteBuffer(constantBuffer, 0, constants.data(), 16);
  if (!matrices.empty()) queue.WriteBuffer(constantBuffer, 16, matrices.data(), matrices.size_bytes());
  wgpu::BindGroupEntry entries[3]{};
  entries[0].binding = 0;
  entries[0].buffer = constantBuffer;
  entries[0].size = bufferDesc.size;
  entries[1].binding = 1;
  entries[1].textureView = source.CreateView();
  entries[2].binding = 2;
  entries[2].textureView = destination.CreateView();
  wgpu::BindGroupDescriptor groupDesc{};
  groupDesc.layout = pipeline.GetBindGroupLayout(groupIndex);
  groupDesc.entryCount = 3;
  groupDesc.entries = entries;
  auto group = device.CreateBindGroup(&groupDesc);
  const uint32_t rowPitch = (outputWidth * 8 + 255) / 256 * 256;
  bufferDesc.size = rowPitch * outputHeight;
  bufferDesc.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
  auto staging = device.CreateBuffer(&bufferDesc);
  auto encoder = device.CreateCommandEncoder();
  auto pass = encoder.BeginComputePass();
  pass.SetPipeline(pipeline);
  pass.SetBindGroup(groupIndex, group);
  pass.DispatchWorkgroups(outputWidth / 8, outputHeight / 8, 1);
  pass.End();
  wgpu::TexelCopyBufferInfo bufferCopy{};
  bufferCopy.buffer = staging;
  bufferCopy.layout.bytesPerRow = rowPitch;
  bufferCopy.layout.rowsPerImage = outputHeight;
  encoder.CopyTextureToBuffer(&outputCopy, &bufferCopy, &textureDesc.size);
  auto command = encoder.Finish();
  queue.Submit(1, &command);
  auto mapped = std::make_shared<bool>(false);
  auto future = staging.MapAsync(wgpu::MapMode::Read, 0, bufferDesc.size, wgpu::CallbackMode::WaitAnyOnly,
    [mapped](wgpu::MapAsyncStatus status, wgpu::StringView) { *mapped = status == wgpu::MapAsyncStatus::Success; });
  Require(instance.WaitAny(future, 30'000'000'000ULL) == wgpu::WaitStatus::Success && *mapped, "Dawn readback failed or timed out");
  const auto* bytes = static_cast<const uint8_t*>(staging.GetConstMappedRange());
  std::vector<float> result(outputWidth * outputHeight * 4);
  for (uint32_t row = 0; row < outputHeight; ++row) {
    const auto* values = reinterpret_cast<const uint16_t*>(bytes + row * rowPitch);
    for (uint32_t component = 0; component < outputWidth * 4; ++component)
      result[row * outputWidth * 4 + component] = XMConvertHalfToFloat(values[component]);
  }
  staging.Unmap();
  return result;
}

void CheckMatrixNumerics(const wgpu::Instance& instance, const wgpu::Device& device,
                         ID3D11Device* native, ID3D11DeviceContext* context) {
  using namespace t850::webgpu;
  ShaderRequest request;
  request.name = "MatrixLayoutRegression";
  request.stage = ShaderStage::Compute;
  request.layout = BindingLayout::BlurV1;
  request.entryPoint = "CS";
  request.source = R"(
cbuffer MatrixConstants : register(b0) {
    uint width; uint height; uint mode; uint unused;
    float4x4 defaultMatrix;
    float4x4 boneMatrices[2];
    row_major float4x4 rowMatrix;
    column_major float4x4 columnMatrix;
};
Texture2D<float4> inputTexture : register(t0);
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture2D<float4> outputTexture : register(u0);
[numthreads(8,8,1)] void CS(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    float4 value = inputTexture.Load(int3(id.xy, 0));
    float4 result;
    if (mode == 0) result = mul(defaultMatrix, value);
    else if (mode == 1) result = mul(value, defaultMatrix);
    else if (mode == 2) result = mul(boneMatrices[id.x % 2], value);
    else if (mode == 3) result = mul(rowMatrix, value);
    else if (mode == 4) result = mul(columnMatrix, value);
    else if (mode == 5) result = defaultMatrix[id.x % 4];
    else result = defaultMatrix[id.x % 4][id.y % 4];
    outputTexture[id.xy] = result;
}
)";
  ShaderArtifact artifact;
  std::string diagnostic;
  const bool translated = TranslateShader(request, artifact, diagnostic);
  Require(translated, diagnostic.c_str());
  ComPtr<ID3DBlob> bytecode;
  ComPtr<ID3DBlob> errors;
  Require(SUCCEEDED(D3DCompile(request.source.data(), request.source.size(), request.name.c_str(), nullptr, nullptr,
    "CS", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &bytecode, &errors)), "Matrix native compile failed");
  ComPtr<ID3D11ComputeShader> shader;
  Require(SUCCEEDED(native->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader)), "Matrix native pipeline failed");
  wgpu::ShaderSourceWGSL source{};
  source.code = artifact.wgsl.c_str();
  wgpu::ShaderModuleDescriptor moduleDesc{};
  moduleDesc.nextInChain = &source;
  wgpu::ComputePipelineDescriptor pipelineDesc{};
  pipelineDesc.compute.module = device.CreateShaderModule(&moduleDesc);
  pipelineDesc.compute.entryPoint = "CS";
  auto pipeline = device.CreateComputePipeline(&pipelineDesc);
  std::array<float, 80> matrices;
  for (size_t component = 0; component < matrices.size(); ++component)
    matrices[component] = static_cast<float>((component * 7 + component / 16) % 19) - 7.0f;
  std::vector<float> input(8 * 8 * 4);
  for (size_t component = 0; component < input.size(); ++component)
    input[component] = static_cast<float>(component % 7) - 3.0f;
  for (int32_t mode = 0; mode < 7; ++mode) {
    const std::array<int32_t, 4> constants{8, 8, mode, 0};
    const auto expected = NativeBlur(native, context, shader.Get(), constants, input, 0, 0, matrices);
    const auto actual = DawnBlur(instance, device, pipeline, constants, input, 0, matrices);
    for (size_t component = 0; component < expected.size(); ++component) {
      if (!std::isfinite(actual[component]) || actual[component] != expected[component])
        throw std::runtime_error("Matrix layout mismatch: mode=" + std::to_string(mode) + " component=" + std::to_string(component)
          + " native=" + std::to_string(expected[component]) + " translated=" + std::to_string(actual[component]));
    }
  }
  std::cout << "Matrix differential PASS: 448 nonidentity input/mode cases, default/row/column packing, array and indexed loads (exact)\n";
}
}

void CheckBlurNumerics(const wgpu::Instance& instance, const wgpu::Device& device,
                       IDXGIAdapter* adapter, const wgpu::ShaderModule& module) {
  ComPtr<ID3D11Device> native;
  ComPtr<ID3D11DeviceContext> context;
  Require(SUCCEEDED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
    D3D11_SDK_VERSION, &native, nullptr, &context)), "D3D11 hardware device failed");
  std::string source;
  Require(t850::ResourceLocator::Instance().ReadText("CS_SeparableBlur.hlsl", source), "Missing HLSL numeric reference");
  ComPtr<ID3DBlob> bytecode;
  ComPtr<ID3DBlob> errors;
  Require(SUCCEEDED(D3DCompile(source.data(), source.size(), "CS_SeparableBlur.hlsl", nullptr, nullptr,
    "CS", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &bytecode, &errors)), "HLSL numeric reference compile failed");
  ComPtr<ID3D11ComputeShader> shader;
  Require(SUCCEEDED(native->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader)), "Native compute shader failed");
  wgpu::ComputePipelineDescriptor pipelineDesc{};
  pipelineDesc.compute.module = module;
  pipelineDesc.compute.entryPoint = "CS";
  auto pipeline = device.CreateComputePipeline(&pipelineDesc);
  unsigned cases = 0;
  float maximumError = 0;
  for (const auto size : {std::array<int32_t, 2>{1, 1}, {1, 7}, {7, 1}, {13, 11}, {16, 8}}) {
    const int32_t width = size[0];
    const int32_t height = size[1];
    const int32_t outputWidth = (width + 7) / 8 * 8;
    const int32_t outputHeight = (height + 7) / 8 * 8;
    for (int pattern = 0; pattern < 3; ++pattern) {
      std::vector<float> input(width * height * 4);
      for (int32_t row = 0; row < height; ++row) {
        for (int32_t column = 0; column < width; ++column) {
          for (int32_t channel = 0; channel < 4; ++channel) {
            input[(row * width + column) * 4 + channel] = pattern == 0 ? (channel + 1) * 0.3f
              : pattern == 1 ? (column * 3 - row * 2 + channel) * 0.1f
              : (column == width / 2 && row == height / 2 ? (channel + 1) * 2.0f : 0.0f);
          }
        }
      }
      for (const auto direction : {std::array<int32_t, 2>{1, 0}, {0, 1}, {-1, 0}, {0, -1}}) {
        const std::array<int32_t, 4> constants{width, height, direction[0], direction[1]};
        const auto hlsl = NativeBlur(native.Get(), context.Get(), shader.Get(), constants, input);
        const auto wgsl = DawnBlur(instance, device, pipeline, constants, input);
        for (int32_t row = 0; row < outputHeight; ++row) {
          for (int32_t column = 0; column < outputWidth; ++column) {
            for (int32_t channel = 0; channel < 4; ++channel) {
              float expected = -999.0f;
              if (column < width && row < height) {
                expected = 0;
                const float weights[]{1, 4, 6, 4, 1};
                for (int32_t tap = -2; tap <= 2; ++tap) {
                  const auto sourceX = std::clamp(column + tap * direction[0], 0, width - 1);
                  const auto sourceY = std::clamp(row + tap * direction[1], 0, height - 1);
                  expected += input[(sourceY * width + sourceX) * 4 + channel] * weights[tap + 2] / 16.0f;
                }
              }
              const auto offset = (row * outputWidth + column) * 4 + channel;
              const auto error = std::max({std::abs(wgsl[offset] - expected), std::abs(hlsl[offset] - expected), std::abs(hlsl[offset] - wgsl[offset])});
              const float tolerance = column >= width || row >= height ? 0.0f : 0.001f + std::abs(expected) * 0.001f;
              if (!std::isfinite(wgsl[offset]) || !std::isfinite(hlsl[offset]) || error > tolerance) {
                throw std::runtime_error("Blur mismatch: case=" + std::to_string(cases) + " pixel=" + std::to_string(column) + "," + std::to_string(row)
                  + " channel=" + std::to_string(channel) + " cpu=" + std::to_string(expected) + " hlsl=" + std::to_string(hlsl[offset]) + " wgsl=" + std::to_string(wgsl[offset]));
              }
              maximumError = std::max(maximumError, error);
            }
          }
        }
        ++cases;
      }
    }
  }
  std::cout << "Blur differential PASS: cases=" << cases << " maxAbsError=" << maximumError
    << " (native HLSL/D3D11, WGSL/Dawn/D3D12, CPU; padded sentinel writes checked)\n";
}

void CheckShaderFunctionNumerics(const wgpu::Instance& instance, const wgpu::Device& device,
                                 IDXGIAdapter* adapter) {
  ComPtr<ID3D11Device> native;
  ComPtr<ID3D11DeviceContext> context;
  Require(SUCCEEDED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
    D3D11_SDK_VERSION, &native, nullptr, &context)), "Numeric D3D11 device failed");
  CheckMatrixNumerics(instance, device, native.Get(), context.Get());
  const std::string nativeEntry = R"(
cbuffer NumericConstants : register(b7) { uint numericWidth; uint numericHeight; int numericMode; int numericUnused; };
Texture2D<float4> numericInput : register(t30);
RWTexture2D<float4> numericOutput : register(u0);
[numthreads(8,8,1)] void CS(uint3 id : SV_DispatchThreadID) {
    if (id.x >= numericWidth || id.y >= numericHeight) return;
    float4 sampleValue = numericInput.Load(int3(id.xy, 0));
    float4 result = 0;
    if (numericMode == 0) result = float4(NormalDistribution(sampleValue.x, sampleValue.y), VisibilityGGX(sampleValue.x, sampleValue.z, sampleValue.y), RangeAttenuation(sampleValue.w * 5, sampleValue.z * 5), 1);
    else if (numericMode == 1) result = float4(FresnelCalc(sampleValue.x, sampleValue.yzw), 1);
    else if (numericMode == 2) result = float4(IBLGGXFresnel(sampleValue.x, sampleValue.y, sampleValue.yzw, float2(sampleValue.z, sampleValue.w)), 1);
    else if (numericMode == 3) result = float4(DistributionCharlie(sampleValue.y, sampleValue.x), VisibilitySheen(sampleValue.x, sampleValue.z, sampleValue.y), LambdaSheen(sampleValue.x, sampleValue.y), 1);
#ifdef NUMERIC_MESH
    else if (numericMode == 4) result = float4(EncodeOctahedralNormal(normalize(sampleValue.xyz * 2 - 1)), 0, 1);
    else if (numericMode == 5) result = float4(LinearToStoredAlbedo(sampleValue.xyz), 1);
#else
    else if (numericMode == 4) result = float4(DecodeOctahedralNormal(sampleValue.xy), 1);
    else if (numericMode == 5) result = float4(LinearToSRGB(sampleValue.xyz), 1);
#endif
    numericOutput[id.xy] = result;
}
)";
  const std::string directEntry = R"(
struct NumericConstants { width: u32, height: u32, mode: i32, unused: i32 }
@group(1) @binding(0) var<uniform> numericConstants: NumericConstants;
@group(1) @binding(1) var numericInput: texture_2d<f32>;
@group(1) @binding(2) var numericOutput: texture_storage_2d<rgba16float, write>;
@compute @workgroup_size(8,8,1) fn CS(@builtin(global_invocation_id) id: vec3<u32>) {
    if (id.x >= numericConstants.width || id.y >= numericConstants.height) { return; }
    let sampleValue = textureLoad(numericInput, vec2<i32>(id.xy), 0);
    var result = vec4<f32>(0.0);
    if (numericConstants.mode == 0) { result = vec4<f32>(NormalDistribution(sampleValue.x, sampleValue.y), VisibilityGGX(sampleValue.x, sampleValue.z, sampleValue.y), RangeAttenuation(sampleValue.w * 5.0, sampleValue.z * 5.0), 1.0); }
    else if (numericConstants.mode == 1) { result = vec4<f32>(FresnelCalc(sampleValue.x, sampleValue.yzw), 1.0); }
    else if (numericConstants.mode == 2) { result = vec4<f32>(IBLGGXFresnel(sampleValue.x, sampleValue.y, sampleValue.yzw, vec2<f32>(sampleValue.z, sampleValue.w)), 1.0); }
    else if (numericConstants.mode == 3) { result = vec4<f32>(DistributionCharlie(sampleValue.y, sampleValue.x), VisibilitySheen(sampleValue.x, sampleValue.z, sampleValue.y), LambdaSheen(sampleValue.x, sampleValue.y), 1.0); }
#ifdef NUMERIC_MESH
    else if (numericConstants.mode == 4) { result = vec4<f32>(EncodeOctahedralNormal(normalize(sampleValue.xyz * 2.0 - vec3<f32>(1.0))), 0.0, 1.0); }
    else if (numericConstants.mode == 5) { result = vec4<f32>(LinearToStoredAlbedo(sampleValue.xyz), 1.0); }
#else
    else if (numericConstants.mode == 4) { result = vec4<f32>(DecodeOctahedralNormal(sampleValue.xy), 1.0); }
    else if (numericConstants.mode == 5) { result = vec4<f32>(LinearToSRGB(sampleValue.xyz), 1.0); }
#endif
    textureStore(numericOutput, vec2<i32>(id.xy), result);
}
)";
  for (const std::string family : {"FS_Mesh", "FS_Quad"}) {
    const auto defines = family == "FS_Mesh" ? "#define SIMPLE_COLOR\n#define NUMERIC_MESH\n" : "#define DEFERRED_PASS\n";
    std::string hlslSource;
    std::string wgslSource;
    Require(t850::ResourceLocator::Instance().ReadText(family + ".hlsl", hlslSource), "Missing HLSL function source");
    Require(t850::ResourceLocator::Instance().ReadText(family + ".wgsl", wgslSource), "Missing WGSL function source");
    hlslSource = defines + hlslSource + nativeEntry;
    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    const auto status = D3DCompile(hlslSource.data(), hlslSource.size(), family.c_str(), nullptr, nullptr,
      "CS", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &bytecode, &errors);
    if (FAILED(status)) throw std::runtime_error(errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "Function HLSL failed");
    ComPtr<ID3D11ComputeShader> shader;
    Require(SUCCEEDED(native->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader)), "Native function pipeline failed");
    std::string prepared;
    std::string diagnostic;
    Require(t850::PreprocessShader(wgslSource + directEntry, family, defines, prepared, diagnostic), diagnostic.c_str());
    wgpu::ShaderSourceWGSL wgsl{};
    wgsl.code = prepared.c_str();
    wgpu::ShaderModuleDescriptor moduleDesc{};
    moduleDesc.nextInChain = &wgsl;
    auto module = device.CreateShaderModule(&moduleDesc);
    wgpu::ComputePipelineDescriptor pipelineDesc{};
    pipelineDesc.compute.module = module;
    pipelineDesc.compute.entryPoint = "CS";
    auto pipeline = device.CreateComputePipeline(&pipelineDesc);
    constexpr int32_t width = 32;
    constexpr int32_t height = 32;
    std::vector<float> input(width * height * 4);
    for (int32_t row = 0; row < height; ++row) {
      for (int32_t column = 0; column < width; ++column) {
        const int32_t offset = (row * width + column) * 4;
        input[offset] = (column + 0.5f) / width;
        input[offset + 1] = 0.04f + 0.96f * (row + 0.5f) / height;
        input[offset + 2] = 0.02f + 0.96f * ((column * 13 + row * 7) % 32) / 31.0f;
        input[offset + 3] = ((column + row * 3) % 32) / 31.0f;
      }
    }
    float maximumRelativeError = 0;
    for (int32_t mode = 0; mode < 6; ++mode) {
      const std::array<int32_t, 4> constants{width, height, mode, 0};
      const auto expected = NativeBlur(native.Get(), context.Get(), shader.Get(), constants, input, 7, 30);
      const auto actual = DawnBlur(instance, device, pipeline, constants, input, 1);
      for (size_t index = 0; index < actual.size(); ++index) {
        const auto error = std::abs(expected[index] - actual[index]);
        const auto scale = std::max(std::abs(expected[index]), 1.0f);
        if (!std::isfinite(expected[index]) || !std::isfinite(actual[index]) || error > 0.002f * scale)
          throw std::runtime_error(family + " function mismatch: mode=" + std::to_string(mode) + " component=" + std::to_string(index)
            + " hlsl=" + std::to_string(expected[index]) + " wgsl=" + std::to_string(actual[index]));
        maximumRelativeError = std::max(maximumRelativeError, error / scale);
      }
    }
    std::cout << family << " function differential PASS: 6144 input/mode cases, maxScaledError=" << maximumRelativeError << '\n';
  }
}