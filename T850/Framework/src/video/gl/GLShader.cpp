#include <pch.h>
#include <video/gl/GLShader.h>
#include <utils/Utils.h>
#include <utils/Log.h>
#include <utils/ShaderDiskCache.h>
#include <video/gl/GLDriver.h>
#include <debug/RenderTrace.h>


namespace t850 {
#define BUFFER_OFFSET(i) ((char *)NULL + (i))

  namespace {
    std::string SafeGLString(GLenum name) {
      const GLubyte* value = glGetString(name);
      return value ? reinterpret_cast<const char*>(value) : "";
    }

    bool GLProgramBinarySupported() {
      GLint count = 0;
      glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &count);
      return count > 0;
    }

    std::string GetOpenGLShaderCacheDriverSignature() {
      std::ostringstream sig;
      sig << "opengl;vendor=" << SafeGLString(GL_VENDOR)
          << ";renderer=" << SafeGLString(GL_RENDERER)
          << ";version=" << SafeGLString(GL_VERSION)
          << ";glsl=" << SafeGLString(GL_SHADING_LANGUAGE_VERSION);

      GLint count = 0;
      glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &count);
      sig << ";binaryFormats=" << count;
      if (count > 0) {
        std::vector<GLint> formats(static_cast<size_t>(count));
        glGetIntegerv(GL_PROGRAM_BINARY_FORMATS, formats.data());
        for (GLint format : formats)
          sig << ":" << format;
      }
      return sig.str();
    }

    bool TryLoadProgramBinary(const ShaderDiskCacheKey& cacheKey, GLuint& program) {
      std::vector<uint8_t> bytes;
      if (!ShaderDiskCache::LoadArtifact(cacheKey, "program.glbin", bytes) || bytes.size() <= sizeof(GLenum))
        return false;

      GLenum format = 0;
      std::memcpy(&format, bytes.data(), sizeof(GLenum));
      GLuint candidate = glCreateProgram();
      glProgramBinary(candidate, format, bytes.data() + sizeof(GLenum), static_cast<GLsizei>(bytes.size() - sizeof(GLenum)));

      GLint linkStatus = 0;
      glGetProgramiv(candidate, GL_LINK_STATUS, &linkStatus);
      if (!linkStatus) {
        glDeleteProgram(candidate);
        return false;
      }

      program = candidate;
      return true;
    }

    void StoreProgramBinary(const ShaderDiskCacheKey& cacheKey, GLuint program) {
      if (!GLProgramBinarySupported())
        return;

      GLint binaryLength = 0;
      glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &binaryLength);
      if (binaryLength <= 0)
        return;

      std::vector<uint8_t> bytes(sizeof(GLenum) + static_cast<size_t>(binaryLength));
      GLenum format = 0;
      GLsizei written = 0;
      glGetProgramBinary(program, binaryLength, &written, &format, bytes.data() + sizeof(GLenum));
      if (written <= 0)
        return;
      std::memcpy(bytes.data(), &format, sizeof(GLenum));
      bytes.resize(sizeof(GLenum) + static_cast<size_t>(written));
      ShaderDiskCache::StoreArtifact(cacheKey, "program.glbin", bytes.data(), bytes.size());
    }
  }

  bool GLShader::CreateShaderAPI(std::string src_vs, std::string src_fs, const std::string& vs_name, const std::string& fs_name) {

    vshader_id = 0;
    fshader_id = 0;
    const std::string driverSignature = GetOpenGLShaderCacheDriverSignature();
    const ShaderDiskCacheKey cacheKey = ShaderDiskCache::MakeKey("opengl", driverSignature, key.bits, vs_name, fs_name, src_vs, src_fs);

    const bool binarySupported = GLProgramBinarySupported();
    if (binarySupported && TryLoadProgramBinary(cacheKey, ShaderProg)) {
      T8_LOG_DEBUG("[ShaderCache][GL] Program binary hit %s", cacheKey.sha1.c_str());
    }
    else {

      ShaderProg = glCreateProgram();
      if (binarySupported)
        glProgramParameteri(ShaderProg, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);

      vshader_id = createShader(GL_VERTEX_SHADER, (char*)src_vs.c_str());
      fshader_id = createShader(GL_FRAGMENT_SHADER, (char*)src_fs.c_str());

      glAttachShader(ShaderProg, vshader_id);
      glAttachShader(ShaderProg, fshader_id);

      glLinkProgram(ShaderProg);

      // Check link status
      GLint linkStatus = 0;
      glGetProgramiv(ShaderProg, GL_LINK_STATUS, &linkStatus);
      if (!linkStatus) {
        GLint logLen = 0;
        glGetProgramiv(ShaderProg, GL_INFO_LOG_LENGTH, &logLen);
        std::string infoLog(logLen + 1, '\0');
        glGetProgramInfoLog(ShaderProg, logLen, nullptr, &infoLog[0]);
        T8_LOG_ERROR("[GL] Shader link FAILED [VS='%s' FS='%s']:\n%s",
                     vs_name.c_str(), fs_name.c_str(), infoLog.c_str());
        return false;
      }

      StoreProgramBinary(cacheKey, ShaderProg);
      ShaderDiskCache::WriteManifest(cacheKey, driverSignature);
    }

    glUseProgram(ShaderProg);

    m_parser.ParseFromMemory(src_vs, src_fs);
    int vertexDeclPos = 0;
    for (auto &it : m_parser.attributes)
    {
      int size = 0;
      t850::InputElement ie;
      switch (it.type)
      {
      case hyperspace::shader::datatype_::INT_:
        size = 4;
        break;
      case hyperspace::shader::datatype_::BOOLEAN_:
        size = 4;
        break;
      case hyperspace::shader::datatype_::FLOAT_:
        size = 4;
        break;
      case hyperspace::shader::datatype_::MAT2_:
        size = 16;
        break;
      case hyperspace::shader::datatype_::MAT3_:
        size = 36;
        break;
      case hyperspace::shader::datatype_::MAT4_:
        size = 64;
        break;
      case hyperspace::shader::datatype_::VECTOR2_:
        size = 8;
        break;
      case hyperspace::shader::datatype_::VECTOR3_:
        size = 12;
        break;
      case hyperspace::shader::datatype_::VECTOR4_:
        size = 16;
        break;
      default:
        break;
      }
      ie.num = it.numItems;
      ie.name = it.name;
      ie.loc = glGetAttribLocation(ShaderProg, ie.name.c_str());
      ie.type = it.type;
      ie.bufferBytePosition = vertexDeclPos;
      ie.size = size/4;
      if (ie.loc != -1)
      {
        locs.push_back(ie);
      }
      vertexDeclPos += size;
    }

#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      // Stash the input layout for the tracer keyed by ShaderBase*; the
      // shader hasn't been registered yet (BaseDriver::CreateShader does
      // that after T8Device->CreateShader returns), so we can't use a
      // shader id here. Mirrors the D3D12/Vulkan path.
      std::vector<TraceShaderAttr> attrs;
      attrs.reserve(locs.size());
      for (const auto& it : locs) {
        TraceShaderAttr a;
        a.semantic   = it.name;
        a.location   = it.loc;
        a.input_slot = 0;
        a.offset     = (uint32_t)it.bufferBytePosition;
        a.size_bytes = (uint32_t)(it.size * 4);
        a.format     = "GL_FLOAT_VEC" + std::to_string(it.size);
        attrs.push_back(std::move(a));
      }
      g_renderTracer->RegisterShaderInputsForPtr(this, (uint32_t)vertexDeclPos, std::move(attrs));
    }
#endif

    int uniformPos = 0;
    std::vector<t850::InputElement> reflectedUniforms;
    for (auto &it : m_parser.uniforms)
    {
      int size = 0;
      switch (it.type)
      {
      case hyperspace::shader::datatype_::INT_:
        size = 4;
        break;
      case hyperspace::shader::datatype_::BOOLEAN_:
        size = 4;
        break;
      case hyperspace::shader::datatype_::FLOAT_:
        size = 4;
        break;
      case hyperspace::shader::datatype_::MAT2_:
        size = 16;
        break;
      case hyperspace::shader::datatype_::MAT3_:
        size = 36;
        break;
      case hyperspace::shader::datatype_::MAT4_:
        size = 64;
        break;
      case hyperspace::shader::datatype_::VECTOR2_:
        size = 8;
        break;
      case hyperspace::shader::datatype_::VECTOR3_:
        size = 12;
        break;
      case hyperspace::shader::datatype_::VECTOR4_:
        size = 16;
        break;
      default:
        continue;
        break;
      }

      t850::InputElement* reflected = nullptr;
      for (auto &other : reflectedUniforms) {
        if (other.name == it.name) {
          reflected = &other;
          break;
        }
      }

      if (!reflected) {
        t850::InputElement ie;
        ie.num = it.numItems;
        ie.name = it.name;
        ie.type = it.type;
        ie.bufferBytePosition = uniformPos;
        ie.size = size * ie.num;
        ie.loc = -1;
        reflectedUniforms.push_back(ie);
        reflected = &reflectedUniforms.back();
        uniformPos += ie.size;
      }

      int loc = glGetUniformLocation(ShaderProg, reflected->name.c_str());
      if (loc != -1) {
        bool active = false;
        for (auto &other : internalUniformsLocs) {
          if (other.name == reflected->name) {
            active = true;
            break;
          }
        }
        if (!active) {
          t850::InputElement ie = *reflected;
          ie.loc = loc;
          internalUniformsLocs.push_back(ie);
        }
      }
    }

    return true;
  }

  void GLShader::Set(const DeviceContext & deviceContext)
  {
    T8_LOG_TRACE("[GL] Shader::Set key=0x%016llX prog=%d", static_cast<unsigned long long>(key.bits), ShaderProg);
    const_cast<DeviceContext*>(&deviceContext)->actualShaderSet = (ShaderBase*)this;
    int stride = reinterpret_cast<const GLDeviceContext*>(&deviceContext)->internalStride;
    glUseProgram(ShaderProg);

    static bool sLoggedOnce = false;
    if (!sLoggedOnce && key.has(t850::ShaderKey::HAS_SKINNING)) {
      T8_LOG_INFO("[GL] Skinned shader attrs (stride=%d):", stride);
      for (auto& it : locs) {
        T8_LOG_INFO("[GL]   attr '%s' loc=%d size=%d bytePos=%d", it.name.c_str(), it.loc, it.size, it.bufferBytePosition);
      }
      T8_LOG_INFO("[GL] Skinned shader uniforms:");
      for (auto& it : internalUniformsLocs) {
        T8_LOG_INFO("[GL]   uniform '%s' loc=%d num=%d bytePos=%d", it.name.c_str(), it.loc, it.num, it.bufferBytePosition);
      }
      sLoggedOnce = true;
    }

    for (auto& it : locs)
    {
      glEnableVertexAttribArray(it.loc);
      glVertexAttribPointer(it.loc, it.size, GL_FLOAT, GL_FALSE, stride, BUFFER_OFFSET(it.bufferBytePosition));
    }

    // Disable vertex attribs not used by this shader to prevent stale
    // attribs from a previous shader leaking state (e.g., mesh → line).
    int maxLoc = 0;
    for (auto& it : locs)
      if (it.loc >= maxLoc) maxLoc = it.loc + 1;
    // Track previous high-water mark across Set() calls
    static int sPrevMaxAttrib = 0;
    for (int a = maxLoc; a < sPrevMaxAttrib; a++)
      glDisableVertexAttribArray(a);
    if (maxLoc > sPrevMaxAttrib) sPrevMaxAttrib = maxLoc;
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int shId = g_renderTracer->LookupShaderId(this);
      g_renderTracer->EvBindShader(shId, key.bits);
      // No PSO concept in GL — intentionally no EvBindPSO.
    }
#endif
  }
  void GLShader::DestroyAPIShader()
  {
    if (vshader_id) glDeleteShader(vshader_id);
    if (fshader_id) glDeleteShader(fshader_id);
    if (ShaderProg) glDeleteProgram(ShaderProg);
  }
}
