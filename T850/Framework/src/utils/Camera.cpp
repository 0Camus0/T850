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

#include <utils/Camera.h>
#include <utils/T8_Spline.h>
const	XVECTOR3	Camera::LookConstCameraSpace = XVECTOR3(0.0f, 0.0f, 1.0f);
const	XVECTOR3	Camera::RightConstCameraSpace = XVECTOR3(1.0f, 0.0f, 0.0f);
const	XVECTOR3	Camera::UpConstCameraSpace = XVECTOR3(0.0f, 1.0f, 0.0f);

Camera::Camera() {
	Reset();
}

void	Camera::InitPerspective(XVECTOR3 position, float fov, float ratio, float np, float fp, bool lf) {
	this->Fov = fov;
	this->AspectRatio = ratio;
	this->NPlane = np;
	this->FPlane = fp;
	this->Eye = position;
	this->LeftHanded = lf;
	this->Ortho = false;

  m_externalControl = false;
  m_lookAtCenter = true;
  m_agent = nullptr;
  LastFrameEye = XVECTOR3(0,0,0);
  CreatePojection();
}

void	Camera::InitOrtho(XVECTOR3 position, float W, float H, float np, float fp, bool lf) {
	this->Width = W;
	this->Height = H;
	this->NPlane = np;
	this->FPlane = fp;
	this->Eye = position;
	this->LeftHanded = lf;
	this->Ortho = true;

	m_externalControl = false;
	m_lookAtCenter = true;
	m_agent = nullptr;
	LastFrameEye = XVECTOR3(0, 0, 0);
	CreatePojection();
}

void	Camera::CreatePojectionPerspective() {
	if(LeftHanded)
		XMatPerspectiveLH(Projection, Fov, AspectRatio, NPlane, FPlane);
	else
		XMatPerspectiveRH(Projection, Fov, AspectRatio, NPlane, FPlane);
}

void	Camera::CreatePojectionOrtho() {
	if (LeftHanded)
		XMatOrthoLH(Projection, Width, Height, NPlane, FPlane);
	else
		XMatOrthoRH(Projection, Width, Height, NPlane, FPlane);
}

void    Camera::CreatePojection() {
	if (this->Ortho)
		CreatePojectionOrtho();
	else
		CreatePojectionPerspective();
}

void	Camera::SetLookAt(XVECTOR3 v) {
	Look = v - Eye;
	Look.Normalize();
	Pitch = asin(-Look.y);
	Yaw = atan2f(Look.x, Look.z);
	Roll = 0.0f;

	Update(1.0f/60.0f);
}

void Camera::MoveUp(float dt) {
	Velocity.y += Speed * dt;
}

void Camera::MoveDown(float dt) {
	Velocity.y -= Speed * dt;
}

void	Camera::MoveForward(float dt) {
	Velocity.z += Speed * dt;
}

void	Camera::MoveBackward(float dt) {
	Velocity.z -= Speed * dt;
}

void	Camera::StrafeLeft(float dt) {
	Velocity.x -= Speed * dt;
}

void	Camera::StrafeRight(float dt) {
	Velocity.x += Speed * dt;
}

void	Camera::SetFov(float f) {
	this->Fov = f;
	CreatePojection();
}

void	Camera::SetRatio(float r) {
	this->AspectRatio = r;
	CreatePojection();
}

void	Camera::SetPlanes(float n, float f) {
	this->NPlane = n;
	this->FPlane = f;
	CreatePojection();
}

void Camera::AttachAgent(const t800::SplineAgent & agent)
{
  m_externalControl = true;
  m_agent = &agent;
}

t800::SplineAgent * Camera::DettachAgent()
{
  m_externalControl = false;
  return const_cast<t800::SplineAgent*>(m_agent);
}

void	Camera::MoveYaw(float f) {
	if (MaxYaw != 0.0) {
		if ((Yaw + f) > MaxYaw || (Yaw + f) < -MaxYaw)
			return;
	}
	Yaw += f;
}

void	Camera::MovePitch(float f) {
	if (MaxPitch != 0.0) {
		if ((Pitch + f) > MaxPitch || (Pitch + f) < -MaxPitch)
			return;
	}
	Pitch += f;
}

void	Camera::MoveRoll(float f) {
	if (MaxRoll != 0.0) {
		if ((Roll + f) > MaxRoll || (Roll + f) < -MaxRoll)
			return;
	}
	Roll += f;
}


void	Camera::Update(float dt) {
	 XMATRIX44	X_, Y_, Z_, T_;
   if (m_externalControl) {
     Eye = m_agent->m_actualPoint;
     if (m_lookAtCenter) {
       if (Eye != LastFrameEye) {
         float		_Yaw;
         float		_Pitch;
         Look = Eye - LastFrameEye;
         Look.Normalize();
         _Pitch = asin(-Look.y);
         _Yaw = atan2f(Look.x, Look.z);

         XMatRotationX(X_, -_Pitch);
         XMatRotationY(Y_, -_Yaw);
         XMatIdentity(Z_);
       }
     }
     else {
       XMatRotationX(X_, -m_agent->m_actualPoint.m_rotation.x);
       XMatRotationY(Y_, -m_agent->m_actualPoint.m_rotation.y);
       XMatRotationZ(Z_, m_agent->m_actualPoint.m_rotation.z);
     }
   }
   else {
     XMatRotationX(X_, -Pitch);
     XMatRotationY(Y_, -Yaw);
     XMatRotationZ(Z_, -Roll);
   }


	  View = Y_*X_*Z_;

	  XMATRIX44 transpose;
	  XMatTranspose(transpose, View);
	  XVecTransformNormal(Look, LookConstCameraSpace, transpose);
	  XVecTransformNormal(Up, UpConstCameraSpace, transpose);
	  XVecTransformNormal(Right, RightConstCameraSpace, transpose);

	  Look.Normalize();
	  Up.Normalize();
	  Right.Normalize();

    if (!m_externalControl) {
      XVECTOR3 currentvelocity = Velocity.x*Right + Velocity.y*Up + Velocity.z*Look;
      Velocity -= Velocity*Friction;
      Eye += currentvelocity;
    }
  
  XVECTOR3 TEYE = -Eye;
	XMatTranslation(T_,TEYE);
	View = T_*View;
	VP = View*Projection;

  LastFrameEye = Eye;
}

void	Camera::Reset() {
	Eye = XVECTOR3(0.0f, 0.0f, 0.0f);
	Velocity = XVECTOR3(0.0f, 0.0f, 0.0f);
	Fov = Deg2Rad(45.0f);
	NPlane = 0.01f;
	FPlane = 1000.0f;
	AspectRatio = 1.0f;
	Speed = 5000.0;
	Yaw = 0.0f;
	Pitch = 0.0f;
	Roll = 0.0f;
	Friction = 0.1f;
	MaxRoll = 0.0f;
	MaxPitch = Deg2Rad(89.0f);
	MaxYaw = 0.0f;
	LeftHanded = true;
	Ortho = false;
	Width = 1280.0f;
	Height = 720.0f;
}
