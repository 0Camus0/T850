/*********************************************************
* Copyright (C) 2017 Daniel Enriquez (camus_mm@hotmail.com)
* All Rights Reserved
*
* You may use, distribute and modify this code under the
* following terms:
* ** Do not claim that you wrote this software
* ** A mention would be appreciated but not needed
* ** I do not and will not provide support, this software is "as is"
* ** Enjoy, learn and share.
*********************************************************/

#pragma once

#include <string>

namespace t850 {

class Device;
class Texture;

namespace terrain {

// A tiled voxel texture atlas loaded from an image file on disk.
// Backend-neutral: decodes through the engine image loader and uploads
// via Device::CreateTextureFromMemory, so it works on every API.
struct VoxelAtlas {
  Texture* texture = nullptr;
  int widthPx = 0;
  int heightPx = 0;
  int tilePx = 16;
  int tilesPerAxisX = 0;
  int tilesPerAxisY = 0;

  bool IsValid() const { return texture != nullptr && tilesPerAxisX > 0 && tilesPerAxisY > 0; }
};

// Loads a square-grid tile atlas from an image file.
//   relativePath: asset-relative path, e.g. "Textures/minecraft_atlas.png"
//                 (a bare "minecraft_atlas.png" is resolved under Textures/).
//   tilePx:       size in pixels of one atlas tile (e.g. 16).
// The texture is uploaded with NEAREST filtering and CLAMP_TO_EDGE addressing
// so voxel faces sample their tile without bleeding into neighbors.
// Returns an invalid VoxelAtlas (texture == nullptr) on any failure.
VoxelAtlas LoadVoxelAtlas(Device* device, const std::string& relativePath, int tilePx);

} // namespace terrain
} // namespace t850
