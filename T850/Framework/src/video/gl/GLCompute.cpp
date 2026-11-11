#include <pch.h>
#include <debug/RuntimeTelemetry.h>
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

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace t850 {
namespace {
  // One place maps a compute storage format to a GL sized internal format, so the image
  // unit binding and the shader's layout() qualifier agree.
  GLenum ToGLStorageFormat(ComputeStorageFormat format) {
    switch (format) {
      case ComputeStorageFormat::Rgba8Unorm:  return GL_RGBA8;
      case ComputeStorageFormat::Rgba16Float: return GL_RGBA16F;
      case ComputeStorageFormat::R32Float:    return GL_R32F;
      default:                                return 0;
    }
  }

  bool IsSupportedStorageFormat(GLenum internalFormat) {
    return internalFormat == GL_RGBA8 || internalFormat == GL_RGBA16F || internalFormat == GL_R32F;
  }

  bool LoadComputeSource(const ComputePipelineDesc& desc, std::string& source) {
    // `desc.debugName` carries the shader path and may carry a `#permutation` suffix, so build the
    // twin name from its stem. A non-base permutation is a separate generated file rather than an
    // `#ifdef` inside the base twin, which keeps the generator from having to merge two bodies.
    //
    // Callers disagree on the spelling: the graphics kernels pass "Shaders/CS_Blur.hlsl" while
    // `FluidSimulation` passes the bare "CS_FluidPressure3D.hlsl" (it reads the HLSL from
    // "Shaders/" + sourceName itself). So an unprefixed name falls back to the Shaders directory.
    std::filesystem::path debugPath(desc.debugName);
    std::vector<std::filesystem::path> directories;
    if (!debugPath.parent_path().empty()) directories.push_back(debugPath.parent_path());
    else directories.push_back("Shaders");
    directories.push_back(std::filesystem::path());
    const std::string stem = debugPath.stem().string();
    const bool permutation = !desc.permutationName.empty() && desc.permutationName != "base";
    std::filesystem::path path;
    bool found = false;
    for (const auto& directory : directories) {
      // A generated permutation is its own file; a hand-written twin carries its permutations as
      // `#ifdef` blocks in the base file (`desc.defines` is inserted after `#version`), so fall
      // back to the base twin when the per-permutation file does not exist.
      if (permutation) {
        const std::filesystem::path candidate = directory / (stem + "." + desc.permutationName + ".glsl");
        if (ResourceLocator::Instance().Exists(candidate.generic_string())) { path = candidate; found = true; break; }
      }
      const std::filesystem::path candidate = directory / (stem + ".glsl");
      if (ResourceLocator::Instance().Exists(candidate.generic_string())) { path = candidate; found = true; break; }
    }
    if (!found) path = directories.front() / (stem + ".glsl");
    if (!found || !ResourceLocator::Instance().ReadText(path.generic_string(), source) || source.empty()) {
      T8_LOG_ERROR("[GL][Compute] GLSL source is missing for '%s' (expected %s)",
                   desc.debugName.c_str(), path.generic_string().c_str());
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
    T8_TELEMETRY_ADD("shader.cache.uncached", 1);
    T8_TELEMETRY_CALL("shader.compile", glCompileShader(shader));
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
    T8_TELEMETRY_CALL("pipeline.create.compute", glLinkProgram(program));
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
  if (!reflectionProgram.link(messages)) {
    T8_LOG_ERROR("[GL][Compute] Reflection link failed for '%s': %s", desc.debugName.c_str(),
                 reflectionProgram.getInfoLog());
    return false;
  }
  std::vector<uint32_t> spirv;
  glslang::GlslangToSpv(*reflectionProgram.getIntermediate(EShLangCompute), spirv);
  std::vector<ComputeBindingLayoutDesc> reflected;
  if (!ReflectComputeBindings(spirv, desc, reflected, threadGroupSize, true)) {
    T8_LOG_ERROR("[GL][Compute] ReflectComputeBindings failed for '%s'", desc.debugName.c_str());
    return false;
  }
  // SPIRV-Cross emits one combined sampler per distinct sampler state, so a cube that is both
  // sampled and queried with GetDimensions (CS_ParticleShade's EnvironmentMap) arrives as two
  // samplerCube aliases on the same binding. They are one texture unit, not two bindings.
  {
    std::vector<ComputeBindingLayoutDesc> unique;
    unique.reserve(reflected.size());
    for (const ComputeBindingLayoutDesc& entry : reflected) {
      const bool duplicate = std::any_of(unique.begin(), unique.end(),
          [&entry](const ComputeBindingLayoutDesc& seen) {
            return seen.type == entry.type && seen.bindingIndex == entry.bindingIndex &&
                   seen.shaderRegister == entry.shaderRegister && seen.dimension == entry.dimension &&
                   seen.storageFormat == entry.storageFormat &&
                   seen.constantCount == entry.constantCount;
          });
      if (!duplicate) unique.push_back(entry);
    }
    reflected.swap(unique);
  }
  if (!SetValidatedLayout(desc, reflected, true)) {
    for (const ComputeBindingLayoutDesc& b : reflected) {
      T8_LOG_ERROR("[GL][Compute] reflected '%s': type=%d reg=%u idx=%u count=%u dim=%d fmt=%d",
                   desc.debugName.c_str(), static_cast<int>(b.type), b.shaderRegister, b.bindingIndex,
                   b.constantCount, static_cast<int>(b.dimension), static_cast<int>(b.storageFormat));
    }
    for (const ComputeBindingLayoutDesc& b : desc.bindings) {
      T8_LOG_ERROR("[GL][Compute] declared  '%s': type=%d reg=%u idx=%u count=%u dim=%d fmt=%d",
                   desc.debugName.c_str(), static_cast<int>(b.type), b.shaderRegister, b.bindingIndex,
                   b.constantCount, static_cast<int>(b.dimension), static_cast<int>(b.storageFormat));
    }
    T8_LOG_ERROR("[GL][Compute] SetValidatedLayout failed for '%s'", desc.debugName.c_str());
    return false;
  }
  if (!CompileComputeProgram(desc, source, program)) {
    T8_LOG_ERROR("[GL][Compute] CompileComputeProgram failed for '%s'", desc.debugName.c_str());
    return false;
  }
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

GLComputeTexture3D::~GLComputeTexture3D() {
  if (texture) glDeleteTextures(1, &texture);
}

bool GLComputeTexture3D::Create(const ComputeTexture3DDesc& desc, const void* initialData) {
  if (!desc.width || !desc.height || !desc.depth) return false;
  // Volume contents are produced on the GPU, so CPU seeding is not supported. D3D11, D3D12 and
  // Vulkan all reject it; a GL-only upload path would be exercised by nothing.
  if (initialData) return false;
  const GLenum internal = ToGLStorageFormat(desc.format);
  if (!IsSupportedStorageFormat(internal)) {
    T8_LOG_ERROR("[GL][Compute] Volume '%s' has an unsupported storage format", desc.debugName.c_str());
    return false;
  }
  descriptor = desc;
  internalFormat = internal;
  // Immutable storage: an image unit's format qualifier must match the internal format exactly,
  // so there is nothing to re-specify later.
  GLint previous = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_3D, &previous);
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_3D, texture);
  glTexStorage3D(GL_TEXTURE_3D, 1, internal, static_cast<GLsizei>(desc.width),
                 static_cast<GLsizei>(desc.height), static_cast<GLsizei>(desc.depth));
  // The kernels only texelFetch, but the default mip-mapped min filter makes the texture
  // incomplete for any future sampling of the same object.
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_3D, static_cast<GLuint>(previous));
  if (glGetError() != GL_NO_ERROR) {
    glDeleteTextures(1, &texture);
    texture = 0;
    return false;
  }
  T8_LOG_INFO("[GL][Compute] Volume created (%ux%ux%u, %s)", desc.width, desc.height, desc.depth,
              desc.format == ComputeStorageFormat::R32Float ? "r32f" :
              desc.format == ComputeStorageFormat::Rgba16Float ? "rgba16f" : "rgba8");
  return true;
}

std::unique_ptr<ComputeTexture3D> GLDriver::CreateComputeTexture3D(const ComputeTexture3DDesc& desc,
                                                                  const void* initialData) {
  if (!SupportsComputeTextures3D()) return {};
  auto volume = std::make_unique<GLComputeTexture3D>();
  return volume->Create(desc, initialData) ? std::move(volume) : nullptr;
}

const GLDriver::ComputeLimits& GLDriver::ComputeLimitsCached() {
  if (!m_computeLimits.initialized) {
    for (GLuint axis = 0; axis < 3; ++axis) {
      GLint limit = 0;
      glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, axis, &limit);
      m_computeLimits.maxWorkGroups[axis] = limit > 0 ? static_cast<uint32_t>(limit) : 65535u;
    }
    GLint value = 0;
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &value);
    m_computeLimits.maxTextureUnits = value > 0 ? static_cast<uint32_t>(value) : 16u;
    glGetIntegerv(GL_MAX_IMAGE_UNITS, &value);
    m_computeLimits.maxImageUnits = value > 0 ? static_cast<uint32_t>(value) : 8u;
    glGetIntegerv(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS, &value);
    m_computeLimits.maxStorageBlocks = value > 0 ? static_cast<uint32_t>(value) : 8u;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &value);
    m_computeLimits.uniformBufferAlignment = value > 0 ? static_cast<uint32_t>(value) : 256u;
    m_computeLimits.initialized = true;
  }
  return m_computeLimits;
}

uint32_t GLDriver::AllocateDispatchConstants(const uint32_t* data, uint32_t byteCount) {
  if (!data || !byteCount) return UINT32_MAX;
  const uint32_t alignment = ComputeLimitsCached().uniformBufferAlignment;
  const uint32_t padded = (byteCount + alignment - 1u) / alignment * alignment;
  if (!m_dispatchConstantsBuffer) {
    // 1 MiB of constant blocks per frame: 120 sweeps x ~7 kernels x 80 bytes is well under.
    m_dispatchConstantsCapacity = 1u << 20;
    glGenBuffers(1, &m_dispatchConstantsBuffer);
    if (!m_dispatchConstantsBuffer) return UINT32_MAX;
    GLint previous = 0;
    glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &previous);
    glBindBuffer(GL_UNIFORM_BUFFER, m_dispatchConstantsBuffer);
    // Orphaned, not sized: the buffer must exist before the first glBufferSubData.
    glBufferData(GL_UNIFORM_BUFFER, m_dispatchConstantsCapacity, nullptr, GL_STREAM_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, static_cast<GLuint>(previous));
    m_dispatchConstantsOffset = 0;
  }
  if (m_dispatchConstantsOffset + padded > m_dispatchConstantsCapacity) {
    m_dispatchConstantsOffset = 0;
    if (padded > m_dispatchConstantsCapacity) return UINT32_MAX;
  }
  const uint32_t offset = m_dispatchConstantsOffset;
  GLint previous = 0;
  glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &previous);
  glBindBuffer(GL_UNIFORM_BUFFER, m_dispatchConstantsBuffer);
  glBufferSubData(GL_UNIFORM_BUFFER, offset, byteCount, data);
  glBindBuffer(GL_UNIFORM_BUFFER, static_cast<GLuint>(previous));
  m_dispatchConstantsOffset = offset + padded;
  return offset;
}

void GLDriver::ResetDispatchConstants() {
  m_dispatchConstantsOffset = 0;
}

GLuint GLDriver::EnsureInlineStorageBuffer(uint32_t byteCount, const void* data) {
  if (!byteCount || !data) return 0;
  while (glGetError() != GL_NO_ERROR) {}
  GLint previous = 0;
  glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &previous);
  if (!m_inlineStorageBuffer) glGenBuffers(1, &m_inlineStorageBuffer);
  if (!m_inlineStorageBuffer) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(previous));
    return 0;
  }
  // Orphan rather than re-use: the previous contents are still referenced by the last dispatch.
  m_inlineStorageBytes = byteCount > m_inlineStorageBytes ? byteCount : m_inlineStorageBytes;
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_inlineStorageBuffer);
  glBufferData(GL_SHADER_STORAGE_BUFFER, m_inlineStorageBytes, nullptr, GL_STREAM_DRAW);
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, byteCount, data);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(previous));
  if (glGetError() != GL_NO_ERROR) return 0;
  return m_inlineStorageBuffer;
}

bool GLDriver::DispatchCompute(ComputePipeline& pipelineBase,
                               const std::vector<ComputeBindingDesc>& dispatchBindings,
                               uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
  auto* pipeline = dynamic_cast<GLComputePipeline*>(&pipelineBase);
  if (!pipeline || !pipeline->ValidateBindings(dispatchBindings) || !SupportsComputeShaders() ||
      !groupCountX || !groupCountY || !groupCountZ) return false;

  const ComputeLimits& limits = ComputeLimitsCached();
  const uint32_t requested[3] = {groupCountX, groupCountY, groupCountZ};
  for (GLuint axis = 0; axis < 3; ++axis)
    if (requested[axis] > limits.maxWorkGroups[axis]) return false;

  const GLint maxTextureUnits = static_cast<GLint>(limits.maxTextureUnits);
  const GLint maxImageUnits = static_cast<GLint>(limits.maxImageUnits);
  std::unordered_set<const ComputeBindingLayoutDesc*> boundLayouts;
  std::unordered_map<uint32_t, const void*> sampledTextures;
  std::unordered_map<uint32_t, const void*> samplerTextures;
  for (const ComputeBindingDesc& binding : dispatchBindings) {
    const ComputeBindingLayoutDesc* layout = pipeline->Find(binding.type, binding.shaderRegister);
    if (!layout || !boundLayouts.insert(layout).second) return false;
    if (binding.type == ComputeBindingType::Constants32) {
      if (!binding.constants || binding.constantCount != layout->constantCount) return false;
    } else if (binding.type == ComputeBindingType::ReadOnlyBuffer && !binding.buffer) {
      // Inline bytes: the driver copies them into its persistent SSBO for this dispatch only.
      if (!binding.inlineData || !binding.inlineBytes || !binding.inlineStride ||
          binding.inlineBytes % binding.inlineStride) return false;
      if (layout->bindingIndex >= limits.maxStorageBlocks) return false;
    } else if (binding.type == ComputeBindingType::ReadOnlyBuffer ||
               binding.type == ComputeBindingType::ReadWriteBuffer) {
        if (!dynamic_cast<GLComputeBuffer*>(binding.buffer) ||
          (binding.type == ComputeBindingType::ReadWriteBuffer &&
           binding.buffer->descriptor.access != ComputeBufferAccess::ReadWrite)) return false;
    } else if (layout->dimension == ComputeTextureDimension::Tex3D) {
      auto* volume = dynamic_cast<GLComputeTexture3D*>(binding.texture3D);
      if (!volume || !volume->texture || !IsSupportedStorageFormat(volume->internalFormat)) return false;
      if (binding.type == ComputeBindingType::ReadOnlyTexture) {
        if (layout->bindingIndex >= static_cast<uint32_t>(maxTextureUnits) ||
            !sampledTextures.emplace(binding.shaderRegister, volume).second) return false;
      }
      if (binding.type == ComputeBindingType::ReadWriteTexture &&
          (!SupportsComputeTextures3D() || layout->bindingIndex >= static_cast<uint32_t>(maxImageUnits) ||
           volume->internalFormat != ToGLStorageFormat(layout->storageFormat))) return false;
    } else {
      auto* texture = dynamic_cast<GLTexture*>(binding.texture);
      if (!texture) return false;
      const bool cube = layout->dimension == ComputeTextureDimension::Cube;
      if (cube) {
        // An environment cube: a GL_TEXTURE_CUBE_MAP read through a samplerCube. Read-only.
        if (binding.type != ComputeBindingType::ReadOnlyTexture ||
            texture->glTarget != GL_TEXTURE_CUBE_MAP) {
          T8_LOG_ERROR("[GL][Compute] Cube t%u does not match: type=%d target=0x%X",
                       binding.shaderRegister, static_cast<int>(binding.type), texture->glTarget);
          return false;
        }
      } else if (texture->glTarget != GL_TEXTURE_2D) return false;
      if (binding.type == ComputeBindingType::ReadOnlyTexture) {
        if (layout->bindingIndex >= static_cast<uint32_t>(maxTextureUnits) ||
            !sampledTextures.emplace(binding.shaderRegister, texture).second) return false;
      }
      if (binding.type == ComputeBindingType::ReadWriteTexture &&
          (!SupportsComputeTextures() || layout->bindingIndex >= static_cast<uint32_t>(maxImageUnits) ||
           !IsSupportedStorageFormat(texture->glInternalFormat))) return false;
      if (binding.type == ComputeBindingType::ReadWriteTexture) {
        for (const auto& reflected : pipeline->bindingLayout) {
          if (reflected.type == binding.type && reflected.shaderRegister == binding.shaderRegister &&
              texture->glInternalFormat != ToGLStorageFormat(reflected.storageFormat))
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
  std::vector<std::pair<GLenum, GLuint>> bufferBindings;
  std::vector<TextureBindingRestore> textureBindings;
  std::vector<std::pair<GLuint, GLint>> samplerBindings;
  std::vector<GLuint> imageBindings;
  glUseProgram(pipeline->program);
  bool bindingsBound = true;
  for (const ComputeBindingDesc& binding : dispatchBindings) {
    const ComputeBindingLayoutDesc* layout = pipeline->Find(binding.type, binding.shaderRegister);
    if (binding.type == ComputeBindingType::Constants32) {
      const uint32_t byteCount = binding.constantCount * static_cast<uint32_t>(sizeof(uint32_t));
      const uint32_t offset = AllocateDispatchConstants(binding.constants, byteCount);
      if (offset == UINT32_MAX) {
        T8_LOG_ERROR("[GL][Compute] Transient constant block (%u bytes) could not be allocated", byteCount);
        bindingsBound = false;
        break;
      }
      glBindBufferRange(GL_UNIFORM_BUFFER, layout->bindingIndex, m_dispatchConstantsBuffer,
                        offset, byteCount);
      bufferBindings.emplace_back(GL_UNIFORM_BUFFER, layout->bindingIndex);
    } else if (binding.type == ComputeBindingType::ReadOnlyBuffer && !binding.buffer) {
      const GLuint inlineBuffer = EnsureInlineStorageBuffer(binding.inlineBytes, binding.inlineData);
      if (!inlineBuffer) {
        T8_LOG_ERROR("[GL][Compute] Inline buffer (%u bytes) could not be uploaded",
                     binding.inlineBytes);
        bindingsBound = false;
        break;
      }
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, layout->bindingIndex, inlineBuffer);
      bufferBindings.emplace_back(GL_SHADER_STORAGE_BUFFER, layout->bindingIndex);
    } else if (binding.type == ComputeBindingType::ReadOnlyBuffer ||
               binding.type == ComputeBindingType::ReadWriteBuffer) {
      auto* buffer = static_cast<GLComputeBuffer*>(binding.buffer);
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, layout->bindingIndex, buffer->buffer);
      bufferBindings.emplace_back(GL_SHADER_STORAGE_BUFFER, layout->bindingIndex);
    } else if (layout->dimension == ComputeTextureDimension::Tex3D &&
               binding.type == ComputeBindingType::ReadOnlyTexture) {
      auto* volume = static_cast<GLComputeTexture3D*>(binding.texture3D);
      glActiveTexture(GL_TEXTURE0 + layout->bindingIndex);
      GLint previousTexture = 0;
      glGetIntegerv(GL_TEXTURE_BINDING_3D, &previousTexture);
      textureBindings.push_back({layout->bindingIndex, GL_TEXTURE_3D, previousTexture});
      glBindTexture(GL_TEXTURE_3D, volume->texture);
    } else if (layout->dimension == ComputeTextureDimension::Tex3D) {
      auto* volume = static_cast<GLComputeTexture3D*>(binding.texture3D);
      // A GL_TEXTURE_3D is not an array texture, so `layered` does not apply: the image unit
      // always carries the whole level and ivec3 coordinates address x, y and z.
      glBindImageTexture(layout->bindingIndex, volume->texture, 0, GL_FALSE, 0,
                         GL_WRITE_ONLY, volume->internalFormat);
      imageBindings.push_back(layout->bindingIndex);
    } else if (binding.type == ComputeBindingType::ReadOnlyTexture) {
      auto* texture = static_cast<GLTexture*>(binding.texture);
      glActiveTexture(GL_TEXTURE0 + layout->bindingIndex);
      GLint previousTexture = 0;
      // Query the binding point of the target being replaced: a cube is not GL_TEXTURE_2D.
      glGetIntegerv(texture->glTarget == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_BINDING_CUBE_MAP
                                                             : GL_TEXTURE_BINDING_2D,
                    &previousTexture);
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

  if (bindingsBound) glDispatchCompute(groupCountX, groupCountY, groupCountZ);
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
  glUseProgram(static_cast<GLuint>(previousProgram));
  if (!bindingsBound) return false;
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