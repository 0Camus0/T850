#include "pch.h"
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

namespace t800 {
  void * D3DXDeviceContext::GetAPIObject() const
  {
    return (void*)APIContext;
  }
  void ** D3DXDeviceContext::GetAPIObjectReference() const
  {
    return (void**)&APIContext;
  }
  void D3DXDeviceContext::release()
  {
    APIContext->Release();
  }
  void D3DXDeviceContext::SetPrimitiveTopology(T8_TOPOLOGY::E topology)
  {
    D3D11_PRIMITIVE_TOPOLOGY apitopology;
    switch (topology)
    {
    case T8_TOPOLOGY::POINT_LIST:
      apitopology = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
      break;
    case T8_TOPOLOGY::LINE_LIST:
      apitopology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
      break;
    case T8_TOPOLOGY::LINE_STRIP:
      apitopology = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
      break;
    case T8_TOPOLOGY::TRIANLE_LIST:
      apitopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
      break;
    case T8_TOPOLOGY::TRIANGLE_STRIP:
      apitopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
      break;
    default:
      break;
    }
    APIContext->IASetPrimitiveTopology(apitopology);
  }

  void D3DXDeviceContext::DrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex)
  {
    APIContext->DrawIndexed(vertexCount, startIndex, startVertex);
  }
}
