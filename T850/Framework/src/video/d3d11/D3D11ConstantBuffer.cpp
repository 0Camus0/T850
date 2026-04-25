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

#include <video/d3d11/D3D11ConstantBuffer.h>

namespace t850 {
  void * D3DXConstantBuffer::GetAPIObject() const
  {
    return (void*)APIBuffer.Get();
  }

  void ** D3DXConstantBuffer::GetAPIObjectReference() const
  {
    return reinterpret_cast<void**>(const_cast<Microsoft::WRL::ComPtr<ID3D11Buffer>&>(APIBuffer).GetAddressOf());
  }

  void D3DXConstantBuffer::Set(const DeviceContext & deviceContext)
  {
    const_cast<DeviceContext*>(&deviceContext)->actualConstantBuffer = (ConstantBuffer*)this;
    ID3D11DeviceContext* context = reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject());
    ID3D11Buffer* raw = APIBuffer.Get();
    context->VSSetConstantBuffers(0, 1, &raw);
    context->PSSetConstantBuffers(0, 1, &raw);
  }
  void D3DXConstantBuffer::Create(const Device & device, BufferDesc desc, void * initialData)
  {
    descriptor = desc;
    ID3D11Device* apiDevice = reinterpret_cast<ID3D11Device*>(device.GetAPIObject());
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
    apiDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
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
  void D3DXConstantBuffer::UpdateFromSystemCopy(const DeviceContext & deviceContext)
  {
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->UpdateSubresource(APIBuffer.Get(), 0, 0, &sysMemCpy[0], 0, 0);
  }
  void D3DXConstantBuffer::UpdateFromBuffer(const DeviceContext & deviceContext, const void * buffer)
  {
    sysMemCpy.clear();
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->UpdateSubresource(APIBuffer.Get(), 0, 0, (char*)buffer, 0, 0);
  }
  void D3DXConstantBuffer::release()
  {
    APIBuffer.Reset();
    sysMemCpy.clear();
    delete this;
  }
}
