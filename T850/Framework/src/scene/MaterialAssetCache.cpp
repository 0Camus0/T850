#include <pch.h>

#include <scene/MaterialAsset.h>
#include <utils/Log.h>

#include <cstring>

namespace t850 {

  MaterialAssetCache& MaterialAssetCache::Get() {
    static MaterialAssetCache instance;
    return instance;
  }

  // FNV-1a 64-bit. Stable, fast, fine for ~hundreds of materials.
  static uint64_t FNV1a64(const void* data, std::size_t bytes, uint64_t seed = 0xcbf29ce484222325ull) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint64_t h = seed;
    for (std::size_t i = 0; i < bytes; ++i) {
      h ^= p[i];
      h *= 0x100000001b3ull;
    }
    return h;
  }

  uint64_t MaterialAssetCache::ComputeHash(const MaterialAsset& mat) {
    // Hash the binary parameter block, the feature key bits, and the
    // texture IDs. We deliberately do NOT hash the Texture* pointer
    // because pointer values are run-dependent; texture IDs come from
    // the path-keyed TextureCache and are stable across instances of
    // the same texture.
    uint64_t h = FNV1a64(&mat.params, sizeof(mat.params));
    h = FNV1a64(&mat.featureKey.bits, sizeof(mat.featureKey.bits), h);
    h = FNV1a64(&mat.textureIds[0], sizeof(mat.textureIds), h);
    return h;
  }

  bool MaterialAssetCache::ContentEqual(const MaterialAsset& a, const MaterialAsset& b) {
    if (a.featureKey.bits != b.featureKey.bits) return false;
    if (std::memcmp(&a.params, &b.params, sizeof(MaterialParams)) != 0) return false;
    if (std::memcmp(a.textureIds, b.textureIds, sizeof(a.textureIds)) != 0) return false;
    return true;
  }

  MaterialAsset* MaterialAssetCache::Acquire(const MaterialAsset& prototype, bool* outCreated) {
    uint64_t hash = ComputeHash(prototype);
    std::lock_guard<std::mutex> lk(m_mutex);
    auto& bucket = m_byHash[hash];
    for (auto& candidate : bucket) {
      if (ContentEqual(*candidate, prototype)) {
        candidate->refCount++;
        if (outCreated) *outCreated = false;
        return candidate.get();
      }
    }
    auto fresh = std::make_unique<MaterialAsset>(prototype);
    fresh->contentHash = hash;
    fresh->refCount = 1;
    MaterialAsset* raw = fresh.get();
    bucket.push_back(std::move(fresh));
    m_total++;
    if (outCreated) *outCreated = true;
    return raw;
  }

  MaterialAsset* MaterialAssetCache::AcquireTextureVariant(const MaterialAsset& base,
                                                           MatTexSlot slot,
                                                           Texture* texture,
                                                           int textureId,
                                                           bool* outCreated) {
    MaterialAsset prototype = base;
    prototype.contentHash = 0;
    prototype.refCount = 0;
    const int slotIndex = static_cast<int>(slot);
    prototype.textures[slotIndex] = texture;
    prototype.textureIds[slotIndex] = textureId;
    return Acquire(prototype, outCreated);
  }

  void MaterialAssetCache::Release(MaterialAsset* asset) {
    if (!asset) return;
    std::lock_guard<std::mutex> lk(m_mutex);
    if (asset->refCount == 0) {
      T8_LOG_ERROR("[MaterialAssetCache] Release on '%s' with refCount==0", asset->name.c_str());
      return;
    }
    asset->refCount--;
    if (asset->refCount > 0) return;

    auto it = m_byHash.find(asset->contentHash);
    if (it == m_byHash.end()) return;
    auto& bucket = it->second;
    for (auto bIt = bucket.begin(); bIt != bucket.end(); ++bIt) {
      if (bIt->get() == asset) {
        bucket.erase(bIt);
        m_total--;
        break;
      }
    }
    if (bucket.empty()) m_byHash.erase(it);
  }

  std::size_t MaterialAssetCache::Size() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_total;
  }

  void MaterialAssetCache::DumpToLog() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    T8_LOG_INFO("[MaterialAssetCache] %zu unique material(s), %zu hash bucket(s):",
                m_total, m_byHash.size());
    std::size_t collisions = 0;
    for (const auto& kv : m_byHash) {
      if (kv.second.size() > 1) collisions += kv.second.size() - 1;
      for (const auto& asset : kv.second) {
        T8_LOG_INFO("  '%s' refs=%u hash=0x%016llX features=0x%016llX",
                    asset->name.c_str(), asset->refCount,
                    static_cast<unsigned long long>(asset->contentHash),
                    static_cast<unsigned long long>(asset->featureKey.bits));
      }
    }
    if (collisions) {
      T8_LOG_INFO("[MaterialAssetCache] (%zu hash collision(s) — distinct content sharing same hash)", collisions);
    }
  }

  void MaterialAssetCache::Clear() {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_byHash.clear();
    m_total = 0;
  }

}
