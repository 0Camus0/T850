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
#include <video/BaseDriver.h>
#include <utils/cil.h>
#include <utils/Log.h>
#include <iostream>
#include <string>
#include <fstream>
#include <string.h>

namespace t800 {
  BaseDriver*	g_pBaseDriver = 0;
  Device*           T8Device;	// Device for create resources
  DeviceContext*    T8DeviceContext; // Context to set and manipulate the resources

#include <utils/Checker.h>

  bool		Texture::LoadTexture(const char *fn) {
    bool found = false;
    std::string path = "Textures/";
    filepath = path + std::string(fn);
    std::ifstream inf(filepath.c_str());
    found = inf.good();
    inf.close();

    int x = 0, y = 0;
    unsigned char *buffer = 0;

    if (!found) {
      buffer = (unsigned char*)g_chkr.pixel_data;
      x = g_chkr.width;
      y = g_chkr.height;
      m_channels = g_chkr.bytes_per_pixel;
      T8_LOG_ERROR("Texture '%s' not found, loading checker", filepath.c_str());
    }
    else {
      //buffer = stbi_load(filepath.c_str(), &x, &y, &channels, 0);
      cil_props = 0;
      buffer = cil_load((filepath.c_str()), &x, &y, &mipmaps, &cil_props, &size);
    }

    if (!buffer)
      return false;

    bounded = 1;
    this->x = x;
    this->y = y;
    this->params = params;
    props = 0;

    if (cil_props&CIL_RGBA) {
      props |= TEXT_BASIC_FORMAT::CH_RGBA;
      m_channels = 4;
    }
    else {
      props |= TEXT_BASIC_FORMAT::CH_RGB;
      m_channels = 3;
    }

    memcpy(&optname[0], fn, strlen(fn));
    optname[strlen(fn)] = '\0';

    if (cil_props & CIL_COMPRESSED) {
      LoadAPITextureCompressed(buffer);
    } else {
      LoadAPITexture(T8DeviceContext, buffer);
    }
    if (found) {
      cil_free_buffer(buffer);
    }

    return true;
  }

  bool Texture::LoadFromMemory(const unsigned char * buff, int w, int h, int channels)
  {
    m_channels = channels;
    cil_props = 0;

    if (!buff)
      return false;

    bounded = 1;
    this->x = w;
    this->y = h;
    this->params = params;
    props = 0;

    if (channels == 4) {
      props |= TEXT_BASIC_FORMAT::CH_RGBA;
    }
    else if (channels == 3){
      props |= TEXT_BASIC_FORMAT::CH_RGB;
    }
    else if (channels == 1) {
      props |= TEXT_BASIC_FORMAT::CH_ALPHA;
    }

    LoadAPITexture(T8DeviceContext, const_cast<unsigned char*>(buff));

    return true;
  }

  bool Texture::CreateCubeMap(const unsigned char * buff, int w, int h)
  {
    m_channels = 4;
    cil_props = CIL_CUBE_MAP;
    bounded = 1;
    this->x = w;
    this->y = h;
    this->params = params;
    props = 0;

    if (m_channels == 4) {
      props |= TEXT_BASIC_FORMAT::CH_RGBA;
    }
    else if (m_channels == 3) {
      props |= TEXT_BASIC_FORMAT::CH_RGB;
    }
    else if (m_channels == 1) {
      props |= TEXT_BASIC_FORMAT::CH_ALPHA;
    }
    LoadAPITexture(T8DeviceContext, const_cast<unsigned char*>(buff));
    return true;
  }

  void Texture::release() {
    DestroyAPITexture();
    delete this;
  }

  bool BaseRT::LoadRT(int nrt, int cf, int df, int w, int h, bool GenMips) {
    this->number_RT = nrt;
    this->color_format = cf;
    this->depth_format = df;
    this->w = w;
    this->h = h;
    this->GenMips = GenMips;
    return LoadAPIRT();
  }

  void BaseRT::release() {
    DestroyAPIRT();
    delete this;
  }

  bool ShaderBase::CreateShader(std::string src_vs, std::string src_fs, ShaderKey key, const std::string& vs_name, const std::string& fs_name) {
    std::string Defines;
    if (key.isValid()) {

      bool LinearDepth = true;

#if defined(USING_OPENGL_ES20)
      LinearDepth = true; // Force for ES 2.0
#endif

#if defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)
      if (g_pBaseDriver->m_currentAPI == GRAPHICS_API::OPENGL) {
        Defines += "#version 300 es\n\n";
        Defines += "#define ES_30\n\n";
      }
#else
      if (g_pBaseDriver->m_currentAPI == GRAPHICS_API::OPENGL) {
        Defines += "#version 300 es\n\n";
        Defines += "#define ES_30\n\n";
      }
#endif
#if VDEBUG_NO_LIGHT
      Defines += "#define NO_LIGHT\n\n";
#endif
#if VDEBUG_SIMPLE_COLOR
      Defines += "#define SIMPLE_COLOR\n\n";
#endif

      // Vertex attributes
      if (key.has(ShaderKey::HAS_NORMALS))    Defines += "#define USE_NORMALS\n\n";
      if (key.has(ShaderKey::HAS_TEXCOORD0))  Defines += "#define USE_TEXCOORD0\n\n";
      if (key.has(ShaderKey::HAS_TEXCOORD1))  Defines += "#define USE_TEXCOORD1\n\n";
      if (key.has(ShaderKey::HAS_TANGENTS))   Defines += "#define USE_TANGENTS\n\n";
      if (key.has(ShaderKey::HAS_BINORMALS))  Defines += "#define USE_BINORMALS\n\n";

      // Texture maps
      if (key.has(ShaderKey::DIFFUSE_MAP))    Defines += "#define DIFFUSE_MAP\n\n";
      if (key.has(ShaderKey::SPECULAR_MAP))   Defines += "#define SPECULAR_MAP\n\n";
      if (key.has(ShaderKey::GLOSS_MAP))      Defines += "#define GLOSS_MAP\n\n";
      if (key.has(ShaderKey::NORMAL_MAP))     Defines += "#define NORMAL_MAP\n\n";
      if (key.has(ShaderKey::REFLECT_MAP))    Defines += "#define REFLECT_MAP\n\n";
      if (key.has(ShaderKey::HEIGHT_MAP))     Defines += "#define HEIGHT_MAP\n\n";
      if (key.has(ShaderKey::METALLIC_MAP))   Defines += "#define METALLIC_MAP\n\n";

      // Special modes
      if (key.has(ShaderKey::NO_LIGHT))       Defines += "#define NO_LIGHT\n\n";
      if (key.has(ShaderKey::OMNI_SHADOWS))   Defines += "#define OMNIDIRECTIONAL_SH\n\n";

      // Effect toggles
      if (key.has(ShaderKey::PARALLAX))       Defines += "#define ENABLE_PARALLAX\n\n";
      if (key.has(ShaderKey::SHADOWS))        Defines += "#define ENABLE_SHADOWS\n\n";
      if (key.has(ShaderKey::SSAO))           Defines += "#define ENABLE_SSAO\n\n";
      if (key.has(ShaderKey::AUTO_FOCUS))     Defines += "#define AUTO_FOCUS\n\n";
      if (key.has(ShaderKey::GOD_RAYS))       Defines += "#define ENABLE_GOD_RAYS\n\n";

      // Pass type
      switch (key.getPass()) {
      case PassType::FORWARD:            break; // default forward path, no define needed
      case PassType::GBUFFER:            Defines += "#define G_BUFFER_PASS\n\n"; break;
      case PassType::SHADOW_MAP:         Defines += "#define SHADOW_MAP_PASS\n\n"; break;
      case PassType::FSQUAD_1_TEX:       Defines += "#define FSQUAD_1_TEX\n\n"; break;
      case PassType::FSQUAD_2_TEX:       Defines += "#define FSQUAD_2_TEX\n\n"; break;
      case PassType::FSQUAD_3_TEX:       Defines += "#define FSQUAD_3_TEX\n\n"; break;
      case PassType::DEFERRED:           Defines += "#define DEFERRED_PASS\n\n"; break;
      case PassType::SHADOW_COMP:        Defines += "#define SHADOW_COMP_PASS\n\n"; break;
      case PassType::VERTICAL_BLUR:      Defines += "#define VERTICAL_BLUR_PASS\n\n"; break;
      case PassType::HORIZONTAL_BLUR:    Defines += "#define HORIZONTAL_BLUR_PASS\n\n"; break;
      case PassType::ONE_PASS_BLUR:      Defines += "#define ONE_PASS_BLUR\n\n"; break;
      case PassType::BRIGHT:             Defines += "#define BRIGHT_PASS\n\n"; break;
      case PassType::HDR_COMP:           Defines += "#define HDR_COMP_PASS\n\n"; break;
      case PassType::LUMINANCE_MAP:      Defines += "#define LUMINANCE_MAP_PASS\n\n"; break;
      case PassType::ADAPT_LUMINANCE:    Defines += "#define ADAPT_LUMINANCE_PASS\n\n"; break;
      case PassType::COC:                Defines += "#define COC_PASS\n\n"; break;
      case PassType::COMBINE_COC:        Defines += "#define COMBINE_COC_PASS\n\n"; break;
      case PassType::DOF:                Defines += "#define DOF_PASS\n\n"; break;
      case PassType::DOF_2:              Defines += "#define DOF_PASS_2\n\n"; break;
      case PassType::BACKBUFFER:         Defines += "#define BACKBUFFER_PASS\n\n"; break;
      case PassType::GOD_RAY_CALCULATION: Defines += "#define GOD_RAY_CALCULATION_PASS\n\n"; break;
      case PassType::GOD_RAY_BLEND:      Defines += "#define GOD_RAY_BLEND_PASS\n\n"; break;
      case PassType::SSAO:               Defines += "#define SSAO_PASS\n\n"; break;
      case PassType::RAY_MARCH:          Defines += "#define RAY_MARCH\n\n"; break;
      case PassType::RADIAL_DEPTH:       Defines += "#define RADIAL_DEPTH_PASS\n\n"; break;
      case PassType::LIGHT_RAY_MARCHING: Defines += "#define LIGHT_RAY_MARCHING\n\n"; break;
      case PassType::LIGHT_ADD:          Defines += "#define LIGHT_ADD\n\n"; break;
      case PassType::FADE:               Defines += "#define FADE\n\n"; break;
      default: break;
      }

      if (!LinearDepth)
        Defines += "#define NON_LINEAR_DEPTH\n\n";

      src_vs = Defines + src_vs;
      src_fs = Defines + src_fs;
    }
    this->key = key;
    if (!CreateShaderAPI(src_vs, src_fs, vs_name, fs_name)) {
      T8_LOG_ERROR("Shader defines for failed key 0x%08X [VS='%s' FS='%s']:\n%s", key.bits, vs_name.c_str(), fs_name.c_str(), Defines.c_str());
      return false;
    }
    return true;
  }
  void ShaderBase::release()
  {
    DestroyAPIShader();
    delete this;
  }
  Texture * BaseDriver::GetRTTexture(int id, int index)
  {
    if (id < 0 || id >= (int)RTs.size())
      exit(666);

    if (index == DEPTH_ATTACHMENT) {
      return RTs[id]->pDepthTexture;
    }
    else {
      return RTs[id]->vColorTextures[index];
    }
  }
  ShaderBase * BaseDriver::GetShader(ShaderKey key)
  {
    auto it = m_shaderCache.find(key.bits);
    if (it != m_shaderCache.end())
      return it->second;
    fprintf(stderr, "[ShaderKey] GetShader miss: key 0x%08X (pass=%d)\n", key.bits, key.getPass());
    T8_LOG_ERROR("GetShader miss: key 0x%08X (pass=%d)", key.bits, key.getPass());
    return nullptr;
  }
  ShaderBase * BaseDriver::GetShaderIdx(int id)
  {
    if (id < 0 || id >= (int)m_shaders.size()) {
      T8_LOG_ERROR("GetShaderIdx: invalid id %d (size=%d)", id, (int)m_shaders.size());
      return nullptr;
    }

    return m_shaders[id];
  }
  Texture * BaseDriver::GetTexture(int id)
  {
    if (id < 0 || id >= (int)Textures.size()) {
      T8_LOG_ERROR("GetTexture: invalid id %d (size=%d)", id, (int)Textures.size());
      return 0;
    }

    return Textures[id];
  }
  void BaseDriver::DestroyShaders()
  {
    for (unsigned int i = 0; i < m_shaders.size(); i++) {
      m_shaders[i]->release();
      m_shaders[i] = nullptr;
    }
    m_shaders.clear();
    m_shaderCache.clear();
  }
  void BaseDriver::DestroyRTs()
  {
    for (unsigned int i = 0; i < RTs.size(); i++) {
      BaseRT *pRT = RTs[i];
      pRT->release();
      pRT = nullptr;
    }
    RTs.clear();
  }
  int BaseDriver::CreateTechnique(std::string path)
  {
    int i = 0;
    for (auto &it : m_techniques) {
      if (it->info.m_path == path)
        return i;
      i++;
    }
    m_techniques.push_back(std::move(new T8Technique(path)));
	return (int)m_techniques.size();
  }
  void BaseDriver::PushRT(int id)
  {
    if (id < 0 || id >= (int)RTs.size())
      return;

    T8_LOG_TRACE("[BaseDriver] PushRT(%d) colors=%d %dx%d", id, RTs[id]->number_RT, RTs[id]->w, RTs[id]->h);
    CurrentRT = id;
    RTs[id]->Set(*T8DeviceContext);
  }
  T8Technique * BaseDriver::GetTechnique(int id)
  {
    if (id < (int)m_techniques.size())
      return m_techniques[id];
    return nullptr;
  }
  void BaseDriver::DestroyRT(int id)
  {
    if (id < 0 || id >= (int)RTs.size())
      return;

    if (RTs[id] != nullptr) {
      RTs[id]->release();
      RTs[id] = nullptr;
    }
  }
  void BaseDriver::DestroyTextures()
  {
    for (unsigned int i = 0; i < Textures.size(); i++) {
      if (Textures[i]) {
        Textures[i]->release();
        Textures[i] = nullptr;
      }
    }
    Textures.clear();
  }
  void BaseDriver::DestroyTexture(int id)
  {
    if (id < (int)Textures.size() && id >= 0) {
      if (Textures[id] != nullptr) {
        Textures[id]->release();
        Textures[id] = nullptr;
      }
    }
  }
  void BaseDriver::DestroyTechniques()
  {
    for (auto &it : m_techniques) {
      it->release();
      delete it;
    }
    m_techniques.clear();
  }
  void BaseDriver::DestroyTechnique(int id)
  {
    if (id >= 0 && id < (int)m_techniques.size()) {
      if (m_techniques[id] != nullptr) {
        m_techniques[id]->release();
        m_techniques[id] = nullptr;
      }
    }
  }
  void BaseDriver::DestroyShader(int id)
  {
    if (id >= 0 && id < (int)m_shaders.size()) {
      if (m_shaders[id] != nullptr) {
        if (m_shaders[id]->key.isValid())
          m_shaderCache.erase(m_shaders[id]->key.bits);
        m_shaders[id]->release();
        m_shaders[id] = nullptr;
      }
    }
  }
  int BaseDriver::CreateTexture(std::string path)
  {
    int firstFreeSlot = -1;
    for (unsigned int i = 0; i < Textures.size(); i++) {
      if (Textures[i] == nullptr) {
        if (firstFreeSlot < 0) firstFreeSlot = i;
        continue;
      }
      if (Textures[i]->filepath == "Textures/" + path) {
        return i;
      }
    }
    Texture *pTex = T8Device->CreateTexture(path);
    if (firstFreeSlot >= 0) {
      Textures[firstFreeSlot] = pTex;
      T8_LOG_DEBUG("Texture created: '%s' -> slot %d (%dx%d)", path.c_str(), firstFreeSlot, pTex->x, pTex->y);
      return firstFreeSlot;
    }
    Textures.push_back(pTex);
    T8_LOG_DEBUG("Texture created: '%s' -> slot %d (%dx%d)", path.c_str(), (int)(Textures.size()-1), pTex->x, pTex->y);
    return static_cast<int>(Textures.size() - 1);
  }
  int BaseDriver::CreateCubeMap(const unsigned char * buff, int w, int h)
  {
    Texture *pTex = T8Device->CreateCubeMap(buff,w,h);
    Textures.push_back(pTex);
    return static_cast<int>(Textures.size() - 1);
  }
  int BaseDriver::CreateShader(std::string src_vs, std::string src_fs, ShaderKey key, const std::string& vs_name, const std::string& fs_name)
  {
    if (key.isValid()) {
      auto it = m_shaderCache.find(key.bits);
      if (it != m_shaderCache.end()) {
        // Already compiled — find its index
        for (int i = 0; i < (int)m_shaders.size(); i++) {
          if (m_shaders[i] == it->second)
            return i;
        }
      }
    }
    ShaderBase* shader = T8Device->CreateShader(src_vs, src_fs, key, vs_name, fs_name);
    if (shader != nullptr) {
      m_shaders.push_back(shader);
      int idx = static_cast<int>(m_shaders.size() - 1);
      if (key.isValid()) {
        m_shaderCache[key.bits] = shader;
        T8_LOG_DEBUG("Shader compiled: key=0x%08X pass=%d -> idx %d", key.bits, key.getPass(), idx);
      }
      return idx;
    }
    T8_LOG_ERROR("Shader compilation FAILED: key=0x%08X pass=%d", key.bits, key.getPass());
    return -1;
  }
  int BaseDriver::CreateRT(int nrt, int cf, int df, int w, int h, bool genMips)
  {
    if (w == 0)
      w = width;
    if (h == 0)
      h = height;
    BaseRT	*pRT = T8Device->CreateRT(nrt,cf,df,w,h,genMips);
    pRT->number_RT = nrt;
    if (pRT!= nullptr) {
      RTs.push_back(pRT);
      T8_LOG_DEBUG("RenderTarget created: handle %d (%dx%d, %d color attachments)", (int)(RTs.size()-1), w, h, nrt);
      return static_cast<int>(RTs.size() - 1);
    }
    return -1;
  }
  void BaseDriver::ModifyRT(int RTID, int nrt, int cf, int df, int w, int h, bool genMips)
  {
    DestroyRT(RTID);
    if (w == 0)
      w = width;
    if (h == 0)
      h = height;
    BaseRT	*pRT = T8Device->CreateRT(nrt, cf, df, w, h, genMips);
    pRT->number_RT = nrt;
    RTs[RTID] = pRT;
  }
}
