#pragma once
#include <string>
#include <vector>
#include <tinyxml2.h>

namespace t850 {
  class ShaderBase;
  class Device;
  class DeviceContext;
  struct TechniqueProfileInfo;
  class TechniqueInfo {
  private:
    void ProcessDefine(tinyxml2::XMLElement* element);
    void ProcessProfile(tinyxml2::XMLElement* element);
    void ProcessShader(tinyxml2::XMLElement* element);
    std::vector<std::string>* m_actualDefines;
  public:
    enum TechniqueProfileType {
      HLSL,
      GLES20,
      GLES30,
      GL,
      COUNT
    };
    TechniqueInfo();
    explicit TechniqueInfo(std::string path);
    void Parse(std::string path);
    void release();


    std::string m_name;
    std::string m_path;
    std::vector<TechniqueProfileInfo> m_profiles;

    std::vector<std::string> m_globalDefines;
    tinyxml2::XMLDocument m_xmlDoc;
  };
  struct TechniqueProfileInfo {
    TechniqueInfo::TechniqueProfileType m_type;
    std::string m_name;
    std::string m_vsPath;
    std::string m_fsPath;
    std::string m_gsPath;
    std::string m_hsPath;
    std::string m_tsPath;
    std::vector<std::string> m_defines;
  };


  //TODO: Separate Parser and implementation on diferent headers
  struct TechniqueProfile;
  class Technique {
  private:
    TechniqueProfile* m_currentProfile;
  public:
    Technique() = default;
    Technique( std::string path);
    void Load(std::string path);
    void UseProfile(const Device& device, TechniqueInfo::TechniqueProfileType profile);
    void SetShaders(const DeviceContext& deviceContext);
    void release();
    std::vector<TechniqueProfile> m_profiles;
    TechniqueInfo info;
  };
  struct TechniqueProfile {
    bool m_loaded;
    void LoadShaders(const Device& device);
    void SetShaders(const DeviceContext & deviceContext);
    void release();
    TechniqueProfileInfo* info;
  private:
    ShaderBase* shaderSet;
    int shaderID;
  };
}