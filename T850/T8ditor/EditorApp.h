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
#include <physics/RagdollEditorTool.h>
#include <utils/Camera.h>

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
    bool LoadObjectRagdollAuthoringFromFile(struct SceneObject& obj);
    bool RecreateObjectRagdoll(struct SceneObject& obj, t850::PhysicsBodyMotion motion);
    bool StartObjectRagdollSimulation(struct SceneObject& obj);
    bool ResetObjectRagdollToAnimation(struct SceneObject& obj);
    void UpdateSkinnedAnimationAndRagdolls();
    void UploadSkinnedBoneTextures();
    void DrawRagdollInspector(struct SceneObject& obj);
    void OpenRagdollEditor(int objectIndex);
    void CloseRagdollEditor();
    void DrawRagdollEditorWindow();
    void DrawRagdollEditorBodyPanel(struct SceneObject& obj);
    void DrawRagdollEditorViewport(struct SceneObject& obj);
    bool EnsureRagdollEditorViewportTarget(int width, int height);
    void DestroyRagdollEditorViewportTarget();
    void FrameSelectedEntity();
    void RenderLoadingProgressFrame();

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

    bool m_ragdollEditorOpen = false;
    bool m_ragdollEditorOpenRequested = false;
    int  m_ragdollEditorObjectIndex = -1;
    int  m_ragdollEditorSelectedBody = -1;
    int  m_ragdollEditorSelectedJoint = -1;
    int  m_ragdollEditorSelectedUnassignedBone = -1;
    int  m_ragdollEditorSelectedAffectedBone = -1;
    void* m_ragdollEditorNativeHandle = nullptr;
    void* m_ragdollEditorLoggedNativeHandle = nullptr;
    bool m_ragdollEditorMainViewportLogged = false;
    bool m_ragdollEditorDirty = false;
    std::string m_ragdollEditorStatus;
    int m_ragdollEditorGBufferRT = -1;
    int m_ragdollEditorViewportRT = -1;
    int m_ragdollEditorViewportW = 0;
    int m_ragdollEditorViewportH = 0;
    int m_ragdollEditorPendingViewportW = 0;
    int m_ragdollEditorPendingViewportH = 0;
    int m_ragdollEditorViewportStableFrames = 0;
    Camera m_ragdollEditorCamera;
    XVECTOR3 m_ragdollEditorOrbitTarget = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    float m_ragdollEditorOrbitYaw = -0.75f;
    float m_ragdollEditorOrbitPitch = 0.35f;
    float m_ragdollEditorOrbitDistance = 4.0f;
    bool m_ragdollEditorCameraInitialized = false;
    bool m_ragdollEditorShowWireframe = false;
    int m_ragdollEditorSelectionMode = static_cast<int>(t850::ragdoll_editor::SelectionMode::Bodies);
    int m_ragdollEditorToolMode = static_cast<int>(t850::ragdoll_editor::ToolMode::Select);
    int m_ragdollEditorSelectedHandle = -1;
    bool m_ragdollEditorHandleDragging = false;
    bool m_ragdollEditorGizmoDragging = false;
    int m_ragdollEditorGizmoAxis = -1;
    float m_ragdollEditorGizmoLastParameter = 0.0f;
    XVECTOR3 m_ragdollEditorGizmoDragCenter = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    XVECTOR3 m_ragdollEditorGizmoDragAxis = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
    XVECTOR3 m_ragdollEditorGizmoLastVector = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
  };

} // namespace t8ditor

#endif // T8DITOR_EDITORAPP_H
