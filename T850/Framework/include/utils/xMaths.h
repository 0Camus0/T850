#ifndef CA_MATHS_H
#define CA_MATHS_H

#include <Config.h>

#include <algorithm>
#include <iostream>
#include <cmath>

#include <string.h>
#include <math.h>
#include <iostream>
#include <vector>


#ifndef DOUBLE_FOR_INVERSE
#define DOUBLE_FOR_INVERSE 1
#endif


#ifndef ATOF_ANGEL
#define ATOF_ANGEL 1
#endif

#ifndef ATOF_TOM_VAN 
#define ATOF_TOM_VAN  0
#endif

#ifndef ATOF_VS 
#define ATOF_VS 0
#endif

#if ATOF_VS

#ifndef ATOF_ANGEL
#undef ATOF_ANGEL
#define ATOF_ANGEL 0
#endif
#ifndef ATOF_TOM_VAN
#undef ATOF_TOM_VAN
#define ATOF_TOM_VAN  0
#endif
#endif


#if (ATOF_ANGEL) || (ATOF_TOM_VAN)

#ifdef atof
#undef atof
#endif

#if ATOF_ANGEL
#define atof(a) CadenaAnumeroFlotantePersonalizada(a)
#elif ATOF_TOM_VAN
#define atof(a) atof_tom(a)
#endif

#endif

#if ATOF_TOM_VAN
#define white_space(c) ((c) == ' ' || (c) == '\t')
#define valid_digit(c) ((c) >= '0' && (c) <= '9')
double atof_tom(const char *p);
#endif

#if ATOF_ANGEL
#define  inicioNumeros 48
float CadenaAnumeroFlotantePersonalizada(char *c);
#endif

#ifndef xNULL
#define  xNULL 0
#endif

#ifndef xPI
#define  xPI 3.141592653589f
#endif

#ifndef xHALF_PI
#define  xHALF_PI 1.57079632679f
#endif

#ifndef xSMALLFLOAT
#define  xSMALLFLOAT 0.000000001f
#endif

#if defined(WIN32) || defined(_WIN32)
#pragma warning( disable : 4521 )
#pragma warning( disable : 4522 )
#endif

#ifndef Rad2Deg
#define Rad2Deg( r ) ((r) * (180.0f / xPI))
#endif

#ifndef Deg2Rad
#define Deg2Rad( d ) ((d) * (xPI / 180.0f))
#endif


#ifndef xmax
#define xmax(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef xmin
#define xmin(a,b)            (((a) < (b)) ? (a) : (b))
#endif

#define USE_LEFT_HANDED 1 // SIMULATE D3DXMATRIX ROTATIONS

#if USE_LEFT_HANDED
#define XMatRotationX		XMatRotationXLH
#define XMatRotationY		XMatRotationYLH
#define XMatRotationZ		XMatRotationZLH
#define XMatViewLookAt		XMatViewLookAtLH
#define XMatPerspective		XMatPerspectiveLH
#define XMatOrtho			XMatOrthoLH
#define XMatRotationAxis	XMatRotationAxisLH
#define XMatOrtho			XMatOrthoLH
#define XVecTransform		XVecTransformLH
#define XVecTransformNormal	XVecTransformNormalLH
#else
#define XMatRotationX		XMatRotationXRH
#define XMatRotationY		XMatRotationYRH
#define XMatRotationZ		XMatRotationZRH
#define XMatViewLookAt		XMatViewLookAtRH
#define XMatPerspective		XMatPerspectiveRH
#define XMatOrtho			XMatOrthoRH
#define XMatRotationAxis	XMatRotationAxisRH
#define XMatOrtho			XMatOrthoRH
#define XVecTransform		XVecTransformRH
#define XVecTransformNormal	XVecTransformNormalRH
#endif
		struct pair_ {
		public:
			pair_() : x(0.0f), y(0.0f) {}

			float x, y;
		};

		struct sample_ {
		public:
			sample_() : weight(0.0f) {}
			sample_(std::vector<pair_> p_, float w) : p(p_), weight(w) {}
			std::vector<pair_> p;
			float weight;
		};

		float distribution(float x, float mu, float sigma);
		std::vector<pair_> sampleInterval(float minInclusive, float maxInclusive, float sampleCount, float sigma);
		float integration(std::vector<pair_> samples);
		float roundTo(float num, float decimals);
		std::vector<sample_> UpdateKernel(float sigma, float kernelSize, float Samplecount);

		struct XMATRIX44;
		struct XVECTOR2;
		struct XVECTOR3;

		void XMatMultiply(XMATRIX44 &, const XMATRIX44 &, const XMATRIX44 &);
		void XMatTranslation(XMATRIX44 &, const float &, const float &, const float &);
		void XMatTranslation(XMATRIX44 &, XVECTOR3&);
		void XMatScaling(XMATRIX44 &, const float &, const float &, const float &);
		void XMatRotationXLH(XMATRIX44 &, const float &);
		void XMatRotationXRH(XMATRIX44 &, const float &);
		void XMatRotationYLH(XMATRIX44 &, const float &);
		void XMatRotationYRH(XMATRIX44 &, const float &);
		void XMatRotationZLH(XMATRIX44 &, const float &);
		void XMatRotationZRH(XMATRIX44 &, const float &);
		void XMatTranspose(XMATRIX44 &,  XMATRIX44 );
		void XMatIdentity(XMATRIX44 &);
		void XMatViewLookAtLH(XMATRIX44 &, const XVECTOR3 &, const XVECTOR3 &, const XVECTOR3 &);
		void XMatViewLookAtRH(XMATRIX44 &, const XVECTOR3 &, const XVECTOR3 &, const XVECTOR3 &);
		void XMatPerspectiveLH(XMATRIX44 &, const float &, const float &, const float &, const float &);
		void XMatPerspectiveRH(XMATRIX44 &, const float &, const float &, const float &, const float &);
		void XMatOrthoLH(XMATRIX44 &, const float &, const float &, const float &, const float &);
		void XMatOrthoRH(XMATRIX44 &, const float &, const float &, const float &, const float &);
		void XMatRotationAxisRH(XMATRIX44 &, const XVECTOR3 &, const float &);
		void XMatRotationAxisLH(XMATRIX44 &, const XVECTOR3 &, const float &);

		void XVecTransformLH(XVECTOR3 &, const XVECTOR3 &, const XMATRIX44 &);
		void XVecTransformNormalLH(XVECTOR3 &, const XVECTOR3 &, const XMATRIX44 &);

		void XVecTransformRH(XVECTOR3 &, const XVECTOR3 &, const XMATRIX44 &);
		void XVecTransformNormalRH(XVECTOR3 &, const XVECTOR3 &, const XMATRIX44 &);

		void XVecDot(float &, const XVECTOR3 &, const XVECTOR3 &);
		void XVecLength(float &, const XVECTOR3 &v);
		void XVecCross(XVECTOR3 &, const XVECTOR3 &, const XVECTOR3 &);
		void XVec3Normalize(XVECTOR3 &, const XVECTOR3 &);
		


		typedef unsigned int	std_uint;

		struct XVECTOR2 {
			XVECTOR2() :x(0.0f), y(0.0f) {}
			XVECTOR2(const float *vf) :x(vf[0]), y(vf[1]) {}
			XVECTOR2(const XVECTOR2& v) :x(v.x), y(v.y) {}
			XVECTOR2(const float &xp, const float &yp) :x(xp), y(yp) {}

			XVECTOR2 & operator= (const XVECTOR2 & other) {
				this->x = other.x; this->y = other.y;
				return *this;
			}
			XVECTOR2 & operator= (XVECTOR2 & other) {
				this->x = other.x; this->y = other.y;
				return *this;
			}

			operator float* ();
			operator const float* () const;

			XVECTOR2& operator += (const XVECTOR2&);
			XVECTOR2& operator -= (const XVECTOR2&);
			XVECTOR2& operator *= (float);
			XVECTOR2& operator /= (float);

			XVECTOR2 operator + () const;
			XVECTOR2 operator - () const;

			XVECTOR2 operator + (const XVECTOR2&) const;
			XVECTOR2 operator - (const XVECTOR2&) const;
			XVECTOR2 operator * (float) const;
			XVECTOR2 operator / (float) const;

			bool operator == (const XVECTOR2&) const;
			bool operator != (const XVECTOR2&) const;

			void Normalize();
			float Length();

			union {
				float	 v[2];

				struct {
					float x;
					float y;
				};
			};
		};

		//Vector 3D

		struct _XVECTOR3 {
			float x, y, z;
		};

		struct XVECTOR3 {
			XVECTOR3() :x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
			XVECTOR3(const float *vf) :x(vf[0]), y(vf[1]), z(vf[2]) { w = 1.0f; }
			XVECTOR3(const XVECTOR3& v) :x(v.x), y(v.y), z(v.z), w(1.0f) {}
			XVECTOR3(const _XVECTOR3& v) :x(v.x), y(v.y), z(v.z), w(1.0f) {}
			
			template<typename T>
			XVECTOR3(T &xp, T &yp, T &zp) :x((float)xp), y((float)yp), z((float)zp), w(1.0f) {  }
			template<typename T>
			XVECTOR3(T &xp, T &yp, T &zp, T &wp) :x((float)xp), y((float)yp), z((float)zp), w(wp) { }
			template<typename T>
			XVECTOR3(const T &xp, const T &yp, const T &zp) :x((float)xp), y((float)yp), z((float)zp), w(1.0f) {  }
			template<typename T>
			XVECTOR3(const T &xp, const T &yp, const T &zp, const T &wp) :x((float)xp), y((float)yp), z((float)zp), w((float)wp) { }
			/*
			XVECTOR3(int &xp, int &yp, int &zp) :x((float)xp), y((float)yp), z((float)zp), w(1.0f) {  }
			XVECTOR3(int &xp, int &yp, int &zp, int &wp) :x((float)xp), y((float)yp), z((float)zp), w((float)wp) { }
			XVECTOR3(const int &xp, const int &yp, const int &zp) :x((float)xp), y((float)yp), z((float)zp), w(1.0f) {  }
			XVECTOR3(const int &xp, const int &yp, const int &zp, const int &wp) :x((float)xp), y((float)yp), z((float)zp), w((float)wp) { }

			XVECTOR3(float &xp, float &yp, float &zp) :x(xp), y(yp), z(zp), w(1.0f) {  }
			XVECTOR3(float &xp, float &yp, float &zp, float &wp) :x(xp), y(yp), z(zp), w(wp) { }
			XVECTOR3(const float &xp, const float &yp, const float &zp) :x(xp), y(yp), z(zp), w(1.0f){  }
			XVECTOR3(const float &xp, const float &yp, const float &zp,const float &wp) :x(xp), y(yp), z(zp), w(wp) { }

			XVECTOR3(double &xp, double &yp, double &zp) :x((float)xp), y((float)yp), z((float)zp), w(1.0f) {  }
			XVECTOR3(double &xp, double &yp, double &zp, double &wp) :x((float)xp), y((float)yp), z((float)zp), w((float)wp) { }
			XVECTOR3(const double &xp, const double &yp, const double &zp) :x((float)xp), y((float)yp), z((float)zp), w(1.0f) {  }
			XVECTOR3(const double &xp, const double &yp, const double &zp, const double &wp) :x((float)xp), y((float)yp), z((float)zp), w((float)wp) { }
			*/
			XVECTOR3 & operator= (const XVECTOR3 & other) {
				this->x = other.x; this->y = other.y; this->z = other.z; this->w = other.w;
				return *this;
			}
			XVECTOR3 & operator= (XVECTOR3 & other) {
				this->x = other.x; this->y = other.y; this->z = other.z; this->w = other.w;
				return *this;
			}

			operator float* ();
			operator const float* () const;

			XVECTOR3& operator += (const XVECTOR3&);
			XVECTOR3& operator -= (const XVECTOR3&);
			XVECTOR3& operator *= (float);
			XVECTOR3& operator /= (float);

			XVECTOR3 operator + () const;
			XVECTOR3 operator - () const;

			XVECTOR3 operator + (const XVECTOR3&) const;
			XVECTOR3 operator - (const XVECTOR3&) const;
			XVECTOR3 operator * (float) const;
			XVECTOR3 operator / (float) const;

			XVECTOR3	   operator * (const XVECTOR3&);

			friend XVECTOR3 operator * (float, const struct XVECTOR3&);

			bool operator == (const XVECTOR3&) const;
			bool operator != (const XVECTOR3&) const;

			void Normalize();
			float Length();

			union {
				float	 v[4];

				struct {
					float x;
					float y;
					float z;
					float w;
				};

				struct {
					float r;
					float g;
					float b;
					float a;
				};
			};
		};

		struct XMATRIX44_64 {
			XMATRIX44_64();
			XMATRIX44_64(XMATRIX44* i);
			union
			{
				double m[4][4];

				struct {
					double        m11, m12, m13, m14;
					double        m21, m22, m23, m24;
					double        m31, m32, m33, m34;
					double        m41, m42, m43, m44;
				};

				double mat[16];
			};
		};
		struct XMATRIX44
		{
			XMATRIX44() :m11(1.0f), m12(0.0f), m13(0.0f), m14(0.0f),
						 m21(0.0f), m22(1.0f), m23(0.0f), m24(0.0f),
						 m31(0.0f), m32(0.0f), m33(1.0f), m34(0.0f),
						 m41(0.0f), m42(0.0f), m43(0.0f), m44(1.0f) {}
			XMATRIX44(const float* m) : m11(m[0]), m12(m[1]), m13(m[2]),   m14(m[3]),
										m21(m[4]), m22(m[5]), m23(m[6]),   m24(m[7]),
										m31(m[8]), m32(m[9]), m33(m[10]),  m34(m[11]),
										m41(m[12]),m42(m[13]),m43(m[14]),  m44(m[15]) {}
			XMATRIX44(const XMATRIX44& m) : m11(m.m11), m12(m.m12), m13(m.m13), m14(m.m14),
											m21(m.m21), m22(m.m22), m23(m.m23), m24(m.m24),
											m31(m.m31), m32(m.m32), m33(m.m33), m34(m.m34),
											m41(m.m41), m42(m.m42), m43(m.m43), m44(m.m44) {}
			XMATRIX44(  const float &m11, const float &m12, const float &m13, const float &m14,
						const float &m21, const float &m22, const float &m23, const float &m24,
						const float &m31, const float &m32, const float &m33, const float &m34,
						const float &m41, const float &m42, const float &m43, const float &m44)
				:   m11(m11), m12(m12), m13(m13), m14(m14),
					m21(m21), m22(m22), m23(m23), m24(m24),
					m31(m31), m32(m32), m33(m33), m34(m34),
					m41(m41), m42(m42), m43(m43), m44(m44) {}

			XMATRIX44 & operator= (const XMATRIX44 & other) {
				this->m11 = other.m11; this->m12 = other.m12; this->m13 = other.m13; this->m14 = other.m14;
				this->m21 = other.m21; this->m22 = other.m22; this->m23 = other.m23; this->m24 = other.m24;
				this->m31 = other.m31; this->m32 = other.m32; this->m33 = other.m33; this->m34 = other.m34;
				this->m41 = other.m41; this->m42 = other.m42; this->m43 = other.m43; this->m44 = other.m44;
				return *this;
			}
			XMATRIX44 & operator= (XMATRIX44 & other) {
				this->m11 = other.m11; this->m12 = other.m12; this->m13 = other.m13; this->m14 = other.m14;
				this->m21 = other.m21; this->m22 = other.m22; this->m23 = other.m23; this->m24 = other.m24;
				this->m31 = other.m31; this->m32 = other.m32; this->m33 = other.m33; this->m34 = other.m34;
				this->m41 = other.m41; this->m42 = other.m42; this->m43 = other.m43; this->m44 = other.m44;
				return *this;
			}

			XMATRIX44& operator *= (const XMATRIX44&);
			XMATRIX44& operator += (const XMATRIX44&);
			XMATRIX44& operator -= (const XMATRIX44&);
			XMATRIX44& operator *= (float);
			XMATRIX44& operator /= (float);

			XMATRIX44 operator + () const;
			XMATRIX44 operator - () const;

			operator float*();
			operator const float* () const;

			XMATRIX44 operator * (const XMATRIX44&) const;
			XMATRIX44 operator + (const XMATRIX44&) const;
			XMATRIX44 operator - (const XMATRIX44&) const;
			XMATRIX44 operator * (float) const;
			XMATRIX44 operator / (float) const;

			friend XMATRIX44 operator * (float, const XMATRIX44&);

			bool operator == (const XMATRIX44&) const;
			bool operator != (const XMATRIX44&) const;

			union
			{
				float m[4][4];

				struct {
					float        m11, m12, m13, m14;
					float        m21, m22, m23, m24;
					float        m31, m32, m33, m34;
					float        m41, m42, m43, m44;
				};

				float mat[16];
			};

			void Inverse(XMATRIX44 *o);

			void Identity() {
				m[0][0] = 1.0f;
				m[0][1] = 0.0f;
				m[0][2] = 0.0f;
				m[0][3] = 0.0f;

				m[1][0] = 0.0f;
				m[1][1] = 1.0f;
				m[1][2] = 0.0f;
				m[1][3] = 0.0f;

				m[2][0] = 0.0f;
				m[2][1] = 0.0f;
				m[2][2] = 1.0f;
				m[2][3] = 0.0f;

				m[3][0] = 0.0f;
				m[3][1] = 0.0f;
				m[3][2] = 0.0f;
				m[3][3] = 1.0f;
			}

			void Zero() {
				m[0][0] = 0.0f;
				m[0][1] = 0.0f;
				m[0][2] = 0.0f;
				m[0][3] = 0.0f;

				m[1][0] = 0.0f;
				m[1][1] = 0.0f;
				m[1][2] = 0.0f;
				m[1][3] = 0.0f;

				m[2][0] = 0.0f;
				m[2][1] = 0.0f;
				m[2][2] = 0.0f;
				m[2][3] = 0.0f;

				m[3][0] = 0.0f;
				m[3][1] = 0.0f;
				m[3][2] = 0.0f;
				m[3][3] = 0.0f;
			}
		};

		struct XQUATERNION {
			XQUATERNION() :x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
			XQUATERNION(const float *vf) :x(vf[0]), y(vf[1]), z(vf[2]), w(vf[3]) {}
			XQUATERNION(const XVECTOR3& v) :x(v.x), y(v.y), z(v.z), w(v.w) {}
			XQUATERNION(const XQUATERNION& v) :x(v.x), y(v.y), z(v.z), w(v.w) {}
			XQUATERNION(const float &xp, const float &yp, const float &zp, const float &wp) :x(xp), y(yp), z(zp), w(wp) {}
			union {
				float	 v[4];

				struct {
					float x;
					float y;
					float z;
					float w;
				};
			};
		};

		


#endif


