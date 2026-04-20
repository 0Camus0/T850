/*********************************************************
 * glTF 2.0 — primitive → xMeshGeometry conversion +
 * top-level ConvertToXDatabase().
 *
 * Per the plan (§4) we emit one xMeshGeometry per glTF primitive
 * (matching the engine's "geometry = single draw with one material"
 * model). The interleave order in xFinalGeometry::pData mirrors
 * XDataBase::CreateSubSets exactly so that RenderMesh — and every
 * shader downstream — needs no changes:
 *
 *      [POS vec4][NORMAL vec4][TANGENT vec4][BINORMAL vec4]
 *      [UV0 vec2][UV1 vec2][UV2 vec2][UV3 vec2]
 *
 * Coordinate-system handling (plan §6.8): glTF is RH/+Y up, the engine
 * is LH. We negate Z on positions/normals/tangents and reverse winding
 * at load time so the rest of the pipeline sees the same convention as
 * the legacy .x assets.
 *********************************************************/

#include <utils/gltf/GLTFLoader.h>
#include <utils/gltf/GLTFAccessor.h>
#include <utils/gltf/GLTFMaterial.h>
#include <utils/gltf/GLTFTypes.h>
#include <utils/XDataBase.h>
#include <utils/xMaths.h>
#include <utils/Log.h>

#include <mikktspace.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace t800 {
namespace gltf {

namespace {

constexpr bool kFlipToLeftHanded = true;

// ── Math helpers (column-major glTF matrices) ──────────────────────────

XMATRIX44 Identity() {
  XMATRIX44 m; m.Identity(); return m;
}

XMATRIX44 FromColumnMajor16(const float* m) {
  // glTF stores matrices column-major, representing the standard
  // column-vector convention M_col. The engine uses row-vector
  // convention (P * M). Therefore we need engine = M_col^T, which is
  // simply the linear copy of the 16 floats into row-major storage:
  //   M_col stored column-major == M_row stored row-major.
  XMATRIX44 r;
  r.m[0][0] = m[0];  r.m[0][1] = m[1];  r.m[0][2] = m[2];  r.m[0][3] = m[3];
  r.m[1][0] = m[4];  r.m[1][1] = m[5];  r.m[1][2] = m[6];  r.m[1][3] = m[7];
  r.m[2][0] = m[8];  r.m[2][1] = m[9];  r.m[2][2] = m[10]; r.m[2][3] = m[11];
  r.m[3][0] = m[12]; r.m[3][1] = m[13]; r.m[3][2] = m[14]; r.m[3][3] = m[15];
  return r;
}

XMATRIX44 MakeTRS(const float* T, const float* R, const float* S) {
  // R is quaternion (xyzw). The engine uses row-vector convention
  // (P_row * M), so the rotation block stored is the transpose of the
  // standard column-vector formula.
  float tx = T ? T[0] : 0.0f, ty = T ? T[1] : 0.0f, tz = T ? T[2] : 0.0f;
  float sx = S ? S[0] : 1.0f, sy = S ? S[1] : 1.0f, sz = S ? S[2] : 1.0f;
  float qx = R ? R[0] : 0.0f, qy = R ? R[1] : 0.0f,
        qz = R ? R[2] : 0.0f, qw = R ? R[3] : 1.0f;

  float xx = qx * qx, yy = qy * qy, zz = qz * qz;
  float xy = qx * qy, xz = qx * qz, yz = qy * qz;
  float wx = qw * qx, wy = qw * qy, wz = qw * qz;

  // Row-vector rotation = transpose of column-vector form.
  float r00 = 1.0f - 2.0f * (yy + zz);
  float r01 =        2.0f * (xy + wz);
  float r02 =        2.0f * (xz - wy);
  float r10 =        2.0f * (xy - wz);
  float r11 = 1.0f - 2.0f * (xx + zz);
  float r12 =        2.0f * (yz + wx);
  float r20 =        2.0f * (xz + wy);
  float r21 =        2.0f * (yz - wx);
  float r22 = 1.0f - 2.0f * (xx + yy);

  XMATRIX44 m;
  // Scale is applied to each row (row-vector convention).
  m.m[0][0] = r00 * sx; m.m[0][1] = r01 * sx; m.m[0][2] = r02 * sx; m.m[0][3] = 0.0f;
  m.m[1][0] = r10 * sy; m.m[1][1] = r11 * sy; m.m[1][2] = r12 * sy; m.m[1][3] = 0.0f;
  m.m[2][0] = r20 * sz; m.m[2][1] = r21 * sz; m.m[2][2] = r22 * sz; m.m[2][3] = 0.0f;
  m.m[3][0] = tx;       m.m[3][1] = ty;       m.m[3][2] = tz;       m.m[3][3] = 1.0f;
  return m;
}

XMATRIX44 NodeLocalMatrix(const Node& n) {
  if (n.matrix.size() == 16) return FromColumnMajor16(n.matrix.data());
  const float* T = n.translation.size() == 3 ? n.translation.data() : nullptr;
  const float* R = n.rotation.size()    == 4 ? n.rotation.data()    : nullptr;
  const float* S = n.scale.size()       == 3 ? n.scale.data()       : nullptr;
  if (!T && !R && !S) return Identity();
  return MakeTRS(T, R, S);
}

void TransformPoint(const XMATRIX44& M, float x, float y, float z,
                    float& ox, float& oy, float& oz) {
  ox = M.m[0][0]*x + M.m[1][0]*y + M.m[2][0]*z + M.m[3][0];
  oy = M.m[0][1]*x + M.m[1][1]*y + M.m[2][1]*z + M.m[3][1];
  oz = M.m[0][2]*x + M.m[1][2]*y + M.m[2][2]*z + M.m[3][2];
}

void TransformDir(const XMATRIX44& M, float x, float y, float z,
                  float& ox, float& oy, float& oz) {
  ox = M.m[0][0]*x + M.m[1][0]*y + M.m[2][0]*z;
  oy = M.m[0][1]*x + M.m[1][1]*y + M.m[2][1]*z;
  oz = M.m[0][2]*x + M.m[1][2]*y + M.m[2][2]*z;
}

// ── Triangulation ─────────────────────────────────────────────────────

// Materialise an index list for the given primitive mode. If the
// primitive has no `indices`, generate a flat 0..N-1 index list first.
bool BuildTriangleIndices(const Primitive& prim, std::size_t vertexCount,
                          const std::vector<uint32_t>& sourceIndices,
                          std::vector<uint32_t>& outTris) {
  outTris.clear();
  std::vector<uint32_t> tmp;
  const std::vector<uint32_t>* src = &sourceIndices;
  if (sourceIndices.empty()) {
    tmp.resize(vertexCount);
    for (std::size_t i = 0; i < vertexCount; ++i) tmp[i] = static_cast<uint32_t>(i);
    src = &tmp;
  }
  switch (prim.mode) {
    case PM_TRIANGLES: {
      if (src->size() % 3 != 0) {
        T8_LOG_ERROR("[glTF] TRIANGLES count %zu not divisible by 3", src->size());
        return false;
      }
      outTris = *src;
      break;
    }
    case PM_TRIANGLE_STRIP: {
      if (src->size() < 3) return false;
      outTris.reserve((src->size() - 2) * 3);
      for (std::size_t i = 0; i + 2 < src->size(); ++i) {
        if ((i & 1u) == 0u) {
          outTris.push_back((*src)[i]);
          outTris.push_back((*src)[i + 1]);
          outTris.push_back((*src)[i + 2]);
        } else {
          outTris.push_back((*src)[i]);
          outTris.push_back((*src)[i + 2]);
          outTris.push_back((*src)[i + 1]);
        }
      }
      break;
    }
    case PM_TRIANGLE_FAN: {
      if (src->size() < 3) return false;
      outTris.reserve((src->size() - 2) * 3);
      for (std::size_t i = 1; i + 1 < src->size(); ++i) {
        outTris.push_back((*src)[0]);
        outTris.push_back((*src)[i]);
        outTris.push_back((*src)[i + 1]);
      }
      break;
    }
    default:
      T8_LOG_ERROR("[glTF] primitive mode %d (POINTS/LINES) not supported", prim.mode);
      return false;
  }
  return true;
}

// ── MikkTSpace tangent generation (plan §6.4) ────────────────────────
//
// Mikk's contract is per-(face, vertex_in_face): the same vertex may
// receive different tangents from different faces. Our pipeline does
// not duplicate vertices (POSITION accessor count == vertex count), so
// we average the per-face tangents into per-vertex slots and re-orthonormalise
// against the vertex normal. The bitangent sign is taken from the first
// face that wrote the slot — for assets coming out of standard DCCs
// (Blender, Substance, Maya) all faces sharing a vertex agree on the
// sign, so this is correct in practice and matches the behaviour of
// other Mikk integrations that don't split vertices (e.g. tinygltf-based
// loaders, glTF-Sample-Viewer's CPU path).
struct MikkCtx {
  const std::vector<float>*    positions;  // N*3
  const std::vector<float>*    normals;    // N*3
  const std::vector<float>*    uvs;        // N*2
  const std::vector<uint32_t>* tris;       // 3*F
  std::vector<float>*          accumXYZ;   // N*3, summed
  std::vector<float>*          firstSign;  // N, first writer's sign (0 = unset)
};

int MikkGetNumFaces(const SMikkTSpaceContext* c) {
  auto* ctx = static_cast<MikkCtx*>(c->m_pUserData);
  return static_cast<int>(ctx->tris->size() / 3);
}
int MikkGetNumVerticesOfFace(const SMikkTSpaceContext*, int) { return 3; }

uint32_t MikkVertexIndex(const MikkCtx* ctx, int iFace, int iVert) {
  return (*ctx->tris)[static_cast<std::size_t>(iFace) * 3 + iVert];
}

void MikkGetPosition(const SMikkTSpaceContext* c, float fv[], int iFace, int iVert) {
  auto* ctx = static_cast<MikkCtx*>(c->m_pUserData);
  uint32_t i = MikkVertexIndex(ctx, iFace, iVert);
  fv[0] = (*ctx->positions)[i*3+0];
  fv[1] = (*ctx->positions)[i*3+1];
  fv[2] = (*ctx->positions)[i*3+2];
}
void MikkGetNormal(const SMikkTSpaceContext* c, float fv[], int iFace, int iVert) {
  auto* ctx = static_cast<MikkCtx*>(c->m_pUserData);
  uint32_t i = MikkVertexIndex(ctx, iFace, iVert);
  fv[0] = (*ctx->normals)[i*3+0];
  fv[1] = (*ctx->normals)[i*3+1];
  fv[2] = (*ctx->normals)[i*3+2];
}
void MikkGetTexCoord(const SMikkTSpaceContext* c, float fv[], int iFace, int iVert) {
  auto* ctx = static_cast<MikkCtx*>(c->m_pUserData);
  uint32_t i = MikkVertexIndex(ctx, iFace, iVert);
  fv[0] = (*ctx->uvs)[i*2+0];
  fv[1] = (*ctx->uvs)[i*2+1];
}
void MikkSetTSpaceBasic(const SMikkTSpaceContext* c, const float fvTangent[],
                        float fSign, int iFace, int iVert) {
  auto* ctx = static_cast<MikkCtx*>(c->m_pUserData);
  uint32_t i = MikkVertexIndex(ctx, iFace, iVert);
  (*ctx->accumXYZ)[i*3+0] += fvTangent[0];
  (*ctx->accumXYZ)[i*3+1] += fvTangent[1];
  (*ctx->accumXYZ)[i*3+2] += fvTangent[2];
  if ((*ctx->firstSign)[i] == 0.0f) (*ctx->firstSign)[i] = fSign;
}

bool GenerateMikkTSpaceTangents(const std::vector<float>& positions,
                                const std::vector<float>& normals,
                                const std::vector<float>& uvs,
                                const std::vector<uint32_t>& tris,
                                std::vector<float>& outTangents) {
  const std::size_t N = positions.size() / 3;
  if (N == 0 || tris.size() < 3 || normals.size() != N * 3
      || uvs.size() != N * 2) {
    return false;
  }
  std::vector<float> accum(N * 3, 0.0f);
  std::vector<float> firstSign(N, 0.0f);

  MikkCtx user{ &positions, &normals, &uvs, &tris, &accum, &firstSign };

  SMikkTSpaceInterface iface{};
  iface.m_getNumFaces          = MikkGetNumFaces;
  iface.m_getNumVerticesOfFace = MikkGetNumVerticesOfFace;
  iface.m_getPosition          = MikkGetPosition;
  iface.m_getNormal            = MikkGetNormal;
  iface.m_getTexCoord          = MikkGetTexCoord;
  iface.m_setTSpaceBasic       = MikkSetTSpaceBasic;

  SMikkTSpaceContext mctx{};
  mctx.m_pInterface = &iface;
  mctx.m_pUserData  = &user;

  if (!genTangSpaceDefault(&mctx)) {
    T8_LOG_ERROR("[glTF] MikkTSpace failed; falling back to naive tangents");
    return false;
  }

  outTangents.assign(N * 4, 0.0f);
  for (std::size_t i = 0; i < N; ++i) {
    float tx = accum[i*3+0], ty = accum[i*3+1], tz = accum[i*3+2];
    // Re-orthonormalise tangent against the vertex normal so the
    // averaged tangent stays in the tangent plane: T = normalize(T - (T·N)N).
    float nx = normals[i*3+0], ny = normals[i*3+1], nz = normals[i*3+2];
    float dot = tx*nx + ty*ny + tz*nz;
    tx -= dot*nx; ty -= dot*ny; tz -= dot*nz;
    float l = std::sqrt(tx*tx + ty*ty + tz*tz);
    if (l > 1e-8f) { tx/=l; ty/=l; tz/=l; }
    float sign = (firstSign[i] == 0.0f) ? 1.0f : firstSign[i];
    outTangents[i*4+0] = tx;
    outTangents[i*4+1] = ty;
    outTangents[i*4+2] = tz;
    outTangents[i*4+3] = sign;
  }
  return true;
}

// ── Naive per-triangle tangent generation. Used as a fallback when
// MikkTSpace cannot run (degenerate input — e.g. no UV0 or no normals).
void GenerateNaiveTangents(const std::vector<float>& positions,   // size N*3
                           const std::vector<float>& uvs,         // size N*2
                           const std::vector<uint32_t>& tris,
                           std::vector<float>& outTangents) {     // size N*4 (xyz, w=1)
  const std::size_t N = positions.size() / 3;
  outTangents.assign(N * 4, 0.0f);
  std::vector<float> accum(N * 3, 0.0f);
  for (std::size_t t = 0; t + 2 < tris.size(); t += 3) {
    uint32_t i0 = tris[t + 0], i1 = tris[t + 1], i2 = tris[t + 2];
    if (i0 >= N || i1 >= N || i2 >= N) continue;
    float dx1 = positions[i1*3+0] - positions[i0*3+0];
    float dy1 = positions[i1*3+1] - positions[i0*3+1];
    float dz1 = positions[i1*3+2] - positions[i0*3+2];
    float dx2 = positions[i2*3+0] - positions[i0*3+0];
    float dy2 = positions[i2*3+1] - positions[i0*3+1];
    float dz2 = positions[i2*3+2] - positions[i0*3+2];
    float du1 = uvs[i1*2+0] - uvs[i0*2+0];
    float dv1 = uvs[i1*2+1] - uvs[i0*2+1];
    float du2 = uvs[i2*2+0] - uvs[i0*2+0];
    float dv2 = uvs[i2*2+1] - uvs[i0*2+1];
    float det = du1*dv2 - du2*dv1;
    if (std::fabs(det) < 1e-8f) continue;
    float r = 1.0f / det;
    float tx = (dx1*dv2 - dx2*dv1) * r;
    float ty = (dy1*dv2 - dy2*dv1) * r;
    float tz = (dz1*dv2 - dz2*dv1) * r;
    for (uint32_t i : {i0, i1, i2}) {
      accum[i*3+0] += tx;
      accum[i*3+1] += ty;
      accum[i*3+2] += tz;
    }
  }
  for (std::size_t i = 0; i < N; ++i) {
    float x = accum[i*3+0], y = accum[i*3+1], z = accum[i*3+2];
    float l = std::sqrt(x*x + y*y + z*z);
    if (l > 1e-8f) { x /= l; y /= l; z /= l; }
    outTangents[i*4+0] = x;
    outTangents[i*4+1] = y;
    outTangents[i*4+2] = z;
    outTangents[i*4+3] = 1.0f;
  }
}

// ── Per-primitive build ───────────────────────────────────────────────

bool BuildGeometry(const Document& doc,
                   const Primitive& prim,
                   const XMATRIX44& worldMatrix,
                   xF::xMeshGeometry& geom) {
  geom = xF::xMeshGeometry{};

  // POSITION is required.
  if (prim.attributes.POSITION < 0) {
    T8_LOG_ERROR("[glTF] primitive missing POSITION attribute");
    return false;
  }
  std::vector<float> pos, nrm, tan, uv0, uv1, col;
  int posElem = 0, nrmElem = 0, tanElem = 0;
  int uv0Elem = 0, uv1Elem = 0, colElem = 0;

  if (!ReadAccessorFloats(doc, prim.attributes.POSITION, pos, &posElem)
      || posElem != 3) {
    T8_LOG_ERROR("[glTF] POSITION accessor invalid");
    return false;
  }
  const std::size_t N = pos.size() / 3;

  bool hasNormal  = false, hasTangent = false;
  bool hasUV0     = false, hasUV1     = false;
  bool hasColor   = false;

  if (prim.attributes.NORMAL >= 0
      && ReadAccessorFloats(doc, prim.attributes.NORMAL, nrm, &nrmElem)
      && nrmElem == 3 && nrm.size() == N * 3) {
    hasNormal = true;
  }
  if (prim.attributes.TANGENT >= 0
      && ReadAccessorFloats(doc, prim.attributes.TANGENT, tan, &tanElem)
      && tanElem == 4 && tan.size() == N * 4) {
    hasTangent = true;
  }
  if (prim.attributes.TEXCOORD_0 >= 0
      && ReadAccessorFloats(doc, prim.attributes.TEXCOORD_0, uv0, &uv0Elem)
      && uv0Elem == 2 && uv0.size() == N * 2) {
    hasUV0 = true;
  }
  if (prim.attributes.TEXCOORD_1 >= 0
      && ReadAccessorFloats(doc, prim.attributes.TEXCOORD_1, uv1, &uv1Elem)
      && uv1Elem == 2 && uv1.size() == N * 2) {
    hasUV1 = true;
  }
  if (prim.attributes.COLOR_0 >= 0
      && ReadAccessorFloats(doc, prim.attributes.COLOR_0, col, &colElem)
      && (colElem == 3 || colElem == 4)) {
    hasColor = true;
  }

  // Indices.
  std::vector<uint32_t> srcIdx;
  if (prim.indices) {
    if (!ReadAccessorIndices(doc, *prim.indices, srcIdx)) return false;
  }
  std::vector<uint32_t> tris;
  if (!BuildTriangleIndices(prim, N, srcIdx, tris)) return false;

  // Auto-generate tangents if a normal map is present in the material
  // but TANGENT is missing. We always try MikkTSpace first (industry
  // standard, matches Blender / Substance / Unity). If that fails for
  // any reason (degenerate input), fall back to the naive per-triangle
  // accumulator so a normal-mapped mesh still gets *something*.
  if (!hasTangent && hasUV0 && hasNormal) {
    if (GenerateMikkTSpaceTangents(pos, nrm, uv0, tris, tan)) {
      hasTangent = true;
    } else {
      GenerateNaiveTangents(pos, uv0, tris, tan);
      hasTangent = (tan.size() == N * 4);
    }
  }

  // ── Set engine attribute mask and per-vertex containers. ──
  geom.NumVertices = static_cast<xF::xDWORD>(N);
  geom.NumChannelsTexCoords = (hasUV1 ? 2 : (hasUV0 ? 1 : 0));
  geom.VertexAttributes = xF::xMeshGeometry::HAS_POSITION;
  if (hasNormal)  geom.VertexAttributes |= xF::xMeshGeometry::HAS_NORMAL;
  if (hasTangent) geom.VertexAttributes |= xF::xMeshGeometry::HAS_TANGENT;
  // Binormals are always generated alongside tangents (cross(N,T)*sign)
  bool hasBinormal = hasTangent && hasNormal;
  if (hasBinormal) geom.VertexAttributes |= xF::xMeshGeometry::HAS_BINORMAL;
  if (hasUV0)     geom.VertexAttributes |= xF::xMeshGeometry::HAS_TEXCOORD0;
  if (hasUV1)     geom.VertexAttributes |= xF::xMeshGeometry::HAS_TEXCOORD1;
  if (hasColor)   geom.VertexAttributes |= xF::xMeshGeometry::HAS_VERTEXCOLOR;

  geom.Positions.resize(N);
  if (hasNormal)  geom.Normals.resize(N);
  if (hasTangent) geom.Tangents.resize(N);
  if (hasBinormal) geom.Binormals.resize(N);
  if (hasUV0)     geom.TexCoordinates[0].resize(N);
  if (hasUV1)     geom.TexCoordinates[1].resize(N);
  if (hasColor)   geom.VertexColors.resize(N);

  for (std::size_t i = 0; i < N; ++i) {
    float x, y, z;
    TransformPoint(worldMatrix, pos[i*3+0], pos[i*3+1], pos[i*3+2], x, y, z);
    if (kFlipToLeftHanded) z = -z;
    geom.Positions[i] = XVECTOR3(x, y, z);

    if (hasNormal) {
      TransformDir(worldMatrix, nrm[i*3+0], nrm[i*3+1], nrm[i*3+2], x, y, z);
      if (kFlipToLeftHanded) z = -z;
      // Renormalise — non-uniform scale support is best-effort.
      float l = std::sqrt(x*x + y*y + z*z);
      if (l > 1e-8f) { x /= l; y /= l; z /= l; }
      geom.Normals[i] = XVECTOR3(x, y, z);
    }
    if (hasTangent) {
      TransformDir(worldMatrix, tan[i*4+0], tan[i*4+1], tan[i*4+2], x, y, z);
      if (kFlipToLeftHanded) z = -z;
      float l = std::sqrt(x*x + y*y + z*z);
      if (l > 1e-8f) { x /= l; y /= l; z /= l; }
      geom.Tangents[i] = XVECTOR3(x, y, z);
    }
    if (hasBinormal) {
      // B = cross(N, T) * tangent.w  (bitangent sign from glTF/MikkTSpace)
      XVECTOR3 N = geom.Normals[i];
      XVECTOR3 T = geom.Tangents[i];
      float sign = (tan.size() > i*4+3) ? tan[i*4+3] : 1.0f;
      XVECTOR3 B(N.y*T.z - N.z*T.y, N.z*T.x - N.x*T.z, N.x*T.y - N.y*T.x);
      float bl = std::sqrt(B.x*B.x + B.y*B.y + B.z*B.z);
      if (bl > 1e-8f) { B.x /= bl; B.y /= bl; B.z /= bl; }
      geom.Binormals[i] = XVECTOR3(B.x*sign, B.y*sign, B.z*sign);
    }
    if (hasUV0) {
      geom.TexCoordinates[0][i].x = uv0[i*2+0];
      geom.TexCoordinates[0][i].y = uv0[i*2+1];
    }
    if (hasUV1) {
      geom.TexCoordinates[1][i].x = uv1[i*2+0];
      geom.TexCoordinates[1][i].y = uv1[i*2+1];
    }
    if (hasColor) {
      geom.VertexColors[i] = XVECTOR3(
          col[i*colElem+0], col[i*colElem+1], col[i*colElem+2]);
    }
  }

  // Triangle list. The engine has historically used 16-bit indices
  // (xWORD), but the renderer now also supports 32-bit IBs (selected
  // per-geometry via xMeshGeometry::Indices32Bit). We pick the narrowest
  // width that fits the largest source index, keeping the vast majority
  // of game/scene assets on the cheaper 16-bit path.
  bool needs32 = false;
  for (uint32_t v : tris) {
    if (v > 0xFFFFu) { needs32 = true; break; }
  }
  geom.Indices32Bit = needs32;
  if (!needs32) {
    geom.Triangles.resize(tris.size());
    for (std::size_t t = 0; t < tris.size(); t += 3) {
      uint32_t a = tris[t + 0], b = tris[t + 1], c = tris[t + 2];
      if (kFlipToLeftHanded) {
        // Reverse winding to keep CCW after Z negation.
        geom.Triangles[t + 0] = static_cast<xF::xWORD>(a);
        geom.Triangles[t + 1] = static_cast<xF::xWORD>(c);
        geom.Triangles[t + 2] = static_cast<xF::xWORD>(b);
      } else {
        geom.Triangles[t + 0] = static_cast<xF::xWORD>(a);
        geom.Triangles[t + 1] = static_cast<xF::xWORD>(b);
        geom.Triangles[t + 2] = static_cast<xF::xWORD>(c);
      }
    }
  } else {
    geom.Triangles32.resize(tris.size());
    for (std::size_t t = 0; t < tris.size(); t += 3) {
      uint32_t a = tris[t + 0], b = tris[t + 1], c = tris[t + 2];
      if (kFlipToLeftHanded) {
        geom.Triangles32[t + 0] = a;
        geom.Triangles32[t + 1] = c;
        geom.Triangles32[t + 2] = b;
      } else {
        geom.Triangles32[t + 0] = a;
        geom.Triangles32[t + 1] = b;
        geom.Triangles32[t + 2] = c;
      }
    }
    T8_LOG_INFO("[glTF] primitive uses 32-bit IB (%zu vertices)", N);
  }
  geom.NumTriangles = static_cast<xF::xDWORD>(tris.size() / 3);
  geom.NumIndices   = static_cast<xF::xDWORD>(tris.size());

  // Single-material primitive → MaterialList of size 1, FaceIndices
  // all zero. The Material payload itself is filled by the caller.
  geom.MaterialList.Materials.resize(1);
  geom.MaterialList.FaceIndices.assign(geom.NumTriangles, 0);
  geom.MaterialList.NumMatProcess = 1;

  geom.RelativeMatrix = Identity(); // baked into vertices already
  return true;
}

// ── Build interleaved xFinalGeometry from a fully-populated
//    xMeshGeometry. Mirrors XDataBase::CreateSubSets so the renderer
//    sees identical layout. ──────────────────────────────────────────
void BuildFinalGeometry(const xF::xMeshGeometry& geom,
                        xF::xFinalGeometry& out) {
  unsigned int vsz = 0;
  const unsigned int v4 = 16; // vec4 stride
  if (geom.VertexAttributes & xF::xMeshGeometry::HAS_POSITION)  vsz += v4;
  if (geom.VertexAttributes & xF::xMeshGeometry::HAS_NORMAL)    vsz += v4;
  if (geom.VertexAttributes & xF::xMeshGeometry::HAS_TANGENT)   vsz += v4;
  if (geom.VertexAttributes & xF::xMeshGeometry::HAS_BINORMAL)  vsz += v4;
  if (geom.VertexAttributes & xF::xMeshGeometry::HAS_TEXCOORD0) vsz += 8;
  if (geom.VertexAttributes & xF::xMeshGeometry::HAS_TEXCOORD1) vsz += 8;
  if (geom.VertexAttributes & xF::xMeshGeometry::HAS_TEXCOORD2) vsz += 8;
  if (geom.VertexAttributes & xF::xMeshGeometry::HAS_TEXCOORD3) vsz += 8;

  out.VertexSize = vsz;
  out.NumVertex  = geom.NumVertices;
  const unsigned int floatsPerVertex = vsz / 4;
  const unsigned int totalFloats     = floatsPerVertex * geom.NumVertices;
  out.pData     = new float[totalFloats];
  out.pDataDest = new float[totalFloats];

  unsigned int c = 0;
  for (unsigned int j = 0; j < geom.NumVertices; ++j) {
    if (geom.VertexAttributes & xF::xMeshGeometry::HAS_POSITION) {
      out.pData[c++] = geom.Positions[j].x;
      out.pData[c++] = geom.Positions[j].y;
      out.pData[c++] = geom.Positions[j].z;
      out.pData[c++] = 1.0f;
    }
    if (geom.VertexAttributes & xF::xMeshGeometry::HAS_NORMAL) {
      out.pData[c++] = geom.Normals[j].x;
      out.pData[c++] = geom.Normals[j].y;
      out.pData[c++] = geom.Normals[j].z;
      out.pData[c++] = 0.0f;
    }
    if (geom.VertexAttributes & xF::xMeshGeometry::HAS_TANGENT) {
      out.pData[c++] = geom.Tangents[j].x;
      out.pData[c++] = geom.Tangents[j].y;
      out.pData[c++] = geom.Tangents[j].z;
      out.pData[c++] = 0.0f;
    }
    if (geom.VertexAttributes & xF::xMeshGeometry::HAS_BINORMAL) {
      out.pData[c++] = geom.Binormals[j].x;
      out.pData[c++] = geom.Binormals[j].y;
      out.pData[c++] = geom.Binormals[j].z;
      out.pData[c++] = 0.0f;
    }
    if (geom.VertexAttributes & xF::xMeshGeometry::HAS_TEXCOORD0) {
      out.pData[c++] = geom.TexCoordinates[0][j].x;
      out.pData[c++] = geom.TexCoordinates[0][j].y;
    }
    if (geom.VertexAttributes & xF::xMeshGeometry::HAS_TEXCOORD1) {
      out.pData[c++] = geom.TexCoordinates[1][j].x;
      out.pData[c++] = geom.TexCoordinates[1][j].y;
    }
    if (geom.VertexAttributes & xF::xMeshGeometry::HAS_TEXCOORD2) {
      out.pData[c++] = geom.TexCoordinates[2][j].x;
      out.pData[c++] = geom.TexCoordinates[2][j].y;
    }
    if (geom.VertexAttributes & xF::xMeshGeometry::HAS_TEXCOORD3) {
      out.pData[c++] = geom.TexCoordinates[3][j].x;
      out.pData[c++] = geom.TexCoordinates[3][j].y;
    }
  }
  for (unsigned int j = 0; j < c; ++j) out.pDataDest[j] = out.pData[j];
}

void BuildSubsets(xF::xMeshGeometry& geom, xF::xFinalGeometry& fg) {
  // Engine convention (mirrors XDataBase): one xSubsetInfo per material,
  // with NumTris = number of faces tagged with that material index.
  xF::xDWORD numMaterials = static_cast<xF::xDWORD>(geom.MaterialList.Materials.size());
  fg.Subsets.reserve(numMaterials);
  for (xF::xDWORD j = 0; j < numMaterials; ++j) {
    xF::xSubsetInfo s;
    s.NumTris = 0;
    for (xF::xDWORD k = 0; k < geom.MaterialList.FaceIndices.size(); ++k) {
      if (geom.MaterialList.FaceIndices[k] == j) ++s.NumTris;
    }
    s.NumVertex   = s.NumTris * 3;
    s.VertexSize  = fg.VertexSize;
    s.VertexAttrib = geom.VertexAttributes;
    s.bAlignedVertex = true;
    fg.Subsets.push_back(s);
  }
}

// ── Scene-graph traversal (compute world matrix per node) ────────────

void GatherNodes(const Document& doc, int nodeIdx, const XMATRIX44& parent,
                 std::vector<std::pair<int, XMATRIX44>>& outMeshInstances) {
  if (nodeIdx < 0 || nodeIdx >= static_cast<int>(doc.nodes.size())) return;
  const Node& n = doc.nodes[nodeIdx];
  XMATRIX44 local = NodeLocalMatrix(n);
  XMATRIX44 world = local * parent;  // engine convention: T = local * parent
  if (n.mesh) outMeshInstances.emplace_back(*n.mesh, world);
  for (int c : n.children) GatherNodes(doc, c, world, outMeshInstances);
}

} // namespace

bool ConvertToXDatabase(const Document& doc, xF::XDataBase& out,
                        const std::string& sourcePath) {
  // Use the requested scene if specified, else scene 0, else all roots.
  std::vector<int> rootNodes;
  int sceneIdx = doc.scene.value_or(doc.scenes.empty() ? -1 : 0);
  if (sceneIdx >= 0 && sceneIdx < static_cast<int>(doc.scenes.size())) {
    rootNodes = doc.scenes[sceneIdx].nodes;
  } else {
    // Fallback: every node that has a mesh (no hierarchy).
    for (std::size_t i = 0; i < doc.nodes.size(); ++i) {
      if (doc.nodes[i].mesh) rootNodes.push_back(static_cast<int>(i));
    }
  }

  // Walk the scene graph collecting (mesh-index, world-matrix) pairs.
  std::vector<std::pair<int, XMATRIX44>> instances;
  XMATRIX44 ident = Identity();
  for (int r : rootNodes) GatherNodes(doc, r, ident, instances);

  if (instances.empty()) {
    T8_LOG_ERROR("[glTF] '%s' has no mesh instances in the scene graph",
                 sourcePath.c_str());
    return false;
  }

  // Set up the single xMeshContainer the legacy XDataBase always uses.
  out.m_name = sourcePath;
  out.XMeshDataBase.push_back(new xF::xMeshContainer);
  xF::xMeshContainer* mc = out.XMeshDataBase.back();
  mc->FileName = sourcePath;

  // Reserve for worst-case (every primitive becomes a Geometry).
  std::size_t totalPrims = 0;
  for (auto& kv : instances) {
    if (kv.first >= 0 && kv.first < static_cast<int>(doc.meshes.size()))
      totalPrims += doc.meshes[kv.first].primitives.size();
  }
  mc->Geometry.reserve(totalPrims);
  out.MeshInfo.reserve(totalPrims);

  for (auto& kv : instances) {
    int meshIdx = kv.first;
    const XMATRIX44& world = kv.second;
    if (meshIdx < 0 || meshIdx >= static_cast<int>(doc.meshes.size())) continue;
    const Mesh& m = doc.meshes[meshIdx];

    for (std::size_t pi = 0; pi < m.primitives.size(); ++pi) {
      const Primitive& prim = m.primitives[pi];
      mc->Geometry.emplace_back();
      xF::xMeshGeometry& geom = mc->Geometry.back();
      if (!BuildGeometry(doc, prim, world, geom)) {
        T8_LOG_ERROR("[glTF] '%s' mesh %d primitive %zu: build failed — skipped",
                     sourcePath.c_str(), meshIdx, pi);
        mc->Geometry.pop_back();
        continue;
      }
      if (!m.name.empty()) geom.Name = m.name;

      // Material: glTF allows -1 (no material) — handled by ConvertMaterial.
      ConvertMaterial(doc, prim.material.value_or(-1),
                      geom.MaterialList.Materials[0]);

      // Build the corresponding interleaved xFinalGeometry.
      out.MeshInfo.emplace_back();
      xF::xFinalGeometry& fg = out.MeshInfo.back();
      BuildFinalGeometry(geom, fg);
      BuildSubsets(geom, fg);
      // Engine reads pActual->VertexSize after CreateSubSets — set it
      // here for parity with the legacy path.
      geom.VertexSize = fg.VertexSize;
    }
  }

  T8_LOG_INFO("[glTF] '%s' converted: %zu geometries / %zu mesh instances",
              sourcePath.c_str(), mc->Geometry.size(), instances.size());
  return !mc->Geometry.empty();
}

} // namespace gltf
} // namespace t800
