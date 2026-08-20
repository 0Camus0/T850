#ifndef T850_RENDER_CONTAINER_H
#define T850_RENDER_CONTAINER_H

#include <array>
#include <scene/IBLResources.h>
#include <scene/PrimitiveInstance.h>
#include <scene/PrimitiveManager.h>
#include <scene/RenderGraph.h>
#include <scene/RenderResourceRegistry.h>
#include <scene/SceneProp.h>
#include <string>
#include <utils/Camera.h>
#include <utils/xMaths.h>
#include <vector>

namespace t850 {

  struct EngineContext;

  struct RenderContainerDesc {
    std::string name;
    std::string renderGraphPath;
    int width = 0;
    int height = 0;
    int finalOutputRT = -1;
    RenderResourceRegistry* resources = nullptr;
    SceneProps* sceneProps = nullptr;
  };

  struct RenderInstanceHandle {
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;
    bool IsValid() const { return index != UINT32_MAX && generation != 0; }
    friend bool operator==(const RenderInstanceHandle&, const RenderInstanceHandle&) = default;
  };

  class RenderContainer {
  public:
    RenderContainer() = default;
    ~RenderContainer() = default;

    bool Initialize(BaseDriver* driver, EngineContext* engineContext, const RenderContainerDesc& desc);
    void Destroy(BaseDriver* driver);
    bool Resize(BaseDriver* driver, int width, int height);

    SceneProps& Props();
    const SceneProps& Props() const;
    RenderGraph& Graph() { return m_renderGraph; }
    const RenderGraph& Graph() const { return m_renderGraph; }
    PrimitiveInst* Quads() { return m_quads.data(); }
    const PrimitiveInst* Quads() const { return m_quads.data(); }

    void SetName(std::string name) { m_name = std::move(name); }
    const std::string& Name() const { return m_name; }
    void SetFinalOutputRT(int rtHandle) { m_finalOutputRT = rtHandle; }
    int FinalOutputRT() const { return m_finalOutputRT; }
    int Width() const { return m_width; }
    int Height() const { return m_height; }

    void SetMainCamera(Camera* camera) { m_mainCamera = camera; }
    void SetLightCamera(Camera* camera) { m_lightCamera = camera; }
    void SetEnvironmentMaps(const EnvironmentMapSet& envMaps) { m_envMaps = envMaps; }
    EnvironmentMapSet& EnvironmentMaps() { return m_envMaps; }

    void ClearMeshes();
    RenderInstanceHandle AddMeshInstance(const PrimitiveInst& instance);
    RenderInstanceHandle AddMeshHandle(const RenderMeshHandle& handle);
    bool RemoveMesh(RenderInstanceHandle handle);
    PrimitiveInst* GetMesh(RenderInstanceHandle handle);
    const PrimitiveInst* GetMesh(RenderInstanceHandle handle) const;
    std::size_t ActiveMeshCount() const { return m_activeMeshCount; }
    std::vector<PrimitiveInst>& Meshes() { return m_meshes; }
    const std::vector<PrimitiveInst>& Meshes() const { return m_meshes; }

    bool Execute(BaseDriver* driver, float deltaSeconds);
    bool Execute(BaseDriver* driver,
                 PrimitiveInst* meshes,
                 int meshCount,
                 Camera* mainCamera,
                 Camera* lightCamera,
                 const EnvironmentMapSet& envMaps,
                 float deltaSeconds,
                 int finalOutputRT = -1);

  private:
    bool CreateQuads(EngineContext* engineContext);

    std::string m_name;
    RenderResourceRegistry* m_resources = nullptr;
    SceneProps m_sceneProps;
    SceneProps* m_externalSceneProps = nullptr;
    RenderGraph m_renderGraph;
    PrimitiveManager m_quadManager;
    std::array<PrimitiveInst, 10> m_quads{};
    std::vector<PrimitiveInst> m_meshes;
    std::vector<uint32_t> m_meshGenerations;
    std::vector<uint8_t> m_meshActive;
    std::vector<uint32_t> m_freeMeshSlots;
    std::vector<PrimitiveInst> m_activeMeshes;
    std::size_t m_activeMeshCount = 0;
    EnvironmentMapSet m_envMaps;
    Camera* m_mainCamera = nullptr;
    Camera* m_lightCamera = nullptr;
    XMATRIX44 m_quadVP;
    XMATRIX44 m_meshVP;
    std::string m_renderGraphPath;
    int m_width = 0;
    int m_height = 0;
    int m_finalOutputRT = -1;
    bool m_initialized = false;
  };

} // namespace t850

#endif
