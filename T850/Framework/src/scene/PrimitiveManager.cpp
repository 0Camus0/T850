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
#include <utils/Log.h>

namespace t800 {
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
    probe->Load(fname);
    T8_LOG_INFO("Loading mesh: '%s'", fname);

    // Check for skin data in any geometry
    bool hasSkin = false;
    if (probe->xFile && !probe->xFile->XMeshDataBase.empty()) {
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
      skinned->xFile = probe->xFile;
      probe->xFile = nullptr;  // transfer ownership
      delete probe;
      skinned->Create();
      primitive = skinned;
    } else {
      probe->Create();
      primitive = probe;
    }

    primitives.push_back(primitive);
    T8_LOG_INFO("Mesh '%s' ready (primitive %d)", fname, (int)(primitives.size()-1));
    return (int)(primitives.size() - 1);
  }

  int PrimitiveManager::CreateQuad() {
    PrimitiveBase *primitive = new RenderQuad();
    primitive->Create();
    primitives.push_back(primitive);
    return (int)(primitives.size() - 1);
  }

  int PrimitiveManager::CreateSpline(Spline& spline)
  {
    SplineWireframe *primitive = new SplineWireframe();
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

  void PrimitiveManager::Init()
  {
    if (!primitives.empty()) return; // already initialised
    primitives.resize(COUNT);
    primitives[QUAD] = new RenderQuad();
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
