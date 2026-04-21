/*********************************************************
 * glTF 2.0 — material conversion (internal helper).
 *********************************************************/

#ifndef T800_GLTF_MATERIAL_H
#define T800_GLTF_MATERIAL_H

#include <utils/gltf/GLTFTypes.h>

namespace xF { struct xMaterial; }

namespace t800 {
namespace gltf {

// Translate glTF material `materialIndex` (or -1 for the default
// material) into an engine xMaterial. Texture indices are resolved
// (which may pre-register embedded images in the driver cache).
void ConvertMaterial(const Document& doc, int materialIndex,
                     xF::xMaterial& outMat);

} // namespace gltf
} // namespace t800

#endif // T800_GLTF_MATERIAL_H
