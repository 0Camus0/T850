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
      std::cout << "Texture [" << filepath << "] not found, loading checker" << std::endl;
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

    LoadAPITexture(T8DeviceContext, buffer);
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

  bool ShaderBase::CreateShader(std::string src_vs, std::string src_fs, unsigned long long sig) {
    if (sig != T8_NO_SIGNATURE) {
      std::string Defines = "";

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
        Defines += "#version 130\n\n";

		Defines += "#define lowp \n\n";
		Defines += "#define mediump \n\n";
		Defines += "#define highp \n\n";
      }
#endif
#if VDEBUG_NO_LIGHT
      Defines += "#define NO_LIGHT\n\n";
#endif

#if VDEBUG_SIMPLE_COLOR
      Defines += "#define SIMPLE_COLOR\n\n";
#endif

      if (sig&Signature::HAS_NORMALS)
        Defines += "#define USE_NORMALS\n\n";
      if (sig&Signature::HAS_TEXCOORDS0)
        Defines += "#define USE_TEXCOORD0\n\n";
      if (sig&Signature::HAS_TEXCOORDS1)
        Defines += "#define USE_TEXCOORD1\n\n";
      if (sig&Signature::HAS_TANGENTS)
        Defines += "#define USE_TANGENTS\n\n";
      if (sig&Signature::HAS_BINORMALS)
        Defines += "#define USE_BINORMALS\n\n";
      if (sig&Signature::DIFFUSE_MAP)
        Defines += "#define DIFFUSE_MAP\n\n";
      if (sig&Signature::SPECULAR_MAP)
        Defines += "#define SPECULAR_MAP\n\n";
      if (sig&Signature::GLOSS_MAP)
        Defines += "#define GLOSS_MAP\n\n";
      if (sig&Signature::NORMAL_MAP)
        Defines += "#define NORMAL_MAP\n\n";
      if (sig&Signature::REFLECT_MAP)
        Defines += "#define REFLECT_MAP\n\n";
      if (sig&Signature::HEIGHT_MAP)
        Defines += "#define HEIGHT_MAP\n\n";
      if (sig&Signature::USE_NO_LIGHT)
        Defines += "#define NO_LIGHT\n\n";
      if (sig&Signature::GBUFF_PASS)
        Defines += "#define G_BUFFER_PASS\n\n";
      if (sig&Signature::FSQUAD_1_TEX)
        Defines += "#define FSQUAD_1_TEX\n\n";
      if (sig&Signature::FSQUAD_2_TEX)
        Defines += "#define FSQUAD_2_TEX\n\n";
      if (sig&Signature::FSQUAD_3_TEX)
        Defines += "#define FSQUAD_3_TEX\n\n";
      if (sig&Signature::SHADOW_MAP_PASS)
        Defines += "#define SHADOW_MAP_PASS\n\n";
      if (!LinearDepth)
        Defines += "#define NON_LINEAR_DEPTH\n\n";
      if (sig&Signature::SHADOW_COMP_PASS)
        Defines += "#define SHADOW_COMP_PASS\n\n";
      if (sig&Signature::DEFERRED_PASS)
        Defines += "#define DEFERRED_PASS\n\n";
      if (sig&Signature::VERTICAL_BLUR_PASS)
        Defines += "#define VERTICAL_BLUR_PASS\n\n";
      if (sig&Signature::HORIZONTAL_BLUR_PASS)
        Defines += "#define HORIZONTAL_BLUR_PASS\n\n";
      if (sig&Signature::ONE_PASS_BLUR)
        Defines += "#define ONE_PASS_BLUR\n\n";
      if (sig&Signature::BRIGHT_PASS)
        Defines += "#define BRIGHT_PASS\n\n";
      if (sig&Signature::HDR_COMP_PASS)
        Defines += "#define HDR_COMP_PASS\n\n";
      if (sig&Signature::COC_PASS)
        Defines += "#define COC_PASS\n\n";
      if (sig&Signature::COMBINE_COC_PASS)
        Defines += "#define COMBINE_COC_PASS\n\n";
      if (sig&Signature::DOF_PASS)
        Defines += "#define DOF_PASS\n\n";
      if (sig&Signature::DOF_PASS_2)
        Defines += "#define DOF_PASS_2\n\n";
      if (sig&Signature::VIGNETTE_PASS)
        Defines += "#define VIGNETTE_PASS\n\n";
      if (sig&Signature::GOD_RAY_CALCULATION_PASS)
        Defines += "#define GOD_RAY_CALCULATION_PASS\n\n";
      if (sig&Signature::GOD_RAY_BLEND_PASS)
        Defines += "#define GOD_RAY_BLEND_PASS\n\n";
      if (sig&Signature::SSAO_PASS)
        Defines += "#define SSAO_PASS\n\n";
      if (sig&Signature::RAY_MARCH)
        Defines += "#define RAY_MARCH\n\n";
      if (sig&Signature::DEPTH_PRE_PASS)
        Defines += "#define DEPTH_PRE_PASS\n\n";
      if (sig&Signature::LIGHT_RAY_MARCHING)
        Defines += "#define LIGHT_RAY_MARCHING\n\n";
      if (sig&Signature::LIGHT_ADD)
        Defines += "#define LIGHT_ADD\n\n";
      if (sig&Signature::FADE_PASS)
        Defines += "#define FADE\n\n";
      
      
      
      
      
      
      

      if (!LinearDepth)
        Defines += "#define NON_LINEAR_DEPTH\n\n";

      //	cout << "Compiling with the following signature[ " << Defines << endl << "]" << endl;

      src_vs = Defines + src_vs;
      src_fs = Defines + src_fs;
    }
    this->Sig = sig;
    return CreateShaderAPI(src_vs, src_fs, sig);
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
  ShaderBase * BaseDriver::GetShaderSig(unsigned long long sig)
  {
    for (unsigned int i = 0; i < m_signatureShaders.size(); i++) {
      if (m_signatureShaders[i]->Sig == sig) {
        return m_signatureShaders[i];
      }
    }
    return nullptr;
  }
  ShaderBase * BaseDriver::GetShaderIdx(int id)
  {
    if (id < 0 || id >= (int)m_signatureShaders.size()) {
      printf("Warning null ptr ShaderBase Idx\n");
      return nullptr;
    }

    return m_signatureShaders[id];
  }
  Texture * BaseDriver::GetTexture(int id)
  {
    if (id < 0 || id >= (int)Textures.size()) {
      printf("Warning null ptr Textures Idx\n");
      return 0;
    }

    return Textures[id];
  }
  void BaseDriver::DestroyShaders()
  {
    for (unsigned int i = 0; i < m_signatureShaders.size(); i++) {
      m_signatureShaders[i]->release();
      m_signatureShaders[i] = nullptr;
    }
    m_signatureShaders.clear();
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
      Textures[i]->release();
      Textures[i] = nullptr;
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
    if (id >= 0 && id < (int)m_signatureShaders.size()) {
      if (m_signatureShaders[id] != nullptr) {
        m_signatureShaders[id]->release();
        m_signatureShaders[id] = nullptr;
      }
    }

  }
  int BaseDriver::CreateTexture(std::string path)
  {
    for (unsigned int i = 0; i < Textures.size(); i++) {
      if (Textures[i]->filepath == path) {
        return i;
      }
    }
    Texture *pTex = T8Device->CreateTexture(path);
     Textures.push_back(pTex);
    return (Textures.size() - 1);
  }
  int BaseDriver::CreateCubeMap(const unsigned char * buff, int w, int h)
  {
    Texture *pTex = T8Device->CreateCubeMap(buff,w,h);
    Textures.push_back(pTex);
    return (Textures.size() - 1);
  }
  int BaseDriver::CreateShader(std::string src_vs, std::string src_fs, unsigned long long sig)
  {
    if (sig != T8_NO_SIGNATURE) {
      for (unsigned int i = 0; i < m_signatureShaders.size(); i++) {
        if (m_signatureShaders[i]->Sig == sig) {
          return i;
        }
      }
    }
    ShaderBase* shader = T8Device->CreateShader(src_vs, src_fs, sig);
    if (shader != nullptr) {
      m_signatureShaders.push_back(shader);
      return (m_signatureShaders.size() - 1);
    }
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
      return (RTs.size() - 1);
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
