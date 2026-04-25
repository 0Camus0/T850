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

namespace t800 {
  void * D3DXIndexBuffer::GetAPIObject() const
  {
    return (void*)APIBuffer.Get();
  }

  void ** D3DXIndexBuffer::GetAPIObjectReference() const
  {
    return reinterpret_cast<void**>(const_cast<Microsoft::WRL::ComPtr<ID3D11Buffer>&>(APIBuffer).GetAddressOf());
  }

  void D3DXIndexBuffer::Set(const DeviceContext & deviceContext, const unsigned offset, T8_IB_FORMAR::E format)
  {
    const_cast<DeviceContext*>(&deviceContext)->actualIndexBuffer = (IndexBuffer*)this;
    DXGI_FORMAT apiformat;
    if (format == T8_IB_FORMAR::R16)
      apiformat = DXGI_FORMAT::DXGI_FORMAT_R16_UINT;
    else
      apiformat = DXGI_FORMAT::DXGI_FORMAT_R32_UINT;
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->IASetIndexBuffer(APIBuffer.Get(), apiformat, offset);
  }
  void D3DXIndexBuffer::Create(const Device & device, BufferDesc desc, void * initialData)
  {
    descriptor = desc;
    D3D11_USAGE usage;
    switch (desc.usage)
    {
    case T8_BUFFER_USAGE::DEFAULT:
      usage = D3D11_USAGE_DEFAULT;
      break;
    case T8_BUFFER_USAGE::DINAMIC:
      usage = D3D11_USAGE_DYNAMIC;
      break;
    case T8_BUFFER_USAGE::STATIC:
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
  }
  void D3DXIndexBuffer::UpdateFromSystemCopy(const DeviceContext& deviceContext)
  {
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->UpdateSubresource(APIBuffer.Get(), 0, 0, &sysMemCpy[0], 0, 0);
  }
  void D3DXIndexBuffer::UpdateFromBuffer(const DeviceContext& deviceContext, const void * buffer)
  {
    sysMemCpy.clear();
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->UpdateSubresource(APIBuffer.Get(), 0, 0, buffer, 0, 0);
  }
  void D3DXIndexBuffer::release()
  {
    APIBuffer.Reset();
    sysMemCpy.clear();
    delete this;
  }
}
