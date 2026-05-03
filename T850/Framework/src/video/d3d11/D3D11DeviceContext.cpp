#include <pch.h>
/*********************************************************
* Copyright (C) 2017 Daniel Enriquez (camus_mm@hotmail.com)
* All Rights Reserved
*
* You may use, distribute and modify this code under the
* following terms:
* ** Do not claim that you wrote this software
* ** A mention would be appreciated but not needed
* ** I do not and will not provide support, this software is "as is"
* ** Enjoy, learn and share.
*********************************************************/

#include <video/d3d11/D3D11DeviceContext.h>
#include <debug/RenderTrace.h>

namespace t850 {
  void * D3DXDeviceContext::GetAPIObject() const
  {
    return (void*)APIContext.Get();
  }
  void ** D3DXDeviceContext::GetAPIObjectReference() const
  {
    // See D3DXDevice::GetAPIObjectReference for the rationale on this cast.
    return reinterpret_cast<void**>(const_cast<Microsoft::WRL::ComPtr<ID3D11DeviceContext>&>(APIContext).GetAddressOf());
  }
  void D3DXDeviceContext::release()
  {
    APIContext.Reset();
  }
  void D3DXDeviceContext::SetPrimitiveTopology(Topology::E topology)
  {
    D3D11_PRIMITIVE_TOPOLOGY apitopology;
    switch (topology)
    {
    case Topology::POINT_LIST:
      apitopology = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
      break;
    case Topology::LINE_LIST:
      apitopology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
      break;
    case Topology::LINE_STRIP:
      apitopology = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
      break;
    case Topology::TRIANLE_LIST:
      apitopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
      break;
    case Topology::TRIANGLE_STRIP:
      apitopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
      break;
    default:
      break;
    }
    APIContext->IASetPrimitiveTopology(apitopology);
    T8_TRACE(EvSetTopology((int)topology));
  }

  void D3DXDeviceContext::DrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex)
  {
    APIContext->DrawIndexed(vertexCount, startIndex, startVertex);
    T8_TRACE(EvDrawIndexed(vertexCount, startIndex, startVertex));
  }
}
