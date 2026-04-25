/*********************************************************
 * Internal accessor reading helpers used by GLTFMesh.cpp /
 * GLTFAnimation.cpp. Not exposed in the public Loader header.
 *********************************************************/

#ifndef T800_GLTF_ACCESSOR_H
#define T800_GLTF_ACCESSOR_H

#include <cstdint>
#include <vector>
#include <utils/gltf/GLTFTypes.h>

namespace t850 {
namespace gltf {

// Read any non-index accessor as floats (component-promoted). On
// success `out` has size accessor.count * elementCount.
bool ReadAccessorFloats(const Document& doc, int accessorIndex,
                        std::vector<float>& out,
                        int* outElementCount = nullptr);

// Read a SCALAR index accessor as uint32. `outMaxValue` is set to the
// largest index value read (used to decide between u16 and u32 IB).
bool ReadAccessorIndices(const Document& doc, int accessorIndex,
                         std::vector<uint32_t>& out,
                         uint32_t* outMaxValue = nullptr);

// base64 decoder (utility, also used by GLTFLoader for data URIs).
bool Base64Decode(const char* src, std::size_t len,
                  std::vector<unsigned char>& out);

// Draco mesh decompression: decode a compressed bufferView into
// per-attribute float arrays and an index array.
struct DracoDecodeResult {
  std::vector<float>    positions;   // N*3
  std::vector<float>    normals;     // N*3 or empty
  std::vector<float>    tangents;    // N*4 or empty
  std::vector<float>    texcoord0;   // N*2 or empty
  std::vector<float>    texcoord1;   // N*2 or empty
  std::vector<float>    colors;      // N*3 or N*4 or empty
  std::vector<uint32_t> indices;
  uint32_t              maxIndex = 0;
  std::size_t           vertexCount = 0;
};

bool DecodeDracoMesh(const Document& doc,
                     const DracoMeshCompression& draco,
                     DracoDecodeResult& result);

} // namespace gltf
} // namespace t850

#endif // T800_GLTF_ACCESSOR_H
