#ifndef XDEFS_H
#define XDEFS_H

#include <Config.h>

#include <utils/xMaths.h>
#include <string>
#include <vector>
#include <iostream>

namespace xF {

	typedef unsigned long       xDWORD;
	typedef int                 xBOOL;
	typedef unsigned char       xBYTE;
	typedef unsigned short      xWORD;
	typedef float               xFLOAT;
	typedef std::string			xSTRING;



	typedef enum _xEFFECTENUM {
		STDX_STRINGS = 0x1,
		STDX_FLOATS = 0x2,
		STDX_DWORDS = 0x3,
	} xEFFECTENUM;


	typedef enum _xDECLMETHOD {
		STDDECLMETHOD_DEFAULT = 0,
		STDDECLMETHOD_PARTIALU = 1,
		STDDECLMETHOD_PARTIALV = 2,
		STDDECLMETHOD_CROSSUV = 3,
		STDDECLMETHOD_UV = 4,
		STDDECLMETHOD_LOOKUP = 5,
		STDDECLMETHOD_LOOKUPPRESAMPLED = 6
	} xDECLMETHOD;

	typedef enum _xDECLTYPE {
		STDDECLTYPE_FLOAT1 = 0,
		STDDECLTYPE_FLOAT2 = 1,
		STDDECLTYPE_FLOAT3 = 2,
		STDDECLTYPE_FLOAT4 = 3,
		STDDECLTYPE_STDCOLOR = 4,
		STDDECLTYPE_UBYTE4 = 5,
		STDDECLTYPE_SHORT2 = 6,
		STDDECLTYPE_SHORT4 = 7,
		STDDECLTYPE_UBYTE4N = 8,
		STDDECLTYPE_SHORT2N = 9,
		STDDECLTYPE_SHORT4N = 10,
		STDDECLTYPE_USHORT2N = 11,
		STDDECLTYPE_USHORT4N = 12,
		STDDECLTYPE_UDEC3 = 13,
		STDDECLTYPE_DEC3N = 14,
		STDDECLTYPE_FLOAT16_2 = 15,
		STDDECLTYPE_FLOAT16_4 = 16,
		STDDECLTYPE_UNUSED = 17
	} xDECLTYPE;

	typedef enum _xDECLUSAGE {
		STDDECLUSAGE_POSITION = 0,
		STDDECLUSAGE_BLENDWEIGHT = 1,
		STDDECLUSAGE_BLENDINDICES = 2,
		STDDECLUSAGE_NORMAL = 3,
		STDDECLUSAGE_PSIZE = 4,
		STDDECLUSAGE_TEXCOORD = 5,
		STDDECLUSAGE_TANGENT = 6,
		STDDECLUSAGE_BINORMAL = 7,
		STDDECLUSAGE_TESSFACTOR = 8,
		STDDECLUSAGE_POSITIONT = 9,
		STDDECLUSAGE_COLOR = 10,
		STDDECLUSAGE_FOG = 11,
		STDDECLUSAGE_DEPTH = 12,
		STDDECLUSAGE_SAMPLE = 13
	} xDECLUSAGE;

	enum xReturnType
	{
		STD_FILE_NOT_FOUND,
		STD_INVALID_FILE,
		STD_UNKNOWN_FORMAT,
		STD_OK
	};

	enum xTemplateType {
		STD_X_TEMPLATE = 0,
		STD_X_KEYVALUEPAIR,
		STD_X_FRAME,
		STD_X_FRAME_TRANSFORM_MTX,
		STD_X_OBJ_CMMTX,
		STD_X_MESH,
		STD_X_MESH_NORMALS,
		STD_X_MESH_TEXCOORD,
		STD_X_DECLDATA,
		STD_X_SKIN_HEADER,
		STD_X_SKIN_WEIGHTS,
		STD_X_MATERIALS_LIST,
		STD_X_MATERIAL,
		STD_X_EFFECT_INSTANCE,
		STD_X_EFFECT_PARAMS_DWORD,
		STD_X_EFFECT_PARAMS_FLOAT,
		STD_X_EFFECT_PARAMS_STRING,
		STD_X_TEXTURE,
		STD_X_TICKSPERSECCOND,
		STD_X_ANIMATIONSET,
		STD_X_ANIMATION,
		STD_X_ANIMATION_KEY,
		STD_X_ANIMATION_OPTIONS,
		STD_X_REF,
		STD_NOT,
		STD_BREAK
	};

	const std::string xTemplatesStr[] = {
		"template ",
		"KeyValuePair ",
		"Frame ",
		"FrameTransformMatrix ",
		"ObjectMatrixComment ",
		"Mesh ",
		"MeshNormals ",
		"MeshTextureCoords ",
		"DeclData ",
		"XSkinMeshHeader ",
		"SkinWeights ",
		"MeshMaterialList ",
		"Material ",
		"EffectInstance ",
		"EffectParamDWord ",
		"EffectParamFloats ",
		"EffectParamString ",
		"TextureFilename ",
		"AnimTicksPerSecond ",
		"AnimationSet ",
		"Animation ",
		"AnimationKey ",
		"AnimationOptions "
	};

	struct xKeyValuePair {
		std::string Key;
		std::string Value;
	};

	struct xVertexElement {
		xVertexElement() : Type(0), Method(0), Usage(0), UsageIndex(0) { }
		xDWORD Type;
		xDWORD Method;
		xDWORD Usage;
		xDWORD UsageIndex;
	};

	struct xDeclData {
		std::vector<xVertexElement>	Elements;
		std::vector<xDWORD>			Values;
	};

	struct xSkinMeshHeader {
		xSkinMeshHeader() : MaxNumWeightPerVertex(0), MaxNumWeightPerFace(0), NumBones(0), NumBonesProcess(0) {}
		xWORD	MaxNumWeightPerVertex;
		xWORD	MaxNumWeightPerFace;
		xWORD	NumBones;
		xWORD	NumBonesProcess;
	};

	struct xSkinWeights {
		std::vector<xDWORD>		VertexIndices;
		std::vector<xFLOAT>		Weights;
		XMATRIX44				MatrixFinal;
		XMATRIX44				MatrixOffset;
		XMATRIX44				*MatrixCombined;
		XMATRIX44				*MatrixCombinedAnimation;
		std::string				NodeName;
	};

	struct xSkinInfo {
		xSkinMeshHeader					SkinMeshHeader;
		std::vector<xSkinWeights>		SkinWeights;
	};

	struct xRotationKey {
		xRotationKey() { t.f_atTime = 0.0f; }

		xRotationKey(const xRotationKey &rk) { *this = rk; }
		xRotationKey(xRotationKey &rk) { *this = rk; }
		xRotationKey & operator= (const xRotationKey & other) {
			this->t.f_atTime = other.t.f_atTime;
			this->Rot = other.Rot;
			return *this;
		}
		xRotationKey & operator= (xRotationKey & other) {
			this->t.f_atTime = other.t.f_atTime;
			this->Rot = other.Rot;
			return *this;
		}

		~xRotationKey() { }

		union {
			unsigned int	i_atTime;
			float			f_atTime;
		}t;
		XQUATERNION		Rot;
	};

	struct xScaleKey {
		xScaleKey() { t.f_atTime = 0.0f; }

		xScaleKey(const xScaleKey &sk) { *this = sk; }
		xScaleKey(xScaleKey &sk) { *this = sk; }
		xScaleKey & operator= (const xScaleKey & other) {
			this->t.f_atTime = other.t.f_atTime;
			this->Scale = other.Scale;
			return *this;
		}
		xScaleKey & operator= (xScaleKey & other) {
			this->t.f_atTime = other.t.f_atTime;
			this->Scale = other.Scale;
			return *this;
		}

		~xScaleKey() {  }

		union {
			unsigned int	i_atTime;
			float			f_atTime;
		}t;
		XVECTOR3		Scale;
	};

	struct xPositionKey {
		xPositionKey() { t.f_atTime = 0.0f; }

		xPositionKey(const xPositionKey &pos) { *this = pos; }
		xPositionKey(xPositionKey &pos) { *this = pos; }
		xPositionKey & operator= (const xPositionKey & other) {
			this->t.f_atTime = other.t.f_atTime;
			this->Position = other.Position;
			return *this;
		}
		xPositionKey & operator= (xPositionKey & other) {
			this->t.f_atTime = other.t.f_atTime;
			this->Position = other.Position;
			return *this;
		}

		~xPositionKey() {  }

		union {
			unsigned int	i_atTime;
			float			f_atTime;
		}t;
		XVECTOR3		Position;
	};

	struct STDRGBAColor {
		STDRGBAColor() : r(0.0f) , g(0.0f) , b(0.0f) , a(0.0f){	}

		STDRGBAColor(const float vr, const float vg, const float vb, const float v_a)
			: r(vr), g(vg), b(vg), a(v_a){	}

		union {
			xFLOAT v[4];
			struct { xFLOAT r, g, b, a; };
		};

	};

	struct xEffectDefault {
		xEffectDefault() : CaseDWORD(0){}
		std::vector<xFLOAT>		CaseFloat;
		xDWORD					CaseDWORD;
		std::string				NameParam;
		std::string				CaseString;
		xEFFECTENUM				Type;
	};

	struct xEffectInstance{
		xEffectInstance() : NumDefaults(0) , NumProcess(0) {}
		std::string					ShaderFileName;
		xDWORD						NumDefaults;
		xDWORD						NumProcess;
		std::vector<xEffectDefault>	pDefaults;
	};

	struct xMaterial{
		xMaterial() : Power(0.0f), bEffects(false) { }
		~xMaterial() { }
		std::string			Name;
		STDRGBAColor		FaceColor;
		xFLOAT				Power;
		STDRGBAColor		Specular;
		STDRGBAColor		Emissive;
		xBOOL				bEffects;
		xEffectInstance		EffectInstance;
	};

	struct xMaterialList{
		xMaterialList() : NumMatProcess(0){}
		xDWORD					NumMatProcess;
		std::vector<xDWORD>		FaceIndices;
		std::vector<xMaterial>	Materials;
	};

	struct xBone {
		xBone() :Dad(0), Touched(0) {}
		XMATRIX44					Bone;
		XMATRIX44					Combined;
		std::string					Name;
		unsigned short				Dad;
		unsigned short				Touched;
		std::vector<unsigned int>	Brothers;
		std::vector<unsigned int>	Sons;
	};

	struct xSkeleton {
		xSkeleton() :NumBones(0) {}
		unsigned int						NumBones;
		std::vector<xBone>					Bones;
	};

	struct xAnimationSingleKey {
		enum { START, RUNNING, FINISHED };

		xAnimationSingleKey() :StatePos(START), StateRot(START), StateSc(START) {}

		xPositionKey	PositionKey;
		xRotationKey	RotationKey;
		xScaleKey		ScaleKey;

		float						LocaltimePos;
		float						LocaltimeRot;
		float						LocaltimeSc;

		float						LocaltimePosLerp;
		float						LocaltimeRotLerp;
		float						LocaltimeScLerp;

		unsigned int				LocalIndexPos;
		unsigned int				LocalIndexRot;
		unsigned int				LocalIndexSc;

		unsigned int				MaxIndexPos;
		unsigned int				MaxIndexRot;
		unsigned int				MaxIndexSc;

		unsigned int				StatePos;
		unsigned int				StateRot;
		unsigned int				StateSc;
	};

	struct xAnimationBone {
		xAnimationBone() :BoneID(0) {}


		std::vector<xPositionKey>	PositionKeys;
		std::vector<xRotationKey>	RotationKeys;
		std::vector<xScaleKey>		ScaleKeys;
		xAnimationSingleKey			ActualKey;
		XMATRIX44					MatrixfromKeys;
		unsigned int				BoneID;
		std::string					BoneName;
	};

	struct xAnimationSet {
		std::string					Name;
		std::vector<xAnimationBone> BonesRef;

		double						m_LocalTime;
		unsigned int				m_LocalIndex;
		long						m_MaxTimeOnTicks;
		long						m_NumofKeyframes;
	};

	struct xAnimationInfo {
		xAnimationInfo() :TicksPerSecond(0), isAnimInfo(false) {}
		std::vector<xAnimationSet>	Animations;
		unsigned int				TicksPerSecond;
		bool						isAnimInfo;
	};

	struct xSubsetInfo {
		xSubsetInfo() :VertexAttrib(0), Id(0), VertexStart(0), NumVertex(0), TriStart(0), NumTris(0), VertexSize(0), IdTex(0), IdTexUniformLoc(0) {}
		~xSubsetInfo() {}
		unsigned long		VertexAttrib;
		unsigned int		Id;
		unsigned int		VertexStart;
		unsigned int		NumVertex;
		unsigned int		TriStart;
		unsigned int		NumTris;
		unsigned int		VertexSize;
		unsigned int		IdTex;
		unsigned int		IdTexUniformLoc;
		bool				bAlignedVertex;
	};

	struct xFinalGeometry {

		xFinalGeometry() : pData(0), pDataDest(0), Id(0), IdIBO(0), VertexSize(0), NumVertex(0) {
			vertexAttribLoc=-1;
			normalAttribLoc=-1;
			tangentAttribLoc=-1;
			binormalAttribLoc=-1;
			uvAttribLoc=-1;
			uvSecAttribLoc=-1;
		}


		xFinalGeometry(const xFinalGeometry &fg) { *this = fg; }
		xFinalGeometry(xFinalGeometry &fg) { *this = fg; }
		xFinalGeometry & operator= (const xFinalGeometry & other) {
			this->Id = other.Id;
			this->IdIBO = other.IdIBO;
			this->VertexSize = other.VertexSize;
			this->NumVertex = other.NumVertex;

			unsigned int NumFloatsPerVertex = this->VertexSize / 4;
			unsigned int NumFloatsPerGeometry = NumFloatsPerVertex*this->NumVertex;

			pData = new float[NumFloatsPerGeometry];
			pDataDest = new float[NumFloatsPerGeometry];
			for (unsigned int i = 0; i < NumFloatsPerGeometry; i++) {
				pData[i] = other.pData[i];
				pDataDest[i] = other.pDataDest[i];
			}

			for (std::size_t i = 0; i < other.Subsets.size(); i++) {
				this->Subsets.push_back(other.Subsets[i]);
			}

			return *this;
		}
		xFinalGeometry & operator= (xFinalGeometry & other) {
			this->Id = other.Id;
			this->IdIBO = other.IdIBO;
			this->VertexSize = other.VertexSize;
			this->NumVertex = other.NumVertex;

			unsigned int NumFloatsPerVertex = this->VertexSize / 4;
			unsigned int NumFloatsPerGeometry = NumFloatsPerVertex*this->NumVertex;

			pData = new float[NumFloatsPerGeometry];
			pDataDest = new float[NumFloatsPerGeometry];
			for (unsigned int i = 0; i < NumFloatsPerGeometry; i++) {
				pData[i] = other.pData[i];
				pDataDest[i] = other.pDataDest[i];
			}

			for (unsigned int i = 0; i < other.Subsets.size(); i++) {
				this->Subsets.push_back(other.Subsets[i]);
			}

			return *this;
		}
		~xFinalGeometry() {
//			delete[] pData;
	//		delete[] pDataDest;
	//		pData = 0;
		}

		float					*pData;
		float					*pDataDest;
		std::vector<xSubsetInfo> Subsets;
		unsigned int ShaderProg;
		int			 vertexAttribLoc;
		int			 normalAttribLoc;
		int			 tangentAttribLoc;
		int			 binormalAttribLoc;
		int			 uvAttribLoc;
		int			 uvSecAttribLoc;

		unsigned int			 matWorldViewProjUniformLoc;
		unsigned int			 matWorldUniformLoc;

		unsigned int			 Id;
		unsigned int			 IdIBO;
		unsigned int			 VertexSize;
		unsigned int			 NumVertex;
	};


	struct xMeshGeometry {
		enum {
			HAS_POSITION = 1,
			HAS_NORMAL = HAS_POSITION << 1,
			HAS_TANGENT = HAS_POSITION << 2,
			HAS_BINORMAL = HAS_POSITION << 3,
			HAS_TEXCOORD0 = HAS_POSITION << 4,
			HAS_TEXCOORD1 = HAS_POSITION << 5,
			HAS_TEXCOORD2 = HAS_POSITION << 6,
			HAS_TEXCOORD3 = HAS_POSITION << 7,
			HAS_VERTEXCOLOR = HAS_POSITION << 8,
			HAS_SKINWEIGHTS0 = HAS_POSITION << 9,
			HAS_SKINWEIGHTS1 = HAS_POSITION << 10,
			HAS_SKININDEXES0 = HAS_POSITION << 11,
			HAS_SKININDEXES1 = HAS_POSITION << 12,
			HAS_CUSTOM_DATA_4BYTES = HAS_POSITION << 13,
			HAS_CUSTOM_DATA_8BYTES = HAS_POSITION << 14,
			HAS_CUSTOM_DATA_16BYTES = HAS_POSITION << 15,
		};
		xMeshGeometry() : NumVertices(0), NumTriangles(0), NumIndices(0), VertexSize(0), VertexAttributes(HAS_POSITION), NumChannelsTexCoords(0) {}
		~xMeshGeometry() {}

		std::vector<XVECTOR3>	 Positions;
		std::vector<XVECTOR3>	 Normals;
		std::vector<XVECTOR3>	 Tangents;
		std::vector<XVECTOR3>	 Binormals;
		std::vector<XVECTOR3>	 VertexColors;
		std::vector<XVECTOR2>	 TexCoordinates[4];

		std::vector<xWORD>		 Triangles;
		xSkinInfo				 Info;
		xMaterialList			 MaterialList;

		XMATRIX44				 RelativeMatrix;
		std::string				 Name;
		xDWORD					 NumVertices;
		xDWORD					 NumTriangles;
		xDWORD					 NumIndices;
		xDWORD					 VertexSize;
		xDWORD					 VertexAttributes;
		xWORD					 NumChannelsTexCoords;
	};

	struct xMeshContainer {

		xMeshContainer() {}

		std::string					FileName;

		std::vector<xMeshGeometry>	Geometry;

		xSkeleton					Skeleton;

		xSkeleton					SkeletonAnimated;

		xAnimationInfo				Animation;


	};

}

#endif

