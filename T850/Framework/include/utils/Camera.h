#ifndef T800_CAMERA_H
#define T800_CAMERA_H

#include <Config.h>

#include <utils/xMaths.h>

namespace t800 {
  class SplineAgent;
}
class Camera {
public:
	Camera();

	void	InitPerspective(XVECTOR3 position,float fov,float ratio,float np,float fp,bool lf=true);
	void	InitOrtho(XVECTOR3 position, float w, float h, float np, float fp, bool lf = true);
	void	CreatePojectionPerspective();
	void	CreatePojectionOrtho();
	void    CreatePojection();

	void	SetLookAt(XVECTOR3 v);

	void	MoveForward(float dt);
	void	MoveBackward(float dt);
	void	StrafeLeft(float dt);
	void	StrafeRight(float dt);
	void	MoveUp(float dt);
	void	MoveDown(float dt);

	void	MoveYaw(float f);
	void	MovePitch(float f);
	void	MoveRoll(float f);

	void	Update(float dt);
	void	Reset();

	void	SetFov(float f);
	void	SetRatio(float r);
	void	SetPlanes(float n, float f);

  const t800::SplineAgent* m_agent;
  void AttachAgent(const t800::SplineAgent& agent);
  t800::SplineAgent* DettachAgent();

	float		Fov;
	float		AspectRatio;
	float		NPlane;
	float		FPlane;

	float		Yaw;
	float		Pitch;
	float		Roll;

	float		MaxRoll;
	float		MaxPitch;
	float		MaxYaw;

	float		Speed;
	float		Friction;

	float		Width;
	float		Height;

	bool		LeftHanded;
	bool		Ortho;

  XVECTOR3  LastFrameEye;
	XVECTOR3	Eye;
	XVECTOR3	Look;
	XVECTOR3	Right;
	XVECTOR3	Up;

	XVECTOR3	Velocity;

	XMATRIX44	Position;
	XMATRIX44	RotX;
	XMATRIX44	RotY;
	XMATRIX44	RotZ;

	XMATRIX44	View;
	XMATRIX44	Projection;
	XMATRIX44	VP;

  bool m_externalControl;
  bool m_lookAtCenter;

	static const	XVECTOR3	LookConstCameraSpace;
	static const	XVECTOR3	RightConstCameraSpace;
	static const	XVECTOR3	UpConstCameraSpace;
};


#endif

