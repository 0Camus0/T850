#include <pch.h>
#include <core/Config.h>
#include <debug/RuntimeTelemetry.h>
#include <utils/Log.h>
#include <video/d3d12/D3D12ShaderCacheSession.h>

#ifdef OS_WINDOWS

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace t850 {
namespace {

constexpr GUID kT850PipelineCacheId =
  {0xa2379959, 0xc611, 0x42b7, {0xa8, 0x55, 0x8e, 0x67, 0x52, 0x55, 0x7f, 0xe1}};
constexpr uint64_t kT850PipelineCacheVersion = 1;

bool IsDisabled() {
  const char* value = std::getenv("T850_D3D12_SHADER_CACHE_SESSION");
  return value && _stricmp(value, "off") == 0;
}

bool ShouldReset() {
  const char* value = std::getenv("T850_D3D12_SHADER_CACHE_SESSION");
  return value && _stricmp(value, "reset") == 0;
}

} // namespace

bool D3D12ShaderCacheSession::Initialize(ID3D12Device* device) {
  Shutdown();
  if (!device || IsDisabled()) {
    T8_LOG_INFO("[D3D12] Shader cache session disabled");
    return false;
  }

  Microsoft::WRL::ComPtr<ID3D12Device9> device9;
  if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device9)))) {
    T8_LOG_INFO("[D3D12] ID3D12ShaderCacheSession unavailable on this runtime");
    return false;
  }

  D3D12_SHADER_CACHE_SESSION_DESC desc{};
  desc.Identifier = kT850PipelineCacheId;
  desc.Mode = D3D12_SHADER_CACHE_MODE_DISK;
  desc.Flags = D3D12_SHADER_CACHE_FLAG_DRIVER_VERSIONED;
  desc.MaximumInMemoryCacheSizeBytes = 64u * 1024u * 1024u;
  desc.MaximumInMemoryCacheEntries = 4096;
  desc.MaximumValueFileSizeBytes = 64u * 1024u * 1024u;
  desc.Version = kT850PipelineCacheVersion;

  HRESULT hr = device9->CreateShaderCacheSession(&desc, IID_PPV_ARGS(&m_session));
  if (FAILED(hr)) {
    T8_LOG_ERROR("[D3D12] CreateShaderCacheSession failed hr=0x%08X", static_cast<unsigned>(hr));
    return false;
  }

  if (ShouldReset()) {
    m_session->SetDeleteOnDestroy();
    m_session.Reset();
    hr = device9->CreateShaderCacheSession(&desc, IID_PPV_ARGS(&m_session));
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D12] Shader cache session reset failed hr=0x%08X", static_cast<unsigned>(hr));
      return false;
    }
  }

  T8_LOG_INFO("[D3D12] Shader cache session enabled (disk, driver-versioned, reset=%d)", ShouldReset() ? 1 : 0);
  return true;
}

void D3D12ShaderCacheSession::Shutdown() {
  if (m_session) {
    T8_LOG_INFO("[D3D12] Shader cache session: hits=%llu misses=%llu stores=%llu rejected=%llu lookupMs=%.6f storeMs=%.6f",
      static_cast<unsigned long long>(m_hits), static_cast<unsigned long long>(m_misses),
      static_cast<unsigned long long>(m_stores), static_cast<unsigned long long>(m_rejected),
      m_lookupMilliseconds, m_storeMilliseconds);
    if (g_config.flags.compileShaders) {
      std::cout << "[D3D12ShaderCacheSessionProfile] hits=" << m_hits
                << " misses=" << m_misses << " stores=" << m_stores
                << " rejected=" << m_rejected << " lookupMs=" << m_lookupMilliseconds
                << " storeMs=" << m_storeMilliseconds << std::endl;
    }
  }
  m_session.Reset();
  m_hits = m_misses = m_stores = m_rejected = 0;
  m_lookupMilliseconds = m_storeMilliseconds = 0.0;
}

bool D3D12ShaderCacheSession::Find(std::span<const uint8_t> key, std::vector<uint8_t>& value) {
  value.clear();
  if (!m_session || key.empty() || key.size() > UINT_MAX) return false;
  const auto started = std::chrono::steady_clock::now();
  UINT size = 0;
  HRESULT hr = m_session->FindValue(key.data(), static_cast<UINT>(key.size()), nullptr, &size);
  if (hr == DXGI_ERROR_NOT_FOUND || hr == DXGI_ERROR_CACHE_HASH_COLLISION || size == 0) {
    m_lookupMilliseconds += std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - started).count();
    ++m_misses;
    T8_TELEMETRY_ADD("d3d12.shader_cache_session.misses", 1);
    return false;
  }
  if (FAILED(hr) && size == 0) {
    m_lookupMilliseconds += std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - started).count();
    ++m_misses;
    return false;
  }
  value.resize(size);
  hr = m_session->FindValue(key.data(), static_cast<UINT>(key.size()), value.data(), &size);
  if (FAILED(hr)) {
    m_lookupMilliseconds += std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - started).count();
    value.clear();
    ++m_misses;
    return false;
  }
  value.resize(size);
  ++m_hits;
  T8_TELEMETRY_ADD("d3d12.shader_cache_session.hits", 1);
  const double lookupMs = std::chrono::duration<double, std::milli>(
    std::chrono::steady_clock::now() - started).count();
  m_lookupMilliseconds += lookupMs;
  T8_TELEMETRY_ADD("d3d12.shader_cache_session.lookup_ms", lookupMs);
  return true;
}

bool D3D12ShaderCacheSession::Store(std::span<const uint8_t> key, ID3D12PipelineState* pipeline) {
  if (!m_session || key.empty() || key.size() > UINT_MAX || !pipeline) return false;
  Microsoft::WRL::ComPtr<ID3DBlob> blob;
  if (FAILED(pipeline->GetCachedBlob(&blob)) || !blob || blob->GetBufferSize() > UINT_MAX) return false;
  const auto started = std::chrono::steady_clock::now();
  const HRESULT hr = m_session->StoreValue(key.data(), static_cast<UINT>(key.size()),
    blob->GetBufferPointer(), static_cast<UINT>(blob->GetBufferSize()));
  if (FAILED(hr)) return false;
  m_storeMilliseconds += std::chrono::duration<double, std::milli>(
    std::chrono::steady_clock::now() - started).count();
  ++m_stores;
  T8_TELEMETRY_ADD("d3d12.shader_cache_session.stores", 1);
  return true;
}

} // namespace t850

#endif