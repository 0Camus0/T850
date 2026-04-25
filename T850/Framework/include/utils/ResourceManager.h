#pragma once
#include <string>
#include <vector>
#include <utils/XDataBase.h>
namespace t850 {
  class ResourceManager {
  public:
    xF::XDataBase* Load(const std::string	& filename);
    void Release();
    std::vector<xF::XDataBase*> m_resources;
  };
}