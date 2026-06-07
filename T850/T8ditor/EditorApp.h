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
#include <utils/CameraProfiles.h>
#include <scene/PrimitiveManager.h>
#include <scene/PrimitiveInstance.h>
#include <scene/RenderResourceRegistry.h>
#include <scene/RenderGraph.h>
#include <scene/RenderSkinnedMesh.h>
#include <scene/RenderViewport.h>
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
#include <chrono>

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

  struct HostedSceneWindowController {
    bool open = false;
    bool loaded = false;
    bool openRequested = false;
    bool closeRequested = false;
    bool guiVisible = true;
    bool viewportInputActive = false;
    void* nativeHandle = nullptr;
    void* loggedNativeHandle = nullptr;
    bool mainViewportLogged = false;
    unsigned int imguiViewportId = 0;
    unsigned int dockspaceId = 0;
    unsigned int dockClassId = 0;
    float viewportPosX = 0.0f;
    float viewportPosY = 0.0f;
    float viewportSizeX = 0.0f;
    float viewportSizeY = 0.0f;
    float imageMinX = 0.0f;
    float imageMinY = 0.0f;
    float imageSizeX = 0.0f;
    float imageSizeY = 0.0f;

    void Open(bool guiVisibleOnOpen = true) {
      open = true;
      loaded = false;
      openRequested = true;
      closeRequested = false;
      guiVisible = guiVisibleOnOpen;
      viewportInputActive = false;
      ResetNativeWindow();
      ResetViewportRect();
    }

    void RequestClose() {
      open = false;
      closeRequested = true;
      viewportInputActive = false;
    }

    void Reset(bool guiVisibleDefault = true) {
      open = false;
      loaded = false;
      openRequested = false;
      closeRequested = false;
      guiVisible = guiVisibleDefault;
      viewportInputActive = false;
      ResetNativeWindow();
      ResetViewportRect();
    }

    void ResetNativeWindow() {
      nativeHandle = nullptr;
      loggedNativeHandle = nullptr;
      mainViewportLogged = false;
      imguiViewportId = 0;
      dockspaceId = 0;
      dockClassId = 0;
    }

    void ResetViewportRect() {
      viewportPosX = 0.0f;
      viewportPosY = 0.0f;
      viewportSizeX = 0.0f;
      viewportSizeY = 0.0f;
      imageMinX = 0.0f;
      imageMinY = 0.0f;
      imageSizeX = 0.0f;
      imageSizeY = 0.0f;
    }
  };

  enum class EditorCameraMode : int {
    Orbit = 0,
    Fly = 1
  };

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
    bool IsModalActive() const override {
      return m_meshEditorOpen || m_meshEditorCloseRequested ||
             m_playSceneOpen || m_playSceneCloseRequested ||
             m_ragdollEditorOpen;
    }
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
    void InvalidateEditorFrozenFrame();
    bool EnsureEditorFrozenFrameTarget(int width, int height);
    void DestroyEditorFrozenFrameTarget();
    void DrawEditorFrozenFrame(t850::BaseDriver* driver);
    SceneFile BuildEditorSceneSnapshot(const std::string& scenePath);
    bool SaveEditorSceneSnapshot(const std::string& path, bool updateLoadedScene);
    t850::SandboxProfileDesc BuildEditorSceneProfile() const;
    void UpsertEditorSceneProfile(std::vector<t850::SandboxProfileDesc>& profiles) const;
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
    t850::RenderSkinnedMesh* GetSelectedSkinnedMesh() const;
    void DrawSelectedAnimationInspector(struct SceneObject& obj);
    void DrawEditorRenderingPanel();
    void SetEditorCubemap(const std::string& cubemapPath);
    bool HasHostedSceneWindowOpen() const;
    void ResetMainEditorFrameLimiter();
    void ThrottleMainEditorFrameIfNeeded();

    Timer m_dtTimer;
    float m_dtSecs   = 0.0f;
    bool  m_firstFrame = true;

    EditorCamera        m_camera;
    EditorLineRenderer  m_lines;
    EditorGrid          m_grid;
    EditorGizmo         m_gizmo;
    EditorMesh          m_mesh;      // wireframe overlay (kept for toggle)
    t850::CameraController m_editorCameraController;
    EditorCameraMode m_editorCameraMode = EditorCameraMode::Orbit;

    // Lit/textured rendering via the Framework pipeline
    SceneProps            m_sceneProps;
    t850::PrimitiveManager m_primMgr;
    t850::RenderResourceRegistry m_renderResources;
    t850::PrimitiveInst   m_meshInst;
    int                   m_meshPrimId = -1;  // -1 = no lit mesh loaded
    XMATRIX44             m_vp;               // VP matrix for the prim mgr
    Camera                m_editorLightCamera;
    t850::SceneSetup      m_editorSceneSetup;
    GaussFilter           m_editorShadowFilter;
    GaussFilter           m_editorBloomFilter;
    GaussFilter           m_editorDofFilter;
    t850::JoltPhysicsSystem m_physics;
    t850::PhysicsDebugRenderer m_physicsDebug;
    bool m_editorHeadlampEnabled = false;
    bool m_editorShowSkeleton = false;
    bool m_editorShowPhysics = false;
    bool m_editorShowLightVolumes = false;
    int m_editorDebugRTSelection = 0;
    int m_editorActiveGaussSelection = 1;
    int m_editorCurrentCubemapIndex = -1;
    std::string m_editorCurrentCubemapPath;
    uint32_t m_editorAnimationInspectorEntityId = 0;
    int m_editorAnimationInspectorAnimSet = -1;

    bool m_assetsCreated = false;
    bool m_imguiReady   = false;
    bool m_mainEditorFrameLimiterActive = false;
    std::chrono::steady_clock::time_point m_nextMainEditorFrameTime{};
    int  m_lastFailedResizeW = 0;
    int  m_lastFailedResizeH = 0;
    std::chrono::steady_clock::time_point m_nextResizeRetryTime{};
    int  m_editorFrozenFrameRT = -1;
    int  m_editorFrozenFrameW = 0;
    int  m_editorFrozenFrameH = 0;
    bool m_editorFrozenFrameValid = false;

    // Panel visibility (persists across frames)
    PanelVisibility m_panels;

    // Resize tracking — poll SDL window size each frame
    int  m_lastW = 0;
    int  m_lastH = 0;

    // Selection — currently single-object; -1 = nothing selected
    bool m_meshSelected = false;

    HostedSceneWindowController m_meshEditorWindow;
    bool& m_meshEditorOpen = m_meshEditorWindow.open;
    bool& m_meshEditorSceneLoaded = m_meshEditorWindow.loaded;
    bool& m_meshEditorOpenRequested = m_meshEditorWindow.openRequested;
    bool& m_meshEditorCloseRequested = m_meshEditorWindow.closeRequested;
    int  m_meshEditorObjectIndex = -1;
    void*& m_meshEditorNativeHandle = m_meshEditorWindow.nativeHandle;
    void*& m_meshEditorLoggedNativeHandle = m_meshEditorWindow.loggedNativeHandle;
    bool& m_meshEditorMainViewportLogged = m_meshEditorWindow.mainViewportLogged;
    unsigned int& m_meshEditorImGuiViewportId = m_meshEditorWindow.imguiViewportId;
    float& m_meshEditorViewportPosX = m_meshEditorWindow.viewportPosX;
    float& m_meshEditorViewportPosY = m_meshEditorWindow.viewportPosY;
    float& m_meshEditorViewportSizeX = m_meshEditorWindow.viewportSizeX;
    float& m_meshEditorViewportSizeY = m_meshEditorWindow.viewportSizeY;
    float& m_meshEditorViewportImageMinX = m_meshEditorWindow.imageMinX;
    float& m_meshEditorViewportImageMinY = m_meshEditorWindow.imageMinY;
    float& m_meshEditorViewportImageSizeX = m_meshEditorWindow.imageSizeX;
    float& m_meshEditorViewportImageSizeY = m_meshEditorWindow.imageSizeY;
    unsigned int& m_meshEditorDockspaceId = m_meshEditorWindow.dockspaceId;
    unsigned int& m_meshEditorDockClassId = m_meshEditorWindow.dockClassId;
    t850::RenderViewport m_meshEditorGBufferTarget;
    t850::RenderViewport m_meshEditorViewportTarget;
    int m_meshEditorDebugLogFramesRemaining = 0;
    bool& m_meshEditorGuiVisible = m_meshEditorWindow.guiVisible;
    bool& m_meshEditorViewportInputActive = m_meshEditorWindow.viewportInputActive;
    std::unique_ptr<::RagdollEditor> m_meshEditorScene;
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

    HostedSceneWindowController m_playSceneWindow;
    bool& m_playSceneOpen = m_playSceneWindow.open;
    bool& m_playSceneLoaded = m_playSceneWindow.loaded;
    bool& m_playSceneOpenRequested = m_playSceneWindow.openRequested;
    bool& m_playSceneCloseRequested = m_playSceneWindow.closeRequested;
    bool m_playSceneHasPreviousConfig = false;
    bool m_playSceneHasVisibleObjects = false;
    bool m_playSceneLaunchFailed = false;
    bool& m_playSceneGuiVisible = m_playSceneWindow.guiVisible;
    int m_playScenePreviousActiveCameraIdx = -1;
    t850::Config m_playScenePreviousConfig;
    SceneFile m_playSceneEditorSnapshot;
    std::unique_ptr<::Quake3Mock> m_playScene;
    std::string m_playSceneTempPath;
    std::string m_playSceneStatus;
    t850::RenderViewport m_playSceneViewportTarget;
    float& m_playSceneViewportImageMinX = m_playSceneWindow.imageMinX;
    float& m_playSceneViewportImageMinY = m_playSceneWindow.imageMinY;
    float& m_playSceneViewportImageSizeX = m_playSceneWindow.imageSizeX;
    float& m_playSceneViewportImageSizeY = m_playSceneWindow.imageSizeY;
    bool& m_playSceneViewportInputActive = m_playSceneWindow.viewportInputActive;
    unsigned int& m_playSceneImGuiViewportId = m_playSceneWindow.imguiViewportId;
    unsigned int& m_playSceneDockspaceId = m_playSceneWindow.dockspaceId;
    unsigned int& m_playSceneDockClassId = m_playSceneWindow.dockClassId;
    t850::EngineContext m_playSceneEngineContext;
    t850::JoltPhysicsSystem m_playScenePhysics;

    HostedSceneWindowController m_ragdollEditorWindow;
    bool& m_ragdollEditorOpen = m_ragdollEditorWindow.open;
    bool& m_ragdollEditorOpenRequested = m_ragdollEditorWindow.openRequested;
    int  m_ragdollEditorObjectIndex = -1;
    int  m_ragdollEditorSelectedBody = -1;
    int  m_ragdollEditorSelectedJoint = -1;
    int  m_ragdollEditorSelectedUnassignedBone = -1;
    int  m_ragdollEditorSelectedAffectedBone = -1;
    void*& m_ragdollEditorNativeHandle = m_ragdollEditorWindow.nativeHandle;
    void*& m_ragdollEditorLoggedNativeHandle = m_ragdollEditorWindow.loggedNativeHandle;
    bool& m_ragdollEditorMainViewportLogged = m_ragdollEditorWindow.mainViewportLogged;
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
