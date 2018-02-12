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

#ifndef T800_TEXTURE_GL_H
#define T800_TEXTURE_GL_H

#include <Config.h>
#include <video/BaseDriver.h>

namespace t800 {
  class GLTexture : public Texture {
  public:
    GLTexture();
    virtual ~GLTexture() {}
    void	SetTextureParams();
    void	GetFormatBpp(unsigned int &props, unsigned int &glFormat, unsigned int &bpp);

    void	LoadAPITexture(DeviceContext* context, unsigned char* buffer);
    void	LoadAPITextureCompressed(unsigned char* buffer);
    void	DestroyAPITexture();

    void  Set(const DeviceContext& deviceContext, unsigned int slot, std::string shaderTextureName) override;
    void  SetSampler(const DeviceContext& deviceContext) override;

    unsigned int glTarget;
  private:
    int APITextureLoc;
  };
}

#endif
