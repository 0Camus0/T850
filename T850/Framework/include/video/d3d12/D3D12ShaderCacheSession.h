#pragma once

#include <Config.h>

#ifdef OS_WINDOWS

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <span>
#include <vector>

namespace t850 {

class D3D12ShaderCacheSession {
public:
  bool Initialize(ID3D12Device* device);
  void Shutdown();

  bool Find(std::span<const uint8_t> key, std::vector<uint8_t>& value);
  bool Store(std::span<const uint8_t> key, ID3D12PipelineState* pipeline);

  bool IsEnabled() const { return static_cast<bool>(m_session); }
  uint64_t Hits() const { return m_hits; }
  uint64_t Misses() const { return m_misses; }
  uint64_t Stores() const { return m_stores; }
  uint64_t Rejected() const { return m_rejected; }
  double LookupMilliseconds() const { return m_lookupMilliseconds; }
  double StoreMilliseconds() const { return m_storeMilliseconds; }
  void RecordRejected() { ++m_rejected; }

private:
  Microsoft::WRL::ComPtr<ID3D12ShaderCacheSession> m_session;
  uint64_t m_hits = 0;
  uint64_t m_misses = 0;
  uint64_t m_stores = 0;
  uint64_t m_rejected = 0;
  double m_lookupMilliseconds = 0.0;
  double m_storeMilliseconds = 0.0;
};

} // namespace t850

#endif