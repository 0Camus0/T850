#ifndef T850_IBL_RESOURCES_H
#define T850_IBL_RESOURCES_H

#include <scene/RenderGraph.h>
#include <scene/SceneProp.h>
#include <string>

namespace t850 {

  struct EnvironmentResourcePaths {
    std::string diffuseIBL;
    std::string specularIBL;
    std::string brdfLUT;
    std::string sheenIBL;
    std::string charlieLUT;
    std::string sheenELUT;
  };

  int CreateGGXBrdfLUTTexture(BaseDriver* driver, int resolution = 256, int sampleCount = 256);

  void LoadEnvironmentIBLResources(
    BaseDriver* driver,
    const EnvironmentResourcePaths& paths,
    EnvironmentMapSet& envMaps,
    int& diffuseTextureIndex,
    int& specularTextureIndex,
    int& brdfTextureIndex,
    int& sheenTextureIndex,
    int& charlieLUTTextureIndex,
    int& sheenELUTTextureIndex);

  void UpdateSceneIBLSettings(SceneProps& props, BaseDriver* driver, const EnvironmentMapSet& envMaps);

}

#endif
