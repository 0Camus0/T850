#include <pch.h>

#include <game/GameObjectRegistry.h>

#include <game/Component.h>
#include <game/StateMachine.h>

#include <algorithm>
#include <unordered_set>

namespace t850::game {

GameObject::GameObject() = default;
GameObject::~GameObject() = default;
GameObject::GameObject(GameObject&&) noexcept = default;
GameObject& GameObject::operator=(GameObject&&) noexcept = default;

GameObject* GameObjectRegistry::Create(
    const t850::scene::SceneGameEntityDesc& desc, GameObjectLinks links) {
  objects_.emplace_back();
  GameObject& object = objects_.back();
  object.runtimeId = nextRuntimeId_++;
  object.sceneId = desc.id;
  object.name = desc.name;
  object.kind = desc.kind;
  object.team = desc.team;
  object.links = std::move(links);

  runtimeIndex_[object.runtimeId] = &object;
  if (!object.sceneId.empty()) sceneIdIndex_[object.sceneId] = &object;
  return &object;
}

void GameObjectRegistry::RequestDestroy(RuntimeGameObjectId id) {
  if (id == kInvalidRuntimeGameObjectId || !Get(id)) return;
  if (std::find(pendingDestroy_.begin(), pendingDestroy_.end(), id) == pendingDestroy_.end()) {
    pendingDestroy_.push_back(id);
  }
}

void GameObjectRegistry::ApplyDeferredDestroys() {
  if (pendingDestroy_.empty()) return;

  const std::unordered_set<RuntimeGameObjectId> destroyed(
      pendingDestroy_.begin(), pendingDestroy_.end());
  objects_.remove_if([&](const GameObject& object) {
    return destroyed.contains(object.runtimeId);
  });
  pendingDestroy_.clear();
  RebuildIndexes();
}

GameObject* GameObjectRegistry::Get(RuntimeGameObjectId id) {
  const auto found = runtimeIndex_.find(id);
  return found == runtimeIndex_.end() ? nullptr : found->second;
}

const GameObject* GameObjectRegistry::Get(RuntimeGameObjectId id) const {
  const auto found = runtimeIndex_.find(id);
  return found == runtimeIndex_.end() ? nullptr : found->second;
}

GameObject* GameObjectRegistry::FindBySceneId(std::string_view sceneId) {
  const auto found = sceneIdIndex_.find(std::string(sceneId));
  return found == sceneIdIndex_.end() ? nullptr : found->second;
}

const GameObject* GameObjectRegistry::FindBySceneId(std::string_view sceneId) const {
  const auto found = sceneIdIndex_.find(std::string(sceneId));
  return found == sceneIdIndex_.end() ? nullptr : found->second;
}

std::list<GameObject>& GameObjectRegistry::Objects() {
  return objects_;
}

const std::list<GameObject>& GameObjectRegistry::Objects() const {
  return objects_;
}

std::size_t GameObjectRegistry::Count() const {
  return objects_.size();
}

void GameObjectRegistry::Clear() {
  pendingDestroy_.clear();
  objects_.clear();
  runtimeIndex_.clear();
  sceneIdIndex_.clear();
  nextRuntimeId_ = 1;
}

void GameObjectRegistry::RebuildIndexes() {
  runtimeIndex_.clear();
  sceneIdIndex_.clear();
  runtimeIndex_.reserve(objects_.size());
  sceneIdIndex_.reserve(objects_.size());
  for (GameObject& object : objects_) {
    runtimeIndex_[object.runtimeId] = &object;
    if (!object.sceneId.empty()) {
      sceneIdIndex_[object.sceneId] = &object;
    }
  }
}

} // namespace t850::game