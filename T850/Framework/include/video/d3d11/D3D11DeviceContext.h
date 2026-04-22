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

#ifndef T800_D3D11DEVICECONTEXT_H
#define T800_D3D11DEVICECONTEXT_H

#include <Config.h>
#include <video\BaseDriver.h>
#include <d3d11.h>

namespace t800 {
  class D3DXDeviceContext : public DeviceContext {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;

    void release() override;
    void SetPrimitiveTopology(T8_TOPOLOGY::E topology) override;
    void DrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex) override;
  private:
    ID3D11DeviceContext* APIContext;
  };
}

#endif
