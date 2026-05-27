#ifndef T800_DEVLAYER_H
#define T800_DEVLAYER_H

#include <core/Core.h>
#include <scene/WireframeSphere.h>

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

    void SetSceneInputBlocked(bool blocked) { m_blockSceneInput = blocked; }

    bool IsPaused() const { return m_paused; }

  private:
    struct CullingDebugVert {
      float x, y, z, w;
    };

    struct CullingDebugCBuffer {
      XMATRIX44 WVP;
    };

    bool EnsureCullingDebugResources();
    void DestroyCullingDebugResources();
    void DrawCullingDebug(const SceneProps& props);
    void BuildCullingFrustumVertices(const Camera& camera, CullingDebugVert* outVerts) const;

    RootFramework* m_framework;
    SceneBase* m_activeScene;
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
