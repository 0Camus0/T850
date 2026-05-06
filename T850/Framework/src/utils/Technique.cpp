#include <pch.h>
#include <utils/Technique.h>
#include <iostream>
#include <video/gl/GLDriver.h>
#include <video/gl/GLShader.h>
#if defined(OS_WINDOWS) || defined(OS_ANDROID)
#include <video/d3d11/D3D11Shader.h>
#include <video/d3d11/D3D11Driver.h>
#endif
namespace t850 {
  void TechniqueInfo::ProcessDefine(tinyxml2::XMLElement * element)
  {
    const char* name = element->Attribute("name");
    if (name == nullptr) {
      std::cout << "error: atribute name not found on define" << std::endl;
      m_xmlDoc.Clear();
      return;
    }
    else {
      std::string def;
      def = "#define ";
      def += name;
      const char* value = element->Attribute("value");
      if (value != nullptr) {
        def += " ";
        def += value;
      }
      m_actualDefines->push_back(def);
    }

  }

  void TechniqueInfo::ProcessProfile(tinyxml2::XMLElement * element)
  {
    auto child = element->FirstChildElement("define");
    if (child != nullptr) {
      do {
        m_actualDefines = &m_profiles.back().m_defines;
        ProcessDefine(child);
        child = child->NextSiblingElement("define");
      } while (child != nullptr);
    }
    auto shader = element->FirstChildElement("shader");
    if (shader == nullptr) {
      std::cout << "error: shader not found on profile" << std::endl;
      m_xmlDoc.Clear();
      return;
    }
    else {
      do {
        ProcessShader(shader);
        shader = shader->NextSiblingElement("shader");
      } while (shader != nullptr);
    }
  }

  void TechniqueInfo::ProcessShader(tinyxml2::XMLElement * element)
  {
    const char* path = element->Attribute("path");
    if (path == nullptr) {
      std::cout << "error: atribute path not found on shader" << std::endl;
      m_xmlDoc.Clear();
      return;
    }
    else {
      const char* type = element->Attribute("type");
      if (type == nullptr) {
        std::cout << "error: atribute type not found on shader" << std::endl;
        m_xmlDoc.Clear();
        return;
      }
      else {
        if (std::string(type) == "VS")
          m_profiles.back().m_vsPath = path;
        else if (std::string(type) == "FS")
          m_profiles.back().m_fsPath = path;
      }
    }
  }


  TechniqueInfo::TechniqueInfo()
  {
  }
  TechniqueInfo::TechniqueInfo(std::string path)
  {
    Parse(path);
  }
  void TechniqueInfo::Parse(std::string path)
  {
    m_actualDefines = &m_globalDefines;
    m_xmlDoc.LoadFile(path.c_str());
    auto technique = m_xmlDoc.FirstChildElement("technique");
    const char* att = technique->Attribute("name");
    if (att == nullptr)
    {
      std::cout << "error: atribute name not found on technique" << std::endl;
      m_xmlDoc.Clear();
      return;
    }
    m_name = att;
    auto global = technique->FirstChildElement("global");
    if (global == nullptr) {
      std::cout << "warning: global label not found" << std::endl;
    }
    else {
      auto child = global->FirstChildElement("define");
      if (child == nullptr) {
        std::cout << "warning: no defines on global scope" << std::endl;
      }
      else {
        do {
          ProcessDefine(child);
          child = child->NextSiblingElement("define");
        } while (child != nullptr);
      }
    }
    //Profiles
    auto profile = technique->FirstChildElement("profile");
    if (profile == nullptr) {
      std::cout << "error: no profile found" << std::endl;
      m_xmlDoc.Clear();
      return;
    }
    else {
      do {
        TechniqueProfileInfo t8Prof;
        t8Prof.m_name = profile->Attribute("name");
        std::string type = profile->Attribute("type");
        if (type == "GLSL")
          t8Prof.m_type = TechniqueProfileType::GL;
        else if (type == "HLSL")
          t8Prof.m_type = TechniqueProfileType::HLSL;
        else if (type == "GLSL_ES2")
          t8Prof.m_type = TechniqueProfileType::GLES20;
        else if (type == "GLSL_ES3")
          t8Prof.m_type = TechniqueProfileType::GLES30;
        else {
          std::cout << "error: profile type not specified" << std::endl;
          m_xmlDoc.Clear();
          return;
        }

        m_profiles.push_back(t8Prof);
        ProcessProfile(profile);
        profile = profile->NextSiblingElement("profile");
      } while (profile != nullptr);
    }
    m_xmlDoc.Clear();
  }

  void TechniqueInfo::release()
  {
    m_xmlDoc.Clear();
  }



  //TODO: Separate Parser and implementation on diferent cpp
  void Technique::Load(std::string path)
  {
    m_currentProfile = nullptr;
    m_profiles.resize(TechniqueInfo::TechniqueProfileType::COUNT);
    info.Parse(path);
    for (auto it : info.m_profiles) {
      m_profiles[it.m_type].m_loaded = false;
      m_profiles[it.m_type].info = &it;
    }
  }


  void Technique::UseProfile(const Device& device, TechniqueInfo::TechniqueProfileType profile)
  {
    if (!m_profiles[profile].m_loaded) {
      m_currentProfile->m_loaded = false;
      m_currentProfile->release();
      m_profiles[profile].LoadShaders(device);
    }
  }
  void Technique::SetShaders(const DeviceContext & deviceContext)
  {
    m_currentProfile->SetShaders(deviceContext);
  }
  void Technique::release()
  {
    info.release();
    m_currentProfile->release();
  }
  void TechniqueProfile::LoadShaders(const Device& device)
  {
    shaderID = g_pBaseDriver->CreateShader(info->m_vsPath, info->m_fsPath);
    shaderSet = g_pBaseDriver->GetShaderIdx(shaderID);
  }
  void TechniqueProfile::SetShaders(const DeviceContext & deviceContext)
  {
    shaderSet->Set(deviceContext);
  }
  void TechniqueProfile::release()
  {
    g_pBaseDriver->DestroyShader(shaderID);
  }
  Technique::Technique( std::string path)
  {
    Load(path);
  }
}