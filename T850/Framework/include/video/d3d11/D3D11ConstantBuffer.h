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

#ifndef T800_D3D11CONSTANTBUFFER_H
#define T800_D3D11CONSTANTBUFFER_H

#include <Config.h>
#include <video\BaseDriver.h>
#include <d3d11.h>

namespace t800 {
  class D3DXConstantBuffer : public ConstantBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;

    void Set(const DeviceContext& deviceContext) override;
    void UpdateFromSystemCopy(const DeviceContext& deviceContext) override;
    void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) override;
    void release() override;
  private:
    friend Device;
    void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) override;
    ID3D11Buffer* APIBuffer;
  };
}

#endif
