#ifndef T800_GLTF_SKIN_MAP_H
#define T800_GLTF_SKIN_MAP_H

#include <utils/gltf/GLTFTypes.h>

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace t850 {
namespace gltf {

struct SkinJointMap {
  std::vector<int> jointNodes;
  std::unordered_map<int, int> nodeToJoint;
  std::vector<std::vector<int>> localToGlobal;
};

namespace detail {

inline void GatherJointNodesDFS(const Document& doc,
                                int nodeIdx,
                                const std::unordered_set<int>& jointNodeSet,
                                std::unordered_set<int>& visited,
                                std::vector<int>& outJointNodes) {
  if (nodeIdx < 0 || nodeIdx >= static_cast<int>(doc.nodes.size())) return;
  if (jointNodeSet.find(nodeIdx) != jointNodeSet.end()
      && visited.insert(nodeIdx).second) {
    outJointNodes.push_back(nodeIdx);
  }
  for (int child : doc.nodes[nodeIdx].children) {
    GatherJointNodesDFS(doc, child, jointNodeSet, visited, outJointNodes);
  }
}

} // namespace detail

inline SkinJointMap BuildSkinJointMap(const Document& doc) {
  SkinJointMap map;
  map.localToGlobal.resize(doc.skins.size());
  if (doc.skins.empty()) return map;

  std::unordered_set<int> jointNodeSet;
  for (const Skin& skin : doc.skins) {
    for (int nodeIdx : skin.joints) {
      if (nodeIdx >= 0 && nodeIdx < static_cast<int>(doc.nodes.size()))
        jointNodeSet.insert(nodeIdx);
    }
  }

  std::vector<int> rootNodes;
  int sceneIdx = doc.scene.value_or(doc.scenes.empty() ? -1 : 0);
  if (sceneIdx >= 0 && sceneIdx < static_cast<int>(doc.scenes.size())) {
    rootNodes = doc.scenes[sceneIdx].nodes;
  } else {
    std::unordered_set<int> childNodes;
    for (const Node& node : doc.nodes) {
      for (int child : node.children) childNodes.insert(child);
    }
    for (int i = 0; i < static_cast<int>(doc.nodes.size()); ++i) {
      if (childNodes.find(i) == childNodes.end()) rootNodes.push_back(i);
    }
  }

  std::unordered_set<int> visited;
  for (int root : rootNodes) {
    detail::GatherJointNodesDFS(doc, root, jointNodeSet, visited, map.jointNodes);
  }

  for (const Skin& skin : doc.skins) {
    for (int nodeIdx : skin.joints) {
      if (nodeIdx >= 0 && nodeIdx < static_cast<int>(doc.nodes.size())
          && visited.insert(nodeIdx).second) {
        map.jointNodes.push_back(nodeIdx);
      }
    }
  }

  for (int i = 0; i < static_cast<int>(map.jointNodes.size()); ++i) {
    map.nodeToJoint[map.jointNodes[i]] = i;
  }

  for (int si = 0; si < static_cast<int>(doc.skins.size()); ++si) {
    const Skin& skin = doc.skins[si];
    map.localToGlobal[si].assign(skin.joints.size(), -1);
    for (int li = 0; li < static_cast<int>(skin.joints.size()); ++li) {
      auto it = map.nodeToJoint.find(skin.joints[li]);
      if (it != map.nodeToJoint.end())
        map.localToGlobal[si][li] = it->second;
    }
  }

  return map;
}

} // namespace gltf
} // namespace t850

#endif // T800_GLTF_SKIN_MAP_H
