#include <imgui/RagdollEditorGui.h>

#include <algorithm>
#include <cmath>

#include <imgui.h>
#include <ImGuizmo.h>

namespace t850::ragdoll_editor {

namespace {

void MatrixToComponents(const XMATRIX44& matrix, float translation[3], float rotationDeg[3], float scale[3]) {
  XMATRIX44 copy = matrix;
  ImGuizmo::DecomposeMatrixToComponents(&copy.m[0][0], translation, rotationDeg, scale);
}

XMATRIX44 MatrixFromComponents(const float translation[3], const float rotationDeg[3], const float scale[3]) {
  XMATRIX44 matrix;
  ImGuizmo::RecomposeMatrixFromComponents(translation, rotationDeg, scale, &matrix.m[0][0]);
  return matrix;
}

std::string BoneLabel(const std::vector<std::string>& names, int boneIndex) {
  std::string label = std::to_string(boneIndex);
  if (boneIndex >= 0 && boneIndex < static_cast<int>(names.size())) {
    label += ": " + names[static_cast<std::size_t>(boneIndex)];
  }
  return label;
}

template <typename T>
void EraseAt(std::vector<T>& values, int index) {
  if (index >= 0 && index < static_cast<int>(values.size())) {
    values.erase(values.begin() + index);
  }
}

} // namespace

void DrawRagdollEditorGui(GuiContext& context) {
  if (!context.tool || !context.state) {
    ImGui::TextDisabled("Ragdoll editor tool is unavailable.");
    return;
  }

  RagdollEditorTool& tool = *context.tool;
  GuiState& state = *context.state;
  tool.EnsureState();

  PhysicsRagdollAuthoringDesc& authoring = tool.Authoring();
  auto& bones = authoring.binding.referencePose.bones;

  auto markDirty = [&]() {
    state.dirty = true;
  };
  auto markBodyChanged = [&](int bodyIndex) {
    markDirty();
    if (context.callbacks.bodyChanged) {
      context.callbacks.bodyChanged(bodyIndex);
    }
  };

  if (ImGui::CollapsingHeader("Runtime controls", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Bodies: %zu", bones.size());
    if (ImGui::Button(state.physicsDebug ? "Physics Debug: On" : "Physics Debug: Off")) {
      state.physicsDebug = !state.physicsDebug;
      if (context.callbacks.togglePhysicsDebug) context.callbacks.togglePhysicsDebug();
    }
    ImGui::SameLine();
    if (ImGui::Button(state.skeletonDebug ? "Skeleton Debug: On" : "Skeleton Debug: Off")) {
      state.skeletonDebug = !state.skeletonDebug;
      if (context.callbacks.toggleSkeletonDebug) context.callbacks.toggleSkeletonDebug();
    }
    if (ImGui::Button("Start Simulation") && context.callbacks.startSimulation) {
      context.callbacks.startSimulation();
    }
    ImGui::SetNextItemWidth(160.0f);
    int simulationSpeedIndex = ClampSimulationSpeedIndex(state.simulationSpeedIndex);
    if (ImGui::SliderInt("Simulation speed",
                         &simulationSpeedIndex,
                         0,
                         static_cast<int>(kSimulationSpeedScales.size()) - 1,
                         SimulationSpeedLabelForIndex(simulationSpeedIndex))) {
      state.simulationSpeedIndex = ClampSimulationSpeedIndex(simulationSpeedIndex);
      if (context.callbacks.setSimulationSpeedIndex) {
        context.callbacks.setSimulationSpeedIndex(state.simulationSpeedIndex);
      }
    }
    if (ImGui::Checkbox("Fixed 1/60 physics delta", &state.fixedSimulationDelta)) {
      if (context.callbacks.setFixedSimulationDelta) {
        context.callbacks.setFixedSimulationDelta(state.fixedSimulationDelta);
      }
    }
    ImGui::TextDisabled(state.fixedSimulationDelta
                            ? "Physics advances one 1/60s input per frame."
                            : "Physics uses the measured frame delta.");
    if (ImGui::Button(state.skeletonEditMode ? "Exit Edit Mode" : "Enter Edit Mode")) {
      state.skeletonEditMode = !state.skeletonEditMode;
      if (context.callbacks.toggleSkeletonEditMode) context.callbacks.toggleSkeletonEditMode();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Physics/Animation") && context.callbacks.resetPhysicsAnimation) {
      context.callbacks.resetPhysicsAnimation();
    }
    ImGui::SameLine();
    if (state.undoCount <= 0) ImGui::BeginDisabled();
    if (ImGui::Button("Undo") && context.callbacks.undo) {
      context.callbacks.undo();
    }
    if (state.undoCount <= 0) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+Z (%d/10)", state.undoCount);
    if (!state.undoLabel.empty()) {
      ImGui::TextDisabled("%s", state.undoLabel.c_str());
    }
  }

  if (ImGui::CollapsingHeader("Skeleton edit mode", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::TextWrapped(state.skeletonEditMode
        ? "Bind-pose edit mode is active. Bone clicks edit the animation skeleton; shape handle clicks edit the physical ragdoll body."
        : "Enter mode to pause animation and move the model to bind pose.");
    if (!state.skeletonEditMode) {
      if (ImGui::Button("Enter Skeleton Edit Mode")) {
        state.skeletonEditMode = true;
        if (context.callbacks.toggleSkeletonEditMode) context.callbacks.toggleSkeletonEditMode();
      }
    }
  }

  if (!ImGui::CollapsingHeader("Ragdoll Bodies", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }

  if (ImGui::CollapsingHeader("Viewport tools", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Selection:");
    ImGui::SameLine();
    ImGui::RadioButton("Bodies", &state.selectionMode, static_cast<int>(SelectionMode::Bodies));
    ImGui::SameLine();
    ImGui::RadioButton("Joints", &state.selectionMode, static_cast<int>(SelectionMode::Joints));
    ImGui::SameLine();
    ImGui::RadioButton("Bones", &state.selectionMode, static_cast<int>(SelectionMode::Bones));

    ImGui::Text("Tool:");
    ImGui::SameLine();
    ImGui::RadioButton("Select", &state.toolMode, static_cast<int>(ToolMode::Select));
    ImGui::SameLine();
    ImGui::RadioButton(state.selectionMode == static_cast<int>(SelectionMode::Joints) ? "Edit Joint" : "Edit Body",
                       &state.toolMode,
                       static_cast<int>(ToolMode::Edit));
    ImGui::SameLine();
    ImGui::RadioButton("Move", &state.toolMode, static_cast<int>(ToolMode::Move));
    ImGui::SameLine();
    ImGui::RadioButton("Rotate", &state.toolMode, static_cast<int>(ToolMode::Rotate));
    ImGui::Checkbox("Show subtle wireframe", &state.showWireframe);
  }

  if (ImGui::CollapsingHeader("Body actions", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::Button("Load Ragdoll Edits") && context.callbacks.loadEdits) {
      context.callbacks.loadEdits();
      tool.EnsureState();
    }
    ImGui::SameLine();
    if (ImGui::Button(state.dirty ? "Save Ragdoll Edits *" : "Save Ragdoll Edits") && context.callbacks.saveEdits) {
      context.callbacks.saveEdits();
      state.dirty = false;
    }

    if (ImGui::Button("Reset All Bodies") && context.callbacks.resetAllBodies) {
      context.callbacks.resetAllBodies();
      tool.EnsureState();
      state.dirty = true;
    }
    ImGui::SameLine();
    const bool canDeleteBody = state.selectedBody >= 0 && state.selectedBody < static_cast<int>(bones.size());
    if (!canDeleteBody) ImGui::BeginDisabled();
    if (ImGui::Button("Delete Selected Body") && context.callbacks.deleteBody) {
      if (context.callbacks.deleteBody(state.selectedBody)) {
        state.selectedBody = (std::min)(state.selectedBody, static_cast<int>(bones.size()) - 1);
        state.selectedJoint = -1;
        state.dirty = true;
      }
    }
    if (!canDeleteBody) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Clear All Bodies")) {
      authoring.binding.referencePose.bones.clear();
      authoring.binding.bodyFromBone.clear();
      authoring.binding.jointFromBone.clear();
      authoring.binding.parentJointTwistFromBody.clear();
      authoring.binding.parentJointPlaneFromBody.clear();
      authoring.binding.childJointTwistFromBody.clear();
      authoring.binding.childJointPlaneFromBody.clear();
      authoring.binding.controlledBoneIndices.clear();
      authoring.binding.controlledBodyFromBone.clear();
      authoring.parentBodyIndices.clear();
      authoring.jointParentBodyIndices.clear();
      authoring.frozenBodies.clear();
      authoring.frozenJoints.clear();
      authoring.contactJoints.clear();
      state.selectedBody = -1;
      state.selectedJoint = -1;
      state.dirty = true;
      if (context.callbacks.clearAllBodies) {
        context.callbacks.clearAllBodies();
      }
    }

    const bool canCreateBody = state.selectedUnassignedBone >= 0;
    if (!canCreateBody) ImGui::BeginDisabled();
    if (ImGui::Button("Create Capsule") && context.callbacks.createBodyForBone) {
      if (context.callbacks.createBodyForBone(state.selectedUnassignedBone, PhysicsShapeType::Capsule)) {
        state.dirty = true;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Create Box") && context.callbacks.createBodyForBone) {
      if (context.callbacks.createBodyForBone(state.selectedUnassignedBone, PhysicsShapeType::Box)) {
        state.dirty = true;
      }
    }
    if (!canCreateBody) ImGui::EndDisabled();
  }

  if (bones.empty()) {
    ImGui::TextWrapped("No editable ragdoll bodies are attached to this model.");
    return;
  }
  if (state.selectedBody < 0 || state.selectedBody >= static_cast<int>(bones.size())) {
    state.selectedBody = 0;
  }

  std::vector<std::string> bodyLabels;
  bodyLabels.reserve(bones.size());
  for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
    const auto& body = bones[static_cast<std::size_t>(i)].body;
    bodyLabels.push_back(std::to_string(i) + ": " + ShapeTypeName(body.shape.type) +
                         " bone " + std::to_string(body.boneIndex) + " " + body.debugName);
  }
  std::vector<const char*> bodyItems;
  bodyItems.reserve(bodyLabels.size());
  for (const std::string& label : bodyLabels) {
    bodyItems.push_back(label.c_str());
  }
  ImGui::SetNextItemWidth(360.0f);
  ImGui::Combo("Body", &state.selectedBody, bodyItems.data(), static_cast<int>(bodyItems.size()));

  const int bodyIndex = state.selectedBody;
  auto& bone = bones[static_cast<std::size_t>(bodyIndex)];
  auto& shape = bone.body.shape;
  auto& localBodyFromBone = authoring.binding.bodyFromBone[static_cast<std::size_t>(bodyIndex)];
  bool previewNeedsRebuild = false;

  bool bodyFrozen = tool.IsBodyFrozen(bodyIndex);
  if (ImGui::Checkbox("Freeze Body", &bodyFrozen)) {
    tool.SetBodyFrozen(bodyIndex, bodyFrozen);
    markDirty();
  }

  char nameBuffer[128] = {};
  std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", bone.body.debugName.c_str());
  if (ImGui::InputText("Body Name", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
    bone.body.debugName = nameBuffer[0] ? nameBuffer : ("body_" + std::to_string(bodyIndex));
    markDirty();
  }
  ImGui::Text("Bone: %d %s", bone.body.boneIndex, BoneLabel(context.skeletonBoneNames, bone.body.boneIndex).c_str());
  ImGui::Text("Shape: %s", ShapeTypeName(shape.type));

  if (ImGui::CollapsingHeader("Selected body", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Body name: %s", bone.body.debugName.c_str());
    ImGui::Text("Bone: %d %s", bone.body.boneIndex, BoneLabel(context.skeletonBoneNames, bone.body.boneIndex).c_str());
    ImGui::Text("Shape: %s", ShapeTypeName(shape.type));
    if (ImGui::Button(bodyFrozen ? "Unfreeze Body" : "Freeze Body")) {
      bodyFrozen = !bodyFrozen;
      tool.SetBodyFrozen(bodyIndex, bodyFrozen);
      markDirty();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(bodyFrozen ? "Frozen" : "Editable");
  }

  if (ImGui::CollapsingHeader("Body relationships", ImGuiTreeNodeFlags_DefaultOpen)) {
    auto buildBodyOptions = [&]() {
      std::vector<std::string> labels;
      labels.emplace_back("<none>");
      for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
        if (i == bodyIndex) continue;
        labels.push_back(std::to_string(i) + ": " + bones[static_cast<std::size_t>(i)].body.debugName);
      }
      return labels;
    };
    auto bodyOptionToIndex = [&](int option) {
      if (option <= 0) return -1;
      int current = 1;
      for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
        if (i == bodyIndex) continue;
        if (current == option) return i;
        ++current;
      }
      return -1;
    };
    auto indexToBodyOption = [&](int index) {
      if (index < 0) return 0;
      int current = 1;
      for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
        if (i == bodyIndex) continue;
        if (i == index) return current;
        ++current;
      }
      return 0;
    };

    std::vector<std::string> bodyOptions = buildBodyOptions();
    std::vector<const char*> bodyOptionItems;
    for (const std::string& label : bodyOptions) bodyOptionItems.push_back(label.c_str());

    int parentOption = indexToBodyOption(authoring.parentBodyIndices[static_cast<std::size_t>(bodyIndex)]);
    if (ImGui::Combo("Logical Parent", &parentOption, bodyOptionItems.data(), static_cast<int>(bodyOptionItems.size()))) {
      authoring.parentBodyIndices[static_cast<std::size_t>(bodyIndex)] = bodyOptionToIndex(parentOption);
      markDirty();
    }

    std::vector<std::string> jointOptions;
    jointOptions.emplace_back("Inherit Logical Parent");
    jointOptions.emplace_back("Disabled");
    for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
      if (i == bodyIndex) continue;
      jointOptions.push_back(std::to_string(i) + ": " + bones[static_cast<std::size_t>(i)].body.debugName);
    }
    std::vector<const char*> jointItems;
    for (const std::string& label : jointOptions) jointItems.push_back(label.c_str());
    auto jointParentToOption = [&](int parent) {
      if (parent == kPhysicsRagdollJointDisabled) return 1;
      if (parent == kPhysicsRagdollJointInheritParent) return 0;
      int current = 2;
      for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
        if (i == bodyIndex) continue;
        if (i == parent) return current;
        ++current;
      }
      return 0;
    };
    auto jointOptionToParent = [&](int option) {
      if (option == 1) return kPhysicsRagdollJointDisabled;
      if (option <= 0) return kPhysicsRagdollJointInheritParent;
      int current = 2;
      for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
        if (i == bodyIndex) continue;
        if (current == option) return i;
        ++current;
      }
      return kPhysicsRagdollJointInheritParent;
    };
    int jointOption = jointParentToOption(authoring.jointParentBodyIndices[static_cast<std::size_t>(bodyIndex)]);
    if (ImGui::Combo("Joint Parent", &jointOption, jointItems.data(), static_cast<int>(jointItems.size()))) {
      authoring.jointParentBodyIndices[static_cast<std::size_t>(bodyIndex)] = jointOptionToParent(jointOption);
      previewNeedsRebuild = true;
      markDirty();
    }
    bool contactJoint = authoring.contactJoints[static_cast<std::size_t>(bodyIndex)] != 0;
    if (ImGui::Checkbox("Contact Joint", &contactJoint)) {
      authoring.contactJoints[static_cast<std::size_t>(bodyIndex)] = contactJoint ? 1 : 0;
      previewNeedsRebuild = true;
      markDirty();
    }
  }

  if (ImGui::CollapsingHeader("Affected bone assignment", ImGuiTreeNodeFlags_DefaultOpen)) {
    auto& controlledBones = authoring.binding.controlledBoneIndices[static_cast<std::size_t>(bodyIndex)];
    auto containsBone = [](const std::vector<int>& list, int boneIndex) {
      return std::find(list.begin(), list.end(), boneIndex) != list.end();
    };
    std::vector<int> unassignedBones;
    for (int boneIndex = 0; boneIndex < static_cast<int>(context.skeletonBoneNames.size()); ++boneIndex) {
      if (tool.FindBodyControllingBone(boneIndex) < 0) unassignedBones.push_back(boneIndex);
    }
    const float listHeight = (std::max)(120.0f, ImGui::GetTextLineHeightWithSpacing() * 7.0f);
    ImGui::Columns(3, "shared_ragdoll_bone_assignment", false);
    ImGui::Text("Unassigned (%zu)", unassignedBones.size());
    ImGui::BeginChild("unassigned_bones", ImVec2(0.0f, listHeight), true);
    for (int boneIndex : unassignedBones) {
      const std::string label = BoneLabel(context.skeletonBoneNames, boneIndex);
      if (ImGui::Selectable(label.c_str(), state.selectedUnassignedBone == boneIndex)) {
        state.selectedUnassignedBone = boneIndex;
        state.selectedAffectedBone = -1;
      }
    }
    ImGui::EndChild();

    ImGui::NextColumn();
    const bool canAdd = state.selectedUnassignedBone >= 0;
    if (!canAdd) ImGui::BeginDisabled();
    if (ImGui::Button("Add ->")) {
      controlledBones.push_back(state.selectedUnassignedBone);
      authoring.binding.controlledBodyFromBone[static_cast<std::size_t>(bodyIndex)].push_back(
          authoring.binding.bodyFromBone[static_cast<std::size_t>(bodyIndex)]);
      state.selectedAffectedBone = state.selectedUnassignedBone;
      state.selectedUnassignedBone = -1;
      previewNeedsRebuild = true;
      markDirty();
    }
    if (!canAdd) ImGui::EndDisabled();
    const bool canRemove = containsBone(controlledBones, state.selectedAffectedBone);
    if (!canRemove) ImGui::BeginDisabled();
    if (ImGui::Button("<- Remove")) {
      const int removeBone = state.selectedAffectedBone;
      for (int i = 0; i < static_cast<int>(controlledBones.size()); ++i) {
        if (controlledBones[static_cast<std::size_t>(i)] == removeBone) {
          controlledBones.erase(controlledBones.begin() + i);
          auto& offsets = authoring.binding.controlledBodyFromBone[static_cast<std::size_t>(bodyIndex)];
          if (i < static_cast<int>(offsets.size())) offsets.erase(offsets.begin() + i);
          break;
        }
      }
      state.selectedUnassignedBone = removeBone;
      state.selectedAffectedBone = -1;
      previewNeedsRebuild = true;
      markDirty();
    }
    if (!canRemove) ImGui::EndDisabled();

    ImGui::NextColumn();
    ImGui::Text("Affected (%zu)", controlledBones.size());
    ImGui::BeginChild("affected_bones", ImVec2(0.0f, listHeight), true);
    for (int boneIndex : controlledBones) {
      const std::string label = BoneLabel(context.skeletonBoneNames, boneIndex);
      if (ImGui::Selectable(label.c_str(), state.selectedAffectedBone == boneIndex)) {
        state.selectedAffectedBone = boneIndex;
        state.selectedUnassignedBone = -1;
      }
    }
    ImGui::EndChild();
    ImGui::Columns(1);
  }

  if (ImGui::CollapsingHeader("Shape transform", ImGuiTreeNodeFlags_DefaultOpen)) {
  if (bodyFrozen) ImGui::BeginDisabled();
  int shapeType = shape.type == PhysicsShapeType::Box ? 1 : 0;
  if (ImGui::RadioButton("Capsule", shapeType == 0)) {
    shape.type = PhysicsShapeType::Capsule;
    previewNeedsRebuild = true;
    markDirty();
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("Box", shapeType == 1)) {
    shape.type = PhysicsShapeType::Box;
    previewNeedsRebuild = true;
    markDirty();
  }

  const float dragStep = (std::max)(0.0005f, context.modelRadius * 0.0005f);
  if (shape.type == PhysicsShapeType::Capsule) {
    float radius = shape.radius;
    float totalLength = (shape.radius + shape.halfHeight) * 2.0f;
    if (ImGui::DragFloat("Capsule Radius", &radius, dragStep, 0.0001f, (std::max)(0.001f, context.modelRadius), "%.4f")) {
      shape.radius = (std::max)(0.0001f, radius);
      shape.halfHeight = (std::max)(0.0001f, totalLength * 0.5f - shape.radius);
      previewNeedsRebuild = true;
      markDirty();
    }
    if (ImGui::DragFloat("Capsule Total Length", &totalLength, dragStep, 0.0003f, (std::max)(0.003f, context.modelRadius * 4.0f), "%.4f")) {
      totalLength = (std::max)(shape.radius * 2.0f + 0.0002f, totalLength);
      shape.halfHeight = (std::max)(0.0001f, totalLength * 0.5f - shape.radius);
      previewNeedsRebuild = true;
      markDirty();
    }
  } else if (shape.type == PhysicsShapeType::Box) {
    float halfExtents[3] = {shape.halfExtents.x, shape.halfExtents.y, shape.halfExtents.z};
    if (ImGui::DragFloat3("Box Half Extents", halfExtents, dragStep, 0.0001f, (std::max)(0.001f, context.modelRadius * 2.0f), "%.4f")) {
      shape.halfExtents = XVECTOR3(
          (std::max)(0.0001f, halfExtents[0]),
          (std::max)(0.0001f, halfExtents[1]),
          (std::max)(0.0001f, halfExtents[2]),
          0.0f);
      previewNeedsRebuild = true;
      markDirty();
    }
  }

  float translation[3], rotationDeg[3], scale[3];
  MatrixToComponents(authoring.binding.bodyFromBone[static_cast<std::size_t>(bodyIndex)], translation, rotationDeg, scale);
  bool transformChanged = false;
  transformChanged |= ImGui::DragFloat3("Local Translate", translation, dragStep, 0.0f, 0.0f, "%.4f");
  transformChanged |= ImGui::DragFloat3("Local Rotate XYZ", rotationDeg, 0.25f, -180.0f, 180.0f, "%.2f deg");
  if (transformChanged) {
    authoring.binding.bodyFromBone[static_cast<std::size_t>(bodyIndex)] = MatrixFromComponents(translation, rotationDeg, scale);
    previewNeedsRebuild = true;
    markDirty();
  }

  float swingDeg = bone.swingLimitRadians * (180.0f / 3.14159265358979323846f);
  float twistDeg = bone.twistLimitRadians * (180.0f / 3.14159265358979323846f);
  if (ImGui::DragFloat("Swing Limit", &swingDeg, 0.5f, 0.0f, 180.0f, "%.1f deg")) {
    bone.swingLimitRadians = std::clamp(swingDeg, 0.0f, 180.0f) * (3.14159265358979323846f / 180.0f);
    previewNeedsRebuild = true;
    markDirty();
  }
  if (ImGui::DragFloat("Twist Limit", &twistDeg, 0.5f, 0.0f, 180.0f, "%.1f deg")) {
    bone.twistLimitRadians = std::clamp(twistDeg, 0.0f, 180.0f) * (3.14159265358979323846f / 180.0f);
    previewNeedsRebuild = true;
    markDirty();
  }
  if (bodyFrozen) ImGui::EndDisabled();
  }

  if (previewNeedsRebuild) {
    if (context.callbacks.bodyChanged) {
      context.callbacks.bodyChanged(bodyIndex);
    }
  }

  if (ImGui::CollapsingHeader("Ragdoll Joints", ImGuiTreeNodeFlags_DefaultOpen)) {
    std::vector<int> jointBodies;
    std::vector<std::string> jointLabels;
    for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
      const int parent = tool.EffectiveJointParent(i);
      if (parent >= 0 && parent < static_cast<int>(bones.size()) && parent != i) {
        jointBodies.push_back(i);
        jointLabels.push_back(std::to_string(i) + " " + bones[static_cast<std::size_t>(i)].body.debugName +
                              " <- " + std::to_string(parent) + " " + bones[static_cast<std::size_t>(parent)].body.debugName);
      }
    }
    if (jointBodies.empty()) {
      ImGui::TextDisabled("No joints are assigned yet. Select a body and use Body Relationships to create one.");
    } else {
      if (state.selectedJoint < 0) state.selectedJoint = jointBodies.front();
      int jointOption = 0;
      for (int i = 0; i < static_cast<int>(jointBodies.size()); ++i) {
        if (jointBodies[static_cast<std::size_t>(i)] == state.selectedJoint) jointOption = i;
      }
      std::vector<const char*> jointItems;
      for (const std::string& label : jointLabels) jointItems.push_back(label.c_str());
      if (ImGui::Combo("Joint", &jointOption, jointItems.data(), static_cast<int>(jointItems.size()))) {
        state.selectedJoint = jointBodies[static_cast<std::size_t>(jointOption)];
        state.selectedBody = state.selectedJoint;
      }
      const int child = state.selectedJoint;
      auto& childBone = bones[static_cast<std::size_t>(child)];
      bool jointFrozen = tool.IsJointFrozen(child);
      if (ImGui::Checkbox("Freeze Joint", &jointFrozen)) {
        tool.SetJointFrozen(child, jointFrozen);
        markDirty();
      }
      if (jointFrozen) ImGui::BeginDisabled();
      int jointType = childBone.jointType == PhysicsRagdollJointType::Fixed ? 1 : 0;
      const char* jointTypes[] = {"Swing/Twist", "Fixed"};
      if (ImGui::Combo("Joint Type", &jointType, jointTypes, 2)) {
        childBone.jointType = jointType == 1 ? PhysicsRagdollJointType::Fixed : PhysicsRagdollJointType::SwingTwist;
        markDirty();
        if (context.callbacks.bodyChanged) context.callbacks.bodyChanged(child);
      }
      ImGui::Text("Current: %s", JointTypeName(childBone.jointType));
      if (jointFrozen) ImGui::EndDisabled();
    }
  }

  if (!context.status.empty()) {
    ImGui::TextWrapped("%s", context.status.c_str());
  }
}

} // namespace t850::ragdoll_editor
