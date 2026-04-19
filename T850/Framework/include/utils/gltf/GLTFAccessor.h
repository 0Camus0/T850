/*********************************************************
 * Internal accessor reading helpers used by GLTFMesh.cpp /
 * GLTFAnimation.cpp. Not exposed in the public Loader header.
 *********************************************************/

#ifndef T800_GLTF_ACCESSOR_H
#define T800_GLTF_ACCESSOR_H

#include <cstdint>
#include <vector>
#include <utils/gltf/GLTFTypes.h>

namespace t800 {
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

} // namespace gltf
} // namespace t800

#endif // T800_GLTF_ACCESSOR_H
