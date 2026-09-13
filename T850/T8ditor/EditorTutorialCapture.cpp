#include "EditorApp.h"
#include "EditorWorld.h"
#include "EditorImGui.h"

#include <utils/Log.h>
#include <game/GameIds.h>
#include <scene/RenderSkinnedMesh.h>
#include <terrain/HeightmapMesh.h>

#include <stdexcept>
#include <set>
#include <filesystem>

namespace t8ditor {

void EditorApp::ConfigureTutorialCapture(std::string step) {
  m_tutorialStep = std::move(step);
  ImGuiSetTransientCapture(true);
}

void EditorApp::PositionTutorialPanel() {
  if (m_tutorialStep.empty()) return;
  const auto* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
  const float width = m_tutorialStep.starts_with("navigation") || m_tutorialStep == "physics" ? 660.0f : 510.0f;
  ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x - width - 10, viewport->WorkPos.y + 44), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(width, viewport->WorkSize.y - 54), ImGuiCond_Always);
}

void EditorApp::PrepareTutorialCapture() {
  if (m_tutorialStep.empty()) return;
  ++m_tutorialFrame;
  auto& world = GetEditorWorld();
  if (m_tutorialPrepared) {
    if (m_tutorialStep == "model-animation" && m_tutorialFrame == 16) {
      auto edited = *world.objects[0].heightmap;
      edited.placements[0].visual.reset();
      m_terrainEditObject = world.objects[0].name;
      m_pendingTerrainSettings = edited;
      UpdateTerrainEditing();
      bool valid = !world.objects[0].heightmap->placements[0].visual && world.objects[0].heightmap->placements.size() == 2;
      world.undoStack.Undo();
      valid &= world.objects[0].heightmap->placements[0].visual.has_value();
      world.undoStack.Redo();
      valid &= !world.objects[0].heightmap->placements[0].visual;
      world.undoStack.Undo();
      auto* restored = dynamic_cast<t850::HeightmapMesh*>(world.objects[0].litInst.pBase);
      valid &= restored && restored->PlacementSkinnedMesh("model-square") && restored->PlacementObstacles().size() == 2;
      if (!valid) {
        T8_LOG_ERROR("[PlacementVisualTest] Clear Model undo/redo lost visual or occupancy");
        m_terrainSelfTestResult = 1;
      } else T8_LOG_INFO("[PlacementVisualTest] PASS Clear Model undo/redo and stable occupancy");
    }
    if (m_tutorialStep.starts_with("model-") && m_tutorialFrame == 20) {
      auto* terrain = world.objects.empty() ? nullptr : dynamic_cast<t850::HeightmapMesh*>(world.objects[0].litInst.pBase);
      auto* first = terrain ? terrain->PlacementSkinnedMesh("model-square") : nullptr;
      auto* second = terrain ? terrain->PlacementSkinnedMesh("model-rectangle") : nullptr;
      if (!first || !second || first == second || (m_tutorialStep != "model-assign" &&
          m_tutorialStep != "model-parts" && m_tutorialStep != "model-play" && (first->GetAnimLocalTime() <= 0 || second->GetAnimLocalTime() <= 0))) {
        T8_LOG_ERROR("[PlacementVisualTest] Model instances or host animation updates were lost");
        m_terrainSelfTestResult = 1;
      } else {
        T8_LOG_INFO("[PlacementVisualTest] PASS independent instances and editor animation/reload lifecycle");
      }
      if (m_tutorialStep == "model-play" && (!m_playSceneLoaded || !m_playScene)) m_terrainSelfTestResult = 1;
      if (m_tutorialStep == "model-play" && m_playSceneLoaded && m_playScene) {
        auto* runtimeTerrain = dynamic_cast<t850::HeightmapMesh*>(m_playScene->Meshes[0].pBase);
        auto* runtimeVisual = runtimeTerrain ? runtimeTerrain->PlacementSkinnedMesh("model-square") : nullptr;
        if (!runtimeVisual || runtimeVisual->GetAnimLocalTime() <= 0) {
          T8_LOG_ERROR("[PlacementVisualTest] Runtime visual did not animate");
          m_terrainSelfTestResult = 1;
        } else T8_LOG_INFO("[PlacementVisualTest] PASS hosted Play model and animation");
      }
    }
    if (m_tutorialStep == "stopped" && m_tutorialFrame == 15 && m_playSceneOpen) {
      ClosePlayScene(true);
      world.selectedIdx = 0;
      world.selectionType = 0;
      m_panels.showTerrainEditor = true;
    }
    if (m_tutorialStep == "reloaded" && m_tutorialFrame == 10) {
      world.selectedIdx = 0;
      world.selectionType = 0;
      m_panels.showTerrainEditor = true;
      if (world.objects.empty() || !world.objects[0].heightmap || world.objects[0].heightmap->placements.size() != 5)
        m_terrainSelfTestResult = 1;
    }
    return;
  }
  if (!world.hasLoadedSceneFile || world.objects.empty() || m_tutorialFrame < 4) return;
  if (m_tutorialStep == "wireframe-depth-on" || m_tutorialStep == "wireframe-depth-off" ||
      m_tutorialStep == "wireframe-depth-far-on" || m_tutorialStep == "wireframe-depth-far-off") {
    m_tutorialPrepared = true;
    m_panels.showHierarchy = m_panels.showInspector = m_panels.showRendering = false;
    m_panels.showConsole = m_panels.showTimeline = m_panels.showGameValidation = false;
    m_panels.showRegions = m_panels.showNavMeshAuthoring = m_panels.showTerrainEditor = false;
    m_panels.showGameOverlays = m_panels.showWireframe = m_panels.showSkybox = false;
    m_panels.showSelectionWireframe = m_tutorialStep.ends_with("-on");
    m_editorNavMeshVisible = m_editorNavMeshShowSourcePreview = m_editorShowPhysics = false;
    m_showPlacementGrid = m_placementMode = m_terrainBrushEnabled = false;
    world.selectedIdx = 0;
    world.selectionType = 0;
    world.activeCameraIdx = -1;
    world.multiSelect.clear();
    world.multiEntitySelect.clear();
    m_gizmo.SetMode(GizmoMode::Select);
    m_camera.SetTarget(XVECTOR3(0.0f, 0.0f, 0.0f));
    m_camera.SetOrbitState(0.0f, 1.3f, m_tutorialStep.starts_with("wireframe-depth-far") ? 1500.0f : 12.0f);
    m_sceneProps.ToogleDOF = 0;
    T8_LOG_INFO("[WireframeDepthTest] Selected rear plane; wireframe=%d", m_panels.showSelectionWireframe);
    return;
  }
  try {
    m_tutorialPrepared = true;
    const std::set<std::string> steps = {"file-menu", "save-menu", "empty-scene", "view-menu", "grid", "flat-import", "image-import", "flat-terrain",
      "heightmap-terrain", "transform", "raise", "flatten", "paint-material", "footprint", "placement-placed",
      "placement-overlap", "placement-bounds", "placement-slope", "placement-list", "placement-removed", "unit-marker",
      "layout", "physics", "navigation-settings", "navigation-links", "navigation-result", "reloaded", "play", "stopped",
      "model-assign", "model-parts", "model-animation", "model-reloaded", "model-play"};
    if (!steps.contains(m_tutorialStep)) throw std::runtime_error("Unknown tutorial capture step");
    if (!world.objects[0].heightmap) throw std::runtime_error("Tutorial capture requires a terrain scene");
    m_panels.showHierarchy = false;
    m_panels.showInspector = false;
    m_panels.showRendering = false;
    m_panels.showConsole = false;
    m_panels.showTimeline = false;
    m_panels.showGameValidation = false;
    m_panels.showRegions = false;
    m_panels.showNavMeshAuthoring = false;
    m_panels.showSelectionWireframe = false;
    m_panels.showWireframe = false;
    m_panels.showTerrainEditor = true;
    m_editorNavMeshVisible = false;
    m_editorNavMeshShowSourcePreview = false;
    m_editorShowPhysics = false;
    for (auto& entity : world.physicsEntities) { entity.showWire = false; entity.visible = false; }
    world.selectedIdx = 0;
    world.selectionType = 0;
    world.activeCameraIdx = -1;
    world.multiSelect.clear();
    world.multiEntitySelect.clear();
    m_gizmo.SetMode(GizmoMode::Select);
    m_camera.SetTarget(XVECTOR3(-8.0f, 1.0f, -10.0f));
    m_camera.SetOrbitState(0.7f, 0.6f, 105.0f);
    auto terrain = *world.objects[0].heightmap;
    const auto examplePlacements = terrain.placements;
    terrain.placements.clear();
    terrain.image.clear();
    terrain.elevations.clear();
    terrain.samples_x = terrain.samples_z = 65;
    terrain.size_x = terrain.size_z = 64;
    terrain.height_offset = 0;
    terrain.height_scale = 12;
    terrain.placement_grid.enabled = true;
    terrain.placement_grid.cell_size = 2;
    auto check = [&](bool success) { if (!success) throw std::runtime_error(m_terrainStatus); };
    if (m_tutorialStep == "heightmap-terrain") {
      terrain.image = "Textures/Terrain/HeightmapExample.bmp";
      terrain.height_offset = -2;
    }
    check(t850::InitializeTerrainEditing(terrain, &m_terrainStatus));
    const bool sculpt = m_tutorialStep == "raise" || m_tutorialStep == "flatten" || m_tutorialStep == "placement-slope";
    m_terrainBrush.x = m_terrainBrush.z = 24;
    m_terrainBrush.radius = 10;
    m_terrainBrush.amount = 6;
    if (sculpt) {
      bool changed = false;
      check(t850::ApplyTerrainBrush(terrain, m_terrainBrush, &changed, &m_terrainStatus));
      if (m_tutorialStep == "flatten") {
        m_terrainBrush.mode = t850::TerrainBrushMode::Flatten;
        m_terrainBrush.hardness = 1;
        m_terrainBrush.amount = 1;
        m_terrainBrush.radius = 7;
        m_terrainBrushStrength = 10;
        check(t850::ApplyTerrainBrush(terrain, m_terrainBrush, &changed, &m_terrainStatus));
      }
    }
    if (m_tutorialStep == "paint-material") {
      terrain.materials.resize(2);
      terrain.materials[0].name = "Grass";
      terrain.materials[0].color = terrain.base_color;
      terrain.materials[1].name = "Path";
      terrain.materials[1].color = {0.7f, 0.35f, 0.15f};
      m_terrainBrush.mode = t850::TerrainBrushMode::Material;
      m_terrainBrush.material = 1;
      bool changed = false;
      check(t850::ApplyTerrainBrush(terrain, m_terrainBrush, &changed, &m_terrainStatus));
    }
    m_placementBrush.cell_x = 4;
    m_placementBrush.cell_z = 4;
    m_placementBrush.width = m_placementBrush.depth = 2;
    m_placementBrush.height = 3;
    m_placementMode = true;
    m_terrainBrushEnabled = false;
    m_showPlacementGrid = true;
    if (m_tutorialStep == "placement-placed" || m_tutorialStep == "placement-overlap" || m_tutorialStep == "placement-removed") {
      auto placement = m_placementBrush;
      placement.id = "tutorial-blue-building";
      placement.name = "Blue Base";
      check(t850::AddTerrainPlacement(terrain, placement, &m_terrainStatus));
      if (m_tutorialStep == "placement-removed") check(t850::RemoveTerrainPlacement(terrain, placement.id));
      if (m_tutorialStep == "placement-placed") m_placementMode = false;
    }
    if (m_tutorialStep == "placement-bounds") m_placementBrush.cell_x = m_placementBrush.cell_z = 31;
    if (m_tutorialStep == "placement-slope") m_placementBrush.cell_x = m_placementBrush.cell_z = 11;
    if (m_tutorialStep == "unit-marker") {
      m_placementBrush.kind = "unit";
      m_placementBrush.width = m_placementBrush.depth = 1;
      m_placementBrush.height = 1.5f;
      m_placementBrush.color = {0.8f, 0.85f, 0.9f};
      m_placementBrush.cell_x = 11;
      m_placementBrush.cell_z = 10;
    }
    const bool fullLayout = m_tutorialStep == "layout" || m_tutorialStep == "placement-list" ||
        m_tutorialStep == "physics" || m_tutorialStep.starts_with("navigation") || m_tutorialStep == "reloaded" ||
        m_tutorialStep == "play" || m_tutorialStep == "stopped";
    if (fullLayout) {
      terrain.placements = examplePlacements;
      m_placementMode = false;
    }
    if (m_tutorialStep == "footprint" || m_tutorialStep.starts_with("placement-") || m_tutorialStep == "unit-marker") {
      const auto position = world.objects[0].wireframe.Position();
      const float cell = terrain.placement_grid.cell_size;
      m_camera.SetTarget(XVECTOR3(position.x + (m_placementBrush.cell_x + m_placementBrush.width * 0.5f) * cell,
          1.0f, position.z + (m_placementBrush.cell_z + m_placementBrush.depth * 0.5f) * cell));
      m_camera.SetOrbitState(0.7f, 0.7f, m_tutorialStep == "placement-list" ? 65.0f : 28.0f);
    }
    if (sculpt || m_tutorialStep == "paint-material") {
      m_camera.SetTarget(XVECTOR3(-8.0f, 1.0f, -8.0f));
      m_camera.SetOrbitState(0.7f, 0.7f, 55.0f);
    }
    if (m_tutorialStep.starts_with("model-")) {
      terrain.placements.clear();
      for (int index = 0; index < 2; ++index) {
        auto placement = m_placementBrush;
        placement.id = index == 0 ? "model-square" : "model-rectangle";
        placement.name = index == 0 ? "Square Building" : "Rectangular Building";
        placement.cell_x = index == 0 ? 4 : 7;
        placement.depth = index == 0 ? 2 : 1;
        placement.visual = t850::scene::ScenePlacementVisualDesc{};
        placement.visual->mesh = "Models/PlacementBuilding.glb";
        if (m_tutorialStep != "model-assign") placement.visual->hidden_geometry = {0};
        if (m_tutorialStep != "model-assign" && m_tutorialStep != "model-parts") placement.visual->animation = index == 0 ? "Stand Work" : "Stand";
        check(t850::AddTerrainPlacement(terrain, placement, &m_terrainStatus));
      }
      m_placementMode = false;
      m_camera.SetTarget(XVECTOR3(-23.0f, 1.0f, -22.0f));
      m_camera.SetOrbitState(0.5f, 0.7f, 20.0f);
      m_sceneProps.Exposure = 3.0f;
      for (auto& light : world.lights) { light.intensity = 12.0f; light.visible = false; }
    }
    check(CommitTerrainEdit(0, terrain));
    if (m_tutorialStep.starts_with("model-")) {
      auto* rendered = dynamic_cast<t850::HeightmapMesh*>(world.objects[0].litInst.pBase);
      for (const auto& placement : terrain.placements) {
        auto* skinned = rendered->PlacementSkinnedMesh(placement.id);
        if (!skinned || !skinned->HasSkinData() || skinned->GetNumBones() != 92 || skinned->GetNumAnimSets() != 17)
          throw std::runtime_error("Reference model did not preserve its skeleton or clips");
        for (size_t geometryIndex = 0; geometryIndex < skinned->Info.size(); ++geometryIndex) {
          const auto attributes = skinned->xFile->XMeshDataBase[0]->Geometry[geometryIndex].VertexAttributes;
          const bool skin = (attributes & xF::xMeshGeometry::HAS_SKINWEIGHTS0) && (attributes & xF::xMeshGeometry::HAS_SKININDEXES0);
          for (const auto& subset : skinned->Info[geometryIndex].SubSets) {
            if (subset.key.has(t850::ShaderKey::HAS_SKINNING_TEX) != skin ||
              (subset.key.has(t850::ShaderKey::NORMAL_MAP) && !(attributes & xF::xMeshGeometry::HAS_TANGENT)))
              throw std::runtime_error("Material/skinning shader is incompatible with primitive attributes");
          }
        }
        t850::RenderMesh::AABB pose;
        XMATRIX44 fittedTransform;
        check(skinned->GetCurrentPoseLocalAABB(pose) && rendered->PlacementVisualTransform(placement.id, fittedTransform));
        const auto fitted = t850::AABB(pose.min, pose.max).Transformed(fittedTransform);
        const float cell = terrain.placement_grid.cell_size;
        if (fitted.vMin.x < placement.cell_x * cell - 0.001f || fitted.vMax.x > (placement.cell_x + placement.width) * cell + 0.001f ||
            fitted.vMin.z < placement.cell_z * cell - 0.001f || fitted.vMax.z > (placement.cell_z + placement.depth) * cell + 0.001f ||
            std::abs(fitted.vMin.y) > 0.001f || fitted.vMax.y > placement.height + 0.001f)
          throw std::runtime_error("Reference model escaped its fitted box");
      }
      if (m_tutorialStep != "model-assign" && m_tutorialStep != "model-parts") {
        auto* skinned = rendered->PlacementSkinnedMesh("model-square");
        auto* second = rendered->PlacementSkinnedMesh("model-rectangle");
        std::vector<XMATRIX44> before, after;
        skinned->ExportBoneMatrices(before);
        const float secondTime = second->GetAnimLocalTime();
        skinned->GetAnimController().Update(0.4f);
        skinned->ExportBoneMatrices(after);
        bool moved = false;
        for (size_t bone = 0; bone < before.size(); ++bone)
          for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
              moved |= std::abs(before[bone].m[row][column] - after[bone].m[row][column]) > 0.00001f;
        if (!moved || second->GetAnimLocalTime() != secondTime) throw std::runtime_error("Bone animation failed or leaked between instances");
        skinned->ResetAnimation();
        skinned->GetAnimController().Update(0.0f);
      }
      auto invalid = terrain;
      invalid.placements[0].visual->mesh = "Models/missing-placement-model.glb";
      if (CommitTerrainEdit(0, invalid) || world.objects[0].heightmap->placements[0].visual->mesh != terrain.placements[0].visual->mesh)
        throw std::runtime_error("Missing model replaced the previous terrain visual");
      invalid = terrain;
      invalid.placements[0].visual->animation = "missing-animation";
      if (CommitTerrainEdit(0, invalid)) throw std::runtime_error("Missing animation was accepted");
      invalid = terrain;
      invalid.placements[0].visual->hidden_geometry = {0, 1, 2, 3};
      if (CommitTerrainEdit(0, invalid)) throw std::runtime_error("An entirely hidden model was accepted for fitting");
      m_terrainStatus.clear();
      T8_LOG_INFO("[PlacementVisualTest] PASS GLB skeleton/clips, fitted square/rectangle, independent bones and transactional failure");
    }
    if (m_tutorialStep == "placement-overlap" || m_tutorialStep == "placement-bounds" || m_tutorialStep == "placement-slope") {
      const auto result = t850::CheckTerrainPlacement(terrain, m_placementBrush);
      if (result.allowed) throw std::runtime_error("Tutorial rejection state unexpectedly allows placement");
    }
    if (sculpt || m_tutorialStep == "paint-material") {
      m_terrainBrushEnabled = m_tutorialStep != "placement-slope";
      m_placementMode = m_tutorialStep == "placement-slope";
      m_showPlacementGrid = m_tutorialStep == "placement-slope";
    }
    if (m_tutorialStep == "flat-terrain" || m_tutorialStep == "heightmap-terrain" || m_tutorialStep == "transform") {
      m_showPlacementGrid = false;
      m_placementMode = false;
    }
    if (m_tutorialStep == "physics" || m_tutorialStep == "transform") {
      m_panels.showTerrainEditor = false;
      m_panels.showInspector = true;
      m_showPlacementGrid = false;
    }
    if (m_tutorialStep.starts_with("navigation")) {
      m_panels.showTerrainEditor = false;
      m_panels.showNavMeshAuthoring = true;
      m_showPlacementGrid = false;
      check(CreateEditorNavMesh());
      m_editorNavMeshVisible = m_tutorialStep == "navigation-result";
      m_editorNavMeshShowWire = m_editorNavMeshVisible;
    }
    if (m_tutorialStep == "layout") {
      m_panels.showTerrainEditor = false;
      world.selectedIdx = -1;
      world.activeCameraIdx = 0;
    }
    if (m_tutorialStep == "file-menu" || m_tutorialStep == "save-menu") {
      m_panels.showTerrainEditor = false;
      ImGuiSetCaptureMenu("File");
    } else if (m_tutorialStep == "view-menu") {
      m_panels.showTerrainEditor = false;
      ImGuiSetCaptureMenu("View");
    } else if (m_tutorialStep == "flat-import" || m_tutorialStep == "image-import") {
      m_panels.showTerrainEditor = false;
      m_showPlacementGrid = false;
    }
    if (m_tutorialStep == "reloaded" || m_tutorialStep == "empty-scene" || m_tutorialStep == "model-reloaded") {
      m_tutorialScenePath = (std::filesystem::temp_directory_path() /
          (t850::game::MakeStableId("tutorial_") + ".t8scene")).string();
      if (m_tutorialStep == "empty-scene") {
        auto emptyScene = BuildEditorSceneSnapshot({});
        emptyScene.objects.clear();
        emptyScene.physics_entities.clear();
        emptyScene.game_entities.clear();
        emptyScene.game_groups.clear();
        emptyScene.navigation_mesh.reset();
        check(t850::scene::SaveEditorSceneFile(emptyScene, m_tutorialScenePath, &m_terrainStatus));
        m_panels.showTerrainEditor = false;
        m_panels.showHierarchy = true;
      } else {
        check(SaveEditorSceneSnapshot(m_tutorialScenePath, false));
      }
      QueueTutorialSceneReload(m_tutorialScenePath);
    }
    if (m_tutorialStep == "play" || m_tutorialStep == "stopped" || m_tutorialStep == "model-play") {
      m_showPlacementGrid = false;
      OpenPlayScene();
    }
    T8_LOG_INFO("[TutorialCapture] Prepared %s", m_tutorialStep.c_str());
  } catch (const std::exception& error) {
    T8_LOG_ERROR("[TutorialCapture] %s", error.what());
    m_terrainSelfTestResult = 1;
  }
}

void EditorApp::SignalTutorialCapture() {
  if (m_tutorialStep.empty() || !m_tutorialPrepared || m_tutorialSignaled || m_tutorialFrame < 25) return;
  if (m_tutorialStep == "play" && !m_playSceneLoaded) {
    T8_LOG_ERROR("[TutorialCapture] Play window did not load");
    m_terrainSelfTestResult = 1;
  }
  if (!m_tutorialScenePath.empty()) {
    std::error_code ignored;
    std::filesystem::remove(m_tutorialScenePath, ignored);
  }
  T8_LOG_INFO("[TutorialCapture] Ready %s", m_tutorialStep.c_str());
  m_tutorialSignaled = true;
}

}