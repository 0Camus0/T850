#pragma once

#include <game/GameObject.h>
#include <scene/EditorSceneFile.h>

#include <cstddef>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace t850::game {

class GameObjectRegistry {
public:
  GameObject* Create(const t850::scene::SceneGameEntityDesc& desc, GameObjectLinks links);
  void RequestDestroy(RuntimeGameObjectId id);
  void ApplyDeferredDestroys();

  GameObject* Get(RuntimeGameObjectId id);
  const GameObject* Get(RuntimeGameObjectId id) const;
  GameObject* FindBySceneId(std::string_view sceneId);
  const GameObject* FindBySceneId(std::string_view sceneId) const;
  std::list<GameObject>& Objects();
  const std::list<GameObject>& Objects() const;
  std::size_t Count() const;
  void Clear();

private:
  void RebuildIndexes();

  RuntimeGameObjectId nextRuntimeId_ = 1;
  std::list<GameObject> objects_;
  std::unordered_map<RuntimeGameObjectId, GameObject*> runtimeIndex_;
  std::unordered_map<std::string, GameObject*> sceneIdIndex_;
  std::vector<RuntimeGameObjectId> pendingDestroy_;
};

} // namespace t850::game