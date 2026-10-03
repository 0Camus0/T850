#pragma once

#include <Config.h>

#ifdef OS_WINDOWS

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace t850 {

class D3D12ShaderCacheSession;

class D3D12PipelineLibrary {
public:
  bool Initialize(ID3D12Device* device, D3D12ShaderCacheSession* session);
  void Shutdown(D3D12ShaderCacheSession* session);

  bool LoadGraphics(std::span<const uint8_t> key,
                    const D3D12_GRAPHICS_PIPELINE_STATE_DESC& descriptor,
                    Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipeline);
  bool LoadCompute(std::span<const uint8_t> key,
                   const D3D12_COMPUTE_PIPELINE_STATE_DESC& descriptor,
                   Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipeline);
  bool Store(std::span<const uint8_t> key, ID3D12PipelineState* pipeline);

  bool IsEnabled() const { return static_cast<bool>(m_library); }

private:
  static std::wstring Name(std::span<const uint8_t> key);

  Microsoft::WRL::ComPtr<ID3D12PipelineLibrary> m_library;
  // CreatePipelineLibrary retains this pointer instead of copying the bytes.
  std::vector<uint8_t> m_serializedInput;
  uint64_t m_hits = 0;
  uint64_t m_misses = 0;
  uint64_t m_stores = 0;
  uint64_t m_rejected = 0;
};

} // namespace t850

#endif