#pragma once

#include <Config.h>

#ifdef OS_WINDOWS

#include <video/d3d12/D3D12PipelineKey.h>
#include <video/d3d12/D3D12PipelineLibrary.h>
#include <video/d3d12/D3D12ShaderCacheSession.h>

#include <d3d12.h>
#include <wrl/client.h>

namespace t850 {

class D3D12Pipeline {
public:
  bool Create(ID3D12Device* device,
              const D3D12PipelineKey& key,
              const D3D12_GRAPHICS_PIPELINE_STATE_DESC& descriptor,
              D3D12PipelineLibrary* pipelineLibrary,
              D3D12ShaderCacheSession* cacheSession);

  ID3D12PipelineState* Get() const { return m_state.Get(); }
  bool RestoredFromSession() const { return m_restoredFromSession; }

private:
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_state;
  bool m_restoredFromSession = false;
};

} // namespace t850

#endif