#include <pch.h>
#include <scene/RenderContainer.h>
#include <core/EngineContext.h>
#include <scene/PrimitiveBase.h>
#include <utils/Log.h>

namespace t850 {

  namespace {
    struct PrimitiveBindingState {
      PrimitiveInst* instance = nullptr;
      PrimitiveBase* primitive = nullptr;
      SceneProps* sceneProps = nullptr;
      XMATRIX44* viewProj = nullptr;
    };

    class PrimitiveBindingGuard {
    public:
      ~PrimitiveBindingGuard() {
        Restore();
      }

      void Bind(PrimitiveInst& instance, SceneProps& sceneProps, XMATRIX44& viewProj) {
        if (!instance.pBase) {
          return;
        }
        m_states.push_back({&instance, instance.pBase, instance.pBase->pScProp, instance.pViewProj});
        instance.pBase->SetSceneProps(&sceneProps);
        instance.pViewProj = &viewProj;
        instance.Update();
      }

      void Restore() {
        if (m_restored) {
          return;
        }
        for (const PrimitiveBindingState& state : m_states) {
          if (state.primitive) {
            state.primitive->SetSceneProps(state.sceneProps);
          }
          if (state.instance) {
            state.instance->pViewProj = state.viewProj;
          }
        }
        m_restored = true;
      }

    private:
      std::vector<PrimitiveBindingState> m_states;
      bool m_restored = false;
    };
  }

  bool RenderContainer::Initialize(BaseDriver* driver, EngineContext* engineContext, const RenderContainerDesc& desc) {
    if (!driver || desc.renderGraphPath.empty()) {
      T8_LOG_ERROR("[RenderContainer] Invalid initialization for '%s'", desc.name.c_str());
      return false;
    }

    Destroy(driver);
    m_name = desc.name;
    m_renderGraphPath = desc.renderGraphPath;
    m_width = desc.width;
    m_height = desc.height;
    m_finalOutputRT = desc.finalOutputRT;
    m_resources = desc.resources;
    m_externalSceneProps = desc.sceneProps;
    m_sceneProps = SceneProps{};
    XMatIdentity(m_quadVP);
    XMatIdentity(m_meshVP);

    if (!m_renderGraph.Load(m_renderGraphPath)) {
      T8_LOG_ERROR("[RenderContainer] Failed to load graph '%s' for '%s'",
                   m_renderGraphPath.c_str(), m_name.c_str());
      return false;
    }
    m_renderGraph.CreateRenderTargets(driver, Props(), m_width, m_height);

    if (!CreateQuads(engineContext)) {
      Destroy(driver);
      return false;
    }

    m_initialized = true;
    T8_LOG_INFO("[RenderContainer] Ready '%s' graph='%s' size=%dx%d finalRT=%d",
                m_name.c_str(), m_renderGraphPath.c_str(), m_width, m_height, m_finalOutputRT);
    return true;
  }

  SceneProps& RenderContainer::Props() {
    return m_externalSceneProps ? *m_externalSceneProps : m_sceneProps;
  }

  const SceneProps& RenderContainer::Props() const {
    return m_externalSceneProps ? *m_externalSceneProps : m_sceneProps;
  }

  void RenderContainer::Destroy(BaseDriver* driver) {
    m_meshes.clear();
    m_quadManager.DestroyPrimitives();
    if (driver) {
      m_renderGraph.DestroyRenderTargets(driver);
    }
    m_externalSceneProps = nullptr;
    m_initialized = false;
  }

  bool RenderContainer::Resize(BaseDriver* driver, int width, int height) {
    if (!driver || !m_initialized) {
      return false;
    }
    width = (std::max)(1, width);
    height = (std::max)(1, height);
    if (m_width == width && m_height == height) {
      return true;
    }
    m_width = width;
    m_height = height;
    m_renderGraph.DestroyRenderTargets(driver);
    m_renderGraph.CreateRenderTargets(driver, Props(), m_width, m_height);
    return true;
  }

  bool RenderContainer::CreateQuads(EngineContext* engineContext) {
    m_quadManager.SetEngineContext(engineContext);
    m_quadManager.Init();
    m_quadManager.SetVP(&m_quadVP);
    m_quadManager.SetSceneProps(&Props());
    PrimitiveBase* quad = m_quadManager.GetPrimitive(PrimitiveManager::QUAD);
    if (!quad) {
      T8_LOG_ERROR("[RenderContainer] Quad primitive unavailable for '%s'", m_name.c_str());
      return false;
    }
    for (PrimitiveInst& instance : m_quads) {
      instance.CreateInstance(quad, &m_quadVP);
      instance.Update();
    }
    return true;
  }

  void RenderContainer::ClearMeshes() {
    m_meshes.clear();
  }

  int RenderContainer::AddMeshInstance(const PrimitiveInst& instance) {
    if (!instance.pBase) {
      return -1;
    }
    PrimitiveInst copy = instance;
    copy.pViewProj = &m_meshVP;
    copy.ClearPhysicsLinks();
    copy.SetVisible(true);
    copy.Update();
    m_meshes.push_back(copy);
    return static_cast<int>(m_meshes.size()) - 1;
  }

  int RenderContainer::AddMeshHandle(const RenderMeshHandle& handle) {
    if (!handle.primitive) {
      return -1;
    }
    PrimitiveInst instance;
    instance.CreateInstance(handle.primitive, &m_meshVP);
    instance.Update();
    m_meshes.push_back(instance);
    return static_cast<int>(m_meshes.size()) - 1;
  }

  bool RenderContainer::Execute(BaseDriver* driver, float deltaSeconds) {
    if (!driver || !m_initialized || !m_mainCamera || m_meshes.empty()) {
      return false;
    }
    Props().FrameDeltaSec = deltaSeconds;
    m_meshVP = m_mainCamera->VP;
    PrimitiveBindingGuard bindingGuard;
    for (PrimitiveInst& mesh : m_meshes) {
      bindingGuard.Bind(mesh, Props(), m_meshVP);
    }

    m_renderGraph.Execute(
        driver,
        Props(),
        m_meshes.data(),
        static_cast<int>(m_meshes.size()),
        m_quads.data(),
        m_mainCamera,
        m_lightCamera,
        nullptr,
        m_envMaps,
        m_finalOutputRT);
    return true;
  }

  bool RenderContainer::Execute(BaseDriver* driver,
                                PrimitiveInst* meshes,
                                int meshCount,
                                Camera* mainCamera,
                                Camera* lightCamera,
                                const EnvironmentMapSet& envMaps,
                                float deltaSeconds,
                                int finalOutputRT) {
    if (!driver || !m_initialized || !mainCamera || !meshes || meshCount <= 0) {
      return false;
    }
    Props().FrameDeltaSec = deltaSeconds;
    m_meshVP = mainCamera->VP;
    PrimitiveBindingGuard bindingGuard;
    for (int i = 0; i < meshCount; ++i) {
      bindingGuard.Bind(meshes[i], Props(), m_meshVP);
    }
    const int outputRT = finalOutputRT >= 0 ? finalOutputRT : m_finalOutputRT;
    m_renderGraph.Execute(
        driver,
        Props(),
        meshes,
        meshCount,
        m_quads.data(),
        mainCamera,
        lightCamera,
        nullptr,
        envMaps,
        outputRT);
    return true;
  }

} // namespace t850
