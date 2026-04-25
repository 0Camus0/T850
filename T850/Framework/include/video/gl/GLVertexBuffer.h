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

#ifndef T800_GLVERTEXBUFFER_H
#define T800_GLVERTEXBUFFER_H

#include <video/BaseDriver.h>

namespace t850 {
  class Device;

  class GLVertexBuffer : public VertexBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;

    GLVertexBuffer() = default;
    GLVertexBuffer(GLVertexBuffer const& other) = default;
    GLVertexBuffer(GLVertexBuffer&& other) = default;

    void Set(const DeviceContext& deviceContext, const unsigned stride, const unsigned offset) override;
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
