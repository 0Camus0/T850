/*********************************************************
 * glTF 2.0 — accessor / bufferView decoding.
 *
 * All raw reads go through std::memcpy so the decoder is alignment-safe
 * on ARM64. Sparse accessors are materialised into a dense vector by
 * first reading the base view (or zero-initialising when bufferView is
 * absent per spec) and then overwriting the indexed slots.
 *
 * Out-of-range / malformed data is detected and logged but does not
 * throw; callers receive `false` and an empty output vector.
 *********************************************************/

#include <utils/gltf/GLTFAccessor.h>
#include <utils/gltf/GLTFTypes.h>
#include <utils/Log.h>

#include <cstring>
#include <cstdint>

namespace t800 {
namespace gltf {

namespace {

// Read N raw bytes at byte offset `off` of buffer `buf` into dst.
// Returns false if out of range.
bool RawRead(const std::vector<unsigned char>& buf,
             std::size_t off, std::size_t n, void* dst) {
  if (off + n > buf.size()) return false;
  std::memcpy(dst, buf.data() + off, n);
  return true;
}

// Promote one raw component to float, applying glTF normalization rules.
float ToFloat(const unsigned char* src, int componentType, bool normalized) {
  switch (componentType) {
    case CT_FLOAT: {
      float v; std::memcpy(&v, src, 4); return v;
    }
    case CT_UNSIGNED_BYTE: {
      uint8_t v = src[0];
      return normalized ? (v / 255.0f) : static_cast<float>(v);
    }
    case CT_BYTE: {
      int8_t v; std::memcpy(&v, src, 1);
      // glTF spec: signed normalized maps -128 to -1.0, +127 to +1.0.
      return normalized ? std::max(v / 127.0f, -1.0f) : static_cast<float>(v);
    }
    case CT_UNSIGNED_SHORT: {
      uint16_t v; std::memcpy(&v, src, 2);
      return normalized ? (v / 65535.0f) : static_cast<float>(v);
    }
    case CT_SHORT: {
      int16_t v; std::memcpy(&v, src, 2);
      return normalized ? std::max(v / 32767.0f, -1.0f) : static_cast<float>(v);
    }
    case CT_UNSIGNED_INT: {
      uint32_t v; std::memcpy(&v, src, 4);
      return static_cast<float>(v);
    }
  }
  return 0.0f;
}

uint32_t ToU32(const unsigned char* src, int componentType) {
  switch (componentType) {
    case CT_UNSIGNED_BYTE:  return src[0];
    case CT_UNSIGNED_SHORT: { uint16_t v; std::memcpy(&v, src, 2); return v; }
    case CT_UNSIGNED_INT:   { uint32_t v; std::memcpy(&v, src, 4); return v; }
    case CT_BYTE:           { int8_t  v; std::memcpy(&v, src, 1); return static_cast<uint32_t>(v); }
    case CT_SHORT:          { int16_t v; std::memcpy(&v, src, 2); return static_cast<uint32_t>(v); }
    case CT_FLOAT:          { float   v; std::memcpy(&v, src, 4); return static_cast<uint32_t>(v); }
  }
  return 0;
}

// Resolve the byte stride for an accessor: bufferView.byteStride if
// set, else tightly packed (componentSize * elementCount).
std::size_t Stride(const Accessor& a, const BufferView* bv) {
  std::size_t packed = static_cast<std::size_t>(ComponentSize(a.componentType))
                     * static_cast<std::size_t>(ElementCount(a.type));
  if (bv && bv->byteStride && *bv->byteStride > 0) return *bv->byteStride;
  return packed;
}

} // namespace

bool ReadAccessorFloats(const Document& doc, int accessorIndex,
                        std::vector<float>& out, int* outElementCount) {
  out.clear();
  if (accessorIndex < 0 || accessorIndex >= static_cast<int>(doc.accessors.size())) {
    T8_LOG_ERROR("[glTF] ReadAccessorFloats: accessor %d out of range", accessorIndex);
    return false;
  }
  const Accessor& a = doc.accessors[accessorIndex];
  const int elem = ElementCount(a.type);
  const int csz  = ComponentSize(a.componentType);
  if (elem == 0 || csz == 0) {
    T8_LOG_ERROR("[glTF] ReadAccessorFloats: bad type/componentType (%s/%d)",
                 a.type.c_str(), a.componentType);
    return false;
  }
  if (outElementCount) *outElementCount = elem;
  out.resize(a.count * static_cast<std::size_t>(elem), 0.0f);

  // Base view (may be absent — sparse-only accessor).
  if (a.bufferView) {
    int bvIdx = *a.bufferView;
    if (bvIdx < 0 || bvIdx >= static_cast<int>(doc.bufferViews.size())) {
      T8_LOG_ERROR("[glTF] accessor %d: bufferView %d OOR", accessorIndex, bvIdx);
      return false;
    }
    const BufferView& bv = doc.bufferViews[bvIdx];
    if (bv.buffer < 0 || bv.buffer >= static_cast<int>(doc._bufferData.size())) {
      T8_LOG_ERROR("[glTF] bufferView %d: buffer %d OOR", bvIdx, bv.buffer);
      return false;
    }
    const auto& buf = doc._bufferData[bv.buffer];
    const std::size_t stride = Stride(a, &bv);
    const std::size_t base   = bv.byteOffset + a.byteOffset;
    for (std::size_t i = 0; i < a.count; ++i) {
      const std::size_t off = base + i * stride;
      if (off + static_cast<std::size_t>(csz) * elem > buf.size()) {
        T8_LOG_ERROR("[glTF] accessor %d: read past end at element %zu",
                     accessorIndex, i);
        return false;
      }
      for (int k = 0; k < elem; ++k) {
        out[i * elem + k] = ToFloat(buf.data() + off + k * csz,
                                    a.componentType, a.normalized);
      }
    }
  }

  // Sparse overrides.
  if (a.sparse && a.sparse->count > 0) {
    const auto& sp = *a.sparse;
    const BufferView& bvI = doc.bufferViews[sp.indices.bufferView];
    const BufferView& bvV = doc.bufferViews[sp.values.bufferView];
    const auto& bI = doc._bufferData[bvI.buffer];
    const auto& bV = doc._bufferData[bvV.buffer];
    const int idxSz = ComponentSize(sp.indices.componentType);
    const std::size_t valStride = static_cast<std::size_t>(csz) * elem;
    for (std::size_t i = 0; i < sp.count; ++i) {
      const std::size_t io = bvI.byteOffset + sp.indices.byteOffset + i * idxSz;
      const std::size_t vo = bvV.byteOffset + sp.values.byteOffset + i * valStride;
      if (io + idxSz > bI.size() || vo + valStride > bV.size()) {
        T8_LOG_ERROR("[glTF] sparse accessor %d: read past end", accessorIndex);
        return false;
      }
      uint32_t target = ToU32(bI.data() + io, sp.indices.componentType);
      if (target >= a.count) {
        T8_LOG_ERROR("[glTF] sparse accessor %d: target %u OOR", accessorIndex, target);
        return false;
      }
      for (int k = 0; k < elem; ++k) {
        out[target * elem + k] = ToFloat(bV.data() + vo + k * csz,
                                         a.componentType, a.normalized);
      }
    }
  }
  return true;
}

bool ReadAccessorIndices(const Document& doc, int accessorIndex,
                         std::vector<uint32_t>& out, uint32_t* outMaxValue) {
  out.clear();
  if (outMaxValue) *outMaxValue = 0;
  if (accessorIndex < 0 || accessorIndex >= static_cast<int>(doc.accessors.size())) {
    T8_LOG_ERROR("[glTF] ReadAccessorIndices: accessor %d OOR", accessorIndex);
    return false;
  }
  const Accessor& a = doc.accessors[accessorIndex];
  if (a.type != "SCALAR") {
    T8_LOG_ERROR("[glTF] ReadAccessorIndices: accessor %d not SCALAR (got '%s')",
                 accessorIndex, a.type.c_str());
    return false;
  }
  const int csz = ComponentSize(a.componentType);
  if (!a.bufferView) {
    T8_LOG_ERROR("[glTF] ReadAccessorIndices: accessor %d missing bufferView",
                 accessorIndex);
    return false;
  }
  const BufferView& bv = doc.bufferViews[*a.bufferView];
  if (bv.buffer < 0 || bv.buffer >= static_cast<int>(doc._bufferData.size())) {
    T8_LOG_ERROR("[glTF] ReadAccessorIndices: bufferView %d: buffer OOR",
                 *a.bufferView);
    return false;
  }
  const auto& buf = doc._bufferData[bv.buffer];
  const std::size_t stride = Stride(a, &bv);
  const std::size_t base   = bv.byteOffset + a.byteOffset;
  out.resize(a.count);
  uint32_t mx = 0;
  for (std::size_t i = 0; i < a.count; ++i) {
    const std::size_t off = base + i * stride;
    if (off + csz > buf.size()) {
      T8_LOG_ERROR("[glTF] indices accessor %d: read past end", accessorIndex);
      return false;
    }
    uint32_t v = ToU32(buf.data() + off, a.componentType);
    out[i] = v;
    if (v > mx) mx = v;
  }
  if (outMaxValue) *outMaxValue = mx;
  return true;
}

} // namespace gltf
} // namespace t800
