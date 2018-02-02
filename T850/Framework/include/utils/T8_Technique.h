#pragma once
#include <string>
#include <vector>
#include <tinyxml2.h>

namespace t800 {
  class ShaderBase;
  class Device;
  class DeviceContext;
  struct T8TechniqueProfileInfo;
  class T8TechniqueInfo {
  private:
    void ProcessDefine(tinyxml2::XMLElement* element);
    void ProcessProfile(tinyxml2::XMLElement* element);
    void ProcessShader(tinyxml2::XMLElement* element);
    std::vector<std::string>* m_actualDefines;
  public:
    enum T8_TECHNIQUE_PROFILE {
      HLSL,
      GLES20,
      GLES30,
      GL,
      COUNT
    };
    T8TechniqueInfo();
    explicit T8TechniqueInfo(std::string path);
    void Parse(std::string path);
    void release();


    std::string m_name;
    std::string m_path;
    std::vector<T8TechniqueProfileInfo> m_profiles;

    std::vector<std::string> m_globalDefines;
    tinyxml2::XMLDocument m_xmlDoc;
  };
  struct T8TechniqueProfileInfo {
    T8TechniqueInfo::T8_TECHNIQUE_PROFILE m_type;
    std::string m_name;
    std::string m_vsPath;
    std::string m_fsPath;
    std::string m_gsPath;
    std::string m_hsPath;
    std::string m_tsPath;
    std::vector<std::string> m_defines;
  };


  //TODO: Separate Parser and implementation on diferent headers
  struct T8TechniqueProfile;
  class T8Technique {
  private:
    T8TechniqueProfile* m_currentProfile;
  public:
    T8Technique() = default;
    T8Technique( std::string path);
    void Load(std::string path);
    void UseProfile(const Device& device, T8TechniqueInfo::T8_TECHNIQUE_PROFILE profile);
    void SetShaders(const DeviceContext& deviceContext);
    void release();
    std::vector<T8TechniqueProfile> m_profiles;
    T8TechniqueInfo info;
  };
  struct T8TechniqueProfile {
    bool m_loaded;
    void LoadShaders(const Device& device);
    void SetShaders(const DeviceContext & deviceContext);
    void release();
    T8TechniqueProfileInfo* info;
  private:
    ShaderBase* shaderSet;
    int shaderID;
  };
}