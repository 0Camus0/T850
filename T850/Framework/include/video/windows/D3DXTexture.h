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

#ifndef T800_TEXTURE_D3D_H
#define T800_TEXTURE_D3D_H

#include <Config.h>

#include <video\BaseDriver.h>


#include <d3d11.h>
#include <wrl.h>
#include <wrl/client.h>
using namespace Microsoft::WRL;

namespace t800 {
  class D3DXTexture : public Texture {
  public:
    D3DXTexture() {}

    void	SetTextureParams();
    void	GetFormatBpp(unsigned int &props, unsigned int &Format, unsigned int &bpp);

    void	LoadAPITexture(DeviceContext* context, unsigned char* buffer);
    void	LoadAPITextureCompressed(unsigned char* buffer);
    void	DestroyAPITexture();
    void  Set(const DeviceContext& deviceContext, unsigned int slot, std::string shaderTextureName) override;
    void  SetSampler(const DeviceContext& deviceContext) override;

    ComPtr<ID3D11Texture2D>				Tex;
    ComPtr<ID3D11ShaderResourceView>    pSRVTex;
    ComPtr<ID3D11SamplerState>          pSampler;

  };
}
#endif