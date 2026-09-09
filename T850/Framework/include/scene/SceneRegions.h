#pragma once

#include <scene/EditorSceneFile.h>
#include <utils/xMaths.h>
#include <span>
#include <string_view>

namespace t850::scene {

bool ValidateSceneRegions(std::span<const SceneRegionDesc> regions, std::string* error = nullptr);
bool RegionContainsPoint(const SceneRegionDesc& region, const XVECTOR3& point);
std::vector<std::string> QuerySceneRegions(std::span<const SceneRegionDesc> regions,
    const XVECTOR3& point, std::string_view tag = {});

}