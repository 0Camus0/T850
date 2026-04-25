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

#include <video/d3d11/D3D11VertexBuffer.h>

namespace t800 {
  void * D3DXVertexBuffer::GetAPIObject() const
  {
    return (void*)APIBuffer.Get();
  }

  void ** D3DXVertexBuffer::GetAPIObjectReference() const
  {
    return reinterpret_cast<void**>(const_cast<Microsoft::WRL::ComPtr<ID3D11Buffer>&>(APIBuffer).GetAddressOf());
  }

  void D3DXVertexBuffer::Set(const DeviceContext & deviceContext, const unsigned stride, const unsigned offset)
  {
    const_cast<DeviceContext*>(&deviceContext)->actualVertexBuffer = (VertexBuffer*)this;
    ID3D11Buffer* raw = APIBuffer.Get();
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->IASetVertexBuffers(0, 1, &raw, &stride, &offset);
  }
  void D3DXVertexBuffer::Create(const Device & device, BufferDesc desc, void * initialData)
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
    apiDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    apiDesc.Usage = usage;
    if (usage == D3D11_USAGE_DYNAMIC)
      apiDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

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
  void D3DXVertexBuffer::UpdateFromSystemCopy(const DeviceContext& deviceContext)
  {
    ID3D11DeviceContext* ctx = reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject());
    if (descriptor.usage == T8_BUFFER_USAGE::DINAMIC) {
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      HRESULT hr = ctx->Map(APIBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
      if (SUCCEEDED(hr)) {
        memcpy(mapped.pData, &sysMemCpy[0], sysMemCpy.size());
        ctx->Unmap(APIBuffer.Get(), 0);
      }
    } else {
      ctx->UpdateSubresource(APIBuffer.Get(), 0, 0, &sysMemCpy[0], 0, 0);
    }
  }
  void D3DXVertexBuffer::UpdateFromBuffer(const DeviceContext& deviceContext, const void * buffer)
  {
    sysMemCpy.clear();
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    ID3D11DeviceContext* ctx = reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject());
    if (descriptor.usage == T8_BUFFER_USAGE::DINAMIC) {
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      HRESULT hr = ctx->Map(APIBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
      if (SUCCEEDED(hr)) {
        memcpy(mapped.pData, buffer, descriptor.byteWidth);
        ctx->Unmap(APIBuffer.Get(), 0);
      }
    } else {
      ctx->UpdateSubresource(APIBuffer.Get(), 0, 0, buffer, 0, 0);
    }
  }
  void D3DXVertexBuffer::release()
  {
    APIBuffer.Reset();
    sysMemCpy.clear();
    delete this;
  }
}
