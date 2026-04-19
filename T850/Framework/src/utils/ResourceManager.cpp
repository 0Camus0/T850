#include "utils/ResourceManager.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <string>
#include <utils/Log.h>
#include <utils/gltf/GLTFLoader.h>

namespace t800 {

  // Lower-case ASCII extension after the last '.', empty if none.
  static std::string FileExtensionLower(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
  }

  xF::XDataBase * ResourceManager::Load(const std::string & filename)
  {
    for (auto &it : m_resources) {
      if (it->m_name == filename)
      {
        return it;
      }
    }

    const std::string ext = FileExtensionLower(filename);
    m_resources.push_back(new xF::XDataBase);
    xF::XDataBase* db = m_resources.back();

    bool ok = false;
    if (ext == "gltf" || ext == "glb") {
      // Modern path.
      gltf::Document doc;
      ok = gltf::LoadGLTF(filename, doc)
        && gltf::ConvertToXDatabase(doc, *db, filename);
    } else {
      // Legacy .x / .X path — kept compiled in for backwards compatibility.
      ok = db->LoadXFile(filename);
    }

    if (ok) {
      T8_LOG_INFO("Load '%s'", filename.c_str());
      return db;
    }

    T8_LOG_ERROR("Failed to load '%s'", filename.c_str());
    delete db;
    m_resources.pop_back();
    return nullptr;
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