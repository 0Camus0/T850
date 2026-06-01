/*********************************************************
* Copyright (C) 2017 Daniel Enriquez (camus_mm@hotmail.com)
* All Rights Reserved
*********************************************************/

// EditorApp — AppBase subclass that hosts the T8ditor.exe shell.
//
// Phase 1b ("editor 101"): orbit camera, XZ reference grid, three-axis
// transform gizmo on the active selection, and wireframe display of an
// optional .x mesh. Keyboard manipulates the selection (no mouse-drag
// gizmo dragging yet — that lands with ImGui+ImGuizmo in Phase 1c).
//
// Hot-keys (when no modifier is down):
//   W / E / R   : switch gizmo to Translate / Rotate / Scale
//   Arrow keys  : (no mouse) orbit camera
//   I / K       : translate selection along world Z
//   J / L       : translate selection along world X
//   U / O       : translate selection along world Y
//   [ / ]       : rotate selection around world Y
//   ; / '       : scale selection down / up (uniform)
//   F           : frame selection (re-center camera distance)
//
// Mouse:
//   middle-drag         : orbit camera around target
//   shift+middle-drag   : pan target
//   right-drag          : orbit (alternate)

#ifndef T8DITOR_EDITORAPP_H
#define T8DITOR_EDITORAPP_H

#include <core/Core.h>
#include <utils/Timer.h>
#include <scene/PrimitiveManager.h>
#include <scene/PrimitiveInstance.h>
#include <scene/SceneProp.h>
#include <physics/JoltPhysicsSystem.h>
#include <physics/PhysicsDebugRenderer.h>

#include <string>

#include "EditorCamera.h"
#include "EditorLineRenderer.h"
#include "EditorGrid.h"
#include "EditorGizmo.h"
#include "EditorMesh.h"
#include "EditorImGui.h"

namespace t8ditor {

  // Set by main.cpp before constructing the app.
  void SetStartupMeshPath(const std::string& p);
  void SetStartupDumpFrame(int frame);

  class EditorApp : public t850::AppBase {
  public:
    EditorApp() : AppBase() {}

    // AppBase contract.
    void InitVars() override;
    void CreateAssets() override;
    void LoadAssets() override;
    void DestroyAssets() override;

    void OnUpdate() override;
    void OnDraw() override;
    void OnInput() override;

    void OnPause() override;
    void OnResume() override;
    void OnReset() override;

    void LoadScene(int id) override;

  private:
    void ProcessSelectionInput();
    void ImportMesh(const std::string& path);
    void CloneSelected();
    void CheckResize();
    void HandleMousePick();
    void SyncSceneObjectTransforms();
    void DestroyObjectRagdoll(struct SceneObject& obj);
    void DestroyAllObjectRagdolls();
    bool EnsureObjectRagdollAuthoring(struct SceneObject& obj);
    bool RecreateObjectRagdoll(struct SceneObject& obj, t850::PhysicsBodyMotion motion);
    bool StartObjectRagdollSimulation(struct SceneObject& obj);
    bool ResetObjectRagdollToAnimation(struct SceneObject& obj);
    void UpdateSkinnedAnimationAndRagdolls();
    void UploadSkinnedBoneTextures();
    void DrawRagdollInspector(struct SceneObject& obj);
    void FrameSelectedEntity();

    Timer m_dtTimer;
    float m_dtSecs   = 0.0f;
    bool  m_firstFrame = true;

    EditorCamera        m_camera;
    EditorLineRenderer  m_lines;
    EditorGrid          m_grid;
    EditorGizmo         m_gizmo;
    EditorMesh          m_mesh;      // wireframe overlay (kept for toggle)

    // Lit/textured rendering via the Framework pipeline
    SceneProps            m_sceneProps;
    t850::PrimitiveManager m_primMgr;
    t850::PrimitiveInst   m_meshInst;
    int                   m_meshPrimId = -1;  // -1 = no lit mesh loaded
    XMATRIX44             m_vp;               // VP matrix for the prim mgr
    t850::JoltPhysicsSystem m_physics;
    t850::PhysicsDebugRenderer m_physicsDebug;

    bool m_assetsCreated = false;
    bool m_imguiReady   = false;

    // Panel visibility (persists across frames)
    PanelVisibility m_panels;

    // Resize tracking — poll SDL window size each frame
    int  m_lastW = 0;
    int  m_lastH = 0;

    // Selection — currently single-object; -1 = nothing selected
    bool m_meshSelected = false;
  };

} // namespace t8ditor

#endif // T8DITOR_EDITORAPP_H
