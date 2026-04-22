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

#ifndef T800_D3D11DEVICE_H
#define T800_D3D11DEVICE_H

#include <Config.h>
#include <video\BaseDriver.h>
#include <d3d11.h>
#include <wrl.h>
#include <wrl/client.h>
#include <string>

namespace t800 {
  class D3DXDevice : public Device {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;

    void release() override;
    Buffer* CreateBuffer(T8_BUFFER_TYPE::E bufferType, BufferDesc desc, void* initialData = nullptr) override;
    ShaderBase* CreateShader(std::string src_vs, std::string src_fs, ShaderKey key = ShaderKey(), const std::string& vs_name = "", const std::string& fs_name = "") override;
    Texture* CreateTexture(std::string path) override;
    Texture* CreateTextureFromMemory(const unsigned char *buff, int w, int h, int channels, std::string name) override;
    Texture* CreateCubeMap(const unsigned char * buff, int w, int h) override;
    BaseRT* CreateRT(int nrt, int cf, int df, int w, int h, bool genMips = false) override;
  private:
    Microsoft::WRL::ComPtr<ID3D11Device> APIDevice;
  };
}

#endif
