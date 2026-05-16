#include <pch.h>
#include <physics/JoltPhysicsSystem.h>

#if defined(T850_ENABLE_JOLT)

#include <utils/Log.h>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/StreamWrapper.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CollisionGroup.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <utils/ResourceLocator.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <unordered_set>
#include <vector>

JPH_SUPPRESS_WARNINGS

namespace {

using t850::AABB;
using t850::PhysicsBodyDesc;
using t850::PhysicsBodyMotion;
using t850::PhysicsCookStats;
using t850::PhysicsMeshBuildQuality;
using t850::PhysicsShapeDesc;
using t850::PhysicsShapeType;
using t850::PhysicsTriangleMeshCookSettings;
using t850::PhysicsTriangleMeshDesc;
using t850::ResourceLocator;

namespace Layers {
static constexpr JPH::ObjectLayer NonMoving = 0;
static constexpr JPH::ObjectLayer Moving = 1;
static constexpr JPH::ObjectLayer Count = 2;
}

static constexpr float kFixedPhysicsStepSeconds = 1.0f / 60.0f;
static constexpr float kMaxSimulationSpeedScale = 32.0f;
static constexpr float kMaxJoltBroadPhaseCoordinate = 1.0e12f;

static bool IsUsablePhysicsCoordinate(float value) {
  return std::isfinite(value) && std::fabs(value) <= kMaxJoltBroadPhaseCoordinate;
}

static bool IsBoundedPhysicsExtent(float value) {
  return std::isfinite(value) && value >= 0.0f && value <= kMaxJoltBroadPhaseCoordinate;
}

static bool IsUsablePhysicsTransform(const XMATRIX44& transform) {
  for (int i = 0; i < 16; ++i) {
    if (!IsUsablePhysicsCoordinate(transform.mat[i])) {
      return false;
    }
  }
  return true;
}

static bool IsUsablePhysicsShape(const PhysicsShapeDesc& shape) {
  switch (shape.type) {
  case PhysicsShapeType::Box:
  case PhysicsShapeType::TriangleMesh:
    return IsBoundedPhysicsExtent(shape.halfExtents.x) &&
           IsBoundedPhysicsExtent(shape.halfExtents.y) &&
           IsBoundedPhysicsExtent(shape.halfExtents.z);
  case PhysicsShapeType::Capsule:
    return IsBoundedPhysicsExtent(shape.radius) &&
           IsBoundedPhysicsExtent(shape.halfHeight);
  default:
    return false;
  }
}

static bool IsUsablePhysicsBodyDesc(const PhysicsBodyDesc& desc) {
  return IsUsablePhysicsTransform(desc.worldTransform) && IsUsablePhysicsShape(desc.shape);
}

namespace BroadPhaseLayers {
static const JPH::BroadPhaseLayer NonMoving(0);
static const JPH::BroadPhaseLayer Moving(1);
static constexpr JPH::uint Count = 2;
}

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
  bool ShouldCollide(JPH::ObjectLayer object1, JPH::ObjectLayer object2) const override {
    switch (object1) {
    case Layers::NonMoving:
      return object2 == Layers::Moving;
    case Layers::Moving:
      return true;
    default:
      JPH_ASSERT(false);
      return false;
    }
  }
};

class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
  BroadPhaseLayerInterfaceImpl() {
    m_objectToBroadPhase[Layers::NonMoving] = BroadPhaseLayers::NonMoving;
    m_objectToBroadPhase[Layers::Moving] = BroadPhaseLayers::Moving;
  }

  JPH::uint GetNumBroadPhaseLayers() const override {
    return BroadPhaseLayers::Count;
  }

  JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
    JPH_ASSERT(layer < Layers::Count);
    return m_objectToBroadPhase[layer];
  }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
    switch (static_cast<JPH::BroadPhaseLayer::Type>(layer)) {
    case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::NonMoving):
      return "NonMoving";
    case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::Moving):
      return "Moving";
    default:
      JPH_ASSERT(false);
      return "Invalid";
    }
  }
#endif

private:
  JPH::BroadPhaseLayer m_objectToBroadPhase[Layers::Count];
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
  bool ShouldCollide(JPH::ObjectLayer objectLayer, JPH::BroadPhaseLayer broadPhaseLayer) const override {
    switch (objectLayer) {
    case Layers::NonMoving:
      return broadPhaseLayer == BroadPhaseLayers::Moving;
    case Layers::Moving:
      return true;
    default:
      JPH_ASSERT(false);
      return false;
    }
  }
};

static int g_joltInstanceCount = 0;
static bool g_createdFactory = false;
static bool g_registeredTypes = false;

static float Length3(float x, float y, float z) {
  return std::sqrt(x * x + y * y + z * z);
}

static void Normalize3(float& x, float& y, float& z) {
  const float length = Length3(x, y, z);
  if (length <= 0.000001f) {
    x = 0.0f;
    y = 0.0f;
    z = 0.0f;
    return;
  }

  x /= length;
  y /= length;
  z /= length;
}

static XMATRIX44 NormalizeRotationPreserveTranslation(const XMATRIX44& matrix) {
  XMATRIX44 out = matrix;
  Normalize3(out.m11, out.m12, out.m13);
  Normalize3(out.m21, out.m22, out.m23);
  Normalize3(out.m31, out.m32, out.m33);
  return out;
}

static JPH::Vec3 ToJoltAxisX(const XMATRIX44& matrix) {
  float x = matrix.m11;
  float y = matrix.m12;
  float z = matrix.m13;
  Normalize3(x, y, z);
  return JPH::Vec3(x, y, z);
}

static JPH::Vec3 ToJoltAxisY(const XMATRIX44& matrix) {
  float x = matrix.m21;
  float y = matrix.m22;
  float z = matrix.m23;
  Normalize3(x, y, z);
  return JPH::Vec3(x, y, z);
}

static JPH::Vec3 ToJoltAxis(const XVECTOR3& axis, const JPH::Vec3& fallback) {
  float x = axis.x;
  float y = axis.y;
  float z = axis.z;
  if (!IsUsablePhysicsCoordinate(x) ||
      !IsUsablePhysicsCoordinate(y) ||
      !IsUsablePhysicsCoordinate(z) ||
      Length3(x, y, z) <= 0.000001f) {
    return fallback;
  }
  Normalize3(x, y, z);
  return JPH::Vec3(x, y, z);
}

static JPH::RVec3 ToJoltPosition(const XMATRIX44& matrix) {
  return JPH::RVec3(matrix.m41, matrix.m42, matrix.m43);
}

static JPH::Quat ToJoltRotation(const XMATRIX44& matrix) {
  XMATRIX44 normalized = NormalizeRotationPreserveTranslation(matrix);
  const float trace = normalized.m11 + normalized.m22 + normalized.m33;

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 1.0f;

  if (trace > 0.0f) {
    const float s = std::sqrt(trace + 1.0f) * 2.0f;
    w = 0.25f * s;
    x = (normalized.m23 - normalized.m32) / s;
    y = (normalized.m31 - normalized.m13) / s;
    z = (normalized.m12 - normalized.m21) / s;
  } else if (normalized.m11 > normalized.m22 && normalized.m11 > normalized.m33) {
    const float s = std::sqrt(1.0f + normalized.m11 - normalized.m22 - normalized.m33) * 2.0f;
    w = (normalized.m23 - normalized.m32) / s;
    x = 0.25f * s;
    y = (normalized.m12 + normalized.m21) / s;
    z = (normalized.m31 + normalized.m13) / s;
  } else if (normalized.m22 > normalized.m33) {
    const float s = std::sqrt(1.0f + normalized.m22 - normalized.m11 - normalized.m33) * 2.0f;
    w = (normalized.m31 - normalized.m13) / s;
    x = (normalized.m12 + normalized.m21) / s;
    y = 0.25f * s;
    z = (normalized.m23 + normalized.m32) / s;
  } else {
    const float s = std::sqrt(1.0f + normalized.m33 - normalized.m11 - normalized.m22) * 2.0f;
    w = (normalized.m12 - normalized.m21) / s;
    x = (normalized.m31 + normalized.m13) / s;
    y = (normalized.m23 + normalized.m32) / s;
    z = 0.25f * s;
  }

  const float length = Length3(x, y, z);
  const float quatLength = std::sqrt(length * length + w * w);
  if (quatLength <= 0.000001f) {
    return JPH::Quat::sIdentity();
  }

  return JPH::Quat(x / quatLength, y / quatLength, z / quatLength, w / quatLength);
}

static XMATRIX44 FromJoltTransform(JPH::RVec3Arg position, JPH::QuatArg rotation) {
  const float x = rotation.GetX();
  const float y = rotation.GetY();
  const float z = rotation.GetZ();
  const float w = rotation.GetW();
  const float xx = x * x;
  const float yy = y * y;
  const float zz = z * z;
  const float xy = x * y;
  const float xz = x * z;
  const float yz = y * z;
  const float wx = w * x;
  const float wy = w * y;
  const float wz = w * z;

  XMATRIX44 out;
  out.m11 = 1.0f - 2.0f * (yy + zz);
  out.m12 = 2.0f * (xy + wz);
  out.m13 = 2.0f * (xz - wy);
  out.m14 = 0.0f;
  out.m21 = 2.0f * (xy - wz);
  out.m22 = 1.0f - 2.0f * (xx + zz);
  out.m23 = 2.0f * (yz + wx);
  out.m24 = 0.0f;
  out.m31 = 2.0f * (xz + wy);
  out.m32 = 2.0f * (yz - wx);
  out.m33 = 1.0f - 2.0f * (xx + yy);
  out.m34 = 0.0f;
  out.m41 = static_cast<float>(position.GetX());
  out.m42 = static_cast<float>(position.GetY());
  out.m43 = static_cast<float>(position.GetZ());
  out.m44 = 1.0f;
  return out;
}

static JPH::EMotionType ToJoltMotion(PhysicsBodyMotion motion) {
  switch (motion) {
  case PhysicsBodyMotion::Static:
    return JPH::EMotionType::Static;
  case PhysicsBodyMotion::Kinematic:
    return JPH::EMotionType::Kinematic;
  case PhysicsBodyMotion::Dynamic:
  default:
    return JPH::EMotionType::Dynamic;
  }
}

static PhysicsBodyMotion FromJoltMotion(JPH::EMotionType motion) {
  switch (motion) {
  case JPH::EMotionType::Static:
    return PhysicsBodyMotion::Static;
  case JPH::EMotionType::Kinematic:
    return PhysicsBodyMotion::Kinematic;
  case JPH::EMotionType::Dynamic:
  default:
    return PhysicsBodyMotion::Dynamic;
  }
}

static JPH::ObjectLayer ToJoltObjectLayer(PhysicsBodyMotion motion) {
  return motion == PhysicsBodyMotion::Static ? Layers::NonMoving : Layers::Moving;
}

static JPH::RefConst<JPH::Shape> CreateJoltShape(const PhysicsShapeDesc& desc) {
  if (desc.type == PhysicsShapeType::Capsule) {
    const float radius = (std::max)(0.001f, desc.radius);
    const float halfHeight = (std::max)(0.001f, desc.halfHeight);
    return new JPH::CapsuleShape(halfHeight, radius);
  }

  if (desc.type == PhysicsShapeType::Box) {
    const float x = (std::max)(0.001f, desc.halfExtents.x);
    const float y = (std::max)(0.001f, desc.halfExtents.y);
    const float z = (std::max)(0.001f, desc.halfExtents.z);
    return new JPH::BoxShape(JPH::Vec3(x, y, z), 0.0f);
  }

  return nullptr;
}

static constexpr std::array<char, 8> kCookedMeshCacheMagic = { 'T', '8', 'J', 'P', 'H', 'Y', 'S', 'M' };
static constexpr uint32_t kCookedMeshCacheVersion = 1;
static constexpr uint32_t kCookedMeshCacheHeaderSize = 72;

template <typename T>
static bool ReadPod(std::ifstream& file, T& value) {
  file.read(reinterpret_cast<char*>(&value), sizeof(T));
  return file.good();
}

template <typename T>
static void WritePod(std::ofstream& file, const T& value) {
  file.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

static uint64_t HashBytes(uint64_t hash, const void* data, std::size_t size) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 0x100000001b3ull;
  }
  return hash;
}

static uint64_t HashValue(uint64_t hash, uint64_t value) {
  return HashBytes(hash, &value, sizeof(value));
}

static uint64_t HashString(uint64_t hash, const std::string& value) {
  return HashBytes(hash, value.data(), value.size());
}

static uint32_t GetCachePlatformTag() {
#if defined(OS_ANDROID)
  uint32_t tag = 0x414E4452u; // ANDR
#elif defined(_WIN32) || defined(_WIN64)
  uint32_t tag = 0x57494E20u; // WIN
#else
  uint32_t tag = 0x504F5320u; // POS
#endif

#if defined(_M_ARM64) || defined(__aarch64__)
  tag ^= 0xA64A64A6u;
#elif defined(_M_X64) || defined(__x86_64__)
  tag ^= 0x64A64A64u;
#elif defined(_M_IX86) || defined(__i386__)
  tag ^= 0x86328632u;
#endif
  tag ^= static_cast<uint32_t>(sizeof(void*) * 8u);
  return tag;
}

static constexpr uint64_t GetJoltCacheVersionId() {
  uint64_t features = 0;
#if defined(JPH_DOUBLE_PRECISION)
  features |= 1ull << 0;
#endif
#if defined(JPH_CROSS_PLATFORM_DETERMINISTIC)
  features |= 1ull << 1;
#endif
#if defined(JPH_FLOATING_POINT_EXCEPTIONS_ENABLED)
  features |= 1ull << 2;
#endif
#if defined(JPH_PROFILE_ENABLED)
  features |= 1ull << 3;
#endif
#if defined(JPH_EXTERNAL_PROFILE)
  features |= 1ull << 4;
#endif
#if defined(JPH_DEBUG_RENDERER)
  features |= 1ull << 5;
#endif
#if defined(JPH_DISABLE_TEMP_ALLOCATOR)
  features |= 1ull << 6;
#endif
#if defined(JPH_DISABLE_CUSTOM_ALLOCATOR)
  features |= 1ull << 7;
#endif
#if defined(JPH_OBJECT_LAYER_BITS) && JPH_OBJECT_LAYER_BITS == 32
  features |= 1ull << 8;
#endif
#if defined(JPH_ENABLE_ASSERTS)
  features |= 1ull << 9;
#endif
#if defined(JPH_OBJECT_STREAM)
  features |= 1ull << 10;
#endif
  return (features << 24)
      | (static_cast<uint64_t>(JPH_VERSION_MAJOR) << 16)
      | (static_cast<uint64_t>(JPH_VERSION_MINOR) << 8)
      | static_cast<uint64_t>(JPH_VERSION_PATCH);
}

static uint32_t CacheBuildQualityValue(PhysicsMeshBuildQuality quality) {
  return quality == PhysicsMeshBuildQuality::FavorBuildSpeed ? 1u : 0u;
}

static uint32_t SanitizedMaxTrianglesPerLeaf(uint32_t value) {
  return (std::max)(1u, (std::min)(8u, value));
}

static uint64_t BuildTriangleMeshCookHash(const PhysicsTriangleMeshDesc& mesh) {
  uint64_t hash = 0xcbf29ce484222325ull;
  hash = HashValue(hash, kCookedMeshCacheVersion);
  hash = HashValue(hash, GetJoltCacheVersionId());
  hash = HashValue(hash, GetCachePlatformTag());
  hash = HashValue(hash, SanitizedMaxTrianglesPerLeaf(mesh.settings.maxTrianglesPerLeaf));
  hash = HashValue(hash, CacheBuildQualityValue(mesh.settings.buildQuality));
  hash = HashString(hash, ResourceLocator::NormalizePath(mesh.sourcePath));
  hash = HashValue(hash, static_cast<uint64_t>(mesh.vertices.size()));
  hash = HashValue(hash, static_cast<uint64_t>(mesh.indices.size()));
  for (const XVECTOR3& vertex : mesh.vertices) {
    hash = HashBytes(hash, &vertex.x, sizeof(vertex.x));
    hash = HashBytes(hash, &vertex.y, sizeof(vertex.y));
    hash = HashBytes(hash, &vertex.z, sizeof(vertex.z));
  }
  for (uint32_t index : mesh.indices) {
    hash = HashBytes(hash, &index, sizeof(index));
  }
  return hash;
}

static std::filesystem::path CookedTriangleMeshCachePath(const PhysicsTriangleMeshDesc& mesh, uint64_t hash) {
  std::filesystem::path source(mesh.sourcePath);
  std::filesystem::path parent = source.parent_path();
  if (parent.empty()) {
    parent = ".";
  }

  std::string stem = source.stem().string();
  if (stem.empty()) {
    stem = "mesh";
  }

  std::ostringstream filename;
  filename << stem << "_joltmesh_v" << kCookedMeshCacheVersion << "_"
           << std::hex << std::setw(16) << std::setfill('0') << hash << ".t8jolt";

  const std::filesystem::path relativeCachePath = parent / ".t8cache" / filename.str();
  if (source.is_absolute()) {
    return relativeCachePath;
  }
  return ResourceLocator::Instance().ResolveCachePath(relativeCachePath.string());
}

static bool ReadCookedMeshHeader(std::ifstream& file,
                                 uint64_t expectedHash,
                                 uint32_t expectedVertexCount,
                                 uint32_t expectedTriangleCount) {
  std::array<char, 8> magic = {};
  file.read(magic.data(), magic.size());
  if (!file.good() || magic != kCookedMeshCacheMagic) {
    return false;
  }

  uint32_t version = 0;
  uint32_t headerSize = 0;
  uint64_t geometryHash = 0;
  uint64_t joltVersion = 0;
  uint32_t platformTag = 0;
  uint32_t vertexCount = 0;
  uint32_t triangleCount = 0;
  uint32_t maxTrianglesPerLeaf = 0;
  uint32_t buildQuality = 0;
  uint64_t reserved0 = 0;
  uint64_t reserved1 = 0;
  if (!ReadPod(file, version)
      || !ReadPod(file, headerSize)
      || !ReadPod(file, geometryHash)
      || !ReadPod(file, joltVersion)
      || !ReadPod(file, platformTag)
      || !ReadPod(file, vertexCount)
      || !ReadPod(file, triangleCount)
      || !ReadPod(file, maxTrianglesPerLeaf)
      || !ReadPod(file, buildQuality)
      || !ReadPod(file, reserved0)
      || !ReadPod(file, reserved1)) {
    return false;
  }

  return version == kCookedMeshCacheVersion
      && headerSize == kCookedMeshCacheHeaderSize
      && geometryHash == expectedHash
      && joltVersion == GetJoltCacheVersionId()
      && platformTag == GetCachePlatformTag()
      && vertexCount == expectedVertexCount
      && triangleCount == expectedTriangleCount;
}

static void WriteCookedMeshHeader(std::ofstream& file,
                                  uint64_t geometryHash,
                                  uint32_t vertexCount,
                                  uint32_t triangleCount,
                                  const PhysicsTriangleMeshCookSettings& settings) {
  file.write(kCookedMeshCacheMagic.data(), kCookedMeshCacheMagic.size());
  WritePod(file, kCookedMeshCacheVersion);
  WritePod(file, kCookedMeshCacheHeaderSize);
  WritePod(file, geometryHash);
  WritePod(file, GetJoltCacheVersionId());
  WritePod(file, GetCachePlatformTag());
  WritePod(file, vertexCount);
  WritePod(file, triangleCount);
  WritePod(file, SanitizedMaxTrianglesPerLeaf(settings.maxTrianglesPerLeaf));
  WritePod(file, CacheBuildQualityValue(settings.buildQuality));
  WritePod(file, uint64_t{0});
  WritePod(file, uint64_t{0});
}

static JPH::RefConst<JPH::Shape> LoadCookedTriangleMeshShape(const std::filesystem::path& cachePath,
                                                             uint64_t geometryHash,
                                                             uint32_t vertexCount,
                                                             uint32_t triangleCount) {
  std::ifstream file(cachePath, std::ios::binary);
  if (!file.is_open()) {
    return nullptr;
  }

  if (!ReadCookedMeshHeader(file, geometryHash, vertexCount, triangleCount)) {
    return nullptr;
  }

  JPH::StreamInWrapper stream(file);
  JPH::Shape::IDToShapeMap shapeMap;
  JPH::Shape::IDToMaterialMap materialMap;
  JPH::Shape::ShapeResult result = JPH::Shape::sRestoreWithChildren(stream, shapeMap, materialMap);
  if (!result.IsValid() || stream.IsFailed()) {
    return nullptr;
  }
  return result.Get();
}

static bool SaveCookedTriangleMeshShape(const std::filesystem::path& cachePath,
                                        uint64_t geometryHash,
                                        uint32_t vertexCount,
                                        uint32_t triangleCount,
                                        const PhysicsTriangleMeshCookSettings& settings,
                                        const JPH::Shape& shape) {
  std::error_code ec;
  const std::filesystem::path parent = cachePath.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      T8_LOG_ERROR("[PhysicsMeshCache] Failed to create cache directory '%s'", parent.string().c_str());
      return false;
    }
  }

  std::ofstream file(cachePath, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    T8_LOG_ERROR("[PhysicsMeshCache] Failed to open cache file '%s' for writing", cachePath.string().c_str());
    return false;
  }

  WriteCookedMeshHeader(file, geometryHash, vertexCount, triangleCount, settings);
  JPH::StreamOutWrapper stream(file);
  JPH::Shape::ShapeToIDMap shapeMap;
  JPH::Shape::MaterialToIDMap materialMap;
  shape.SaveWithChildren(stream, shapeMap, materialMap);
  return !stream.IsFailed() && file.good();
}

static JPH::RefConst<JPH::Shape> CreateOrLoadTriangleMeshShape(const PhysicsTriangleMeshDesc& mesh,
                                                               PhysicsCookStats& stats) {
  stats.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
  stats.triangleCount = static_cast<uint32_t>(mesh.indices.size() / 3u);
  if (mesh.vertices.empty() || stats.triangleCount == 0) {
    return nullptr;
  }

  const uint64_t geometryHash = BuildTriangleMeshCookHash(mesh);
  const std::filesystem::path cachePath = CookedTriangleMeshCachePath(mesh, geometryHash);
  stats.cachePath = cachePath.string();

  if (mesh.settings.useDiskCache) {
    const auto loadStart = std::chrono::steady_clock::now();
    JPH::RefConst<JPH::Shape> cachedShape = LoadCookedTriangleMeshShape(cachePath, geometryHash, stats.vertexCount, stats.triangleCount);
    const auto loadEnd = std::chrono::steady_clock::now();
    stats.cacheLoadMs = std::chrono::duration<double, std::milli>(loadEnd - loadStart).count();
    if (cachedShape != nullptr) {
      stats.cacheHit = true;
      return cachedShape;
    }
  }

  const auto cookStart = std::chrono::steady_clock::now();
  JPH::VertexList vertices;
  vertices.reserve(mesh.vertices.size());
  for (const XVECTOR3& vertex : mesh.vertices) {
    vertices.emplace_back(vertex.x, vertex.y, vertex.z);
  }

  JPH::IndexedTriangleList triangles;
  triangles.reserve(stats.triangleCount);
  for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    triangles.emplace_back(mesh.indices[i + 0], mesh.indices[i + 1], mesh.indices[i + 2], 0);
  }

  JPH::MeshShapeSettings shapeSettings(std::move(vertices), std::move(triangles));
  shapeSettings.mMaxTrianglesPerLeaf = SanitizedMaxTrianglesPerLeaf(mesh.settings.maxTrianglesPerLeaf);
  shapeSettings.mBuildQuality = mesh.settings.buildQuality == PhysicsMeshBuildQuality::FavorBuildSpeed
      ? JPH::MeshShapeSettings::EBuildQuality::FavorBuildSpeed
      : JPH::MeshShapeSettings::EBuildQuality::FavorRuntimePerformance;

  JPH::Shape::ShapeResult result = shapeSettings.Create();
  const auto cookEnd = std::chrono::steady_clock::now();
  stats.cookMs = std::chrono::duration<double, std::milli>(cookEnd - cookStart).count();
  if (!result.IsValid()) {
    T8_LOG_ERROR("[PhysicsMeshCache] Jolt mesh cook failed for '%s': %s",
                 mesh.sourcePath.c_str(),
                 result.HasError() ? result.GetError().c_str() : "unknown error");
    return nullptr;
  }

  JPH::RefConst<JPH::Shape> shape = result.Get();
  if (mesh.settings.useDiskCache) {
    const auto saveStart = std::chrono::steady_clock::now();
    stats.cacheSaved = SaveCookedTriangleMeshShape(cachePath, geometryHash, stats.vertexCount, stats.triangleCount, mesh.settings, *shape);
    const auto saveEnd = std::chrono::steady_clock::now();
    stats.cacheSaveMs = std::chrono::duration<double, std::milli>(saveEnd - saveStart).count();
  }

  return shape;
}

static std::shared_ptr<const std::vector<uint32_t>> BuildTriangleMeshDebugLineIndices(const std::vector<uint32_t>& triangleIndices,
                                                                                      uint32_t vertexCount) {
  std::shared_ptr<std::vector<uint32_t>> lineIndices = std::make_shared<std::vector<uint32_t>>();
  lineIndices->reserve(triangleIndices.size() * 2u);
  std::unordered_set<uint64_t> uniqueEdges;
  uniqueEdges.reserve(triangleIndices.size());

  auto addEdge = [&](uint32_t a, uint32_t b) {
    if (a >= vertexCount || b >= vertexCount || a == b) {
      return;
    }

    const uint32_t lo = (std::min)(a, b);
    const uint32_t hi = (std::max)(a, b);
    const uint64_t key = (static_cast<uint64_t>(lo) << 32u) | static_cast<uint64_t>(hi);
    if (!uniqueEdges.insert(key).second) {
      return;
    }

    lineIndices->push_back(lo);
    lineIndices->push_back(hi);
  };

  const uint32_t triangleCount = static_cast<uint32_t>(triangleIndices.size() / 3u);
  for (uint32_t triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex) {
    const std::size_t base = static_cast<std::size_t>(triangleIndex) * 3u;
    const uint32_t i0 = triangleIndices[base + 0u];
    const uint32_t i1 = triangleIndices[base + 1u];
    const uint32_t i2 = triangleIndices[base + 2u];

    addEdge(i0, i1);
    addEdge(i1, i2);
    addEdge(i2, i0);
  }

  return lineIndices;
}

static uint64_t MakeBodyUserData(uint32_t entityId, int boneIndex) {
  const uint64_t packedBone = static_cast<uint64_t>(static_cast<uint32_t>(boneIndex + 1) & 0xffffu);
  return (static_cast<uint64_t>(entityId) << 32) | packedBone;
}

static JPH::uint GetWorkerThreadCount() {
  const unsigned int threadCount = std::thread::hardware_concurrency();
  return threadCount > 1 ? threadCount - 1 : 1;
}

static void TraceImpl(const char* format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  T8_LOG_DEBUG("%s", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
static bool AssertFailedImpl(const char* expression, const char* message, const char* file, JPH::uint line) {
  T8_LOG_ERROR("%s:%u: (%s) %s", file, line, expression, message ? message : "");
  return true;
}
#endif

static void InitializeJoltGlobals() {
  if (g_joltInstanceCount == 0) {
    JPH::RegisterDefaultAllocator();
    JPH::Trace = TraceImpl;
#ifdef JPH_ENABLE_ASSERTS
    JPH::AssertFailed = AssertFailedImpl;
#endif

    if (JPH::Factory::sInstance == nullptr) {
      JPH::Factory::sInstance = new JPH::Factory();
      g_createdFactory = true;
    }

    JPH::RegisterTypes();
    g_registeredTypes = true;
  }

  ++g_joltInstanceCount;
}

static void ShutdownJoltGlobals() {
  if (g_joltInstanceCount == 0) {
    return;
  }

  --g_joltInstanceCount;
  if (g_joltInstanceCount != 0) {
    return;
  }

  if (g_registeredTypes) {
    JPH::UnregisterTypes();
    g_registeredTypes = false;
  }

  if (g_createdFactory) {
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
    g_createdFactory = false;
  }
}

} // namespace

namespace t850 {

struct JoltPhysicsSystem::Impl {
  struct BodySlot {
    JPH::BodyID id;
    uint32_t entityId = 0;
    int boneIndex = -1;
    PhysicsShapeDesc shape;
    std::string debugName;
    std::shared_ptr<const std::vector<XVECTOR3>> debugVertices;
    std::shared_ptr<const std::vector<uint32_t>> debugLineIndices;
    PhysicsBodyMotion motion = PhysicsBodyMotion::Static;
    bool alive = false;
  };

  struct RagdollSlot {
    uint32_t entityId = 0;
    std::vector<PhysicsBodyHandle> bodies;
    std::vector<JPH::Ref<JPH::Constraint>> constraints;
    JPH::Ref<JPH::GroupFilterTable> groupFilter;
    bool alive = false;
  };

  BroadPhaseLayerInterfaceImpl broadPhaseLayerInterface;
  ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter;
  ObjectLayerPairFilterImpl objectLayerPairFilter;
  JPH::TempAllocatorImpl tempAllocator;
  JPH::JobSystemThreadPool jobSystem;
  JPH::PhysicsSystem physicsSystem;
  std::vector<BodySlot> bodies;
  std::vector<RagdollSlot> ragdolls;
  float simulationSpeedScale = 1.0f;
  bool useFixedSimulationDelta = false;
  uint32_t updateStatsFrames = 0;
  uint32_t updateStatsTotalJoltUpdates = 0;
  uint32_t updateStatsMaxJoltUpdates = 0;
  uint32_t updateStatsMaxMovingStaticContacts = 0;
  uint32_t updateStatsMaxMovingMovingContacts = 0;
  double updateStatsTotalMs = 0.0;
  double updateStatsMaxMs = 0.0;

  Impl()
      : tempAllocator(10 * 1024 * 1024),
        jobSystem(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, GetWorkerThreadCount()) {}

  BodySlot* Resolve(PhysicsBodyHandle handle) {
    if (!handle.IsValid() || handle.value >= bodies.size()) {
      return nullptr;
    }

    BodySlot& slot = bodies[handle.value];
    return slot.alive ? &slot : nullptr;
  }

  const BodySlot* Resolve(PhysicsBodyHandle handle) const {
    if (!handle.IsValid() || handle.value >= bodies.size()) {
      return nullptr;
    }

    const BodySlot& slot = bodies[handle.value];
    return slot.alive ? &slot : nullptr;
  }

  RagdollSlot* Resolve(PhysicsRagdollHandle handle) {
    if (!handle.IsValid() || handle.value >= ragdolls.size()) {
      return nullptr;
    }

    RagdollSlot& slot = ragdolls[handle.value];
    return slot.alive ? &slot : nullptr;
  }

  const RagdollSlot* Resolve(PhysicsRagdollHandle handle) const {
    if (!handle.IsValid() || handle.value >= ragdolls.size()) {
      return nullptr;
    }

    const RagdollSlot& slot = ragdolls[handle.value];
    return slot.alive ? &slot : nullptr;
  }
};

JoltPhysicsSystem::JoltPhysicsSystem()
    : m_impl(nullptr),
      m_initialized(false) {}

JoltPhysicsSystem::~JoltPhysicsSystem() {
  Shutdown();
}

bool JoltPhysicsSystem::Initialize() {
  if (m_initialized) {
    return true;
  }

  InitializeJoltGlobals();
  m_impl = new Impl();

  constexpr JPH::uint maxBodies = 65536;
  constexpr JPH::uint numBodyMutexes = 0;
  constexpr JPH::uint maxBodyPairs = 65536;
  constexpr JPH::uint maxContactConstraints = 10240;

  m_impl->physicsSystem.Init(
      maxBodies,
      numBodyMutexes,
      maxBodyPairs,
      maxContactConstraints,
      m_impl->broadPhaseLayerInterface,
      m_impl->objectVsBroadPhaseLayerFilter,
      m_impl->objectLayerPairFilter);
  m_impl->physicsSystem.OptimizeBroadPhase();

  m_initialized = true;
  T8_LOG_INFO("Jolt Physics initialized (max concurrency=%d)", m_impl->jobSystem.GetMaxConcurrency());
  return true;
}

void JoltPhysicsSystem::Shutdown() {
  if (!m_initialized) {
    return;
  }

  for (uint32_t i = 0; i < m_impl->ragdolls.size(); ++i) {
    PhysicsRagdollHandle handle;
    handle.value = i;
    DestroyRagdoll(handle);
  }
  for (uint32_t i = 0; i < m_impl->bodies.size(); ++i) {
    PhysicsBodyHandle handle;
    handle.value = i;
    DestroyBody(handle);
  }

  delete m_impl;
  m_impl = nullptr;
  m_initialized = false;
  ShutdownJoltGlobals();
}

void JoltPhysicsSystem::Update(float deltaSeconds) {
  if (!m_initialized || !m_impl) {
    return;
  }
  if (!std::isfinite(deltaSeconds)) {
    T8_LOG_ERROR("Ignoring invalid Jolt update delta %.6f", deltaSeconds);
    return;
  }
  if (deltaSeconds <= 0.0f || m_impl->simulationSpeedScale <= 0.0f) {
    return;
  }

  constexpr int collisionSteps = 2;
  const float updateDeltaSeconds =
      m_impl->useFixedSimulationDelta ? kFixedPhysicsStepSeconds : deltaSeconds;
  const float simulationDeltaSeconds = updateDeltaSeconds * m_impl->simulationSpeedScale;
  if (!std::isfinite(simulationDeltaSeconds) || simulationDeltaSeconds <= 0.0f) {
    T8_LOG_ERROR("Ignoring invalid scaled Jolt update delta %.6f", simulationDeltaSeconds);
    return;
  }

  const auto updateStart = std::chrono::high_resolution_clock::now();
  m_impl->physicsSystem.Update(simulationDeltaSeconds, collisionSteps, &m_impl->tempAllocator, &m_impl->jobSystem);
  const auto updateEnd = std::chrono::high_resolution_clock::now();
  const double updateMs = std::chrono::duration<double, std::milli>(updateEnd - updateStart).count();

  const JPH::uint32 activeRigidBodies = m_impl->physicsSystem.GetNumActiveBodies(JPH::EBodyType::RigidBody);
  uint32_t movingStaticContacts = 0;
  uint32_t movingMovingContacts = 0;
  for (std::size_t i = 0; i < m_impl->bodies.size(); ++i) {
    const auto& bodyA = m_impl->bodies[i];
    if (!bodyA.alive) {
      continue;
    }
    for (std::size_t j = i + 1; j < m_impl->bodies.size(); ++j) {
      const auto& bodyB = m_impl->bodies[j];
      if (!bodyB.alive || !m_impl->physicsSystem.WereBodiesInContact(bodyA.id, bodyB.id)) {
        continue;
      }
      const bool aMoving = bodyA.motion != PhysicsBodyMotion::Static;
      const bool bMoving = bodyB.motion != PhysicsBodyMotion::Static;
      if (aMoving && bMoving) {
        ++movingMovingContacts;
      } else if (aMoving || bMoving) {
        ++movingStaticContacts;
      }
    }
  }
  constexpr uint32_t joltUpdates = 1;
  if (activeRigidBodies > 0) {
    ++m_impl->updateStatsFrames;
    m_impl->updateStatsTotalMs += updateMs;
    m_impl->updateStatsMaxMs = (std::max)(m_impl->updateStatsMaxMs, updateMs);
    m_impl->updateStatsTotalJoltUpdates += joltUpdates;
    m_impl->updateStatsMaxJoltUpdates = (std::max)(m_impl->updateStatsMaxJoltUpdates, joltUpdates);
    m_impl->updateStatsMaxMovingStaticContacts =
        (std::max)(m_impl->updateStatsMaxMovingStaticContacts, movingStaticContacts);
    m_impl->updateStatsMaxMovingMovingContacts =
        (std::max)(m_impl->updateStatsMaxMovingMovingContacts, movingMovingContacts);
    if (m_impl->updateStatsFrames >= 120) {
      const double avgMs = m_impl->updateStatsTotalMs / static_cast<double>(m_impl->updateStatsFrames);
      const double avgJoltUpdates =
          static_cast<double>(m_impl->updateStatsTotalJoltUpdates) / static_cast<double>(m_impl->updateStatsFrames);
      T8_LOG_DEBUG("[JoltPhysics] update avg=%.3fms max=%.3fms avgJoltUpdates=%.2f maxJoltUpdates=%u stepDt=%.4f activeRigid=%u bodies=%u contactsMS=%u contactsMM=%u maxContactsMS=%u maxContactsMM=%u mode=%s speed=%.3fx concurrency=%d",
                   avgMs,
                   m_impl->updateStatsMaxMs,
                   avgJoltUpdates,
                   m_impl->updateStatsMaxJoltUpdates,
                   simulationDeltaSeconds,
                   activeRigidBodies,
                   m_impl->physicsSystem.GetNumBodies(),
                   movingStaticContacts,
                   movingMovingContacts,
                   m_impl->updateStatsMaxMovingStaticContacts,
                   m_impl->updateStatsMaxMovingMovingContacts,
                   m_impl->useFixedSimulationDelta ? "fixed" : "delta",
                   m_impl->simulationSpeedScale,
                   m_impl->jobSystem.GetMaxConcurrency());
      m_impl->updateStatsFrames = 0;
      m_impl->updateStatsTotalJoltUpdates = 0;
      m_impl->updateStatsMaxJoltUpdates = 0;
      m_impl->updateStatsMaxMovingStaticContacts = 0;
      m_impl->updateStatsMaxMovingMovingContacts = 0;
      m_impl->updateStatsTotalMs = 0.0;
      m_impl->updateStatsMaxMs = 0.0;
    }
  }
}

void JoltPhysicsSystem::SetSimulationSpeedScale(float scale) {
  if (!m_impl) {
    return;
  }
  if (!std::isfinite(scale)) {
    T8_LOG_ERROR("Ignoring invalid Jolt simulation speed scale %.3f", scale);
    return;
  }

  m_impl->simulationSpeedScale = (std::min)((std::max)(scale, 0.0f), kMaxSimulationSpeedScale);
}

float JoltPhysicsSystem::GetSimulationSpeedScale() const {
  return m_impl ? m_impl->simulationSpeedScale : 1.0f;
}

void JoltPhysicsSystem::SetUseFixedSimulationDelta(bool useFixedDelta) {
  if (!m_impl) {
    return;
  }
  m_impl->useFixedSimulationDelta = useFixedDelta;
}

bool JoltPhysicsSystem::GetUseFixedSimulationDelta() const {
  return m_impl ? m_impl->useFixedSimulationDelta : false;
}

PhysicsBodyHandle JoltPhysicsSystem::CreateBody(const PhysicsBodyDesc& desc) {
  return CreateBodyInternal(desc, nullptr);
}

PhysicsBodyHandle JoltPhysicsSystem::CreateBodyInternal(const PhysicsBodyDesc& desc, const void* collisionGroup) {
  if (!m_initialized || !m_impl) {
    return {};
  }

  if (!IsUsablePhysicsBodyDesc(desc)) {
    T8_LOG_ERROR("Jolt body creation skipped for entity %u ('%s'): invalid or oversized transform/shape",
                 desc.entityId,
                 desc.debugName.c_str());
    return {};
  }

  JPH::RefConst<JPH::Shape> shape = CreateJoltShape(desc.shape);
  if (shape == nullptr) {
    T8_LOG_ERROR("Jolt body creation failed for entity %u: shape is null", desc.entityId);
    return {};
  }

  JPH::BodyCreationSettings settings(
      shape,
      ToJoltPosition(desc.worldTransform),
      ToJoltRotation(desc.worldTransform),
      ToJoltMotion(desc.motion),
      ToJoltObjectLayer(desc.motion));
  settings.mUserData = MakeBodyUserData(desc.entityId, desc.boneIndex);
  settings.mFriction = desc.friction;
  settings.mRestitution = desc.restitution;
  settings.mIsSensor = desc.sensor;
  if (collisionGroup) {
    settings.mCollisionGroup = *static_cast<const JPH::CollisionGroup*>(collisionGroup);
  }
  if (desc.motion != PhysicsBodyMotion::Static && desc.mass > 0.0f) {
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = desc.mass;
  }

  JPH::BodyInterface& bodyInterface = m_impl->physicsSystem.GetBodyInterface();
  const JPH::EActivation activation = desc.motion == PhysicsBodyMotion::Static
      ? JPH::EActivation::DontActivate
      : JPH::EActivation::Activate;
  JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(settings, activation);
  if (bodyId.IsInvalid()) {
    T8_LOG_ERROR("Jolt body creation failed for entity %u", desc.entityId);
    return {};
  }

  Impl::BodySlot slot;
  slot.id = bodyId;
  slot.entityId = desc.entityId;
  slot.boneIndex = desc.boneIndex;
  slot.shape = desc.shape;
  slot.debugName = desc.debugName;
  slot.motion = desc.motion;
  slot.alive = true;

  PhysicsBodyHandle handle;
  handle.value = static_cast<uint32_t>(m_impl->bodies.size());
  m_impl->bodies.push_back(slot);
  return handle;
}

PhysicsBodyHandle JoltPhysicsSystem::CreateTriangleMeshBody(const PhysicsTriangleMeshBodyDesc& desc,
                                                            PhysicsCookStats* outStats) {
  PhysicsCookStats stats = outStats ? *outStats : PhysicsCookStats{};
  const auto totalStart = std::chrono::steady_clock::now();
  if (!m_initialized || !m_impl) {
    if (outStats) {
      *outStats = stats;
    }
    return {};
  }

  JPH::RefConst<JPH::Shape> shape = CreateOrLoadTriangleMeshShape(desc.mesh, stats);
  if (shape == nullptr) {
    T8_LOG_ERROR("Jolt triangle mesh body creation failed for entity %u: shape is null", desc.entityId);
    if (outStats) {
      *outStats = stats;
    }
    return {};
  }

  JPH::BodyCreationSettings settings(
      shape,
      ToJoltPosition(desc.worldTransform),
      ToJoltRotation(desc.worldTransform),
      JPH::EMotionType::Static,
      Layers::NonMoving);
  settings.mUserData = MakeBodyUserData(desc.entityId, -1);
  settings.mFriction = desc.friction;
  settings.mRestitution = desc.restitution;
  settings.mIsSensor = desc.sensor;

  JPH::BodyInterface& bodyInterface = m_impl->physicsSystem.GetBodyInterface();
  JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
  if (bodyId.IsInvalid()) {
    T8_LOG_ERROR("Jolt triangle mesh body creation failed for entity %u", desc.entityId);
    if (outStats) {
      *outStats = stats;
    }
    return {};
  }

  Impl::BodySlot slot;
  slot.id = bodyId;
  slot.entityId = desc.entityId;
  slot.boneIndex = -1;
  slot.shape = PhysicsShapeDesc::TriangleMeshBounds(desc.mesh.localBounds.Extents());
  slot.debugName = desc.debugName;
  slot.debugVertices = std::make_shared<std::vector<XVECTOR3>>(desc.mesh.vertices);
  slot.debugLineIndices = BuildTriangleMeshDebugLineIndices(desc.mesh.indices, static_cast<uint32_t>(desc.mesh.vertices.size()));
  slot.motion = PhysicsBodyMotion::Static;
  slot.alive = true;

  PhysicsBodyHandle handle;
  handle.value = static_cast<uint32_t>(m_impl->bodies.size());
  m_impl->bodies.push_back(slot);

  const auto totalEnd = std::chrono::steady_clock::now();
  stats.totalMs = std::chrono::duration<double, std::milli>(totalEnd - totalStart).count();
  if (outStats) {
    *outStats = stats;
  }
  return handle;
}

PhysicsBodyHandle JoltPhysicsSystem::CreateBoxBodyFromBounds(uint32_t entityId,
                                                             const AABB& localBounds,
                                                             const XMATRIX44& worldFromLocal,
                                                             PhysicsBodyMotion motion) {
  if (!localBounds.IsValid()) {
    return {};
  }

  XVECTOR3 center = localBounds.Center();
  XVECTOR3 extents = localBounds.Extents();
  const float sx = (std::max)(Length3(worldFromLocal.m11, worldFromLocal.m12, worldFromLocal.m13), 0.001f);
  const float sy = (std::max)(Length3(worldFromLocal.m21, worldFromLocal.m22, worldFromLocal.m23), 0.001f);
  const float sz = (std::max)(Length3(worldFromLocal.m31, worldFromLocal.m32, worldFromLocal.m33), 0.001f);

  extents.x = (std::max)(0.001f, extents.x * sx);
  extents.y = (std::max)(0.001f, extents.y * sy);
  extents.z = (std::max)(0.001f, extents.z * sz);

  XMATRIX44 bodyTransform = NormalizeRotationPreserveTranslation(worldFromLocal);
  XVECTOR3 worldCenter = TransformPoint(center, worldFromLocal);
  bodyTransform.m41 = worldCenter.x;
  bodyTransform.m42 = worldCenter.y;
  bodyTransform.m43 = worldCenter.z;

  PhysicsBodyDesc desc;
  desc.entityId = entityId;
  desc.shape = PhysicsShapeDesc::Box(extents);
  desc.worldTransform = bodyTransform;
  desc.motion = motion;
  return CreateBody(desc);
}

bool JoltPhysicsSystem::DestroyBody(PhysicsBodyHandle handle) {
  if (!m_initialized || !m_impl) {
    return false;
  }

  Impl::BodySlot* slot = m_impl->Resolve(handle);
  if (!slot) {
    return false;
  }

  JPH::BodyInterface& bodyInterface = m_impl->physicsSystem.GetBodyInterface();
  if (bodyInterface.IsAdded(slot->id)) {
    bodyInterface.RemoveBody(slot->id);
  }
  bodyInterface.DestroyBody(slot->id);
  slot->alive = false;
  return true;
}

bool JoltPhysicsSystem::SetBodyMotion(PhysicsBodyHandle handle, PhysicsBodyMotion motion) {
  if (!m_initialized || !m_impl) {
    return false;
  }

  Impl::BodySlot* slot = m_impl->Resolve(handle);
  if (!slot) {
    return false;
  }

  JPH::BodyInterface& bodyInterface = m_impl->physicsSystem.GetBodyInterface();
  const JPH::ObjectLayer objectLayer = ToJoltObjectLayer(motion);
  if (bodyInterface.GetObjectLayer(slot->id) != objectLayer) {
    bodyInterface.SetObjectLayer(slot->id, objectLayer);
  }
  bodyInterface.SetMotionType(slot->id, ToJoltMotion(motion), JPH::EActivation::Activate);
  if (motion == PhysicsBodyMotion::Dynamic) {
    bodyInterface.SetMotionQuality(slot->id, JPH::EMotionQuality::Discrete);
    bodyInterface.ActivateBody(slot->id);
  }
  slot->motion = motion;
  return true;
}

bool JoltPhysicsSystem::SetBodyVelocity(PhysicsBodyHandle handle, const XVECTOR3& linearVelocity, const XVECTOR3& angularVelocity) {
  if (!m_initialized || !m_impl) {
    return false;
  }

  Impl::BodySlot* slot = m_impl->Resolve(handle);
  if (!slot || slot->motion == PhysicsBodyMotion::Static) {
    return false;
  }

  JPH::BodyInterface& bodyInterface = m_impl->physicsSystem.GetBodyInterface();
  bodyInterface.SetLinearAndAngularVelocity(
      slot->id,
      JPH::Vec3(linearVelocity.x, linearVelocity.y, linearVelocity.z),
      JPH::Vec3(angularVelocity.x, angularVelocity.y, angularVelocity.z));
  bodyInterface.ActivateBody(slot->id);
  return true;
}

bool JoltPhysicsSystem::DriveBodyKinematic(PhysicsBodyHandle handle, const XMATRIX44& worldTransform, float deltaSeconds) {
  if (!m_initialized || !m_impl) {
    return false;
  }
  (void)deltaSeconds;

  if (!IsUsablePhysicsTransform(worldTransform)) {
    T8_LOG_ERROR("Jolt kinematic drive skipped for handle %u: invalid or oversized target transform",
                 handle.value);
    return false;
  }

  Impl::BodySlot* slot = m_impl->Resolve(handle);
  if (!slot) {
    return false;
  }

  if (slot->motion != PhysicsBodyMotion::Kinematic) {
    SetBodyMotion(handle, PhysicsBodyMotion::Kinematic);
  }

  JPH::BodyInterface& bodyInterface = m_impl->physicsSystem.GetBodyInterface();
  bodyInterface.SetPositionAndRotation(slot->id, ToJoltPosition(worldTransform), ToJoltRotation(worldTransform), JPH::EActivation::Activate);
  return true;
}

bool JoltPhysicsSystem::SetBodyTransform(PhysicsBodyHandle handle, const XMATRIX44& worldTransform, bool activate) {
  if (!m_initialized || !m_impl) {
    return false;
  }

  Impl::BodySlot* slot = m_impl->Resolve(handle);
  if (!slot) {
    return false;
  }

  if (!IsUsablePhysicsTransform(worldTransform)) {
    T8_LOG_ERROR("Jolt body transform skipped for handle %u: invalid or oversized target transform",
                 handle.value);
    return false;
  }

  m_impl->physicsSystem.GetBodyInterface().SetPositionAndRotation(
      slot->id,
      ToJoltPosition(worldTransform),
      ToJoltRotation(worldTransform),
      activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
  return true;
}

bool JoltPhysicsSystem::GetBodyState(PhysicsBodyHandle handle, PhysicsBodyState& outState) const {
  if (!m_initialized || !m_impl) {
    return false;
  }

  const Impl::BodySlot* slot = m_impl->Resolve(handle);
  if (!slot) {
    return false;
  }

  const JPH::BodyInterface& bodyInterface = m_impl->physicsSystem.GetBodyInterface();
  JPH::RVec3 position;
  JPH::Quat rotation;
  bodyInterface.GetPositionAndRotation(slot->id, position, rotation);
  JPH::Vec3 linearVelocity;
  JPH::Vec3 angularVelocity;
  bodyInterface.GetLinearAndAngularVelocity(slot->id, linearVelocity, angularVelocity);

  outState.handle = handle;
  outState.entityId = slot->entityId;
  outState.boneIndex = slot->boneIndex;
  outState.worldTransform = FromJoltTransform(position, rotation);
  outState.linearVelocity = XVECTOR3(linearVelocity.GetX(), linearVelocity.GetY(), linearVelocity.GetZ(), 0.0f);
  outState.angularVelocity = XVECTOR3(angularVelocity.GetX(), angularVelocity.GetY(), angularVelocity.GetZ(), 0.0f);
  outState.motion = FromJoltMotion(bodyInterface.GetMotionType(slot->id));
  return true;
}

PhysicsRagdollHandle JoltPhysicsSystem::CreateRagdoll(const PhysicsRagdollDesc& desc, PhysicsBodyMotion initialMotion) {
  if (!m_initialized || !m_impl || desc.bones.empty()) {
    return {};
  }

  Impl::RagdollSlot slot;
  slot.entityId = desc.entityId;
  slot.alive = true;
  slot.bodies.reserve(desc.bones.size());
  const JPH::CollisionGroup::GroupID groupId =
      static_cast<JPH::CollisionGroup::GroupID>(0x10000u + static_cast<uint32_t>(m_impl->ragdolls.size()));
  slot.groupFilter = new JPH::GroupFilterTable(static_cast<JPH::uint>(desc.bones.size()));

  for (std::size_t boneIndex = 0; boneIndex < desc.bones.size(); ++boneIndex) {
    const PhysicsRagdollBoneDesc& bone = desc.bones[boneIndex];
    PhysicsBodyDesc bodyDesc = bone.body;
    bodyDesc.entityId = desc.entityId;
    bodyDesc.motion = initialMotion;
    JPH::CollisionGroup collisionGroup(
        slot.groupFilter,
        groupId,
        static_cast<JPH::CollisionGroup::SubGroupID>(boneIndex));
    PhysicsBodyHandle body = CreateBodyInternal(bodyDesc, &collisionGroup);
    if (body.IsValid()) {
      slot.bodies.push_back(body);
    }
  }

  if (slot.bodies.empty()) {
    return {};
  }

  JPH::BodyInterface& bodyInterface = m_impl->physicsSystem.GetBodyInterface();
  for (const PhysicsRagdollBoneDesc& bone : desc.bones) {
    if (bone.parentBoneIndex < 0) {
      continue;
    }

    PhysicsBodyHandle childHandle;
    PhysicsBodyHandle parentHandle;
    const XMATRIX44* childTransform = nullptr;
    const XMATRIX44* parentTransform = nullptr;
    for (std::size_t i = 0; i < slot.bodies.size(); ++i) {
      const Impl::BodySlot* bodySlot = m_impl->Resolve(slot.bodies[i]);
      if (!bodySlot) {
        continue;
      }

      const PhysicsRagdollBoneDesc* ragdollBone = nullptr;
      for (const PhysicsRagdollBoneDesc& candidate : desc.bones) {
        if (candidate.body.boneIndex == bodySlot->boneIndex) {
          ragdollBone = &candidate;
          break;
        }
      }
      if (!ragdollBone) {
        continue;
      }

      if (bodySlot->boneIndex == bone.body.boneIndex) {
        childHandle = slot.bodies[i];
        childTransform = &ragdollBone->body.worldTransform;
      } else if (bodySlot->boneIndex == bone.parentBoneIndex) {
        parentHandle = slot.bodies[i];
        parentTransform = &ragdollBone->body.worldTransform;
      }
    }

    Impl::BodySlot* childSlot = m_impl->Resolve(childHandle);
    Impl::BodySlot* parentSlot = m_impl->Resolve(parentHandle);
    if (!childSlot || !parentSlot || !childTransform || !parentTransform) {
      continue;
    }

    JPH::TwoBodyConstraint* constraint = nullptr;
    const JPH::RVec3 jointPosition(bone.jointWorldPosition.x, bone.jointWorldPosition.y, bone.jointWorldPosition.z);
    const JPH::Vec3 parentPlaneAxis = ToJoltAxis(bone.parentJointPlaneAxis, ToJoltAxisX(*parentTransform));
    const JPH::Vec3 parentTwistAxis = ToJoltAxis(bone.parentJointTwistAxis, ToJoltAxisY(*parentTransform));
    const JPH::Vec3 childPlaneAxis = ToJoltAxis(bone.childJointPlaneAxis, ToJoltAxisX(*childTransform));
    const JPH::Vec3 childTwistAxis = ToJoltAxis(bone.childJointTwistAxis, ToJoltAxisY(*childTransform));
    if (bone.jointType == PhysicsRagdollJointType::Fixed) {
      JPH::FixedConstraintSettings settings;
      settings.mSpace = JPH::EConstraintSpace::WorldSpace;
      settings.mAutoDetectPoint = false;
      settings.mPoint1 = jointPosition;
      settings.mPoint2 = jointPosition;
      settings.mAxisX1 = parentPlaneAxis;
      settings.mAxisY1 = parentTwistAxis;
      settings.mAxisX2 = childPlaneAxis;
      settings.mAxisY2 = childTwistAxis;
      constraint = bodyInterface.CreateConstraint(&settings, parentSlot->id, childSlot->id);
    } else {
      JPH::SwingTwistConstraintSettings settings;
      settings.mSpace = JPH::EConstraintSpace::WorldSpace;
      settings.mPosition1 = jointPosition;
      settings.mPosition2 = settings.mPosition1;
      settings.mTwistAxis1 = parentTwistAxis;
      settings.mTwistAxis2 = childTwistAxis;
      settings.mPlaneAxis1 = parentPlaneAxis;
      settings.mPlaneAxis2 = childPlaneAxis;
      settings.mSwingType = JPH::ESwingType::Cone;
      settings.mNormalHalfConeAngle = bone.swingLimitRadians;
      settings.mPlaneHalfConeAngle = bone.swingLimitRadians;
      settings.mTwistMinAngle = -bone.twistLimitRadians;
      settings.mTwistMaxAngle = bone.twistLimitRadians;
      constraint = bodyInterface.CreateConstraint(&settings, parentSlot->id, childSlot->id);
    }
    if (constraint) {
      m_impl->physicsSystem.AddConstraint(constraint);
      bodyInterface.ActivateConstraint(constraint);
      slot.constraints.push_back(constraint);
    }
  }

  PhysicsRagdollHandle handle;
  handle.value = static_cast<uint32_t>(m_impl->ragdolls.size());
  m_impl->ragdolls.push_back(slot);
  T8_LOG_DEBUG("Jolt ragdoll created: bodies=%zu constraints=%zu",
               m_impl->ragdolls.back().bodies.size(),
               m_impl->ragdolls.back().constraints.size());
  return handle;
}

bool JoltPhysicsSystem::DestroyRagdoll(PhysicsRagdollHandle handle) {
  if (!m_initialized || !m_impl) {
    return false;
  }

  Impl::RagdollSlot* slot = m_impl->Resolve(handle);
  if (!slot) {
    return false;
  }

  for (JPH::Ref<JPH::Constraint>& constraint : slot->constraints) {
    if (constraint.GetPtr() != nullptr) {
      m_impl->physicsSystem.RemoveConstraint(constraint.GetPtr());
    }
  }
  slot->constraints.clear();

  for (PhysicsBodyHandle body : slot->bodies) {
    DestroyBody(body);
  }
  slot->bodies.clear();
  slot->alive = false;
  return true;
}

bool JoltPhysicsSystem::SetRagdollMotion(PhysicsRagdollHandle handle, PhysicsBodyMotion motion) {
  if (!m_initialized || !m_impl) {
    return false;
  }

  Impl::RagdollSlot* slot = m_impl->Resolve(handle);
  if (!slot) {
    return false;
  }

  bool changed = false;
  for (PhysicsBodyHandle body : slot->bodies) {
    changed = SetBodyMotion(body, motion) || changed;
  }
  return changed;
}

bool JoltPhysicsSystem::SetRagdollVelocity(PhysicsRagdollHandle handle, const XVECTOR3& linearVelocity, const XVECTOR3& angularVelocity) {
  if (!m_initialized || !m_impl) {
    return false;
  }

  Impl::RagdollSlot* slot = m_impl->Resolve(handle);
  if (!slot) {
    return false;
  }

  bool updated = false;
  for (PhysicsBodyHandle body : slot->bodies) {
    updated = SetBodyVelocity(body, linearVelocity, angularVelocity) || updated;
  }
  return updated;
}

bool JoltPhysicsSystem::DriveRagdollFromPose(PhysicsRagdollHandle handle, const PhysicsRagdollDesc& pose, float deltaSeconds) {
  if (!m_initialized || !m_impl) {
    return false;
  }

  Impl::RagdollSlot* slot = m_impl->Resolve(handle);
  if (!slot) {
    return false;
  }

  const std::size_t count = (std::min)(slot->bodies.size(), pose.bones.size());
  bool updated = false;
  for (std::size_t i = 0; i < count; ++i) {
    updated = DriveBodyKinematic(slot->bodies[i], pose.bones[i].body.worldTransform, deltaSeconds) || updated;
  }
  return updated;
}

bool JoltPhysicsSystem::GetRagdollState(PhysicsRagdollHandle handle, std::vector<PhysicsBodyState>& outStates) const {
  if (!m_initialized || !m_impl) {
    return false;
  }

  const Impl::RagdollSlot* slot = m_impl->Resolve(handle);
  if (!slot) {
    return false;
  }

  outStates.clear();
  outStates.reserve(slot->bodies.size());
  for (PhysicsBodyHandle body : slot->bodies) {
    PhysicsBodyState state;
    if (GetBodyState(body, state)) {
      outStates.push_back(state);
    }
  }
  return !outStates.empty();
}

bool JoltPhysicsSystem::GetDebugBodies(std::vector<PhysicsDebugBody>& outBodies) const {
  outBodies.clear();
  if (!m_initialized || !m_impl) {
    return false;
  }

  outBodies.reserve(m_impl->bodies.size());
  for (uint32_t i = 0; i < m_impl->bodies.size(); ++i) {
    const Impl::BodySlot& slot = m_impl->bodies[i];
    if (!slot.alive) {
      continue;
    }

    PhysicsBodyHandle handle;
    handle.value = i;
    PhysicsDebugBody debugBody;
    if (GetBodyState(handle, debugBody.state)) {
      debugBody.shape = slot.shape;
      debugBody.debugName = slot.debugName;
      debugBody.debugVertices = slot.debugVertices;
      debugBody.debugLineIndices = slot.debugLineIndices;
      outBodies.push_back(debugBody);
    }
  }

  return !outBodies.empty();
}

bool JoltPhysicsSystem::IsAvailable() const {
  return true;
}

bool JoltPhysicsSystem::IsInitialized() const {
  return m_initialized;
}

} // namespace t850

#else

namespace t850 {

JoltPhysicsSystem::JoltPhysicsSystem()
    : m_impl(nullptr),
      m_initialized(false) {}

JoltPhysicsSystem::~JoltPhysicsSystem() = default;

bool JoltPhysicsSystem::Initialize() {
  return false;
}

void JoltPhysicsSystem::Shutdown() {
  m_initialized = false;
}

void JoltPhysicsSystem::Update(float) {}

void JoltPhysicsSystem::SetSimulationSpeedScale(float) {}

float JoltPhysicsSystem::GetSimulationSpeedScale() const {
  return 1.0f;
}

void JoltPhysicsSystem::SetUseFixedSimulationDelta(bool) {}

bool JoltPhysicsSystem::GetUseFixedSimulationDelta() const {
  return false;
}

PhysicsBodyHandle JoltPhysicsSystem::CreateBody(const PhysicsBodyDesc&) {
  return {};
}

PhysicsBodyHandle JoltPhysicsSystem::CreateTriangleMeshBody(const PhysicsTriangleMeshBodyDesc&, PhysicsCookStats*) {
  return {};
}

PhysicsBodyHandle JoltPhysicsSystem::CreateBoxBodyFromBounds(uint32_t, const AABB&, const XMATRIX44&, PhysicsBodyMotion) {
  return {};
}

bool JoltPhysicsSystem::DestroyBody(PhysicsBodyHandle) {
  return false;
}

bool JoltPhysicsSystem::SetBodyMotion(PhysicsBodyHandle, PhysicsBodyMotion) {
  return false;
}

bool JoltPhysicsSystem::SetBodyVelocity(PhysicsBodyHandle, const XVECTOR3&, const XVECTOR3&) {
  return false;
}

bool JoltPhysicsSystem::DriveBodyKinematic(PhysicsBodyHandle, const XMATRIX44&, float) {
  return false;
}

bool JoltPhysicsSystem::SetBodyTransform(PhysicsBodyHandle, const XMATRIX44&, bool) {
  return false;
}

bool JoltPhysicsSystem::GetBodyState(PhysicsBodyHandle, PhysicsBodyState&) const {
  return false;
}

PhysicsRagdollHandle JoltPhysicsSystem::CreateRagdoll(const PhysicsRagdollDesc&, PhysicsBodyMotion) {
  return {};
}

bool JoltPhysicsSystem::DestroyRagdoll(PhysicsRagdollHandle) {
  return false;
}

bool JoltPhysicsSystem::SetRagdollMotion(PhysicsRagdollHandle, PhysicsBodyMotion) {
  return false;
}

bool JoltPhysicsSystem::SetRagdollVelocity(PhysicsRagdollHandle, const XVECTOR3&, const XVECTOR3&) {
  return false;
}

bool JoltPhysicsSystem::DriveRagdollFromPose(PhysicsRagdollHandle, const PhysicsRagdollDesc&, float) {
  return false;
}

bool JoltPhysicsSystem::GetRagdollState(PhysicsRagdollHandle, std::vector<PhysicsBodyState>&) const {
  return false;
}

bool JoltPhysicsSystem::GetDebugBodies(std::vector<PhysicsDebugBody>& outBodies) const {
  outBodies.clear();
  return false;
}

bool JoltPhysicsSystem::IsAvailable() const {
  return false;
}

bool JoltPhysicsSystem::IsInitialized() const {
  return false;
}

} // namespace t850

#endif
