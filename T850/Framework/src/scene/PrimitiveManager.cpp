#include <pch.h>
/*********************************************************
* Copyright (C) 2017 Daniel Enriquez (camus_mm@hotmail.com)
* All Rights Reserved
*
* You may use, distribute and modify this code under the
* following terms:
* ** Do not claim that you wrote this software
* ** A mention would be appreciated but not needed
* ** I do not and will not provide support, this software is "as is"
* ** Enjoy, learn and share.
*********************************************************/

#include <scene/PrimitiveManager.h>

#include <scene/RenderMesh.h>
#include <scene/RenderSkinnedMesh.h>
#include <scene/RenderQuad.h>
#include <scene/SplineWireframe.h>
#include <core/EngineContext.h>
#include <utils/Log.h>

namespace t850 {
  PrimitiveBase*	PrimitiveManager::GetPrimitive(unsigned int index) const {
    if (index >= primitives.size())
      return 0;

    return primitives[index];
  }

  int  PrimitiveManager::CreateTriangle() {
    return (int)(primitives.size() - 1);
  }

  int	 PrimitiveManager::CreateCube() {
    return (int)(primitives.size() - 1);
  }

  int	 PrimitiveManager::CreateMesh(const char *fname) {
    // Probe: load to check if the model has skin/animation data
    RenderMesh* probe = new RenderMesh();
    probe->SetEngineContext(m_engineContext);
    T8_LOG_INFO("Loading mesh begin: '%s'", fname);
    probe->Load(fname);
    T8_LOG_INFO("Loading mesh: '%s'", fname);
    if (!probe->xFile) {
      T8_LOG_ERROR("Mesh '%s' failed to load; primitive creation aborted", fname);
      delete probe;
      return -1;
    }

    // Check for skin data in any geometry
    bool hasSkin = false;
    if (probe->xFile && !probe->xFile->XMeshDataBase.empty() && probe->xFile->XMeshDataBase[0]) {
      xF::xMeshContainer* mc = probe->xFile->XMeshDataBase[0];
      for (auto& geom : mc->Geometry) {
        if ((geom.VertexAttributes & xF::xMeshGeometry::HAS_SKINWEIGHTS0) &&
            (geom.VertexAttributes & xF::xMeshGeometry::HAS_SKININDEXES0)) {
          hasSkin = true;
          break;
        }
      }
      // Also check for animation data without skin vertex attribs
      if (!hasSkin && mc->Animation.isAnimInfo && !mc->Animation.Animations.empty()) {
        hasSkin = true;
      }
    }

    PrimitiveBase* primitive;
    if (hasSkin) {
      T8_LOG_INFO("Detected skinned/animated mesh, using RenderSkinnedMesh");
      RenderSkinnedMesh* skinned = new RenderSkinnedMesh();
      skinned->SetEngineContext(m_engineContext);
      skinned->xFile = probe->xFile;
      skinned->m_asset = probe->m_asset;
      skinned->m_sourcePath = probe->m_sourcePath;
      probe->xFile = nullptr;  // transfer ownership
      probe->m_asset = nullptr;
      probe->m_sourcePath.clear();
      delete probe;
      skinned->Create();
      primitive = skinned;
    } else {
      probe->Create();
      if (probe->Info.empty()) {
        T8_LOG_ERROR("Mesh '%s' has no drawable geometry; primitive creation aborted", fname);
        delete probe;
        return -1;
      }
      primitive = probe;
    }

    primitives.push_back(primitive);
    T8_LOG_INFO("Mesh '%s' ready (primitive %d)", fname, (int)(primitives.size()-1));
    return (int)(primitives.size() - 1);
  }

  int PrimitiveManager::CreateQuad() {
    PrimitiveBase *primitive = new RenderQuad();
    primitive->SetEngineContext(m_engineContext);
    primitive->Create();
    primitives.push_back(primitive);
    return (int)(primitives.size() - 1);
  }

  int PrimitiveManager::CreateSpline(Spline& spline)
  {
    SplineWireframe *primitive = new SplineWireframe();
    primitive->SetEngineContext(m_engineContext);
    primitive->m_spline = &spline;
    primitive->Create();
    primitives.push_back(primitive);
    return (int)(primitives.size() - 1);
  }

  void PrimitiveManager::SetSceneProps(SceneProps *p) {
    for (unsigned int i = 0; i < primitives.size(); i++) {
      primitives[i]->SetSceneProps(p);
    }
  }

  void PrimitiveManager::SetEngineContext(EngineContext* context) {
    m_engineContext = context;
    for (PrimitiveBase* primitive : primitives) {
      if (primitive)
        primitive->SetEngineContext(context);
    }
  }

  void PrimitiveManager::Init()
  {
    if (!primitives.empty()) return; // already initialised
    primitives.resize(COUNT);
    primitives[QUAD] = new RenderQuad();
    primitives[QUAD]->SetEngineContext(m_engineContext);
    primitives[QUAD]->Create();
  }

  void PrimitiveManager::DrawPrimitives() {
    for (unsigned int i = 0; i < primitives.size(); i++) {
      primitives[i]->Draw(0, &(*pVP).m[0][0]);
    }
  }

  void PrimitiveManager::DestroyPrimitives() {
    for (unsigned int i = 0; i < primitives.size(); i++) {
      if (primitives[i]) {
        primitives[i]->Destroy();
        delete primitives[i];
      }
    }
    primitives.clear();
  }
}
