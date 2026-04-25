/*********************************************************
 * glTF 2.0 — image → engine texture resolution.
 * Internal helper used by GLTFMaterial.cpp.
 *********************************************************/

#ifndef T800_GLTF_IMAGE_H
#define T800_GLTF_IMAGE_H

#include <string>
#include <vector>
#include <utils/gltf/GLTFTypes.h>

namespace t850 {
namespace gltf {

// On success, `outName` is the texture name to store in the engine
// EffectDefault (NOT prefixed with "Textures/" — RenderMesh::LoadTex
// does that). For embedded images, the texture is also pre-registered
// in the driver so a future CreateTexture(name) hits the cache.
//
// `outSlot` is the engine texture slot, or -1 if deferred (external
// URIs are loaded lazily by RenderMesh).
bool ResolveImage(const Document& doc, int imageIndex,
                  std::string& outName, int& outSlot);

// Batch-resolve all images with parallel CPU decode.
// Results are stored in outNames[i] and outSlots[i].
// GPU upload happens on the calling (main) thread.
void ResolveAllImages(const Document& doc,
                      std::vector<std::string>& outNames,
                      std::vector<int>& outSlots);

} // namespace gltf
} // namespace t850

#endif // T800_GLTF_IMAGE_H
