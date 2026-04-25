#include <pch.h>
/*********************************************************
* T850 Engine — D3D12 Backend
* D3D12DeviceContext.cpp: Device context implementation
*********************************************************/

#include <video/d3d12/D3D12DeviceContext.h>

#ifdef OS_WINDOWS

#include <utils/Log.h>
#include <debug/T8_Profiler.h>

namespace t800 {

  // ══════════════════════════════════════════════════════
  //  D3D12DeviceContext
  // ══════════════════════════════════════════════════════

  void* D3D12DeviceContext::GetAPIObject() const { return (void*)m_commandList.Get(); }
  void** D3D12DeviceContext::GetAPIObjectReference() const { return nullptr; }
  void D3D12DeviceContext::release() { m_commandList.Reset(); }

  void D3D12DeviceContext::SetPrimitiveTopology(T8_TOPOLOGY::E topology) {
    D3D12_PRIMITIVE_TOPOLOGY t;
    switch (topology) {
      case T8_TOPOLOGY::POINT_LIST:     t = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;    break;
      case T8_TOPOLOGY::LINE_LIST:      t = D3D_PRIMITIVE_TOPOLOGY_LINELIST;     break;
      case T8_TOPOLOGY::LINE_STRIP:     t = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;    break;
      case T8_TOPOLOGY::TRIANLE_LIST:   t = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
      case T8_TOPOLOGY::TRIANGLE_STRIP: t = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;break;
      default: t = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
    }
    m_commandList->IASetPrimitiveTopology(t);
  }

  void D3D12DeviceContext::DrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex) {
    T8_LOG_TRACE("[D3D12] DrawIndexed(%u, %u, %u)", vertexCount, startIndex, startVertex);
    m_commandList->DrawIndexedInstanced(vertexCount, 1, startIndex, startVertex, 0);
#ifdef T8_ENABLE_PROFILER
    if (t800::g_profiler) t800::g_profiler->AddDrawCall(vertexCount);
#endif
  }

} // namespace t800

#endif // OS_WINDOWS
