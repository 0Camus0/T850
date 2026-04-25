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

#ifndef T800_GLINDEXBUFFER_H
#define T800_GLINDEXBUFFER_H

#include <video/BaseDriver.h>

namespace t850 {
  class Device;

  class GLIndexBuffer : public IndexBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;

    void Set(const DeviceContext& deviceContext, const unsigned offset, IndexBufferFormat::E format = IndexBufferFormat::R32) override;
    void UpdateFromSystemCopy(const DeviceContext& deviceContext) override;
    void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) override;
    void release() override;
  private:
    friend Device;
    void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) override;
    unsigned int APIID;
  };
}
#endif
