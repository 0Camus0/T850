#pragma once

#include <scene/MutableMeshData.h>
#include <scene/EditorSceneFile.h>
#include <span>

namespace t850 {

enum class TerrainBrushMode { Raise, Lower, Flatten, Smooth, Material };

struct TerrainBrush {
    TerrainBrushMode mode = TerrainBrushMode::Raise;
    float x = 0.0f;
    float z = 0.0f;
    float radius = 5.0f;
    float amount = 1.0f;
    float targetHeight = 0.0f;
    float hardness = 0.25f;
    uint32_t material = 0;
};

bool InitializeTerrainEditing(scene::SceneHeightmapDesc& desc, std::string* error = nullptr);
bool ApplyTerrainBrush(scene::SceneHeightmapDesc& desc, const TerrainBrush& brush,
        bool* changed = nullptr, std::string* error = nullptr);

bool BuildHeightmapTerrain(const scene::SceneHeightmapDesc& desc,
    std::span<const float> heights, uint32_t width, uint32_t height,
    MutableMeshSnapshot& output, std::string* error = nullptr);
bool LoadHeightmapTerrain(const scene::SceneHeightmapDesc& desc,
    MutableMeshSnapshot& output, std::string* error = nullptr);

}