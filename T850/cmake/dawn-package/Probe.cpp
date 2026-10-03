#include <webgpu/webgpu_cpp.h>

#include <atomic>
#include <iostream>
#include <memory>
#include <string_view>

namespace {
std::atomic<unsigned> errors{0};

std::string_view Message(wgpu::StringView text) {
  if (!text.data) return {};
  return text.length == WGPU_STRLEN ? std::string_view(text.data)
                                   : std::string_view(text.data, text.length);
}

bool Wait(const wgpu::Instance& instance, wgpu::Future future, const char* operation) {
  const auto status = instance.WaitAny(future, 30'000'000'000ULL);
  if (status == wgpu::WaitStatus::Success) return true;
  std::cerr << operation << " wait failed: " << static_cast<unsigned>(status) << '\n';
  return false;
}
}

int main() {
  wgpu::InstanceFeatureName feature = wgpu::InstanceFeatureName::TimedWaitAny;
  wgpu::InstanceDescriptor instanceDesc{};
  instanceDesc.requiredFeatureCount = 1;
  instanceDesc.requiredFeatures = &feature;
  auto instance = wgpu::CreateInstance(&instanceDesc);
  if (!instance) return 1;

  auto adapter = std::make_shared<wgpu::Adapter>();
  wgpu::RequestAdapterOptions options{};
  options.backendType = wgpu::BackendType::D3D12;
  options.featureLevel = wgpu::FeatureLevel::Core;
  options.powerPreference = wgpu::PowerPreference::HighPerformance;
  const auto adapterFuture = instance.RequestAdapter(
    &options, wgpu::CallbackMode::WaitAnyOnly,
    [adapter](wgpu::RequestAdapterStatus status, wgpu::Adapter result, wgpu::StringView message) {
      if (status == wgpu::RequestAdapterStatus::Success) *adapter = std::move(result);
      else std::cerr << "RequestAdapter failed: " << Message(message) << '\n';
    });
  if (!Wait(instance, adapterFuture, "adapter") || !*adapter) return 1;

  wgpu::AdapterInfo info{};
  if (adapter->GetInfo(&info) != wgpu::Status::Success ||
      info.backendType != wgpu::BackendType::D3D12 || info.adapterType == wgpu::AdapterType::CPU) {
    std::cerr << "Expected a hardware D3D12 adapter; fallback rejected.\n";
    return 1;
  }
  wgpu::Limits limits{};
  if (adapter->GetLimits(&limits) != wgpu::Status::Success) return 1;
  std::cout << "provider=Dawn backend=D3D12 adapter=" << Message(info.device)
            << " vendorId=" << info.vendorID << " deviceId=" << info.deviceID << '\n'
            << "adapterLimits: colorAttachments=" << limits.maxColorAttachments
            << " colorBytesPerSample=" << limits.maxColorAttachmentBytesPerSample
            << " texturesPerStage=" << limits.maxSampledTexturesPerShaderStage
            << " samplersPerStage=" << limits.maxSamplersPerShaderStage
            << " uniformAlignment=" << limits.minUniformBufferOffsetAlignment << '\n';

  auto device = std::make_shared<wgpu::Device>();
  wgpu::DeviceDescriptor deviceDesc{};
  deviceDesc.SetUncapturedErrorCallback(
    [](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message) {
      ++errors;
      std::cerr << "Dawn error: " << Message(message) << '\n';
    });
  deviceDesc.SetDeviceLostCallback(wgpu::CallbackMode::AllowProcessEvents,
    [](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message) {
      if (reason != wgpu::DeviceLostReason::Destroyed && reason != wgpu::DeviceLostReason::CallbackCancelled) {
        ++errors;
        std::cerr << "Device lost: " << Message(message) << '\n';
      }
    });
  const auto deviceFuture = adapter->RequestDevice(
    &deviceDesc, wgpu::CallbackMode::WaitAnyOnly,
    [device](wgpu::RequestDeviceStatus status, wgpu::Device result, wgpu::StringView message) {
      if (status == wgpu::RequestDeviceStatus::Success) *device = std::move(result);
      else std::cerr << "RequestDevice failed: " << Message(message) << '\n';
    });
  if (!Wait(instance, deviceFuture, "device") || !*device) return 1;
  {
    auto queue = device->GetQueue();
    auto completed = std::make_shared<bool>(false);
    queue.Submit(0, nullptr);
    const auto work = queue.OnSubmittedWorkDone(wgpu::CallbackMode::WaitAnyOnly,
      [completed](wgpu::QueueWorkDoneStatus status, wgpu::StringView message) {
        *completed = status == wgpu::QueueWorkDoneStatus::Success;
        if (!*completed) std::cerr << "Queue completion failed: " << Message(message) << '\n';
      });
    if (!Wait(instance, work, "queue") || !*completed) return 1;
  }
  device->Destroy();
  instance.ProcessEvents();
  *device = nullptr;
  *adapter = nullptr;
  instance.ProcessEvents();
  instance = nullptr;
  if (errors.load() != 0) return 1;
  std::cout << "Dawn package probe PASS: hardware D3D12 device, queue completion and teardown.\n";
  return 0;
}