#pragma once

#include <functional>
#include <string>
#include <vector>

#include <physics/RagdollEditorTool.h>

namespace t850::ragdoll_editor {

struct GuiState {
  int selectedBody = -1;
  int selectedJoint = -1;
  int selectedUnassignedBone = -1;
  int selectedAffectedBone = -1;
  int selectionMode = static_cast<int>(SelectionMode::Bodies);
  int toolMode = static_cast<int>(ToolMode::Select);
  bool showWireframe = false;
  bool physicsDebug = true;
  bool skeletonDebug = true;
  bool skeletonEditMode = false;
  bool fixedSimulationDelta = false;
  int simulationSpeedIndex = 3;
  int undoCount = 0;
  bool dirty = false;
  std::string undoLabel = "Undo";
};

struct GuiCallbacks {
  std::function<void()> loadEdits;
  std::function<void()> saveEdits;
  std::function<void()> resetAllBodies;
  std::function<void()> clearAllBodies;
  std::function<bool(int)> deleteBody;
  std::function<bool(int, PhysicsShapeType)> createBodyForBone;
  std::function<void(int)> bodyChanged;
  std::function<void()> startSimulation;
  std::function<void()> resetPhysicsAnimation;
  std::function<void()> togglePhysicsDebug;
  std::function<void()> toggleSkeletonDebug;
  std::function<void()> toggleSkeletonEditMode;
  std::function<void(int)> setSimulationSpeedIndex;
  std::function<void(bool)> setFixedSimulationDelta;
  std::function<void()> undo;
};

struct GuiContext {
  RagdollEditorTool* tool = nullptr;
  GuiState* state = nullptr;
  std::vector<std::string> skeletonBoneNames;
  float modelRadius = 1.0f;
  std::string status;
  GuiCallbacks callbacks;
};

void DrawRagdollEditorGui(GuiContext& context);

} // namespace t850::ragdoll_editor
