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
#include <scene/RenderQuad.h>
#include <scene/SplineWireframe.h>

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

  int	 PrimitiveManager::CreateMesh(char *fname) {
    PrimitiveBase *primitive = new RenderMesh();
    primitive->Load(fname);
    primitive->Create();
    primitives.push_back(primitive);
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
      primitives[i]->Destroy();
      delete primitives[i];
    }
    primitives.clear();
  }
}
