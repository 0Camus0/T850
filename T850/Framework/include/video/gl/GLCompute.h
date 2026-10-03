#ifndef T800_GLCOMPUTE_H
#define T800_GLCOMPUTE_H

#include <Config.h>
#include <video/BaseDriver.h>

#if defined(USING_OPENGL)
#include <GL/glew.h>

namespace t850 {

  class GLComputePipeline final : public ComputePipeline {
  public:
    ~GLComputePipeline() override;
    bool Create(const ComputePipelineDesc& desc);
    const ComputeBindingLayoutDesc* Find(ComputeBindingType type, uint32_t shaderRegister) const;

    GLuint program = 0;
    std::vector<ComputeBindingLayoutDesc> bindings;
  };

  class GLComputeBuffer final : public ComputeBuffer {
  public:
    ~GLComputeBuffer() override;
    bool Create(const ComputeBufferDesc& desc, const void* initialData);

    GLuint buffer = 0;
  };

} // namespace t850
#endif
#endif