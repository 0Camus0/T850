#include <pch.h>
#include <core/Config.h>
#include <utils/Log.h>
#include <video/d3d12/D3D12PipelineLibrary.h>
#include <video/d3d12/D3D12ShaderCacheSession.h>

#ifdef OS_WINDOWS

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace t850 {
namespace {

constexpr char kLibrarySessionKey[] = "t850-d3d12-pipeline-library-v1";

bool IsDisabled() {
  const char* value = std::getenv("T850_D3D12_PIPELINE_LIBRARY");
  return !value || (_stricmp(value, "on") != 0 && _stricmp(value, "reset") != 0);
}

bool ShouldReset() {
  const char* value = std::getenv("T850_D3D12_PIPELINE_LIBRARY");
  return value && _stricmp(value, "reset") == 0;
}

uint64_t Hash(std::span<const uint8_t> key, uint64_t seed) {
  constexpr uint64_t prime = 1099511628211ull;
  uint64_t value = seed;
  for (const uint8_t byte : key) value = (value ^ byte) * prime;
  return value;
}

std::span<const uint8_t> LibrarySessionKey() {
  return {reinterpret_cast<const uint8_t*>(kLibrarySessionKey), sizeof(kLibrarySessionKey) - 1};
}

} // namespace

bool D3D12PipelineLibrary::Initialize(ID3D12Device* device, D3D12ShaderCacheSession* session) {
  m_library.Reset();
  m_serializedInput.clear();
  if (!device || IsDisabled()) {
    T8_LOG_INFO("[D3D12] Pipeline library disabled");
    return false;
  }
  Microsoft::WRL::ComPtr<ID3D12Device1> device1;
  if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device1)))) {
    T8_LOG_INFO("[D3D12] ID3D12PipelineLibrary unavailable on this runtime");
    return false;
  }
  D3D12_FEATURE_DATA_SHADER_CACHE support{};
  if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_CACHE, &support, sizeof(support))) ||
      !(support.SupportFlags & D3D12_SHADER_CACHE_SUPPORT_LIBRARY)) {
    T8_LOG_INFO("[D3D12] Pipeline library unsupported by this driver");
    return false;
  }
  if (!ShouldReset() && session) session->Find(LibrarySessionKey(), m_serializedInput);
  const void* serializedData = m_serializedInput.empty() ? nullptr : m_serializedInput.data();
  HRESULT hr = device1->CreatePipelineLibrary(serializedData, m_serializedInput.size(), IID_PPV_ARGS(&m_library));
  if (FAILED(hr) && !m_serializedInput.empty()) {
    ++m_rejected;
    m_serializedInput.clear();
    hr = device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&m_library));
  }
  if (FAILED(hr)) {
    T8_LOG_ERROR("[D3D12] CreatePipelineLibrary failed hr=0x%08X", static_cast<unsigned>(hr));
    return false;
  }
  T8_LOG_INFO("[D3D12] Pipeline library enabled (restored=%d reset=%d)", m_serializedInput.empty() ? 0 : 1, ShouldReset() ? 1 : 0);
  return true;
}

void D3D12PipelineLibrary::Shutdown(D3D12ShaderCacheSession* session) {
  if (!m_library) return;
  const SIZE_T size = m_library->GetSerializedSize();
  std::vector<uint8_t> serialized(size);
  if (session && size > 0 && SUCCEEDED(m_library->Serialize(serialized.data(), serialized.size()))) {
    session->Store(LibrarySessionKey(), serialized);
  }
  T8_LOG_INFO("[D3D12] Pipeline library: hits=%llu misses=%llu stores=%llu rejected=%llu bytes=%llu",
    static_cast<unsigned long long>(m_hits), static_cast<unsigned long long>(m_misses),
    static_cast<unsigned long long>(m_stores), static_cast<unsigned long long>(m_rejected),
    static_cast<unsigned long long>(size));
  if (g_config.flags.compileShaders) {
    std::cout << "[D3D12PipelineLibraryProfile] hits=" << m_hits << " misses=" << m_misses
              << " stores=" << m_stores << " rejected=" << m_rejected << " bytes=" << size << std::endl;
  }
  m_library.Reset();
  m_serializedInput.clear();
  m_hits = m_misses = m_stores = m_rejected = 0;
}

std::wstring D3D12PipelineLibrary::Name(std::span<const uint8_t> key) {
  std::wostringstream stream;
  stream << L"t850-" << std::hex << std::setfill(L'0')
         << std::setw(16) << Hash(key, 1469598103934665603ull)
         << std::setw(16) << Hash(key, 1099511628211ull);
  return stream.str();
}

bool D3D12PipelineLibrary::LoadGraphics(std::span<const uint8_t> key,
                                        const D3D12_GRAPHICS_PIPELINE_STATE_DESC& descriptor,
                                        Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipeline) {
  if (!m_library) return false;
  const std::wstring name = Name(key);
  const HRESULT hr = m_library->LoadGraphicsPipeline(name.c_str(), &descriptor, IID_PPV_ARGS(&pipeline));
  if (SUCCEEDED(hr)) { ++m_hits; return true; }
  pipeline.Reset();
  if (m_misses < 4) T8_LOG_DEBUG("[D3D12] Pipeline library graphics miss hr=0x%08X name=%ls", static_cast<unsigned>(hr), name.c_str());
  ++m_misses;
  return false;
}

bool D3D12PipelineLibrary::LoadCompute(std::span<const uint8_t> key,
                                       const D3D12_COMPUTE_PIPELINE_STATE_DESC& descriptor,
                                       Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipeline) {
  if (!m_library) return false;
  const std::wstring name = Name(key);
  const HRESULT hr = m_library->LoadComputePipeline(name.c_str(), &descriptor, IID_PPV_ARGS(&pipeline));
  if (SUCCEEDED(hr)) { ++m_hits; return true; }
  pipeline.Reset();
  if (m_misses < 4) T8_LOG_DEBUG("[D3D12] Pipeline library compute miss hr=0x%08X name=%ls", static_cast<unsigned>(hr), name.c_str());
  ++m_misses;
  return false;
}

bool D3D12PipelineLibrary::Store(std::span<const uint8_t> key, ID3D12PipelineState* pipeline) {
  if (!m_library || !pipeline) return false;
  const std::wstring name = Name(key);
  const HRESULT hr = m_library->StorePipeline(name.c_str(), pipeline);
  if (FAILED(hr)) return false;
  ++m_stores;
  return true;
}

} // namespace t850

#endif