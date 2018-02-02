#include <video/GLShader.h>
#include <utils/Utils.h>
#include "video/GLDriver.h"


namespace t800 {
#define BUFFER_OFFSET(i) ((char *)NULL + (i))
  bool GLShader::CreateShaderAPI(std::string src_vs, std::string src_fs, unsigned long long sig) {

    ShaderProg = glCreateProgram();

    vshader_id = createShader(GL_VERTEX_SHADER, (char*)src_vs.c_str());
    fshader_id = createShader(GL_FRAGMENT_SHADER, (char*)src_fs.c_str());

    glAttachShader(ShaderProg, vshader_id);
    glAttachShader(ShaderProg, fshader_id);

    glLinkProgram(ShaderProg);
    glUseProgram(ShaderProg);

    m_parser.ParseFromMemory(src_vs, src_fs);
    int vertexDeclPos = 0;
    for (auto &it : m_parser.attributes)
    {
      int size = 0;
      t800::InputElement ie;
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

    int uniformPos = 0;
    for (auto &it : m_parser.uniforms)
    {
      bool process = true;
      for (auto &other : internalUniformsLocs) {
        if (other.name == it.name) {
          process = false;
          continue;
        }
      }
      if (process) {
        int size = 0;
        t800::InputElement ie;
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
        ie.num = it.numItems;
        ie.name = it.name;
        ie.loc = glGetUniformLocation(ShaderProg, ie.name.c_str());
        ie.type = it.type;
        ie.bufferBytePosition = uniformPos;
        ie.size = size * ie.num;
        if (ie.loc != -1)
        {
          internalUniformsLocs.push_back(ie);
        }
        uniformPos += ie.size;
      }
    }

    return true;
  }

  void GLShader::Set(const DeviceContext & deviceContext)
  {
    const_cast<DeviceContext*>(&deviceContext)->actualShaderSet = (ShaderBase*)this;
    int stride = reinterpret_cast<const GLDeviceContext*>(&deviceContext)->internalStride;
    glUseProgram(ShaderProg);

    for (auto& it : locs)
    {
      glEnableVertexAttribArray(it.loc);
      glVertexAttribPointer(it.loc, it.size, GL_FLOAT, GL_FALSE, stride, BUFFER_OFFSET(it.bufferBytePosition));
    }
  }
  void GLShader::DestroyAPIShader()
  {
    glDeleteShader(vshader_id);
    glDeleteShader(fshader_id);
    glDeleteProgram(ShaderProg);
  }
}
