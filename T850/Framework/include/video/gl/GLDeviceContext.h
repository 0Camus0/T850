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

#ifndef T800_GLDEVICECONTEXT_H
#define T800_GLDEVICECONTEXT_H

#include <video/BaseDriver.h>

namespace t800 {
  class GLDeviceContext : public DeviceContext {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;

    void release() override;
    void SetPrimitiveTopology(T8_TOPOLOGY::E topology) override;
    void DrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex) override;
    int internalIBFormat;
    int internalTopology;
    int internalStride;
    unsigned int internalShaderProgram;
  private:
  };
}
#endif
