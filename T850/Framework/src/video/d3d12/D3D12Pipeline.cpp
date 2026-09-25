#include <pch.h>
#include <debug/RuntimeTelemetry.h>
#include <video/d3d12/D3D12Pipeline.h>

#ifdef OS_WINDOWS

#include <array>
#include <cstring>
#include <vector>

namespace t850 {
namespace {

uint64_t HashBytes(const void* data, size_t size) {
  constexpr uint64_t offset = 1469598103934665603ull;
  constexpr uint64_t prime = 1099511628211ull;
  uint64_t hash = offset;
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * prime;
  return hash;
}

#pragma pack(push, 1)
struct PersistentGraphicsPipelineKey {
  uint32_t magic = 0x50383554u;
  uint16_t version = 1;
  uint16_t type = 1;
  uint64_t vertexFamily = 0;
  uint64_t fragmentFamily = 0;
  uint64_t permutation = 0;
  uint64_t flow = 0;
  uint64_t vertexBytecode = 0;
  uint64_t fragmentBytecode = 0;
  uint8_t blend = 0;
  uint8_t depth = 0;
  uint8_t cull = 0;
  uint8_t topology = 0;
  uint8_t numRTVs = 0;
  uint32_t rtvFormats[8]{};
  uint32_t dsvFormat = 0;
};
#pragma pack(pop)

std::array<uint8_t, sizeof(PersistentGraphicsPipelineKey)> MakePersistentKey(
    const D3D12PipelineKey& key, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) {
  PersistentGraphicsPipelineKey persistent{};
  persistent.vertexFamily = key.program.family.vertex;
  persistent.fragmentFamily = key.program.family.fragment;
  persistent.permutation = key.program.permutation;
  persistent.flow = static_cast<uint64_t>(key.program.flow);
  persistent.vertexBytecode = HashBytes(desc.VS.pShaderBytecode, desc.VS.BytecodeLength);
  persistent.fragmentBytecode = HashBytes(desc.PS.pShaderBytecode, desc.PS.BytecodeLength);
  persistent.blend = key.blend;
  persistent.depth = key.depth;
  persistent.cull = key.cull;
  persistent.topology = key.topology;
  persistent.numRTVs = key.numRTVs;
  for (size_t i = 0; i < key.rtvFormats.size(); ++i) persistent.rtvFormats[i] = key.rtvFormats[i];
  persistent.dsvFormat = key.dsvFormat;
  std::array<uint8_t, sizeof(PersistentGraphicsPipelineKey)> bytes{};
  std::memcpy(bytes.data(), &persistent, sizeof(persistent));
  return bytes;
}

} // namespace

bool D3D12Pipeline::Create(ID3D12Device* device,
                           const D3D12PipelineKey& key,
                           const D3D12_GRAPHICS_PIPELINE_STATE_DESC& descriptor,
                           D3D12ShaderCacheSession* cacheSession) {
  if (!device) return false;
  const auto persistentKey = MakePersistentKey(key, descriptor);
  std::vector<uint8_t> cachedBlob;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC candidate = descriptor;
  if (cacheSession && cacheSession->Find(persistentKey, cachedBlob)) {
    candidate.CachedPSO = {cachedBlob.data(), cachedBlob.size()};
    m_restoredFromSession = true;
  }
  HRESULT hr = T8_TELEMETRY_CALL("pipeline.create.graphics",
    device->CreateGraphicsPipelineState(&candidate, IID_PPV_ARGS(&m_state)));
  if (FAILED(hr) && m_restoredFromSession) {
    cacheSession->RecordRejected();
    m_restoredFromSession = false;
    candidate.CachedPSO = {};
    hr = T8_TELEMETRY_CALL("pipeline.create.graphics",
      device->CreateGraphicsPipelineState(&candidate, IID_PPV_ARGS(&m_state)));
  }
  if (FAILED(hr)) return false;
  if (cacheSession && !m_restoredFromSession) cacheSession->Store(persistentKey, m_state.Get());
  return true;
}

} // namespace t850

#endif