#include <pch.h>
#include <debug/GraphicsFixture.h>

#if defined(_WIN32) && defined(_M_X64)
#include <video/WindowsDriverFactory.h>
#include <video/webgpu/WebGPUDriver.h>
#include <core/EngineContext.h>
#include <utils/ResourceLocator.h>
#include <utils/Log.h>
#include <Windows.h>
#include <d3d12.h>
#include <array>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace t850 {
extern Device* T8Device;
extern DeviceContext* T8DeviceContext;
namespace {
void Require(bool success, const std::string& message) { if (!success) throw std::runtime_error("[GraphicsFixture] " + message); }
struct BufferDeleter { void operator()(Buffer* buffer) const { if (buffer) buffer->release(); } };
using FixtureBuffer = std::unique_ptr<Buffer, BufferDeleter>;
struct FixtureVertex { float position[4]; float normal[4]; float uv[2]; };
struct FixtureConstants { std::array<float, 16> wvp; std::array<float, 16> world; };
struct FixtureImage { unsigned width = 0; unsigned height = 0; std::vector<uint8_t> pixels; };
struct FixtureRun { std::vector<FixtureImage> images; uint64_t adapterLuid = 0; };

FixtureImage ReadImage(const std::string& path, bool validateRegions = true) {
  std::vector<unsigned char> bytes;
  Require(ResourceLocator::Instance().ReadBinary(path, bytes), "Missing capture: " + path);
  std::istringstream input(std::string(bytes.begin(), bytes.end()));
  std::string magic;
  FixtureImage image;
  unsigned maximum = 0;
  input >> magic >> image.width >> image.height >> maximum;
  Require(magic == "P6" && maximum == 255 && input.get() == '\n', "Invalid PPM capture");
  const auto offset = static_cast<size_t>(input.tellg());
  Require(offset + static_cast<size_t>(image.width) * image.height * 3 == bytes.size(), "Incomplete PPM capture");
  image.pixels.assign(bytes.begin() + offset, bytes.end());
  if (!validateRegions) return image;
  const auto center = (static_cast<size_t>(image.height / 2) * image.width + image.width / 2) * 3;
  Require(image.pixels[center] > 240 && image.pixels[center + 1] < 10 && image.pixels[center + 2] < 10,
    "Depth test or buffer snapshot failed: near red triangle missing at center");
  unsigned red = 0;
  unsigned blue = 0;
  unsigned background = 0;
  for (size_t pixel = 0; pixel < image.pixels.size(); pixel += 3) {
    red += image.pixels[pixel] > 240 && image.pixels[pixel + 1] < 10 && image.pixels[pixel + 2] < 10;
    blue += image.pixels[pixel] < 10 && image.pixels[pixel + 1] < 10 && image.pixels[pixel + 2] > 240;
    background += image.pixels[pixel] < 30 && image.pixels[pixel + 1] > 10 && image.pixels[pixel + 2] < 50;
  }
  const auto count = image.width * image.height;
  Require(red > count / 20 && blue > count / 10 && background > count / 10, "Fixture capture is blank or missing expected regions");
  return image;
}

FixtureRun RunDriver(GraphicsApi::E api, webgpu::ShaderFlow flow, HWND window, const std::filesystem::path& output, unsigned run) {
  std::unique_ptr<BaseDriver> driver(CreateWindowsGraphicsDriver(api));
  g_pBaseDriver = driver.get();
  const std::string tag = driver->ApiTag();
  if (api == GraphicsApi::WEBGPU) static_cast<WebGPUDriver*>(driver.get())->SetShaderFlow(flow);
  driver->SetWindowHandle(WindowHandle::FromHWND(window));
  driver->SetDimensions(320, 240);
  FixtureRun result;
  try {
    driver->InitDriver();
    RefreshEngineContextFromGlobals();
        const auto targetsBeforeValidation = driver->RTs.size();
        for (const int invalidFormat : {-1, 999}) {
          Require(driver->CreateRT(1, invalidFormat, BaseRT::F32, 8, 8) == -1,
            "Invalid color format reached render-target allocation");
          Require(driver->CreateRT(1, BaseRT::RGBA8, invalidFormat, 8, 8) == -1,
            "Invalid depth format reached render-target allocation");
        }
        Require(driver->CreateRT(2, std::vector<int>{BaseRT::RGBA8}, BaseRT::F32, 8, 8) == -1,
          "Mismatched attachment count was accepted");
        if (!driver->SupportsRenderTargetDepthFormat(BaseRT::FD16))
          Require(driver->CreateRT(0, BaseRT::NOTHING, BaseRT::FD16, 8, 8) == -1,
            "Unsupported depth format was silently substituted");
        if (!driver->SupportsCubeRenderTargets())
          Require(driver->CreateRT(0, BaseRT::NOTHING, BaseRT::CUBE_F32, 8, 8) == -1,
            "Unsupported cube target was accepted");
        if (!driver->SupportsRenderTargetMipGeneration())
          Require(driver->CreateRT(1, BaseRT::RGBA8, BaseRT::NOTHING, 8, 8, true) == -1,
            "Unsupported direct mip request was silently accepted");
        Require(driver->RTs.size() == targetsBeforeValidation, "Rejected descriptors allocated render targets");
        std::cout << "Render-target capability rejection PASS: api=" << tag << '\n';
    if (api == GraphicsApi::WEBGPU) result.adapterLuid = static_cast<WebGPUDriver*>(driver.get())->AdapterLuid();
    else {
      const auto luid = static_cast<ID3D12Device*>(T8Device->GetAPIObject())->GetAdapterLuid();
      result.adapterLuid = static_cast<uint64_t>(static_cast<uint32_t>(luid.HighPart)) << 32 | luid.LowPart;
    }
    std::cout << "Graphics fixture adapter LUID=" << result.adapterLuid << '\n';
    {
      const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
      FixtureConstants constants{identity, identity};
      std::array<FixtureVertex, 4> nearVertices{{
        {{-0.5f, -0.5f, 0.8f, 1}, {0, 0, 1, 0}, {0, 1}},
        {{0, 0.55f, 0.8f, 1}, {0, 0, 1, 0}, {0.5f, 0}},
        {{0.5f, -0.5f, 0.8f, 1}, {0, 0, 1, 0}, {1, 1}},
        {{0, 0, 0, 1}, {0, 0, 1, 0}, {0, 0}}
      }};
      auto farVertices = nearVertices;
      for (auto& vertex : farVertices) { vertex.position[0] *= 1.8f; vertex.position[1] *= 1.6f; vertex.position[2] = 0.2f; }
      std::array<FixtureVertex, 4> screenVertices{{
        {{-1, -1, 0.5f, 1}, {0, 0, 1, 0}, {0, 1}},
        {{-1, 1, 0.5f, 1}, {0, 0, 1, 0}, {0, 0}},
        {{1, 1, 0.5f, 1}, {0, 0, 1, 0}, {1, 0}},
        {{1, -1, 0.5f, 1}, {0, 0, 1, 0}, {1, 1}}
      }};
      std::array<uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
      FixtureBuffer vertex(T8Device->CreateBuffer(BufferType::VERTEX, {sizeof(nearVertices), BufferUsage::DINAMIC}, nearVertices.data()));
      FixtureBuffer index(T8Device->CreateBuffer(BufferType::INDEX, {sizeof(indices), BufferUsage::DEFAULT}, indices.data()));
      FixtureBuffer constant(T8Device->CreateBuffer(BufferType::CONSTANT, {sizeof(constants), BufferUsage::DINAMIC}, &constants));
      std::array<uint8_t, 16> red{255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255};
      std::array<uint8_t, 16> blue{0, 0, 255, 255, 0, 0, 128, 255, 0, 0, 128, 255, 0, 0, 255, 255};
      const int redTexture = driver->CreateTextureFromMemory("fixture-red", red.data(), 2, 2, 4);
      const int blueTexture = driver->CreateTextureFromMemory("fixture-blue", blue.data(), 2, 2, 4);
      for (const auto texture : {redTexture, blueTexture}) {
        auto* resource = driver->GetTexture(texture);
        resource->params = CLAMP_TO_EDGE | NEAREST_FILTER;
        resource->SetTextureParams();
      }
      std::string vertexSource;
      std::string fragmentSource;
      Require(ResourceLocator::Instance().ReadText("Shaders/VS.hlsl", vertexSource)
        && ResourceLocator::Instance().ReadText("Shaders/FS.hlsl", fragmentSource), "Missing fixture shader sources");
      const int shaderIndex = driver->CreateShader(vertexSource, fragmentSource, ShaderKey(ShaderKey::HAS_NORMALS | ShaderKey::HAS_TEXCOORD0), "Shaders/VS.hlsl", "Shaders/FS.hlsl");
      Require(shaderIndex >= 0, "Fixture shader creation failed");
      auto* shader = driver->GetShaderIdx(shaderIndex);
      Require(shader != nullptr, "Fixture shader unavailable");
      if (!driver->SupportsComparisonSamplers()) {
        const auto comparisonPath = output / ("comparison-" + tag + ".hlsl");
        auto wgslPath = comparisonPath;
        wgslPath.replace_extension(".wgsl");
        const std::string comparisonSource = R"(
Texture2D<float> sourceDepth : register(t0);
SamplerComparisonState comparisonSampler : register(s0);
float4 FS(float4 position : SV_POSITION) : SV_TARGET {
  float value = sourceDepth.SampleCmpLevelZero(comparisonSampler, position.xy * 0.001, 0.5);
  return float4(value, value, value, 1);
})";
        { std::ofstream file(comparisonPath); file << comparisonSource; }
        { std::ofstream file(wgslPath); file << R"(
@group(0) @binding(0) var sourceDepth: texture_depth_2d;
@group(0) @binding(32) var comparisonSampler: sampler_comparison;
@fragment fn FS(@builtin(position) position: vec4<f32>) -> @location(0) vec4<f32> {
  let value = textureSampleCompareLevel(sourceDepth, comparisonSampler, position.xy * 0.001, 0.5);
  return vec4<f32>(value, value, value, 1.0);
})"; }
        Require(driver->CreateShader(vertexSource, comparisonSource, ShaderKey(), "Shaders/VS.hlsl",
                                     comparisonPath.generic_string()) == -1,
                "Unsupported comparison sampler did not fail at shader load");
        std::cout << "Comparison sampler load rejection PASS: api=" << tag << '\n';
      }
      driver->BuildPipelineObjects();
      for (const auto dimensions : {std::array<int, 2>{320, 240}, {257, 193}}) {
        Require(driver->ResizeSwapchain(dimensions[0], dimensions[1]), "Resize failed");
        const int target = driver->CreateRT(1, BaseRT::RGBA8, BaseRT::F32, dimensions[0], dimensions[1]);
        for (unsigned frame = 0; frame < 12; ++frame) {
          const bool offscreen = frame % 3 == 2;
          driver->BeginFrame(offscreen ? BaseDriver::FrameTargetMode::Offscreen : BaseDriver::FrameTargetMode::Swapchain);
          driver->PushRT(target);
          driver->ClearWithColor(0.04f, 0.07f, 0.12f, 1);
          driver->SetCullFace(frame == 1 ? BaseDriver::FRONT_FACES : BaseDriver::FRONT_AND_BACK);
          driver->SetBlendState(BaseDriver::BLEND_OPAQUE);
          driver->SetDepthStencilState(BaseDriver::READ_WRITE);
          shader->Set(*T8DeviceContext);
          T8DeviceContext->SetPrimitiveTopology(Topology::TRIANLE_LIST);
          index->UpdateFromSystemCopy(*T8DeviceContext);
          static_cast<IndexBuffer*>(index.get())->Set(*T8DeviceContext, 0, IndexBufferFormat::R32);
          constants = {identity, identity};
          vertex->UpdateFromBuffer(*T8DeviceContext, nearVertices.data());
          constant->UpdateFromBuffer(*T8DeviceContext, &constants);
          static_cast<VertexBuffer*>(vertex.get())->Set(*T8DeviceContext, sizeof(FixtureVertex), 0);
          static_cast<ConstantBuffer*>(constant.get())->Set(*T8DeviceContext, 0);
          driver->GetTexture(redTexture)->Set(*T8DeviceContext, 0, "TextureRGB");
          driver->GetTexture(redTexture)->SetSampler(*T8DeviceContext, 0);
          T8DeviceContext->DrawIndexed(3, 0, 0);
          constants.wvp[12] = 0.05f;
          vertex->UpdateFromBuffer(*T8DeviceContext, farVertices.data());
          constant->UpdateFromBuffer(*T8DeviceContext, &constants);
          static_cast<VertexBuffer*>(vertex.get())->Set(*T8DeviceContext, sizeof(FixtureVertex), 0);
          static_cast<ConstantBuffer*>(constant.get())->Set(*T8DeviceContext, 0);
          driver->GetTexture(blueTexture)->Set(*T8DeviceContext, 0, "TextureRGB");
          driver->GetTexture(blueTexture)->SetSampler(*T8DeviceContext, 0);
          T8DeviceContext->DrawIndexed(3, 0, 0);
          if (frame == 7) {
            for (unsigned snapshot = 0; snapshot < 5000; ++snapshot) {
              constants.wvp[12] = snapshot % 2 ? 0.05f : -0.05f;
              constant->UpdateFromBuffer(*T8DeviceContext, &constants);
              static_cast<ConstantBuffer*>(constant.get())->Set(*T8DeviceContext, 0);
              T8DeviceContext->DrawIndexed(3, 0, 0);
            }
          }
          if (!offscreen) {
            driver->PopRT();
            driver->ClearWithColor(0, 0, 0, 1);
            driver->SetDepthStencilState(BaseDriver::NONE);
            constants = {identity, identity};
            vertex->UpdateFromBuffer(*T8DeviceContext, screenVertices.data());
            constant->UpdateFromBuffer(*T8DeviceContext, &constants);
            static_cast<VertexBuffer*>(vertex.get())->Set(*T8DeviceContext, sizeof(FixtureVertex), 0);
            static_cast<ConstantBuffer*>(constant.get())->Set(*T8DeviceContext, 0);
            auto* rendered = driver->GetRTTexture(target, 0);
            rendered->Set(*T8DeviceContext, 0, "TextureRGB");
            rendered->SetSampler(*T8DeviceContext, 0);
            T8DeviceContext->DrawIndexed(6, 0, 0);
            if (frame == 1) {
              const auto liveCapture = output / (std::to_string(run) + "-" + tag + "-live-" + std::to_string(dimensions[0]));
              driver->SaveScreenshot(liveCapture.string());
              ReadImage(liveCapture.string() + ".ppm");
              T8DeviceContext->DrawIndexed(6, 0, 0);
            }
          }
          driver->EndFrame();
          driver->CompleteFrame(offscreen ? BaseDriver::FrameCompletionMode::SubmitNoPresent : BaseDriver::FrameCompletionMode::Present);
          if (frame == 7) {
            driver->WaitForGPU();
            const auto rollover = output / (std::to_string(run) + "-" + tag + "-rollover-" + std::to_string(dimensions[0]));
            driver->SaveRTToFile(target, 0, rollover.string());
            result.images.push_back(ReadImage(rollover.string() + ".ppm"));
          }
          if (frame == 11) driver->WaitForGPU();
          MSG message{};
          while (PeekMessageW(&message, window, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        const auto path = output / (std::to_string(run) + "-" + tag + "-" + std::to_string(dimensions[0]));
        driver->SaveRTToFile(target, 0, path.string());
        result.images.push_back(ReadImage(path.string() + ".ppm"));
        driver->DestroyRT(target);
      }
      if (flow != webgpu::ShaderFlow::Wgsl) {
        Require(driver->SupportsDeferredRendering(), "MRT-capable driver must report deferred rendering support");
        const std::string mrtVertex = "float4 VS(float4 position : POSITION) : SV_POSITION { return position; }";
        const std::string mrtFragment = R"(
struct Output {
  float4 color0 : SV_TARGET0; float4 color1 : SV_TARGET1; float4 color2 : SV_TARGET2;
  float4 color3 : SV_TARGET3; float4 color4 : SV_TARGET4; float4 color5 : SV_TARGET5; float4 color6 : SV_TARGET6;
};
Output FS() {
  Output result;
  result.color0 = float4(0.25, 0.5, 0.75, 1); result.color1 = float4(-0.5, 2, 0.125, 1);
  result.color2 = float4(0.5, 0, 0, 1); result.color3 = float4(-0.25, 0, 0, 1);
  result.color4 = float4(2, -0.5, 0.25, 1); result.color5 = float4(0.75, 0.5, 0.25, 1);
  result.color6 = float4(0.125, 0.25, 0.5, 1);
  return result;
})";
        const int mrtShaderIndex = driver->CreateShader(mrtVertex, mrtFragment);
        const std::array<std::array<float, 4>, 7> expected{{
          {{0.25f, 0.5f, 0.75f, 1}}, {{-0.5f, 2, 0.125f, 1}}, {{0.5f, 0, 0, 1}}, {{-0.25f, 0, 0, 1}},
          {{2, -0.5f, 0.25f, 1}}, {{0.75f, 0.5f, 0.25f, 1}}, {{0.125f, 0.25f, 0.5f, 1}}
        }};
        std::vector<int> formats{BaseRT::RGBA8, BaseRT::RGBA16F, BaseRT::R8, BaseRT::F16, BaseRT::RGBA32F, BaseRT::RGBA8, BaseRT::RGBA16F};
        for (unsigned variant = 0; variant < 2; ++variant) {
          const int target = driver->CreateRT(7, formats, BaseRT::F32, 17, 11);
          driver->BeginFrame(BaseDriver::FrameTargetMode::Offscreen);
          driver->PushRT(target);
          driver->ClearWithColor(1, 1, 1, 1);
          driver->PushRT(target);
          driver->CompleteFrame(BaseDriver::FrameCompletionMode::SubmitNoPresent);
          driver->WaitForGPU();
          for (unsigned attachment = 0; attachment < formats.size(); ++attachment) {
            float cleared[4]{};
            Require(driver->ReadRTColorFloat(target, attachment, cleared), "MRT clear readback failed");
            const bool single = formats[attachment] == BaseRT::R8 || formats[attachment] == BaseRT::F16;
            Require(cleared[0] == 0 && cleared[1] == 0 && cleared[2] == 0 && cleared[3] == (single ? 1.0f : 0.0f), "MRT bind did not clear all channels to zero");
          }
          driver->BeginFrame(BaseDriver::FrameTargetMode::Offscreen);
          driver->PushRTLoad(target);
          if (api == GraphicsApi::WEBGPU) {
            bool rejected = false;
            try { driver->GetRTTexture(target, 6)->Set(*T8DeviceContext, 31, "alias-test"); }
            catch (const std::exception&) { rejected = true; }
            Require(rejected, "Sampling a nonzero active color attachment was accepted");
          }
          driver->SetBlendState(BaseDriver::BLEND_OPAQUE);
          driver->SetDepthStencilState(BaseDriver::READ_WRITE);
          driver->SetCullFace(BaseDriver::FRONT_AND_BACK);
          driver->GetShaderIdx(mrtShaderIndex)->Set(*T8DeviceContext);
          vertex->UpdateFromBuffer(*T8DeviceContext, screenVertices.data());
          static_cast<VertexBuffer*>(vertex.get())->Set(*T8DeviceContext, sizeof(FixtureVertex), 0);
          static_cast<IndexBuffer*>(index.get())->Set(*T8DeviceContext, 0, IndexBufferFormat::R32);
          T8DeviceContext->SetPrimitiveTopology(Topology::TRIANLE_LIST);
          T8DeviceContext->DrawIndexed(6, 0, 0);
          driver->CompleteFrame(BaseDriver::FrameCompletionMode::SubmitNoPresent);
          driver->WaitForGPU();
          driver->BeginFrame(BaseDriver::FrameTargetMode::Offscreen);
          driver->PushRTLoad(target);
          driver->CompleteFrame(BaseDriver::FrameCompletionMode::SubmitNoPresent);
          driver->WaitForGPU();
          for (unsigned attachment = 0; attachment < formats.size(); ++attachment) {
            float actual[4]{};
            Require(driver->ReadRTColorFloat(target, attachment, actual), "Mixed-format readback failed");
            for (unsigned channel = 0; channel < 4; ++channel) {
              const bool single = formats[attachment] == BaseRT::R8 || formats[attachment] == BaseRT::F16;
              float value = single ? (channel == 3 ? 1.0f : expected[attachment][0]) : expected[attachment][channel];
              if (formats[attachment] == BaseRT::RGBA8 || formats[attachment] == BaseRT::R8) value = std::clamp(value, 0.0f, 1.0f);
              Require(std::isfinite(actual[channel]) && std::abs(actual[channel] - value) < 0.004f,
                "Mixed-format output mismatch at attachment " + std::to_string(attachment));
            }
          }
          driver->SaveRTToFile(target, BaseDriver::DEPTH_ATTACHMENT, (output / (tag + "-mrt-depth-" + std::to_string(variant))).string());
          driver->DestroyRT(target);
          std::reverse(formats.begin(), formats.end());
        }
        std::cout << "Mixed-format MRT draw/readback PASS: seven attachments, HDR/single-channel values, zero clears, load preservation, reversed format order\n";
        const int depthShader = driver->CreateShader(mrtVertex, "float FS() : SV_DEPTH { return 0.625; }");
        const int depthTarget = driver->CreateRT(0, BaseRT::NOTHING, BaseRT::F32, 17, 11);
        driver->BeginFrame(BaseDriver::FrameTargetMode::Offscreen);
        driver->PushRT(depthTarget);
        driver->SetDepthStencilState(BaseDriver::READ_WRITE);
        driver->SetBlendState(BaseDriver::BLEND_OPAQUE);
        driver->GetShaderIdx(depthShader)->Set(*T8DeviceContext);
        static_cast<VertexBuffer*>(vertex.get())->Set(*T8DeviceContext, sizeof(FixtureVertex), 0);
        static_cast<IndexBuffer*>(index.get())->Set(*T8DeviceContext, 0, IndexBufferFormat::R32);
        T8DeviceContext->SetPrimitiveTopology(Topology::TRIANLE_LIST);
        T8DeviceContext->DrawIndexed(6, 0, 0);
        driver->CompleteFrame(BaseDriver::FrameCompletionMode::SubmitNoPresent);
        driver->WaitForGPU();
        const auto depthPath = output / (std::to_string(run) + "-" + tag + "-depth-only");
        driver->SaveRTToFile(depthTarget, BaseDriver::DEPTH_ATTACHMENT, depthPath.string());
        auto depthImage = ReadImage(depthPath.string() + ".ppm", false);
        Require(std::all_of(depthImage.pixels.begin(), depthImage.pixels.end(), [](uint8_t value) { return value == 159; }), "Depth-only fragment output mismatch");
        result.images.push_back(std::move(depthImage));
        const int depthRampShader = driver->CreateShader(mrtVertex,
          "float FS(float4 position : SV_POSITION) : SV_DEPTH { return 0.125 + floor(position.x) / 32.0 + floor(position.y) / 64.0; }");
        const int depthSampleShader = driver->CreateShader(mrtVertex, R"(
Texture2D<float> sourceDepth : register(t0);
SamplerState sourceSampler : register(s0);
float4 FS(float4 position : SV_POSITION) : SV_TARGET {
  float value = sourceDepth.SampleLevel(sourceSampler, (position.xy + 0.25) / float2(17, 11), 0);
  return float4(value, value, value, 1);
})");
        const int depthSampleTarget = driver->CreateRT(1, BaseRT::RGBA8, BaseRT::NOTHING, 17, 11);
        std::vector<float> floatData(17 * 11 * 4, 0.0f);
        const int floatTexture = driver->CreateFloatTexture(17, 11, floatData.data());
        for (unsigned sampleCase = 0; sampleCase < 5; ++sampleCase) {
          if (sampleCase == 2) {
            driver->BeginFrame(BaseDriver::FrameTargetMode::Offscreen);
            driver->PushRT(depthTarget);
            driver->CompleteFrame(BaseDriver::FrameCompletionMode::SubmitNoPresent);
          }
          if (sampleCase == 3) {
            driver->BeginFrame(BaseDriver::FrameTargetMode::Offscreen);
            driver->PushRT(depthTarget);
            driver->SetScissorRect(0, 0, 17, 11);
            driver->SetDepthStencilState(BaseDriver::READ_WRITE);
            driver->GetShaderIdx(depthRampShader)->Set(*T8DeviceContext);
            static_cast<VertexBuffer*>(vertex.get())->Set(*T8DeviceContext, sizeof(FixtureVertex), 0);
            static_cast<IndexBuffer*>(index.get())->Set(*T8DeviceContext, 0, IndexBufferFormat::R32);
            T8DeviceContext->SetPrimitiveTopology(Topology::TRIANLE_LIST);
            T8DeviceContext->DrawIndexed(6, 0, 0);
            driver->CompleteFrame(BaseDriver::FrameCompletionMode::SubmitNoPresent);
          }
          auto* sampledDepth = sampleCase == 4 ? driver->GetTexture(floatTexture) : driver->GetRTTexture(depthTarget, BaseDriver::DEPTH_ATTACHMENT);
          if (sampleCase == 4) {
            std::fill(floatData.begin(), floatData.end(), 0.375f);
            sampledDepth->UpdateFloatData(*T8DeviceContext, 17, 11, floatData.data());
          }
          sampledDepth->params = CLAMP_TO_EDGE | (sampleCase == 0 ? NEAREST_FILTER : LINEAR_FILTER);
          sampledDepth->SetTextureParams();
          driver->BeginFrame(BaseDriver::FrameTargetMode::Offscreen);
          driver->PushRT(depthSampleTarget);
          driver->SetScissorRect(0, 0, 8, 11);
          driver->SetDepthStencilState(BaseDriver::NONE);
          driver->GetShaderIdx(depthSampleShader)->Set(*T8DeviceContext);
          static_cast<VertexBuffer*>(vertex.get())->Set(*T8DeviceContext, sizeof(FixtureVertex), 0);
          static_cast<IndexBuffer*>(index.get())->Set(*T8DeviceContext, 0, IndexBufferFormat::R32);
          T8DeviceContext->SetPrimitiveTopology(Topology::TRIANLE_LIST);
          sampledDepth->Set(*T8DeviceContext, 0, "sourceDepth");
          sampledDepth->SetSampler(*T8DeviceContext, 0);
          T8DeviceContext->DrawIndexed(6, 0, 0);
          driver->CompleteFrame(BaseDriver::FrameCompletionMode::SubmitNoPresent);
          driver->WaitForGPU();
          const auto path = output / (std::to_string(run) + "-" + tag + "-depth-sample-" + std::to_string(sampleCase));
          driver->SaveRTToFile(depthSampleTarget, 0, path.string());
          auto image = ReadImage(path.string() + ".ppm", false);
          for (unsigned row = 0; row < image.height; ++row) {
            for (unsigned column = 0; column < image.width; ++column) {
              int value = sampleCase < 2 && column < 8 ? 159 : 0;
              if (sampleCase == 3 && column < 8)
                value = static_cast<int>((0.125f + (column + 0.25f) / 32.0f + (std::min(row + 0.25f, 10.0f)) / 64.0f) * 255.0f);
              if (sampleCase == 4 && column < 8) value = 95;
              for (unsigned channel = 0; channel < 3; ++channel)
                Require(std::abs(int(image.pixels[(row * image.width + column) * 3 + channel]) - value) <= 1,
                  "Depth sampling failed: case=" + std::to_string(sampleCase) + " pixel=" + std::to_string(column) + "," + std::to_string(row)
                  + " expected=" + std::to_string(value) + " actual=" + std::to_string(image.pixels[(row * image.width + column) * 3 + channel]));
            }
          }
          result.images.push_back(std::move(image));
        }
        driver->DestroyRT(depthSampleTarget);
        driver->DestroyRT(depthTarget);
        driver->DestroyTexture(floatTexture);
        std::cout << "Depth sampling, interpolated ramp and float texture update PASS\n";
      } else {
        std::cout << "Mixed-format inline-HLSL regression skipped in strict WGSL mode; use auto or spirv\n";
      }
      driver->FlushGPUResources();
    }
    driver->DestroyDriver();
  } catch (...) {
    try { driver->DestroyDriver(); } catch (...) {}
    g_pBaseDriver = nullptr;
    ClearEngineContext();
    throw;
  }
  g_pBaseDriver = nullptr;
  ClearEngineContext();
  std::cout << "Graphics fixture PASS: api=" << tag << " indexed/textured/depth-tested offscreen draw, presentation, resize and capture\n";
  return result;
}
}

int RunGraphicsFixture(int argc, char** argv) {
  GraphicsApi::E api = GraphicsApi::WEBGPU;
  webgpu::ShaderFlow flow = webgpu::ShaderFlow::Auto;
  bool compare = false;
  auto output = std::filesystem::current_path() / "graphics-fixture" / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  HWND window = nullptr;
  const auto application = GetModuleHandle(nullptr);
  try {
    for (int argument = 1; argument < argc; ++argument) {
      const std::string option = argv[argument];
      if (option == "--help" || option == "-h") {
        std::cout << "DayScene --graphics-fixture [--api webgpu|d3d12] [--shaderFlow auto|wgsl|spirv] [--compare] [--output directory]\n";
        return 0;
      }
      if (option == "--graphics-fixture") continue;
      if (option == "--compare") { compare = true; continue; }
      Require(argument + 1 < argc, "Missing fixture option value");
      const std::string value = argv[++argument];
      if (option == "--api") {
        Require(value == "webgpu" || value == "d3d12", "Fixture currently supports webgpu or d3d12");
        api = value == "webgpu" ? GraphicsApi::WEBGPU : GraphicsApi::D3D12;
      } else if (option == "--shaderFlow") Require(webgpu::ParseShaderFlow(value, flow), "Invalid shader flow");
      else if (option == "--output") output = std::filesystem::absolute(value);
      else throw std::runtime_error("Unknown fixture option: " + option);
    }
    std::filesystem::create_directories(output);
    ResourceLocator::Instance().SetBasePath(std::filesystem::current_path());
    ResourceLocator::Instance().SetCachePath(output / "cache");
    Log::Init(Log::LVL_INFO, Log::T8_LOG_BACKEND_CONSOLE, nullptr);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = application;
    windowClass.lpszClassName = L"T850GraphicsFixture";
    Require(RegisterClassW(&windowClass) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS, "Window class failed");
    window = CreateWindowW(windowClass.lpszClassName, L"T850 Integrated Graphics Fixture", WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, nullptr, nullptr, application, nullptr);
    Require(window != nullptr, "Window creation failed");
    ShowWindow(window, SW_SHOWNOACTIVATE);
    std::vector<GraphicsApi::E> sequence = compare ? std::vector<GraphicsApi::E>{GraphicsApi::D3D12, GraphicsApi::WEBGPU, GraphicsApi::D3D12} : std::vector<GraphicsApi::E>{api};
    std::vector<FixtureImage> baseline;
    uint64_t baselineLuid = 0;
    for (unsigned run = 0; run < sequence.size(); ++run) {
      const auto result = RunDriver(sequence[run], flow, window, output, run);
      const auto& images = result.images;
      if (baseline.empty()) { baseline = images; baselineLuid = result.adapterLuid; }
      else {
        Require(result.adapterLuid == baselineLuid, "Native and WebGPU selected different adapters");
        for (size_t image = 0; image < images.size(); ++image) {
          Require(images[image].width == baseline[image].width && images[image].height == baseline[image].height, "Capture dimensions differ");
          size_t differing = 0;
          for (size_t channel = 0; channel < images[image].pixels.size(); ++channel)
            differing += std::abs(int(images[image].pixels[channel]) - int(baseline[image].pixels[channel])) > 2;
          Require(differing == 0, "Native/WebGPU fixture capture mismatch: channels=" + std::to_string(differing));
          std::cout << "Capture comparison PASS: " << images[image].width << 'x' << images[image].height << " channelsOutsideTolerance=" << differing << '\n';
        }
      }
    }
    DestroyWindow(window);
    UnregisterClassW(L"T850GraphicsFixture", application);
    Log::Shutdown();
    std::cout << "Graphics fixture complete: " << output.string() << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Graphics fixture FAILED: " << error.what() << '\n';
    if (window) DestroyWindow(window);
    UnregisterClassW(L"T850GraphicsFixture", application);
    Log::Shutdown();
    return 1;
  }
}
}
#endif