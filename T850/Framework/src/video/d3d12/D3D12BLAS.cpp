#include <pch.h>
/*********************************************************
 * T850 Engine — D3D12 Ray Tracing
 * D3D12BLAS.cpp: Bottom-level acceleration structure build
 *********************************************************/

#include <video/d3d12/D3D12Driver.h>

#ifdef OS_WINDOWS

#include <utils/Log.h>

namespace t850 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  static ID3D12Device5* GetNativeDevice5() {
    return static_cast<D3D12Device*>(T8Device)->GetNativeDevice();
  }
  static D3D12Driver* GetDriver() { return static_cast<D3D12Driver*>(g_pBaseDriver); }

  // ─────────────────────────────────────────────────────
  void D3D12BLAS::Build(bool allowUpdate) {
    ID3D12Device5* device = GetNativeDevice5();
    if (!device) return;

    m_buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    if (allowUpdate) {
      m_buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
                     D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    }

    // Describe the geometry
    D3D12_RAYTRACING_GEOMETRY_DESC geoDesc = {};
    geoDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geoDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geoDesc.Triangles.VertexBuffer.StartAddress = vertexGPUVA;
    geoDesc.Triangles.VertexBuffer.StrideInBytes = vertexStride;
    geoDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geoDesc.Triangles.VertexCount = vertexCount;
    geoDesc.Triangles.IndexBuffer = indexGPUVA;
    geoDesc.Triangles.IndexFormat = is32BitIndex ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
    geoDesc.Triangles.IndexCount = indexCount;

    // Query required sizes
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.Flags = m_buildFlags;
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &geoDesc;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

    // Create / reuse result + scratch buffers
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
      if (FAILED(hr)) T8_LOG_ERROR("[BLAS] Buffer alloc failed hr=0x%08X", hr);
      return buf;
    };

    if (!m_resultBuffer || m_resultBuffer->GetDesc().Width < info.ResultDataMaxSizeInBytes) {
      m_resultBuffer = CreateUAVBuffer(info.ResultDataMaxSizeInBytes);
      if (!m_resultBuffer) return;
      m_resultGPUVA = m_resultBuffer->GetGPUVirtualAddress();
    }
    if (!m_scratchBuffer || m_scratchBuffer->GetDesc().Width < info.ScratchDataSizeInBytes) {
      m_scratchBuffer = CreateUAVBuffer(info.ScratchDataSizeInBytes);
      if (!m_scratchBuffer) return;
    }

    // Build
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.DestAccelerationStructureData = m_resultGPUVA;
    buildDesc.ScratchAccelerationStructureData = m_scratchBuffer->GetGPUVirtualAddress();

    auto* cmd = static_cast<ID3D12GraphicsCommandList4*>(GetDriver()->GetCmdList());
    cmd->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    // UAV barrier to ensure the build finishes before the TLAS references this BLAS
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = m_resultBuffer.Get();
    cmd->ResourceBarrier(1, &barrier);

    isBuilt = true;
    T8_LOG_DEBUG("[BLAS] Built (verts=%u, idx=%u, update=%s)", vertexCount, indexCount, allowUpdate?"yes":"no");
  }

  void D3D12BLAS::Refit() {
    if (!isBuilt || !m_resultBuffer) { Build(true); return; }

    // Same as Build but with PERFORM_UPDATE flag and the existing result as source
    D3D12_RAYTRACING_GEOMETRY_DESC geoDesc = {};
    geoDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geoDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geoDesc.Triangles.VertexBuffer.StartAddress = vertexGPUVA;
    geoDesc.Triangles.VertexBuffer.StrideInBytes = vertexStride;
    geoDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geoDesc.Triangles.VertexCount = vertexCount;
    geoDesc.Triangles.IndexBuffer = indexGPUVA;
    geoDesc.Triangles.IndexFormat = is32BitIndex ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
    geoDesc.Triangles.IndexCount = indexCount;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.Flags = m_buildFlags | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &geoDesc;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.DestAccelerationStructureData = m_resultGPUVA;
    buildDesc.SourceAccelerationStructureData = m_resultGPUVA;
    buildDesc.ScratchAccelerationStructureData = m_scratchBuffer->GetGPUVirtualAddress();

    auto* cmd = static_cast<ID3D12GraphicsCommandList4*>(GetDriver()->GetCmdList());
    cmd->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = m_resultBuffer.Get();
    cmd->ResourceBarrier(1, &barrier);
  }

  void D3D12BLAS::Destroy() {
    m_resultBuffer.Reset();
    m_scratchBuffer.Reset();
    m_resultGPUVA = 0;
    isBuilt = false;
  }

  uint64_t D3D12BLAS::GetGPUAddress() const {
    return m_resultGPUVA;
  }

} // namespace t850

#endif // OS_WINDOWS
