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

#ifndef T800_D3DXSHADER_H
#define T800_D3DXSHADER_H

#include <Config.h>

#include <video/BaseDriver.h>
#include <d3d11.h>
#include <dxgi.h>
#include <D3Dcompiler.h>

#include <wrl.h>
#include <wrl/client.h>
using namespace Microsoft::WRL;

namespace t800 {
  class D3DXShader : public ShaderBase {
  public:

    bool CreateShaderAPI(std::string src_vs, std::string src_fs, unsigned long long sig);
    void  Set(const DeviceContext& deviceContext) override;
    void DestroyAPIShader() override;
    ComPtr<ID3D11VertexShader>  pVS;
    ComPtr<ID3D11PixelShader>   pFS;
    ComPtr<ID3DBlob>            VS_blob;
    ComPtr<ID3DBlob>            FS_blob;
    ComPtr<ID3D11InputLayout>   Layout;
    std::vector<D3D11_INPUT_ELEMENT_DESC>	VertexDecl;
  };
}

#endif