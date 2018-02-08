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

#ifndef T800_D3DXRT_H
#define T800_D3DXRT_H

#include <Config.h>

#include <video/BaseDriver.h>
#include <video/windows/D3DXTexture.h>

#include <d3d11.h>
#include <dxgi.h>
#include <vector>

#include <wrl.h>
#include <wrl/client.h>
using namespace Microsoft::WRL;

namespace t800 {
  class D3DXRT : public BaseRT {
  public:
    bool			LoadAPIRT();
    void			DestroyAPIRT();
    void Set(const DeviceContext& context) override;
    void ChangeCubeDepthTexture(int i) override;
    std::vector<ComPtr<ID3D11RenderTargetView>>		vD3D11RenderTargetView;
    std::vector<ComPtr<ID3D11Texture2D>>			vD3D11ColorTex;
    ComPtr<ID3D11Texture2D>							D3D11DepthTex;
    ComPtr<ID3D11DepthStencilView>					D3D11DepthStencilTargetView;
  };
}


#endif