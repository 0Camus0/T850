#include <pch.h>
/*********************************************************
 * T850 Engine — D3D12 Ray Tracing
 * D3D12TLAS.cpp: Top-level acceleration structure build
 *********************************************************/

#include <video/d3d12/D3D12Driver.h>

#ifdef OS_WINDOWS

#include <utils/Log.h>
#include <cstring>

namespace t850 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  static ID3D12Device5* GetNativeDevice5() {
    return static_cast<D3D12Device*>(T8Device)->GetNativeDevice();
  }
  static D3D12Driver* GetDriver() { return static_cast<D3D12Driver*>(g_pBaseDriver); }

  // ─────────────────────────────────────────────────────
  void D3D12TLAS::EnsureBuffers(uint32_t instanceCount) {
    ID3D12Device5* device = GetNativeDevice5();
    if (!device) return;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
                   D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    inputs.NumDescs = instanceCount;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

    auto CreateUAVBuffer = [&](UINT64 size) -> ComPtr<ID3D12Resource> {
      D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
      D3D12_RESOURCE_DESC rd = {};
      rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      rd.Width = size; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
      rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
      ComPtr<ID3D12Resource> buf;
      HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&buf));
      if (FAILED(hr)) T8_LOG_ERROR("[TLAS] Buffer alloc failed hr=0x%08X", hr);
      return buf;
    };

    if (!m_resultBuffer) {
      m_resultBuffer = CreateUAVBuffer(info.ResultDataMaxSizeInBytes);
      if (!m_resultBuffer) return;
      m_resultGPUVA = m_resultBuffer->GetGPUVirtualAddress();

      // Create SRV in the CBV_SRV_UAV_VISIBLE heap so shaders can bind it
      auto& heap = GetDriver()->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE);
      m_srvHeapIndex = heap.GetCurrentIndex();
      D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap.AllocateCPU();
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
      srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srvDesc.RaytracingAccelerationStructure.Location = m_resultGPUVA;
      device->CreateShaderResourceView(nullptr, &srvDesc, cpu);
    }
    if (!m_scratchBuffer) {
      m_scratchBuffer = CreateUAVBuffer(info.ScratchDataSizeInBytes);
    }

    // Instance buffer (upload heap, CPU-writable each frame)
    if (!m_instanceBuffer) {
      UINT64 instBufSize = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * m_maxInstances;
      D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
      D3D12_RESOURCE_DESC rd = {};
      rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      rd.Width = instBufSize; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
      rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_instanceBuffer));
      if (FAILED(hr)) { T8_LOG_ERROR("[TLAS] Instance buffer alloc failed hr=0x%08X", hr); return; }
      m_instanceBuffer->Map(0, nullptr, &m_instanceMapped);
    }

    m_initialized = true;
  }

  void D3D12TLAS::Build(const RTInstanceDesc* instances, uint32_t instanceCount) {
    if (instanceCount > m_maxInstances) instanceCount = m_maxInstances;

    EnsureBuffers(instanceCount);
    if (!m_initialized) return;

    // Copy instance descriptors into the upload buffer.
    // RTInstanceDesc is layout-compatible with D3D12_RAYTRACING_INSTANCE_DESC
    // (identical memory layout: 3×4 float transform, flags, GPU VA).
    std::memcpy(m_instanceMapped, instances,
                sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instanceCount);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
                   D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    inputs.NumDescs = instanceCount;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.InstanceDescs = m_instanceBuffer->GetGPUVirtualAddress();

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.DestAccelerationStructureData = m_resultGPUVA;
    buildDesc.ScratchAccelerationStructureData = m_scratchBuffer->GetGPUVirtualAddress();

    auto* cmd = static_cast<ID3D12GraphicsCommandList4*>(GetDriver()->GetCmdList());
    cmd->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = m_resultBuffer.Get();
    cmd->ResourceBarrier(1, &barrier);

    T8_LOG_TRACE("[TLAS] Built (%u instances)", instanceCount);
  }

  void D3D12TLAS::Destroy() {
    if (m_instanceMapped && m_instanceBuffer) {
      m_instanceBuffer->Unmap(0, nullptr);
      m_instanceMapped = nullptr;
    }
    m_resultBuffer.Reset();
    m_scratchBuffer.Reset();
    m_instanceBuffer.Reset();
    m_resultGPUVA = 0;
    m_initialized = false;
  }

  uint64_t D3D12TLAS::GetGPUAddress() const {
    return m_resultGPUVA;
  }

} // namespace t850

#endif // OS_WINDOWS
