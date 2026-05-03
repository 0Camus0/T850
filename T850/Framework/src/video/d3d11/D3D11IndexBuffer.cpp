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

#include <video/d3d11/D3D11IndexBuffer.h>
#include <debug/RenderTrace.h>

namespace t850 {
  void * D3DXIndexBuffer::GetAPIObject() const
  {
    return (void*)APIBuffer.Get();
  }

  void ** D3DXIndexBuffer::GetAPIObjectReference() const
  {
    return reinterpret_cast<void**>(const_cast<Microsoft::WRL::ComPtr<ID3D11Buffer>&>(APIBuffer).GetAddressOf());
  }

  void D3DXIndexBuffer::Set(const DeviceContext & deviceContext, const unsigned offset, IndexBufferFormat::E format)
  {
    const_cast<DeviceContext*>(&deviceContext)->actualIndexBuffer = (IndexBuffer*)this;
    DXGI_FORMAT apiformat;
    if (format == IndexBufferFormat::R16)
      apiformat = DXGI_FORMAT::DXGI_FORMAT_R16_UINT;
    else
      apiformat = DXGI_FORMAT::DXGI_FORMAT_R32_UINT;
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->IASetIndexBuffer(APIBuffer.Get(), apiformat, offset);
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int bufId = g_renderTracer->EnsureBufferId(this, "ib");
      g_renderTracer->EvBindIndexBufferRequest(bufId, (int)format, offset);
    }
#endif
  }
  void D3DXIndexBuffer::Create(const Device & device, BufferDesc desc, void * initialData)
  {
    descriptor = desc;
    D3D11_USAGE usage;
    switch (desc.usage)
    {
    case BufferUsage::DEFAULT:
      usage = D3D11_USAGE_DEFAULT;
      break;
    case BufferUsage::DINAMIC:
      usage = D3D11_USAGE_DYNAMIC;
      break;
    case BufferUsage::STATIC:
      usage = D3D11_USAGE_IMMUTABLE;
      break;
    default:
      usage = D3D11_USAGE_DEFAULT;
      break;
    }
    D3D11_BUFFER_DESC apiDesc{ 0 };
    apiDesc.ByteWidth = desc.byteWidth;
    apiDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    apiDesc.Usage = usage;

    if (initialData != nullptr)
    {
      sysMemCpy.assign((char*)initialData, (char*)initialData + desc.byteWidth);
      D3D11_SUBRESOURCE_DATA subData = { initialData, 0, 0 };
      reinterpret_cast<ID3D11Device*>(device.GetAPIObject())->CreateBuffer(&apiDesc, &subData, APIBuffer.ReleaseAndGetAddressOf());
    }
    else
    {
      reinterpret_cast<ID3D11Device*>(device.GetAPIObject())->CreateBuffer(&apiDesc, 0, APIBuffer.ReleaseAndGetAddressOf());
    }
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE() && initialData) {
      int bufId = g_renderTracer->EnsureBufferId(this, "ib");
      g_renderTracer->RecordBufferUpdate(bufId, initialData, desc.byteWidth, "ib", "");
    }
#endif
  }
  void D3DXIndexBuffer::UpdateFromSystemCopy(const DeviceContext& deviceContext)
  {
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->UpdateSubresource(APIBuffer.Get(), 0, 0, &sysMemCpy[0], 0, 0);
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE() && !sysMemCpy.empty()) {
      int bufId = g_renderTracer->EnsureBufferId(this, "ib");
      g_renderTracer->RecordBufferUpdate(bufId, sysMemCpy.data(), (uint32_t)sysMemCpy.size(), "ib", "");
    }
#endif
  }
  void D3DXIndexBuffer::UpdateFromBuffer(const DeviceContext& deviceContext, const void * buffer)
  {
    sysMemCpy.clear();
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->UpdateSubresource(APIBuffer.Get(), 0, 0, buffer, 0, 0);
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int bufId = g_renderTracer->EnsureBufferId(this, "ib");
      g_renderTracer->RecordBufferUpdate(bufId, buffer, descriptor.byteWidth, "ib", "");
    }
#endif
  }
  void D3DXIndexBuffer::release()
  {
    APIBuffer.Reset();
    sysMemCpy.clear();
    delete this;
  }
}
