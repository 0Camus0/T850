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

#ifndef T800_PRIMITIVEMANAGER_H
#define T800_PRIMITIVEMANAGER_H

#include <Config.h>

#include <vector>
#include <utils/xMaths.h>
#include <scene/PrimitiveBase.h>
#include <scene/SceneProp.h>
namespace t800 {
  class Spline;
  class PrimitiveManager {
  private:
    int  CreateQuad();
  public:
    enum PRIMITIVES {
      QUAD = 0,
      COUNT
    };
    void SetVP(XMATRIX44 *vp) {
      pVP = vp;
    }
    int  CreateTriangle();
    int	 CreateCube();
    int	 CreateMesh(char *fname);
    int  CreateSpline(Spline& spline);

    void SetSceneProps(SceneProps *p);
    void Init();

    void DrawPrimitives();
    void DestroyPrimitives();
    PrimitiveBase*	GetPrimitive(unsigned int) const;

    std::vector<PrimitiveBase*> primitives;

    XMATRIX44 *pVP;
  };
}

#endif
