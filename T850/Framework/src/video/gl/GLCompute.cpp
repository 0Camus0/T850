#include <pch.h>
#include <video/gl/GLCompute.h>

#if defined(USING_OPENGL)
#include <video/gl/GLDriver.h>
#include <video/gl/GLTexture.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>
#include <utils/ShaderPermutationDump.h>
#include <utils/SPIRVReflection.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <filesystem>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace t850 {
namespace {
  bool LoadComputeSource(const ComputePipelineDesc& desc, std::string& source) {
    std::filesystem::path path(desc.debugName);
    path.replace_extension(".glsl");
    if (!ResourceLocator::Instance().ReadText(path.generic_string(), source) || source.empty()) {
      T8_LOG_ERROR("[GL][Compute] GLSL source is missing for '%s'", desc.debugName.c_str());
      return false;
    }
    const size_t versionEnd = source.find('\n');
    if (source.rfind("#version", 0) != 0 || versionEnd == std::string::npos) {
      T8_LOG_ERROR("[GL][Compute] GLSL source '%s' must begin with #version", path.generic_string().c_str());
      return false;
    }
    std::ostringstream combined;
    combined << source.substr(0, versionEnd + 1);
    for (const std::string& define : desc.defines)
      if (!define.empty()) combined << "#define " << define << '\n';
    combined << source.substr(versionEnd + 1);
    source = combined.str();
    return true;
  }

  bool CompileComputeProgram(const ComputePipelineDesc& desc, const std::string& source, GLuint& program) {
    const char* text = source.c_str();
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
      GLint length = 0;
      glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
      std::string log(static_cast<size_t>((std::max)(length, 1)), '\0');
      glGetShaderInfoLog(shader, length, nullptr, log.data());
      T8_LOG_ERROR("[GL][Compute] Shader compile failed for '%s': %s", desc.debugName.c_str(), log.c_str());
      glDeleteShader(shader);
      return false;
    }

    program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    glDeleteShader(shader);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
      GLint length = 0;
      glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
      std::string log(static_cast<size_t>((std::max)(length, 1)), '\0');
      glGetProgramInfoLog(program, length, nullptr, log.data());
      T8_LOG_ERROR("[GL][Compute] Program link failed for '%s': %s", desc.debugName.c_str(), log.c_str());
      glDeleteProgram(program);
      program = 0;
      return false;
    }
    return true;
  }
}

GLComputePipeline::~GLComputePipeline() {
  if (program) glDeleteProgram(program);
}

const ComputeBindingLayoutDesc* GLComputePipeline::Find(ComputeBindingType type, uint32_t shaderRegister) const {
  for (const ComputeBindingLayoutDesc& binding : bindings)
    if (binding.type == type && binding.shaderRegister == shaderRegister) return &binding;
  return nullptr;
}

bool GLComputePipeline::Create(const ComputePipelineDesc& desc) {
  if (desc.source.empty() || desc.entryPoint.empty() || desc.bindings.empty()) return false;
  std::unordered_set<uint32_t> bindingIndices;
  std::unordered_set<uint64_t> logicalBindings;
  for (const ComputeBindingLayoutDesc& binding : desc.bindings) {
    const uint64_t logicalKey = (static_cast<uint64_t>(binding.type) << 32u) | binding.shaderRegister;
    if (!bindingIndices.insert(binding.bindingIndex).second ||
        !logicalBindings.insert(logicalKey).second ||
        (binding.type == ComputeBindingType::Constants32 && binding.constantCount == 0)) {
      T8_LOG_ERROR("[GL][Compute] Invalid or duplicate binding in pipeline '%s'", desc.debugName.c_str());
      return false;
    }
  }
  std::string source;
  if (!LoadComputeSource(desc, source)) return false;
  static const bool initialized = glslang::InitializeProcess();
  if (!initialized) return false;
  glslang::TShader reflectionShader(EShLangCompute);
  const char* text = source.c_str();
  reflectionShader.setStrings(&text, 1);
  reflectionShader.setEnvInput(glslang::EShSourceGlsl, EShLangCompute, glslang::EShClientOpenGL, 430);
  reflectionShader.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
  reflectionShader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
  const auto messages = static_cast<EShMessages>(EShMsgSpvRules);
  if (!reflectionShader.parse(GetDefaultResources(), 430, false, messages)) {
    T8_LOG_ERROR("[GL][Compute] Reflection compile failed: %s", reflectionShader.getInfoLog());
    return false;
  }
  glslang::TProgram reflectionProgram;
  reflectionProgram.addShader(&reflectionShader);
  if (!reflectionProgram.link(messages)) return false;
  std::vector<uint32_t> spirv;
  glslang::GlslangToSpv(*reflectionProgram.getIntermediate(EShLangCompute), spirv);
  std::vector<ComputeBindingLayoutDesc> reflected;
  if (!ReflectComputeBindings(spirv, desc, reflected, threadGroupSize, true) ||
      !SetValidatedLayout(desc, reflected, true) || !CompileComputeProgram(desc, source, program)) return false;
  GLint reflectedGroupSize[3] = {};
  glGetProgramiv(program, GL_COMPUTE_WORK_GROUP_SIZE, reflectedGroupSize);
  if (reflectedGroupSize[0] <= 0 || reflectedGroupSize[1] <= 0 || reflectedGroupSize[2] <= 0) {
    T8_LOG_ERROR("[GL][Compute] Shader '%s' has an invalid thread-group size",
                 desc.debugName.c_str());
    return false;
  }
  threadGroupSize = {
    static_cast<uint32_t>(reflectedGroupSize[0]),
    static_cast<uint32_t>(reflectedGroupSize[1]),
    static_cast<uint32_t>(reflectedGroupSize[2])
  };
  bindings = desc.bindings;
  ShaderPermutationDump::RecordCompute(desc.debugName, desc.entryPoint, desc.permutationName, desc.defines);
  T8_LOG_INFO("[GL][Compute] Pipeline '%s' created (bindings=%zu)", desc.debugName.c_str(), bindings.size());
  return true;
}

GLComputeBuffer::~GLComputeBuffer() {
  if (buffer) glDeleteBuffers(1, &buffer);
}

bool GLComputeBuffer::Create(const ComputeBufferDesc& desc, const void* initialData) {
  if (!desc.byteWidth || !desc.structureStride || desc.byteWidth % desc.structureStride) return false;
  descriptor = desc;
  GLint previous = 0;
  glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &previous);
  glGenBuffers(1, &buffer);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
  glBufferData(GL_SHADER_STORAGE_BUFFER, desc.byteWidth, initialData, GL_DYNAMIC_COPY);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(previous));
  if (glGetError() != GL_NO_ERROR) {
    glDeleteBuffers(1, &buffer);
    buffer = 0;
    return false;
  }
  return true;
}

std::unique_ptr<ComputePipeline> GLDriver::CreateComputePipeline(const ComputePipelineDesc& desc) {
  if (!SupportsComputeShaders()) return {};
  auto pipeline = std::make_unique<GLComputePipeline>();
  return pipeline->Create(desc) ? std::move(pipeline) : nullptr;
}

std::unique_ptr<ComputeBuffer> GLDriver::CreateComputeBuffer(const ComputeBufferDesc& desc, const void* initialData) {
  if (!SupportsComputeShaders()) return {};
  auto buffer = std::make_unique<GLComputeBuffer>();
  return buffer->Create(desc, initialData) ? std::move(buffer) : nullptr;
}

bool GLDriver::DispatchCompute(ComputePipeline& pipelineBase,
                               const std::vector<ComputeBindingDesc>& dispatchBindings,
                               uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
  auto* pipeline = dynamic_cast<GLComputePipeline*>(&pipelineBase);
  if (!pipeline || !pipeline->ValidateBindings(dispatchBindings) || !SupportsComputeShaders() ||
      !groupCountX || !groupCountY || !groupCountZ) return false;

  for (GLuint axis = 0; axis < 3; ++axis) {
    GLint limit = 0;
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, axis, &limit);
    const uint32_t requested = axis == 0 ? groupCountX : axis == 1 ? groupCountY : groupCountZ;
    if (requested > static_cast<uint32_t>(limit)) return false;
  }

  GLint maxTextureUnits = 0;
  GLint maxImageUnits = 0;
  glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxTextureUnits);
  glGetIntegerv(GL_MAX_IMAGE_UNITS, &maxImageUnits);
  std::unordered_set<const ComputeBindingLayoutDesc*> boundLayouts;
  std::unordered_map<uint32_t, GLTexture*> sampledTextures;
  std::unordered_map<uint32_t, GLTexture*> samplerTextures;
  for (const ComputeBindingDesc& binding : dispatchBindings) {
    const ComputeBindingLayoutDesc* layout = pipeline->Find(binding.type, binding.shaderRegister);
    if (!layout || !boundLayouts.insert(layout).second) return false;
    if (binding.type == ComputeBindingType::Constants32) {
      if (!binding.constants || binding.constantCount != layout->constantCount) return false;
    } else if (binding.type == ComputeBindingType::ReadOnlyBuffer ||
               binding.type == ComputeBindingType::ReadWriteBuffer) {
        if (!dynamic_cast<GLComputeBuffer*>(binding.buffer) ||
          (binding.type == ComputeBindingType::ReadWriteBuffer &&
           binding.buffer->descriptor.access != ComputeBufferAccess::ReadWrite)) return false;
    } else {
      auto* texture = dynamic_cast<GLTexture*>(binding.texture);
      if (!texture || texture->glTarget != GL_TEXTURE_2D) return false;
      if (binding.type == ComputeBindingType::ReadOnlyTexture) {
        if (layout->bindingIndex >= static_cast<uint32_t>(maxTextureUnits) ||
            !sampledTextures.emplace(binding.shaderRegister, texture).second) return false;
      }
      if (binding.type == ComputeBindingType::ReadWriteTexture &&
          (!SupportsComputeTextures() || layout->bindingIndex >= static_cast<uint32_t>(maxImageUnits) ||
           (texture->glInternalFormat != GL_RGBA8 && texture->glInternalFormat != GL_RGBA16F))) return false;
      if (binding.type == ComputeBindingType::ReadWriteTexture) {
        for (const auto& reflected : pipeline->bindingLayout) {
          if (reflected.type == binding.type && reflected.shaderRegister == binding.shaderRegister &&
              texture->glInternalFormat != (reflected.storageFormat == ComputeStorageFormat::Rgba16Float ? GL_RGBA16F : GL_RGBA8))
            return false;
        }
      }
      if (binding.type == ComputeBindingType::Sampler) {
        const ComputeBindingLayoutDesc* textureLayout = pipeline->Find(
          ComputeBindingType::ReadOnlyTexture, binding.shaderRegister);
        if (!textureLayout || textureLayout->bindingIndex >= static_cast<uint32_t>(maxTextureUnits) ||
            !samplerTextures.emplace(binding.shaderRegister, texture).second) return false;
      }
    }
  }
  if (boundLayouts.size() != pipeline->bindings.size()) return false;
  for (const auto& [shaderRegister, texture] : samplerTextures) {
    const auto sampled = sampledTextures.find(shaderRegister);
    if (sampled == sampledTextures.end() || sampled->second != texture) return false;
  }

  while (glGetError() != GL_NO_ERROR) {}

  GLint previousProgram = 0;
  GLint previousActiveTexture = 0;
  glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
  struct TextureBindingRestore { GLuint unit; GLenum target; GLint texture; };
  std::vector<GLuint> transientConstants;
  std::vector<std::pair<GLenum, GLuint>> bufferBindings;
  std::vector<TextureBindingRestore> textureBindings;
  std::vector<std::pair<GLuint, GLint>> samplerBindings;
  std::vector<GLuint> imageBindings;
  glUseProgram(pipeline->program);
  for (const ComputeBindingDesc& binding : dispatchBindings) {
    const ComputeBindingLayoutDesc* layout = pipeline->Find(binding.type, binding.shaderRegister);
    if (binding.type == ComputeBindingType::Constants32) {
      GLuint constants = 0;
      glGenBuffers(1, &constants);
      glBindBuffer(GL_UNIFORM_BUFFER, constants);
      glBufferData(GL_UNIFORM_BUFFER, binding.constantCount * sizeof(uint32_t), binding.constants, GL_STREAM_DRAW);
      glBindBufferBase(GL_UNIFORM_BUFFER, layout->bindingIndex, constants);
      transientConstants.push_back(constants);
      bufferBindings.emplace_back(GL_UNIFORM_BUFFER, layout->bindingIndex);
    } else if (binding.type == ComputeBindingType::ReadOnlyBuffer ||
               binding.type == ComputeBindingType::ReadWriteBuffer) {
      auto* buffer = static_cast<GLComputeBuffer*>(binding.buffer);
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, layout->bindingIndex, buffer->buffer);
      bufferBindings.emplace_back(GL_SHADER_STORAGE_BUFFER, layout->bindingIndex);
    } else if (binding.type == ComputeBindingType::ReadOnlyTexture) {
      auto* texture = static_cast<GLTexture*>(binding.texture);
      glActiveTexture(GL_TEXTURE0 + layout->bindingIndex);
      GLint previousTexture = 0;
      glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
      textureBindings.push_back({layout->bindingIndex, texture->glTarget, previousTexture});
      glBindTexture(texture->glTarget, texture->id);
    } else if (binding.type == ComputeBindingType::ReadWriteTexture) {
      auto* texture = static_cast<GLTexture*>(binding.texture);
      glBindImageTexture(layout->bindingIndex, texture->id, 0, GL_FALSE, 0,
                         GL_WRITE_ONLY, texture->glInternalFormat);
      imageBindings.push_back(layout->bindingIndex);
    } else if (binding.type == ComputeBindingType::Sampler) {
      const ComputeBindingLayoutDesc* textureLayout = pipeline->Find(
        ComputeBindingType::ReadOnlyTexture, binding.shaderRegister);
      GLint previousSampler = 0;
      glGetIntegeri_v(GL_SAMPLER_BINDING, textureLayout->bindingIndex, &previousSampler);
      samplerBindings.emplace_back(textureLayout->bindingIndex, previousSampler);
      glBindSampler(textureLayout->bindingIndex, 0);
    }
  }

  glDispatchCompute(groupCountX, groupCountY, groupCountZ);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                  GL_TEXTURE_FETCH_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
  const GLenum error = glGetError();
  for (const auto& [target, index] : bufferBindings) glBindBufferBase(target, index, 0);
  for (GLuint index : imageBindings)
    glBindImageTexture(index, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
  for (auto it = samplerBindings.rbegin(); it != samplerBindings.rend(); ++it)
    glBindSampler(it->first, static_cast<GLuint>(it->second));
  for (auto it = textureBindings.rbegin(); it != textureBindings.rend(); ++it) {
    glActiveTexture(GL_TEXTURE0 + it->unit);
    glBindTexture(it->target, static_cast<GLuint>(it->texture));
  }
  glActiveTexture(static_cast<GLenum>(previousActiveTexture));
  if (!transientConstants.empty()) glDeleteBuffers(static_cast<GLsizei>(transientConstants.size()), transientConstants.data());
  glUseProgram(static_cast<GLuint>(previousProgram));
  if (error != GL_NO_ERROR) {
    T8_LOG_ERROR("[GL][Compute] Dispatch failed with GL error 0x%X", error);
    return false;
  }
  return true;
}

bool GLDriver::ReadComputeBuffer(ComputeBuffer& bufferBase, void* destination, size_t byteCount) {
  auto* buffer = dynamic_cast<GLComputeBuffer*>(&bufferBase);
  if (!buffer || !destination || !byteCount || byteCount % 4 || byteCount > buffer->descriptor.byteWidth) return false;
  GLint previous = 0;
  glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &previous);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer->buffer);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(byteCount), destination);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(previous));
  return glGetError() == GL_NO_ERROR;
}

} // namespace t850
#endif