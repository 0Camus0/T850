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


#include <utils/xMaths.h>


float distribution(float x, float mu, float sigma) {
	float d = x - mu;
	float n = 1.0f / (sqrtf(2.0f*3.1415926535f)*sigma);
	return expf(-(d*d) / (2.0f*sigma*sigma))*n;
}

std::vector<pair_> sampleInterval(float minInclusive, float maxInclusive, float sampleCount, float sigma) {
	std::vector<pair_> result;
	float stepSize = (maxInclusive - minInclusive) / (sampleCount - 1.0f);
	for (int s = 0; s<(int)sampleCount; s++) {
		pair_ tp;
		tp.x = minInclusive + (float)s*stepSize;
		tp.y = distribution(tp.x, 0.0f, sigma);
		result.push_back(tp);
	}
	return result;
}

float integration(std::vector<pair_> samples) {
	float result = samples[0].y + samples[samples.size() - 1].y;
	for (unsigned int s = 1; s<(samples.size() - 1); s++) {
		float sampleWeight = (s % 2 == 0) ? 2.0f : 4.0f;
		result += sampleWeight*samples[s].y;
	}
	float h = (samples[samples.size() - 1].x - samples[0].x) / (samples.size() - 1);
	return (result*h) / 3.0f;
}

float roundTo(float num, float decimals) {
	float shift = powf(10.0f, decimals);
	return roundf(num*shift) / shift;
}

std::vector<sample_> UpdateKernel(float sigma, float kernelSize, float Samplecount) {
	float samplePerBin = ceilf(Samplecount / kernelSize);
	if ((int)samplePerBin % 2 == 0)
		samplePerBin++;

	float weightSum = 0.0f;
	float kernelLeft = -floorf(kernelSize / 2.0f);

	std::vector<pair_> outsideSamplesLeft = sampleInterval(-5.0f*sigma, kernelLeft - 0.5f, samplePerBin, sigma);
	std::vector<pair_> outsideSamplesRight = sampleInterval(-kernelLeft + 0.5f, 5.0f*sigma, samplePerBin, sigma);

	std::vector<sample_> allSamples;
	allSamples.push_back(sample_(outsideSamplesLeft, 0.0f));
	for (float tap = 0; tap<kernelSize; tap++) {
		float left = kernelLeft - 0.5f + tap;
		std::vector<pair_> tapSamples = sampleInterval(left, left + 1.0f, samplePerBin, sigma);
		float tapWeight = integration(tapSamples);

		allSamples.push_back(sample_(tapSamples, tapWeight));
		weightSum += tapWeight;
	}
	allSamples.push_back(sample_(outsideSamplesRight, 0.0f));

	for (unsigned int i = 0; i<allSamples.size(); i++) {
		allSamples[i].weight = roundTo(allSamples[i].weight / weightSum, 6.0f);
	}
/*
	for (unsigned int i = 1; i < allSamples.size() - 1; i++) {
		std::cout << roundTo(allSamples[i].weight, 6.0f) << " ";
	}
	*/
	/*
	for (unsigned int i = 1; i < allSamples.size() - 1; i++) {
	for (unsigned int j = 1; j < allSamples.size() - 1; j++) {
	std::cout << roundTo(allSamples[i].weight*allSamples[j].weight, 6.0f) << " ";
	}
	std::cout << std::endl;
	}
	*/
	return allSamples;
}


 void XMatMultiply(XMATRIX44 &mpout, const XMATRIX44 &mp1, const XMATRIX44 &mp2) {

	float a00 = mp1.m[0][0], a01 = mp1.m[0][1], a02 = mp1.m[0][2], a03 = mp1.m[0][3],
		a10 = mp1.m[1][0], a11 = mp1.m[1][1], a12 = mp1.m[1][2], a13 = mp1.m[1][3],
		a20 = mp1.m[2][0], a21 = mp1.m[2][1], a22 = mp1.m[2][2], a23 = mp1.m[2][3],
		a30 = mp1.m[3][0], a31 = mp1.m[3][1], a32 = mp1.m[3][2], a33 = mp1.m[3][3];

	float b00 = mp2.m[0][0], b01 = mp2.m[0][1], b02 = mp2.m[0][2], b03 = mp2.m[0][3],
		b10 = mp2.m[1][0], b11 = mp2.m[1][1], b12 = mp2.m[1][2], b13 = mp2.m[1][3],
		b20 = mp2.m[2][0], b21 = mp2.m[2][1], b22 = mp2.m[2][2], b23 = mp2.m[2][3],
		b30 = mp2.m[3][0], b31 = mp2.m[3][1], b32 = mp2.m[3][2], b33 = mp2.m[3][3];

	mpout.m[0][0] = a00*b00 + a01*b10 + a02*b20 + a03*b30;
	mpout.m[0][1] = a00*b01 + a01*b11 + a02*b21 + a03*b31;
	mpout.m[0][2] = a00*b02 + a01*b12 + a02*b22 + a03*b32;
	mpout.m[0][3] = a00*b03 + a01*b13 + a02*b23 + a03*b33;
	mpout.m[1][0] = a10*b00 + a11*b10 + a12*b20 + a13*b30;
	mpout.m[1][1] = a10*b01 + a11*b11 + a12*b21 + a13*b31;
	mpout.m[1][2] = a10*b02 + a11*b12 + a12*b22 + a13*b32;
	mpout.m[1][3] = a10*b03 + a11*b13 + a12*b23 + a13*b33;
	mpout.m[2][0] = a20*b00 + a21*b10 + a22*b20 + a23*b30;
	mpout.m[2][1] = a20*b01 + a21*b11 + a22*b21 + a23*b31;
	mpout.m[2][2] = a20*b02 + a21*b12 + a22*b22 + a23*b32;
	mpout.m[2][3] = a20*b03 + a21*b13 + a22*b23 + a23*b33;
	mpout.m[3][0] = a30*b00 + a31*b10 + a32*b20 + a33*b30;
	mpout.m[3][1] = a30*b01 + a31*b11 + a32*b21 + a33*b31;
	mpout.m[3][2] = a30*b02 + a31*b12 + a32*b22 + a33*b32;
	mpout.m[3][3] = a30*b03 + a31*b13 + a32*b23 + a33*b33;
}

 void XMatTranslation(XMATRIX44 &mpout, XVECTOR3& v) {
	XMatIdentity(mpout);
	mpout.m[3][0] = v.x; mpout.m[3][1] = v.y; mpout.m[3][2] = v.z; mpout.m[3][3] = 1.0f;
}

 void XMatTranslation(XMATRIX44 &mpout, XVECTOR3 v) {
	XMatIdentity(mpout);
	mpout.m[3][0] = v.x; mpout.m[3][1] = v.y; mpout.m[3][2] = v.z; mpout.m[3][3] = 1.0f;
}

 void XMatTranslation(XMATRIX44 &mpout, const float &x, const float &y, const float &z) {
	XMatIdentity(mpout);
	mpout.m[3][0] = x; mpout.m[3][1] = y; mpout.m[3][2] = z; mpout.m[3][3] = 1.0f;
}

 void XMatScaling(XMATRIX44 &mpout, const float &x, const float &y, const float &z) {
	mpout.m[0][0] = x;
	mpout.m[1][1] = y;
	mpout.m[2][2] = z;
}

 void XMatRotationXLH(XMATRIX44 &mpout, const float &rads) {
	mpout.m[0][0] = 1.0f;
	mpout.m[1][1] = std::cos(rads);
	mpout.m[1][2] = std::sin(rads);
	mpout.m[2][1] = -std::sin(rads);
	mpout.m[2][2] = std::cos(rads);
}

 void XMatRotationXRH(XMATRIX44 &mpout, const float &rads) {
	mpout.m[0][0] = 1.0f;
	mpout.m[1][1] = std::cos(rads);
	mpout.m[1][2] = -std::sin(rads);
	mpout.m[2][1] = std::sin(rads);
	mpout.m[2][2] = std::cos(rads);
}

 void XMatRotationYLH(XMATRIX44 &mpout, const float &rads) {
	mpout.m[1][1] = 1.0f;
	mpout.m[0][0] = std::cos(rads);
	mpout.m[0][2] = -std::sin(rads);
	mpout.m[2][0] = std::sin(rads);
	mpout.m[2][2] = std::cos(rads);
}

 void XMatRotationYRH(XMATRIX44 &mpout, const float &rads) {
	mpout.m[1][1] = 1.0f;
	mpout.m[0][0] = std::cos(rads);
	mpout.m[0][2] = std::sin(rads);
	mpout.m[2][0] = -std::sin(rads);
	mpout.m[2][2] = std::cos(rads);
}

 void XMatRotationZLH(XMATRIX44 &mpout, const float &rads) {
	mpout.m[2][2] = 1.0f;
	mpout.m[0][0] = std::cos(rads);
	mpout.m[0][1] = std::sin(rads);
	mpout.m[1][0] = -std::sin(rads);
	mpout.m[1][1] = std::cos(rads);
}

 void XMatRotationZRH(XMATRIX44 &mpout, const float &rads) {
	mpout.m[2][2] = 1.0f;
	mpout.m[0][0] = std::cos(rads);
	mpout.m[0][1] = -std::sin(rads);
	mpout.m[1][0] = std::sin(rads);
	mpout.m[1][1] = std::cos(rads);
}


 void XMatTranspose(XMATRIX44 &mpout, const XMATRIX44 mat) {
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			mpout.m[i][j] = mat.m[j][i];
		}
	}
}

 void XMatIdentity(XMATRIX44 &mat) {
	mat.m[0][0] = 1.0f;
	mat.m[0][1] = 0.0f;
	mat.m[0][2] = 0.0f;
	mat.m[0][3] = 0.0f;

	mat.m[1][0] = 0.0f;
	mat.m[1][1] = 1.0f;
	mat.m[1][2] = 0.0f;
	mat.m[1][3] = 0.0f;

	mat.m[2][0] = 0.0f;
	mat.m[2][1] = 0.0f;
	mat.m[2][2] = 1.0f;
	mat.m[2][3] = 0.0f;

	mat.m[3][0] = 0.0f;
	mat.m[3][1] = 0.0f;
	mat.m[3][2] = 0.0f;
	mat.m[3][3] = 1.0f;
}

 void XMatViewLookAtLH(XMATRIX44 &mpout, const XVECTOR3 &camPos, const XVECTOR3 &lookAt, const XVECTOR3 &upAxis) {
	 XVECTOR3 zaxe = lookAt - camPos;
	 zaxe.Normalize();

	 XVECTOR3 xaxe;
	 XVecCross(xaxe, upAxis, zaxe);
	 xaxe.Normalize();

	 XVECTOR3 yaxe;
	 XVecCross(yaxe, zaxe, xaxe);

	 mpout.m[0][0] = xaxe.x;
	 mpout.m[0][1] = yaxe.x;
	 mpout.m[0][2] = zaxe.x;
	 XVecDot(mpout.m[3][0], xaxe, camPos);
	 mpout.m[3][0] = -mpout.m[3][0];

	 mpout.m[1][0] = xaxe.y;
	 mpout.m[1][1] = yaxe.y;
	 mpout.m[1][2] = zaxe.y;
	 XVecDot(mpout.m[3][1], yaxe, camPos);
	 mpout.m[3][1] = -mpout.m[3][1];

	 mpout.m[2][0] = xaxe.z;
	 mpout.m[2][1] = yaxe.z;
	 mpout.m[2][2] = zaxe.z;
	 XVecDot(mpout.m[3][2], zaxe, camPos);
	 mpout.m[3][2] = -mpout.m[3][2];

	 mpout.m[0][3] = 0.0f;
	 mpout.m[1][3] = 0.0f;
	 mpout.m[2][3] = 0.0f;
	 mpout.m[3][3] = 1.0f;
}

 void XMatViewLookAtRH(XMATRIX44 &mpout, const XVECTOR3 &camPos, const XVECTOR3 &lookAt, const XVECTOR3 &upAxis) {
	 XVECTOR3 zaxe = camPos - lookAt;
	 zaxe.Normalize();

	 XVECTOR3 xaxe;
	 XVecCross(xaxe, upAxis, zaxe);
	 xaxe.Normalize();

	 XVECTOR3 yaxe;
	 XVecCross(yaxe, zaxe, xaxe);

	 mpout.m[0][0] = xaxe.x;
	 mpout.m[0][1] = yaxe.x;
	 mpout.m[0][2] = zaxe.x;
	 XVecDot(mpout.m[3][0], xaxe, camPos);
	 mpout.m[3][0] = -mpout.m[3][0];

	 mpout.m[1][0] = xaxe.y;
	 mpout.m[1][1] = yaxe.y;
	 mpout.m[1][2] = zaxe.y;
	 XVecDot(mpout.m[3][1], yaxe, camPos);
	 mpout.m[3][1] = -mpout.m[3][1];

	 mpout.m[2][0] = xaxe.z;
	 mpout.m[2][1] = yaxe.z;
	 mpout.m[2][2] = zaxe.z;
	 XVecDot(mpout.m[3][2], zaxe, camPos);
	 mpout.m[3][2] = -mpout.m[3][2];

	 mpout.m[0][3] = 0.0f;
	 mpout.m[1][3] = 0.0f;
	 mpout.m[2][3] = 0.0f;
	 mpout.m[3][3] = 1.0f;
}


 void XMatPerspectiveLH(XMATRIX44 &mpout, const float &FOV, const float &Aspect, const float &NearPlane, const float &FarPlane) {
	 float x, y;
	 float ang = FOV / 2.0f;

	 y = cos(ang) / sin(ang);
	 x = y / Aspect;

	 mpout.m[0][0] = x;
	 mpout.m[1][1] = y;
	 mpout.m[2][2] = FarPlane / (FarPlane - NearPlane);
	 mpout.m[2][3] = 1.0;
	 mpout.m[3][2] = -(NearPlane*FarPlane) / (FarPlane - NearPlane);
	 mpout.m[3][3] = 0.0f;

}

 void XMatPerspectiveRH(XMATRIX44 &mpout, const float &FOV, const float &Aspect, const float &NearPlane, const float &FarPlane)
{
	 float x, y;
	 float ang = FOV / 2.0f;

	 y = cos(ang) / sin(ang);
	 x = y / Aspect;

	 mpout.m[0][0] = x;
	 mpout.m[1][1] = y;
	 mpout.m[2][2] = FarPlane / (NearPlane-FarPlane);
	 mpout.m[2][3] = -1.0;
	 mpout.m[3][2] = (NearPlane*FarPlane) / (NearPlane-FarPlane);
	 mpout.m[3][3] = 0.0f;
}

 void XMatOrthoLH(XMATRIX44 &m, const float &w, const float &h, const float &zn, const float &zf) {
	m.m[0][0] = 2.0f / w;
	m.m[1][1] = 2.0f / h;
	m.m[2][2] = 1.0f / (zf - zn);
	m.m[3][2] = -zn / (zf - zn);
	m.m[3][3] = 1.0f;
}

 void XMatOrthoRH(XMATRIX44 &m, const float &w, const float &h, const float &zn, const float &zf) {
	m.m[0][0] = 2.0f / w;
	m.m[1][1] = 2.0f / h;
	m.m[2][2] = 1.0f / (zn - zf);
	m.m[3][2] = zn / (zn - zf);
	m.m[3][3] = 1.0f;
}

 void XMatRotationAxisRH(XMATRIX44 &mpout, const XVECTOR3 &axis, const float &angle) {
	float cosangle = std::cos(angle);
	float sinangle = std::sin(angle);
	float A = 1.0f - cosangle;

	mpout.m[0][0] = A*axis.x*axis.x + cosangle;
	mpout.m[1][0] = A*axis.x*axis.y + sinangle*axis.z;
	mpout.m[2][0] = A*axis.x*axis.z - sinangle*axis.y;
	mpout.m[3][0] = 0.0f;

	mpout.m[0][1] = A*axis.x*axis.y - sinangle*axis.z;
	mpout.m[1][1] = A*axis.y*axis.y + cosangle;
	mpout.m[2][1] = A*axis.y*axis.z + sinangle*axis.x;
	mpout.m[3][1] = 0.0f;

	mpout.m[0][2] = A*axis.x*axis.z + sinangle*axis.y;
	mpout.m[1][2] = A*axis.y*axis.z - sinangle*axis.x;
	mpout.m[2][2] = A*axis.z*axis.z + cosangle;
	mpout.m[3][2] = 0.0f;

	mpout.m[0][3] = mpout.m[1][3] = mpout.m[2][3] = 0.0f; mpout.m[3][3] = 1.0f;
}

 void XMatRotationAxisLH(XMATRIX44 &mpout, const XVECTOR3 &axis, const float &angle) {
	float cosangle = std::cos(angle);
	float sinangle = std::sin(angle);
	float A = 1.0f - cosangle;

	mpout.m[0][0] = A*axis.x*axis.x + cosangle;
	mpout.m[1][0] = A*axis.x*axis.y - sinangle*axis.z;
	mpout.m[2][0] = A*axis.x*axis.z + sinangle*axis.y;
	mpout.m[3][0] = 0.0f;

	mpout.m[0][1] = A*axis.x*axis.y + sinangle*axis.z;
	mpout.m[1][1] = A*axis.y*axis.y + cosangle;
	mpout.m[2][1] = A*axis.y*axis.z - sinangle*axis.x;
	mpout.m[3][1] = 0.0f;

	mpout.m[0][2] = A*axis.x*axis.z - sinangle*axis.y;
	mpout.m[1][2] = A*axis.y*axis.z + sinangle*axis.x;
	mpout.m[2][2] = A*axis.z*axis.z + cosangle;
	mpout.m[3][2] = 0.0f;

	mpout.m[0][3] = mpout.m[1][3] = mpout.m[2][3] = 0.0f; mpout.m[3][3] = 1.0f;
}

 void XVecTransformLH(XVECTOR3 &vpout, const XVECTOR3 &v, const XMATRIX44 &mat) {
	vpout.x = v.x*mat.m[0][0] + v.y*mat.m[1][0] + v.z*mat.m[2][0] + 1.0f*mat.m[3][0];
	vpout.y = v.x*mat.m[0][1] + v.y*mat.m[1][1] + v.z*mat.m[2][1] + 1.0f*mat.m[3][1];
	vpout.z = v.x*mat.m[0][2] + v.y*mat.m[1][2] + v.z*mat.m[2][2] + 1.0f*mat.m[3][2];
	vpout.w = v.x*mat.m[0][3] + v.y*mat.m[1][3] + v.z*mat.m[2][3] + 1.0f*mat.m[3][3];
}

 void XVecTransformNormalLH(XVECTOR3 &vpout, const XVECTOR3 &v, const XMATRIX44 &mat) {
	vpout.x = v.x*mat.m[0][0] + v.y*mat.m[1][0] + v.z*mat.m[2][0];
	vpout.y = v.x*mat.m[0][1] + v.y*mat.m[1][1] + v.z*mat.m[2][1];
	vpout.z = v.x*mat.m[0][2] + v.y*mat.m[1][2] + v.z*mat.m[2][2];
	vpout.w = 1.0f;
}

 void XVecTransformRH(XVECTOR3 &vpout, const XVECTOR3 &v, const XMATRIX44 &mat) {
	vpout.x = v.x*mat.m[0][0] + v.y*mat.m[0][1] + v.z*mat.m[0][2] + 1.0f*mat.m[0][3];
	vpout.y = v.x*mat.m[1][0] + v.y*mat.m[1][1] + v.z*mat.m[1][2] + 1.0f*mat.m[1][3];
	vpout.z = v.x*mat.m[2][0] + v.y*mat.m[2][1] + v.z*mat.m[2][2] + 1.0f*mat.m[2][3];
	vpout.w = v.x*mat.m[3][0] + v.y*mat.m[3][1] + v.z*mat.m[3][2] + 1.0f*mat.m[3][3];
}

 void XVecTransformNormalRH(XVECTOR3 &vpout, const XVECTOR3 &v, const XMATRIX44 &mat) {
	vpout.x = v.x*mat.m[0][0] + v.y*mat.m[0][1] + v.z*mat.m[0][2];
	vpout.y = v.x*mat.m[1][0] + v.y*mat.m[1][1] + v.z*mat.m[1][2];
	vpout.z = v.x*mat.m[2][0] + v.y*mat.m[2][1] + v.z*mat.m[2][2];
	vpout.w = 1.0f;
}

 void XVecDot(float &f, const XVECTOR3 &v1, const XVECTOR3 &v2) {
	f = v1.x*v2.x + v1.y*v2.y + v1.z*v2.z;
}

 void XVecLength(float &f, const XVECTOR3 &v) {
	f = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
}

 void XVecCross(XVECTOR3 &vpout, const XVECTOR3 &lv, const XVECTOR3 &rv) {
	vpout.x = lv.y*rv.z - lv.z*rv.y;
	vpout.y = lv.z*rv.x - lv.x*rv.z;
	vpout.z = lv.x*rv.y - lv.y*rv.x;
}

 void XVec3Normalize(XVECTOR3 &pOut, const XVECTOR3 &pV) {
	float v = 1.0f / sqrtf(pV.x * pV.x + pV.y * pV.y + pV.z * pV.z);
	pOut.x = pV.x * v;
	pOut.y = pV.y * v;
	pOut.z = pV.z * v;
}

 XVECTOR2& XVECTOR2::operator += (const XVECTOR2& v)
{
	x += v.x;
	y += v.y;
	return *this;
}

 XVECTOR2& XVECTOR2::operator -= (const XVECTOR2& v)
{
	x -= v.x;
	y -= v.y;
	return *this;
}

 XVECTOR2& XVECTOR2::operator *= (float f)
{
	x *= f;
	y *= f;
	return *this;
}

 XVECTOR2& XVECTOR2::operator /= (float f)
{
	x /= f;
	y /= f;
	return *this;
}

 XVECTOR2 XVECTOR2::operator + () const
{
	return *this;
}

 XVECTOR2 XVECTOR2::operator - () const
{
	return XVECTOR2(-x, -y);
}

 XVECTOR2 XVECTOR2::operator + (const XVECTOR2& v) const
{
	return XVECTOR2(x + v.x, y + v.y);
}

 XVECTOR2 XVECTOR2::operator - (const XVECTOR2& v) const
{
	return XVECTOR2(x - v.x, y - v.y);
}

 XVECTOR2 XVECTOR2::operator * (float f) const
{
	return XVECTOR2(x*f, y*f);
}

 XVECTOR2 XVECTOR2::operator / (float f) const
{
	return XVECTOR2(x / f, y / f);
}

 bool XVECTOR2::operator == (const XVECTOR2& v) const
{
	return x == v.x && y == v.y;
}

 bool XVECTOR2::operator != (const XVECTOR2& v) const
{
	return x != v.x || y != v.y;
}

 void XVECTOR2::Normalize()
{
	float mod = sqrt(x*x + y*y);
	x /= mod;
	y /= mod;
}

 XVECTOR2::operator float* ()
{
	return (float *)&x;
}

 XVECTOR2::operator const float* () const
{
	return (const float *)&x;
}

 float XVECTOR2::Length()
{
	return sqrt(x*x + y*y);
}

 XVECTOR3::operator float* ()
{
	return (float *)&x;
}

 XVECTOR3::operator const float* () const
{
	return (const float *)&x;
}

 XVECTOR3& XVECTOR3::operator += (const XVECTOR3& v)
{
	x += v.x;
	y += v.y;
	z += v.z;
	return *this;
}

 XVECTOR3& XVECTOR3::operator -= (const XVECTOR3& v)
{
	x -= v.x;
	y -= v.y;
	z -= v.z;
	return *this;
}

 XVECTOR3& XVECTOR3::operator *= (float f)
{
	x *= f;
	y *= f;
	z *= f;
	return *this;
}

 XVECTOR3& XVECTOR3::operator /= (float f)
{
	x /= f;
	y /= f;
	z /= f;
	return *this;
}


 XVECTOR3 XVECTOR3::operator + () const
{
	return *this;
}

 XVECTOR3 XVECTOR3::operator - () const
{
	return	XVECTOR3(-x, -y, -z);
}

 XVECTOR3 operator * (float f, const struct XVECTOR3& v)
{
	return XVECTOR3(f * v.x, f * v.y, f * v.z);
}

 XVECTOR3 XVECTOR3::operator * (const XVECTOR3& v)
{
	return XVECTOR3((this->x*v.x), (this->y*v.y), (this->z*v.z));
}

 XVECTOR3 XVECTOR3::operator + (const XVECTOR3& v) const
{
	return XVECTOR3(x + v.x, y + v.y, z + v.z);
}

 XVECTOR3 XVECTOR3::operator - (const XVECTOR3& v) const
{
	return XVECTOR3(x - v.x, y - v.y, z - v.z);
}

 XVECTOR3 XVECTOR3::operator * (float f) const
{
	return XVECTOR3(x*f, y*f, z*f);
}

 XVECTOR3 XVECTOR3::operator / (float f) const
{
	return XVECTOR3(x / f, y / f, z / f);
}

 bool XVECTOR3::operator == (const XVECTOR3& v) const
{
	return (x == v.x && y == v.y && z == v.z);
}

 bool XVECTOR3::operator != (const XVECTOR3& v) const
{
	return (x != v.x || y != v.y || z != v.z);
}

 void XVECTOR3::Normalize()
{
	float mod = sqrt(x*x + y*y + z*z);
	x /= mod;
	y /= mod;
	z /= mod;
}

 float XVECTOR3::Length()
{
	return sqrt(x*x + y*y + z*z);
}

 XMATRIX44_64::XMATRIX44_64() {
	 this->m[0][0] = 1.0f;
	 this->m[0][1] = 0.0f;
	 this->m[0][2] = 0.0f;
	 this->m[0][3] = 0.0f;

	 this->m[1][0] = 0.0f;
	 this->m[1][1] = 1.0f;
	 this->m[1][2] = 0.0f;
	 this->m[1][3] = 0.0f;

	 this->m[2][0] = 0.0f;
	 this->m[2][1] = 0.0f;
	 this->m[2][2] = 1.0f;
	 this->m[2][3] = 0.0f;

	 this->m[3][0] = 0.0f;
	 this->m[3][1] = 0.0f;
	 this->m[3][2] = 0.0f;
	 this->m[3][3] = 1.0f;
 }

 XMATRIX44_64::XMATRIX44_64(XMATRIX44* im) {
	 for (int i = 0; i < 16; i++) {
		 mat[i] = static_cast<double>((*im).mat[i]);
	 }
 }

 XMATRIX44& XMATRIX44::operator *= (const XMATRIX44& mb)
{
	float a00 = this->m[0][0], a01 = this->m[0][1], a02 = this->m[0][2], a03 = this->m[0][3],
		a10 = this->m[1][0], a11 = this->m[1][1], a12 = this->m[1][2], a13 = this->m[1][3],
		a20 = this->m[2][0], a21 = this->m[2][1], a22 = this->m[2][2], a23 = this->m[2][3],
		a30 = this->m[3][0], a31 = this->m[3][1], a32 = this->m[3][2], a33 = this->m[3][3];

	float b00 = mb.m[0][0], b01 = mb.m[0][1], b02 = mb.m[0][2], b03 = mb.m[0][3],
		b10 = mb.m[1][0], b11 = mb.m[1][1], b12 = mb.m[1][2], b13 = mb.m[1][3],
		b20 = mb.m[2][0], b21 = mb.m[2][1], b22 = mb.m[2][2], b23 = mb.m[2][3],
		b30 = mb.m[3][0], b31 = mb.m[3][1], b32 = mb.m[3][2], b33 = mb.m[3][3];

	this->m[0][0] = a00*b00 + a01*b10 + a02*b20 + a03*b30;
	this->m[0][1] = a00*b01 + a01*b11 + a02*b21 + a03*b31;
	this->m[0][2] = a00*b02 + a01*b12 + a02*b22 + a03*b32;
	this->m[0][3] = a00*b03 + a01*b13 + a02*b23 + a03*b33;
	this->m[1][0] = a10*b00 + a11*b10 + a12*b20 + a13*b30;
	this->m[1][1] = a10*b01 + a11*b11 + a12*b21 + a13*b31;
	this->m[1][2] = a10*b02 + a11*b12 + a12*b22 + a13*b32;
	this->m[1][3] = a10*b03 + a11*b13 + a12*b23 + a13*b33;
	this->m[2][0] = a20*b00 + a21*b10 + a22*b20 + a23*b30;
	this->m[2][1] = a20*b01 + a21*b11 + a22*b21 + a23*b31;
	this->m[2][2] = a20*b02 + a21*b12 + a22*b22 + a23*b32;
	this->m[2][3] = a20*b03 + a21*b13 + a22*b23 + a23*b33;
	this->m[3][0] = a30*b00 + a31*b10 + a32*b20 + a33*b30;
	this->m[3][1] = a30*b01 + a31*b11 + a32*b21 + a33*b31;
	this->m[3][2] = a30*b02 + a31*b12 + a32*b22 + a33*b32;
	this->m[3][3] = a30*b03 + a31*b13 + a32*b23 + a33*b33;
	return *this;
}

 XMATRIX44& XMATRIX44::operator += (const XMATRIX44& mb)
{
	m11 += mb.m11; m12 += mb.m12; m13 += mb.m13; m14 += mb.m14;
	m21 += mb.m21; m22 += mb.m22; m23 += mb.m23; m24 += mb.m24;
	m31 += mb.m31; m32 += mb.m32; m33 += mb.m33; m34 += mb.m34;
	m41 += mb.m41; m42 += mb.m42; m43 += mb.m43; m44 += mb.m44;
	return *this;
}

 XMATRIX44& XMATRIX44::operator -= (const XMATRIX44& mb)
{
	m11 -= mb.m11; m12 -= mb.m12; m13 -= mb.m13; m14 -= mb.m14;
	m21 -= mb.m21; m22 -= mb.m22; m23 -= mb.m23; m24 -= mb.m24;
	m31 -= mb.m31; m32 -= mb.m32; m33 -= mb.m33; m34 -= mb.m34;
	m41 -= mb.m41; m42 -= mb.m42; m43 -= mb.m43; m44 -= mb.m44;
	return *this;
}

 XMATRIX44::operator float* ()
{
	return (float *)&m11;
}


 XMATRIX44::operator const float* () const
{
	return (const float *)&m11;
}

 XMATRIX44& XMATRIX44::operator *= (float f)
{
	m11 *= f; m12 *= f; m13 *= f; m14 *= f;
	m21 *= f; m22 *= f; m23 *= f; m24 *= f;
	m31 *= f; m32 *= f; m33 *= f; m34 *= f;
	m41 *= f; m42 *= f; m43 *= f; m44 *= f;
	return *this;
}

 XMATRIX44& XMATRIX44::operator /= (float f)
{
	m11 /= f; m12 /= f; m13 /= f; m14 /= f;
	m21 /= f; m22 /= f; m23 /= f; m24 /= f;
	m31 /= f; m32 /= f; m33 /= f; m34 /= f;
	m41 /= f; m42 /= f; m43 /= f; m44 /= f;
	return *this;
}

 XMATRIX44 XMATRIX44::operator + () const
{
	return *this;
}

 XMATRIX44 XMATRIX44::operator - () const
{
	return XMATRIX44(-m11, -m12, -m13, -m14,
		-m21, -m22, -m23, -m24,
		-m31, -m32, -m33, -m34,
		-m41, -m42, -m43, -m44);
}

 XMATRIX44 XMATRIX44::operator * (const XMATRIX44& mb) const
{
	XMATRIX44	tmp;

	float a00 = this->m[0][0], a01 = this->m[0][1], a02 = this->m[0][2], a03 = this->m[0][3],
		a10 = this->m[1][0], a11 = this->m[1][1], a12 = this->m[1][2], a13 = this->m[1][3],
		a20 = this->m[2][0], a21 = this->m[2][1], a22 = this->m[2][2], a23 = this->m[2][3],
		a30 = this->m[3][0], a31 = this->m[3][1], a32 = this->m[3][2], a33 = this->m[3][3];

	float b00 = mb.m[0][0], b01 = mb.m[0][1], b02 = mb.m[0][2], b03 = mb.m[0][3],
		b10 = mb.m[1][0], b11 = mb.m[1][1], b12 = mb.m[1][2], b13 = mb.m[1][3],
		b20 = mb.m[2][0], b21 = mb.m[2][1], b22 = mb.m[2][2], b23 = mb.m[2][3],
		b30 = mb.m[3][0], b31 = mb.m[3][1], b32 = mb.m[3][2], b33 = mb.m[3][3];

	tmp.m[0][0] = a00*b00 + a01*b10 + a02*b20 + a03*b30;
	tmp.m[0][1] = a00*b01 + a01*b11 + a02*b21 + a03*b31;
	tmp.m[0][2] = a00*b02 + a01*b12 + a02*b22 + a03*b32;
	tmp.m[0][3] = a00*b03 + a01*b13 + a02*b23 + a03*b33;
	tmp.m[1][0] = a10*b00 + a11*b10 + a12*b20 + a13*b30;
	tmp.m[1][1] = a10*b01 + a11*b11 + a12*b21 + a13*b31;
	tmp.m[1][2] = a10*b02 + a11*b12 + a12*b22 + a13*b32;
	tmp.m[1][3] = a10*b03 + a11*b13 + a12*b23 + a13*b33;
	tmp.m[2][0] = a20*b00 + a21*b10 + a22*b20 + a23*b30;
	tmp.m[2][1] = a20*b01 + a21*b11 + a22*b21 + a23*b31;
	tmp.m[2][2] = a20*b02 + a21*b12 + a22*b22 + a23*b32;
	tmp.m[2][3] = a20*b03 + a21*b13 + a22*b23 + a23*b33;
	tmp.m[3][0] = a30*b00 + a31*b10 + a32*b20 + a33*b30;
	tmp.m[3][1] = a30*b01 + a31*b11 + a32*b21 + a33*b31;
	tmp.m[3][2] = a30*b02 + a31*b12 + a32*b22 + a33*b32;
	tmp.m[3][3] = a30*b03 + a31*b13 + a32*b23 + a33*b33;

	return tmp;
}

 XMATRIX44 XMATRIX44::operator + (const XMATRIX44& mat) const
{
	return XMATRIX44(m11 + mat.m11, m12 + mat.m12, m13 + mat.m13, m14 + mat.m14,
		m21 + mat.m21, m22 + mat.m22, m23 + mat.m23, m24 + mat.m24,
		m31 + mat.m31, m32 + mat.m32, m33 + mat.m33, m34 + mat.m34,
		m41 + mat.m41, m42 + mat.m42, m43 + mat.m43, m44 + mat.m44);
}

 XMATRIX44 XMATRIX44::operator - (const XMATRIX44& mat) const
{
	return XMATRIX44(m11 - mat.m11, m12 - mat.m12, m13 - mat.m13, m14 - mat.m14,
		m21 - mat.m21, m22 - mat.m22, m23 - mat.m23, m24 - mat.m24,
		m31 - mat.m31, m32 - mat.m32, m33 - mat.m33, m34 - mat.m34,
		m41 - mat.m41, m42 - mat.m42, m43 - mat.m43, m44 - mat.m44);
}

 XMATRIX44 operator * (float f, const XMATRIX44& mat)
{
	return XMATRIX44(f * mat.m11, f * mat.m12, f * mat.m13, f * mat.m14,
		f * mat.m21, f * mat.m22, f * mat.m23, f * mat.m24,
		f * mat.m31, f * mat.m32, f * mat.m33, f * mat.m34,
		f * mat.m41, f * mat.m42, f * mat.m43, f * mat.m44);
}

 XMATRIX44 XMATRIX44::operator * (float f) const
{
	return XMATRIX44(m11*f, m12*f, m13*f, m14*f,
		m21*f, m22*f, m23*f, m24*f,
		m31*f, m32*f, m33*f, m34*f,
		m41*f, m42*f, m43*f, m44*f);
}

 XMATRIX44 XMATRIX44::operator / (float f) const
{
	return XMATRIX44(m11 / f, m12 / f, m13 / f, m14 / f,
		m21 / f, m22 / f, m23 / f, m24 / f,
		m31 / f, m32 / f, m33 / f, m34 / f,
		m41 / f, m42 / f, m43 / f, m44 / f);
}

 bool XMATRIX44::operator == (const XMATRIX44& mat) const
{
	return 0 == memcmp(this, &mat, sizeof(XMATRIX44));
}
 bool XMATRIX44::operator != (const XMATRIX44& mat) const
{
	return 0 != memcmp(this, &mat, sizeof(XMATRIX44));
}

 void XMATRIX44::Inverse(XMATRIX44 *o) {
#if DOUBLE_FOR_INVERSE
	 XMATRIX44_64 tmp64 = XMATRIX44_64(this);
	 double det =
#else
	 XMATRIX44 tmp64 = *this;
	 float det =
#endif
		 tmp64.m11*tmp64.m22*tmp64.m33*tmp64.m44 + tmp64.m11*tmp64.m23*tmp64.m34*tmp64.m42 + tmp64.m11*tmp64.m24*tmp64.m32*tmp64.m43
				 + tmp64.m12*tmp64.m21*tmp64.m34*tmp64.m43 + tmp64.m12*tmp64.m23*tmp64.m31*tmp64.m44 + tmp64.m12*tmp64.m24*tmp64.m33*tmp64.m41
				 + tmp64.m13*tmp64.m21*tmp64.m32*tmp64.m44 + tmp64.m13*tmp64.m22*tmp64.m34*tmp64.m41 + tmp64.m13*tmp64.m24*tmp64.m31*tmp64.m42
				 + tmp64.m14*tmp64.m21*tmp64.m33*tmp64.m42 + tmp64.m14*tmp64.m22*tmp64.m31*tmp64.m43 + tmp64.m14*tmp64.m23*tmp64.m32*tmp64.m41
				 - tmp64.m11*tmp64.m22*tmp64.m34*tmp64.m43 - tmp64.m11*tmp64.m23*tmp64.m32*tmp64.m44 - tmp64.m11*tmp64.m24*tmp64.m33*tmp64.m42
				 - tmp64.m12*tmp64.m21*tmp64.m33*tmp64.m44 - tmp64.m12*tmp64.m23*tmp64.m34*tmp64.m41 - tmp64.m12*tmp64.m24*tmp64.m31*tmp64.m43
				 - tmp64.m13*tmp64.m21*tmp64.m34*tmp64.m42 - tmp64.m13*tmp64.m22*tmp64.m31*tmp64.m44 - tmp64.m13*tmp64.m24*tmp64.m32*tmp64.m41
				 - tmp64.m14*tmp64.m21*tmp64.m32*tmp64.m43 - tmp64.m14*tmp64.m22*tmp64.m33*tmp64.m41 - tmp64.m14*tmp64.m23*tmp64.m31*tmp64.m42;
#if DOUBLE_FOR_INVERSE
	 if (det == 0.0) {
#else
	 if (det == 0.0f) {
#endif
		 printf("\n\nThe Universe stopped, no det for Matrix, all ends here.\n\n");
		 exit(666);
	 }
#if DOUBLE_FOR_INVERSE
	 det = 1.0 / det;
	 XMATRIX44_64 out;
#else
	 det = 1.0f / det;
	 XMATRIX44 out;
#endif

	 out.m11 = tmp64.m22*tmp64.m33*tmp64.m44 + tmp64.m23*tmp64.m34*tmp64.m42 + tmp64.m24*tmp64.m32*tmp64.m43 - tmp64.m22*tmp64.m34*tmp64.m43 - tmp64.m23*tmp64.m32*tmp64.m44 - tmp64.m24*tmp64.m33*tmp64.m42;
	 out.m12 = tmp64.m12*tmp64.m34*tmp64.m43 + tmp64.m13*tmp64.m32*tmp64.m44 + tmp64.m14*tmp64.m33*tmp64.m42 - tmp64.m12*tmp64.m33*tmp64.m44 - tmp64.m13*tmp64.m34*tmp64.m42 - tmp64.m14*tmp64.m32*tmp64.m43;
	 out.m13 = tmp64.m12*tmp64.m23*tmp64.m44 + tmp64.m13*tmp64.m24*tmp64.m42 + tmp64.m14*tmp64.m22*tmp64.m43 - tmp64.m12*tmp64.m24*tmp64.m43 - tmp64.m13*tmp64.m22*tmp64.m44 - tmp64.m14*tmp64.m23*tmp64.m42;
	 out.m14 = tmp64.m12*tmp64.m24*tmp64.m33 + tmp64.m13*tmp64.m22*tmp64.m34 + tmp64.m14*tmp64.m23*tmp64.m32 - tmp64.m12*tmp64.m23*tmp64.m34 - tmp64.m13*tmp64.m24*tmp64.m32 - tmp64.m14*tmp64.m22*tmp64.m33;
	 out.m21 = tmp64.m21*tmp64.m34*tmp64.m43 + tmp64.m23*tmp64.m31*tmp64.m44 + tmp64.m24*tmp64.m33*tmp64.m41 - tmp64.m21*tmp64.m33*tmp64.m44 - tmp64.m23*tmp64.m34*tmp64.m41 - tmp64.m24*tmp64.m31*tmp64.m43;
	 out.m22 = tmp64.m11*tmp64.m33*tmp64.m44 + tmp64.m13*tmp64.m34*tmp64.m41 + tmp64.m14*tmp64.m31*tmp64.m43 - tmp64.m11*tmp64.m34*tmp64.m43 - tmp64.m13*tmp64.m31*tmp64.m44 - tmp64.m14*tmp64.m33*tmp64.m41;
	 out.m23 = tmp64.m11*tmp64.m24*tmp64.m43 + tmp64.m13*tmp64.m21*tmp64.m44 + tmp64.m14*tmp64.m23*tmp64.m41 - tmp64.m11*tmp64.m23*tmp64.m44 - tmp64.m13*tmp64.m24*tmp64.m41 - tmp64.m14*tmp64.m21*tmp64.m43;
	 out.m24 = tmp64.m11*tmp64.m23*tmp64.m34 + tmp64.m13*tmp64.m24*tmp64.m31 + tmp64.m14*tmp64.m21*tmp64.m33 - tmp64.m11*tmp64.m24*tmp64.m33 - tmp64.m13*tmp64.m21*tmp64.m34 - tmp64.m14*tmp64.m23*tmp64.m31;
	 out.m31 = tmp64.m21*tmp64.m32*tmp64.m44 + tmp64.m22*tmp64.m34*tmp64.m41 + tmp64.m24*tmp64.m31*tmp64.m42 - tmp64.m21*tmp64.m34*tmp64.m42 - tmp64.m22*tmp64.m31*tmp64.m44 - tmp64.m24*tmp64.m32*tmp64.m41;
	 out.m32 = tmp64.m11*tmp64.m34*tmp64.m42 + tmp64.m12*tmp64.m31*tmp64.m44 + tmp64.m14*tmp64.m32*tmp64.m41 - tmp64.m11*tmp64.m32*tmp64.m44 - tmp64.m12*tmp64.m34*tmp64.m41 - tmp64.m14*tmp64.m31*tmp64.m42;
	 out.m33 = tmp64.m11*tmp64.m22*tmp64.m44 + tmp64.m12*tmp64.m24*tmp64.m41 + tmp64.m14*tmp64.m21*tmp64.m42 - tmp64.m11*tmp64.m24*tmp64.m42 - tmp64.m12*tmp64.m21*tmp64.m44 - tmp64.m14*tmp64.m22*tmp64.m41;
	 out.m34 = tmp64.m11*tmp64.m24*tmp64.m32 + tmp64.m12*tmp64.m21*tmp64.m34 + tmp64.m14*tmp64.m22*tmp64.m31 - tmp64.m11*tmp64.m22*tmp64.m34 - tmp64.m12*tmp64.m24*tmp64.m31 - tmp64.m14*tmp64.m21*tmp64.m32;
	 out.m41 = tmp64.m21*tmp64.m33*tmp64.m42 + tmp64.m22*tmp64.m31*tmp64.m43 + tmp64.m23*tmp64.m32*tmp64.m41 - tmp64.m21*tmp64.m32*tmp64.m43 - tmp64.m22*tmp64.m33*tmp64.m41 - tmp64.m23*tmp64.m31*tmp64.m42;
	 out.m42 = tmp64.m11*tmp64.m32*tmp64.m43 + tmp64.m12*tmp64.m33*tmp64.m41 + tmp64.m13*tmp64.m31*tmp64.m42 - tmp64.m11*tmp64.m33*tmp64.m42 - tmp64.m12*tmp64.m31*tmp64.m43 - tmp64.m13*tmp64.m32*tmp64.m41;
	 out.m43 = tmp64.m11*tmp64.m23*tmp64.m42 + tmp64.m12*tmp64.m21*tmp64.m43 + tmp64.m13*tmp64.m22*tmp64.m41 - tmp64.m11*tmp64.m22*tmp64.m43 - tmp64.m12*tmp64.m23*tmp64.m41 - tmp64.m13*tmp64.m21*tmp64.m42;
	 out.m44 = tmp64.m11*tmp64.m22*tmp64.m33 + tmp64.m12*tmp64.m23*tmp64.m31 + tmp64.m13*tmp64.m21*tmp64.m32 - tmp64.m11*tmp64.m23*tmp64.m32 - tmp64.m12*tmp64.m21*tmp64.m33 - tmp64.m13*tmp64.m22*tmp64.m31;

	 for (int i = 0; i < 16; i++) {
#if DOUBLE_FOR_INVERSE
		double val = det*out.mat[i];
		(*o).mat[i] = static_cast<float>(val);
#else
		(*o).mat[i] = det*out.mat[i];
#endif
	 }
 }


#if ATOF_ANGEL
#define  inicioNumeros 48
 float CadenaAnumeroFlotantePersonalizada(char *c) {
	 float numero = 0.0f;
	 int index = 0;
	 char *ptr = c;

	 //Buscando si tiene notacion exponencial
	 while (ptr[index] != '\0') {
		 if (ptr[index] == 'e') {
			 return numero;
		 }
		 index++;
	 }

	 index = 0;
	 bool negativo = ptr[index] == '-';
	 if (negativo) {
		 index++;
	 }

	 int point = 0;
	 int last_index = index;
	 while (ptr[index] != '\0') {
		 if (ptr[index] == '.') {
			 point = index;
			 break;
		 }
		 index++;
	 }
	 point--;

	 float num = 0.0f;
	 float multiplicador = 1.0f;

	 int i = point;
	 do {
		 numero += (ptr[i] - inicioNumeros)*multiplicador;
		 multiplicador *= 10.0f;
		 i--;
	 } while (i >= last_index && i >= 0);


	 index = last_index;

	 if (ptr[index] != '\0') {
		 while (ptr[index] != '\0') {
			 index++;
		 }
		 multiplicador = 1.0f;
		 for (i = index; i > (point + 2); i--) {
			 num += (ptr[i - 1] - inicioNumeros)*multiplicador;
			 multiplicador *= 10.0f;
		 }
		 num /= multiplicador;
		 numero += num;
	 }

	 if (negativo) {
		 numero = -numero;
	 }

	 return numero;
 }
#endif
