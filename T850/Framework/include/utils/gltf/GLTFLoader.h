/*********************************************************
 * glTF 2.0 — public loader API
 *
 * Two-stage design:
 *   1. LoadGLTF()           — parse a .gltf or .glb file into a
 *                             gltf::Document (pure spec data + resolved
 *                             buffer bytes).
 *   2. ConvertToXDatabase() — translate the document into the engine's
 *                             xF::XDataBase so that RenderMesh and the
 *                             existing render graph consume it unchanged.
 *
 * Both calls log via T8_LOG_* and return false on any error.
 *********************************************************/

#ifndef T800_GLTF_LOADER_H
#define T800_GLTF_LOADER_H

#include <string>
#include <utils/gltf/GLTFTypes.h>

namespace xF { class XDataBase; }

namespace t850 {
namespace gltf {

// Parse a .gltf (JSON + external resources) or .glb (binary container)
// file. The format is detected from the file's magic bytes, not the
// extension. On success, `out` contains the spec data and resolved
// _bufferData payloads.
bool LoadGLTF(const std::string& path, Document& out);

// Decode a single JSON document blob into Document. Used internally by
// LoadGLTF and exposed for tests / GLB JSON-chunk parsing.
bool ParseJson(const std::string& json, Document& out);

// Validate cross-references in a parsed Document (indices in range,
// required fields present). Logs the first error and returns false.
bool ValidateDocument(const Document& doc);

// Bridge to the engine's in-memory model. The conversion populates a
// single xMeshContainer with one xMeshGeometry per glTF primitive,
// builds materials, performs the RH→LH coordinate flip and the
// hierarchy bake, and triangulates strip/fan modes.
//
// `sourcePath` is the original .gltf/.glb file path (used to resolve
// external image URIs and for cache keys).
bool ConvertToXDatabase(const Document& doc,
                        xF::XDataBase&  out,
                        const std::string& sourcePath);

} // namespace gltf
} // namespace t850

#endif // T800_GLTF_LOADER_H
