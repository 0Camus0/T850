/*********************************************************
* T850 Engine — D3D12 Backend
*
* D3D12PipelineKey.h: Pipeline state cache key structs
*********************************************************/

#ifndef T800_D3D12PIPELINEKEY_H
#define T800_D3D12PIPELINEKEY_H

#include <Config.h>

#ifdef OS_WINDOWS

#include <dxgi1_4.h>
#include <cstdint>
#include <functional>
#include <array>

namespace t850 {

  // ══════════════════════════════════════════════════════
  //  D3D12 Pipeline State cache key
  // ══════════════════════════════════════════════════════
  struct D3D12PipelineKey {
    uintptr_t shaderPtr;   // shader object address — unique per shader
    uint8_t  blend;
    uint8_t  depth;
    uint8_t  cull;
    uint8_t  numRTVs;
    std::array<DXGI_FORMAT, 8> rtvFormats;
    DXGI_FORMAT dsvFormat;
    bool operator==(const D3D12PipelineKey& o) const {
      return shaderPtr == o.shaderPtr && blend == o.blend &&
             depth == o.depth && cull == o.cull && numRTVs == o.numRTVs &&
             rtvFormats == o.rtvFormats && dsvFormat == o.dsvFormat;
    }
  };

  struct D3D12PipelineKeyHash {
    size_t operator()(const D3D12PipelineKey& k) const {
      size_t h = std::hash<uintptr_t>()(k.shaderPtr);
      h ^= std::hash<uint8_t>()(k.blend)    << 1;
      h ^= std::hash<uint8_t>()(k.depth)    << 2;
      h ^= std::hash<uint8_t>()(k.cull)     << 3;
      h ^= std::hash<uint8_t>()(k.numRTVs)  << 4;
      for (size_t i = 0; i < k.rtvFormats.size(); ++i)
        h ^= std::hash<uint32_t>()((uint32_t)k.rtvFormats[i]) << (5 + i);
      h ^= std::hash<uint32_t>()((uint32_t)k.dsvFormat) << 13;
      return h;
    }
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_D3D12PIPELINEKEY_H
