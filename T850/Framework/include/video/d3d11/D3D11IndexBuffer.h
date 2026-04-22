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

#ifndef T800_D3D11INDEXBUFFER_H
#define T800_D3D11INDEXBUFFER_H

#include <Config.h>
#include <video\BaseDriver.h>
#include <d3d11.h>
#include <wrl.h>
#include <wrl/client.h>

namespace t800 {
  class D3DXIndexBuffer : public IndexBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;

    void Set(const DeviceContext& deviceContext, const unsigned offset, T8_IB_FORMAR::E format = T8_IB_FORMAR::R32) override;
    void UpdateFromSystemCopy(const DeviceContext& deviceContext) override;
    void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) override;
    void release() override;
  private:
    friend Device;
    void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) override;
    Microsoft::WRL::ComPtr<ID3D11Buffer> APIBuffer;
  };
}

#endif
