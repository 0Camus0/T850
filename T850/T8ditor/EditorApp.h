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
#include <core/EngineContext.h>
#include <utils/Timer.h>
#include <scene/PrimitiveManager.h>
#include <scene/PrimitiveInstance.h>
#include <scene/RenderResourceRegistry.h>
#include <scene/RenderGraph.h>
#include <scene/SceneSetup.h>
#include <scene/SceneProp.h>
#include <core/Config.h>
#include <physics/JoltPhysicsSystem.h>
#include <physics/PhysicsDebugRenderer.h>
#include <physics/RagdollEditorTool.h>
#include <utils/Camera.h>
#include <RagdollEditor.h>
#include <Quake3Mock.h>

#include <string>
#include <memory>

#include "EditorCamera.h"
#include "EditorLineRenderer.h"
#include "EditorGrid.h"
#include "EditorGizmo.h"
#include "EditorMesh.h"
#include "EditorScene.h"
#include "EditorImGui.h"

namespace t8ditor {

  // Set by main.cpp before constructing the app.
  void SetStartupMeshPath(const std::string& p);
  void SetStartupDumpFrame(int frame);

  class EditorApp : public t850::AppBase {
  public:
    EditorApp() : AppBase() {}
    ~EditorApp() override;

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
    bool IsModalActive() const override { return m_meshEditorOpen; }
    bool WantsRelativeMouseMode() const override {
      return m_playSceneOpen && m_playSceneLoaded && !m_playSceneGuiVisible && !m_playSceneCloseRequested;
    }

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
    void OpenMeshEditor(int objectIndex);
    void CloseMeshEditor();
    void DrawMeshEditorWindow();
    void DrawMeshEditorViewport(struct SceneObject& obj);
    bool EnsureMeshEditorViewportTarget(int width, int height);
    void DestroyMeshEditorViewportTarget();
    bool EnsureMeshEditorSceneState(struct SceneObject& obj);
    void ApplyMeshEditorProfiles(struct SceneObject& obj);
    void ApplyMeshEditorProfileState(struct SceneObject& obj, const t850::SandboxProfileDesc& state);
    void SetMeshEditorCubemap(const std::string& cubemapPath);
    void DestroyMeshEditorSceneResources();
    bool EnsureMeshEditorEmbeddedScene(struct SceneObject& obj);
    void OpenRagdollEditor(int objectIndex);
    void CloseRagdollEditor();
    void DrawRagdollEditorWindow();
    void DrawRagdollEditorBodyPanel(struct SceneObject& obj);
    void DrawRagdollEditorViewport(struct SceneObject& obj);
    bool EnsureRagdollEditorViewportTarget(int width, int height);
    void DestroyRagdollEditorViewportTarget();
    SceneFile BuildEditorSceneSnapshot(const std::string& scenePath);
    bool SaveEditorSceneSnapshot(const std::string& path, bool updateLoadedScene);
    bool ExportTemporaryPlayScene(std::string& outPath);
    void RestoreEditorStateAfterPlay();
    void OpenPlayScene();
    void ClosePlayScene(bool restoreEditorScene = true);
    void DrawPlaySceneWindow();
    void DrawPlaySceneViewport();
    bool EnsurePlaySceneViewportTarget(int width, int height);
    void DestroyPlaySceneViewportTarget();
    bool EnsurePlaySceneRuntimeLoaded();
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
    t850::RenderResourceRegistry m_renderResources;
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

    bool m_meshEditorOpen = false;
    bool m_meshEditorOpenRequested = false;
    int  m_meshEditorObjectIndex = -1;
    void* m_meshEditorNativeHandle = nullptr;
    void* m_meshEditorLoggedNativeHandle = nullptr;
    bool m_meshEditorMainViewportLogged = false;
    unsigned int m_meshEditorImGuiViewportId = 0;
    float m_meshEditorViewportPosX = 0.0f;
    float m_meshEditorViewportPosY = 0.0f;
    float m_meshEditorViewportSizeX = 0.0f;
    float m_meshEditorViewportSizeY = 0.0f;
    float m_meshEditorViewportImageMinX = 0.0f;
    float m_meshEditorViewportImageMinY = 0.0f;
    float m_meshEditorViewportImageSizeX = 0.0f;
    float m_meshEditorViewportImageSizeY = 0.0f;
    unsigned int m_meshEditorDockspaceId = 0;
    unsigned int m_meshEditorDockClassId = 0;
    int m_meshEditorGBufferRT = -1;
    int m_meshEditorViewportRT = -1;
    int m_meshEditorViewportW = 0;
    int m_meshEditorViewportH = 0;
    int m_meshEditorPendingViewportW = 0;
    int m_meshEditorPendingViewportH = 0;
    int m_meshEditorViewportStableFrames = 0;
    int m_meshEditorDebugLogFramesRemaining = 0;
    bool m_meshEditorGuiVisible = true;
    bool m_meshEditorViewportInputActive = false;
    std::unique_ptr<::RagdollEditor> m_meshEditorScene;
    bool m_meshEditorSceneLoaded = false;
    SceneProps m_meshEditorSceneProps;
    t850::SceneSetup m_meshEditorSceneSetup;
    t850::RenderGraph m_meshEditorRenderGraph;
    bool m_meshEditorSceneSetupLoaded = false;
    bool m_meshEditorRenderGraphLoaded = false;
    bool m_meshEditorSceneReady = false;
    bool m_meshEditorSSAOTextureReady = false;
    std::string m_meshEditorSceneModelKey;
    std::string m_meshEditorCurrentCubemapPath;
    int m_meshEditorCurrentCubemapIndex = -1;
    t850::EnvironmentMapSet m_meshEditorEnvMaps;
    int m_meshEditorEnvMapIdx = -1;
    int m_meshEditorDiffuseIBLTexIndex = -1;
    int m_meshEditorSpecularIBLTexIndex = -1;
    int m_meshEditorBrdfLUTTexIndex = -1;
    int m_meshEditorSheenIBLTexIndex = -1;
    int m_meshEditorCharlieLUTTexIndex = -1;
    int m_meshEditorSheenELUTTexIndex = -1;
    Camera m_meshEditorCamera;
    XVECTOR3 m_meshEditorOrbitTarget = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    float m_meshEditorOrbitYaw = -0.75f;
    float m_meshEditorOrbitPitch = 0.35f;
    float m_meshEditorOrbitDistance = 4.0f;
    float m_meshEditorFovDeg = 45.0f;
    bool m_meshEditorCameraInitialized = false;
    bool m_meshEditorShowWireframe = false;
    bool m_meshEditorShowSkeleton = false;

    bool m_playSceneOpen = false;
    bool m_playSceneLoaded = false;
    bool m_playSceneOpenRequested = false;
    bool m_playSceneCloseRequested = false;
    bool m_playSceneHasPreviousConfig = false;
    bool m_playSceneHasVisibleObjects = false;
    bool m_playSceneLaunchFailed = false;
    bool m_playSceneGuiVisible = false;
    int m_playScenePreviousActiveCameraIdx = -1;
    t850::Config m_playScenePreviousConfig;
    SceneFile m_playSceneEditorSnapshot;
    std::unique_ptr<::Quake3Mock> m_playScene;
    std::string m_playSceneTempPath;
    std::string m_playSceneStatus;
    int m_playSceneViewportRT = -1;
    int m_playSceneViewportW = 0;
    int m_playSceneViewportH = 0;
    int m_playScenePendingViewportW = 0;
    int m_playScenePendingViewportH = 0;
    int m_playSceneViewportStableFrames = 0;
    float m_playSceneViewportImageMinX = 0.0f;
    float m_playSceneViewportImageMinY = 0.0f;
    float m_playSceneViewportImageSizeX = 0.0f;
    float m_playSceneViewportImageSizeY = 0.0f;
    bool m_playSceneViewportInputActive = false;
    unsigned int m_playSceneImGuiViewportId = 0;
    unsigned int m_playSceneDockspaceId = 0;
    unsigned int m_playSceneDockClassId = 0;
    t850::EngineContext m_playSceneEngineContext;
    t850::JoltPhysicsSystem m_playScenePhysics;

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
