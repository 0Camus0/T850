#include "utils/ResourceManager.h"
#include <iostream>
#include <utils/Log.h>
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
      T8_LOG_INFO("Load '%s'", filename.c_str());
      return m_resources.back();
    }
    else {
      T8_LOG_ERROR("Failed to load '%s'", filename.c_str());
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