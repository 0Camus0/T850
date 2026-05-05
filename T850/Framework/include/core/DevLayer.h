#ifndef T800_DEVLAYER_H
#define T800_DEVLAYER_H

#include <core/Core.h>
#include <gui/GUIManager.h>
#include <scene/WireframeSphere.h>
#include <string>

namespace t850 {

  class DevLayer {
  public:
    DevLayer();

    void Init(RootFramework* framework);
    void Destroy();

    // Scene lifecycle
    void SetActiveScene(SceneBase* scene);
    SceneBase* GetActiveScene() const;

    // Per-frame forwarding
    void Update(float dt);
    void Draw();
    void ProcessInput(InputManager* input);

    // Scene transitions
    void LoadScene(SceneBase* scene);
    void UnloadScene();

    GUIManager& GetGUI() { return m_gui; }
    const GUIManager& GetGUI() const { return m_gui; }
    void RebuildGUIForScene();
    void SetLegacyGuiEnabled(bool enabled);
    bool IsLegacyGuiEnabled() const { return m_legacyGuiEnabled; }
    bool IsLegacyPopupActive() const;
    void SetSceneInputBlocked(bool blocked) { m_blockSceneInput = blocked; }

    void SetEditMode(bool e);
    void SetSnapToGrid(bool s);
    void SetControlEditMode(bool e);
    bool SetControlEditTargetByName(const std::string& targetName);
    bool IsPaused() const { return m_paused; }

  private:
    struct CullingDebugVert {
      float x, y, z, w;
    };

    struct CullingDebugCBuffer {
      XMATRIX44 WVP;
    };

    static constexpr const char* kLayoutPath = "Layouts/gui_layout.json";
    static constexpr const char* kControlLayoutPath = "Layouts/gui_controls_layout.json";

    bool EnsureCullingDebugResources();
    void DestroyCullingDebugResources();
    void DrawCullingDebug(const SceneProps& props);
    void BuildCullingFrustumVertices(const Camera& camera, CullingDebugVert* outVerts) const;

    RootFramework* m_framework;
    SceneBase* m_activeScene;
    GUIManager m_gui;
    bool m_guiInited;
    bool m_legacyGuiEnabled = true;
    bool m_blockSceneInput = false;
    bool m_paused = false;
    BaseDriver* m_cullingDebugDriver = nullptr;
    ShaderBase* m_cullingDebugShader = nullptr;
    VertexBuffer* m_cullingDebugVB = nullptr;
    IndexBuffer* m_cullingDebugIB = nullptr;
    ConstantBuffer* m_cullingDebugCB = nullptr;
    CullingDebugCBuffer m_cullingDebugCBuffer;
    WireframeSphere m_cullingDebugCameraSphere;
  };

} // namespace t850

#endif
