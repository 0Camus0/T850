#include "utils/ResourceManager.h"
#include <iostream>
namespace t800 {
  xF::XDataBase * ResourceManager::Load(const std::string & filename)
  {
    for (auto &it : m_resources) {
      if (it->m_name == filename)
      {
        return it;
      }
    }
    m_resources.push_back(new xF::XDataBase);
    if (m_resources.back()->LoadXFile(filename)) {
      std::cout << " Load " << filename << std::endl;
      return m_resources.back();
    }
    else {
      std::cout << " Failed to load " << filename << std::endl;
      delete m_resources.back();
      m_resources.pop_back();
      return nullptr;
    }
  }
  void ResourceManager::Release()
  {
    for (auto &it : m_resources)
    {
      delete it;
    }
    m_resources.clear();
  }
}