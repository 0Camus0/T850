/*********************************************************
 * Copyright (C) 2017 Daniel Enriquez (camus_mm@hotmail.com)
 * All Rights Reserved
 *********************************************************/

#ifndef T8DITOR_EDITORAPP_H
#define T8DITOR_EDITORAPP_H

#include <core/Core.h>
#include <utils/Timer.h>
#include <scene/PrimitiveManager.h>
#include <scene/PrimitiveInstance.h>
#include <scene/SceneProp.h>

#include <string>
#include <vector>

#include "EditorCamera.h"
#include "EditorLineRenderer.h"
#include "EditorGrid.h"
#include "EditorGizmo.h"
#include "EditorMesh.h"
#include "EditorImGui.h"

namespace t8ditor {

  void SetStartupMeshPath(const std::string& p);

  // Per-object data for multi-mesh scenes.
  struct SceneObject {
    EditorMesh            wireframe;   // wireframe overlay + AABB + transform
    t800::PrimitiveInst   litInst;     // lit/textured rendering instance
    int                   primId = -1; // index into m_primMgr.primitives
    std::string           name;        // display name (file path or user label)
  };

  class EditorApp : public t800::AppBase {
  public:
    EditorApp() : AppBase() {}

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
    void CheckResize();
    void HandleMousePick();

    Timer m_dtTimer;
    float m_dtSecs   = 0.0f;
    bool  m_firstFrame = true;

    EditorCamera        m_camera;
    EditorLineRenderer  m_lines;
    EditorGrid          m_grid;
    EditorGizmo         m_gizmo;

    // Scene objects (multi-mesh)
    std::vector<SceneObject> m_objects;
    int  m_selectedIdx = -1;  // -1 = nothing selected

    // Lit rendering pipeline (shared by all scene objects)
    SceneProps            m_sceneProps;
    t800::PrimitiveManager m_primMgr;
    XMATRIX44             m_vp;

    bool m_assetsCreated = false;
    bool m_imguiReady   = false;

    PanelVisibility m_panels;

    int  m_lastW = 0;
    int  m_lastH = 0;
  };

} // namespace t8ditor

#endif // T8DITOR_EDITORAPP_H
