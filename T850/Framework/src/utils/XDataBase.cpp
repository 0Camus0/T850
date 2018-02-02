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


#include <utils/XDataBase.h>

#define DEBUG_COUTS 0
#define DEBUG_GET_TEMPLATE 0

#define DEBUG_GET_BRACE 0
#define DEBUG_MATRICES 0
#define DEBUG_VERTICES 0
#define DEBUG_INDICES 0
#define DEBUG_NORMALS 0
#define DEBUG_TEXCOORDS 0
#define DEBUG_WEIGHTS 0
#define DEBUG_MATERIAL_INDICES 0
#define DEBUG_MATERIAL_COLORS 0
#define DEBUG_NUMBER_MATERIALS 0
#define DEBUG_ANIMATION 0
#define DEBUG_TICKS_PER_SECOND 0
#define DEBUG_ANIMATION_KEYS 0
#define DEBUG_END_BLOCKS 0

#define DEBUG_EFFECT_INSTANCE 0
#define DEBUG_EFFECT_DWORDS 0
#define DEBUG_EFFECT_FLOATS 0
#define DEBUG_EFFECT_STRING 0


#define PROFILE_PARSING 0
#define PROFILE_LOAD_X_FILE 0
#define PROFILE_MATRICES 0
#define PROFILE_BRACES 0
#define PROFILE_SKIN_WEIGHTS 0
#define PROFILE_FRAME_BONE 0
#define PROFILE_MESH_BLOCK 0
#define PROFILE_VERTICES_BLOCK 0
#define PROFILE_INDICES_BLOCK 0
#define PROFILE_NORMALS_BLOCK 0
#define PROFILE_TEXCOORD_BLOCK 0
#define PROFILE_DECL_DATA_BLOCK 0
#define PROFILE_ANIMATION_SET 0
#define PROFILE_SKIN_HEADER 0
#define PROFILE_MATERIAL 0
#define PROFILE_EFFECT_INSTANCE 0
#define PROFILE_EFFECT_STRING 0
#define PROFILE_EFFECT_FLOATS 0
#define PROFILE_EFFECT_DWORDS 0
#define PROFILE_MATERIAL_BLOCK 0
#define PROFILE_ANIMATION 0
#define PROFILE_ANIMATION_ROTATION 0
#define PROFILE_ANIMATION_SCALE 0
#define PROFILE_ANIMATION_POSITION 0
#define PROFILE_TICKS 0

#define USE_PREFETCH_ANIMATION_SETS_NUM 0
#define USE_VECTOR_RESERVE_AND_PUSH 0
#define USE_VECTOR_ARRAY_MODE 1


#define  LogPrintDebug printf


const char* xTemplatesc_Str[] = {
	"template",
	"KeyValuePair",
	"Frame",
	"FrameTransformMatrix",
	"ObjectMatrixComment",
	"Mesh",
	"MeshNormals",
	"MeshTextureCoords",
	"DeclData",
	"XSkinMeshHeader",
	"SkinWeights",
	"MeshMaterialList",
	"Material",
	"EffectInstance",
	"EffectParamDWord",
	"EffectParamFloats",
	"EffectParamString",
	"TextureFilename",
	"AnimTicksPerSecond",
	"AnimationSet",
	"Animation",
	"AnimationKey",
	"AnimationOptions"
	};

namespace xF {
#if !USE_STRING_STREAM
	void		XDataBase::advance_to_next_open_brace() {
		do{
			index++;

			if(pData[index]=='\0')
				break;

		}while (pData[index] != '{');
	}

	void XDataBase::advance_to_next_close_brace() {
		do {
			index++;

			if (pData[index] == '\0')
				break;

		} while (pData[index] != '}');
		index++;
	}

	void XDataBase::advance_to_next_close_brace_pre() {
		while (pData[index] != '}') {
			index++;
		}
	}

	void XDataBase::advance_to_next_space() {
		do {
			index++;
		} while (pData[index] != ' ');
	}
#endif

	bool	XDataBase::LoadXFile(const std::string	&FileName) {
    m_name = FileName;
#if PROFILE_LOAD_X_FILE
		PROFILING_SCOPE("LoadXFile")
#endif
		std::string Path = "";
		Path += FileName;

		ifstream	inFile(Path.c_str(), ios::binary | ios::in);
		if (!inFile.good()) {
			inFile.close();
			return false;
		}

		inFile.seekg(0, std::ios::end);
		unsigned int Size = static_cast<unsigned int>(inFile.tellg());
		inFile.seekg(0, std::ios::beg);


		pData = new char[Size + 1];
		pData[Size] = '\0';

		inFile.read(pData, Size);
		inFile.close();

#if USE_STRING_STREAM
		m_ActualStream.clear();
		m_ActualStream.str(std::string());
		m_ActualStream.write((const char*)pData, Size);
		m_ActualStream.seekg(0, std::ios::beg);
#endif
		index = 0;
		Parse(Path);

		delete [] pData;
		pData = 0;

		CreateSubSets();

		return true;
	}

	bool	XDataBase::LoadXFromMemory(char* pData, const unsigned int &size) {

		if (!pData || !size)
			return false;


		m_ActualStream.clear();
		m_ActualStream.str(std::string());

		m_ActualStream.write((const char*)pData, size);

		m_ActualStream.seekg(0, std::ios::beg);

		return Parse("FromMemory");

	}

	void XDataBase::CreateSubSets() {
		const std::size_t size_geometry = XMeshDataBase[0]->Geometry.size();

		for (unsigned int i = 0; i < size_geometry; i++) {
			unsigned int VertexSizeinBytes = 0;
			unsigned int VectorSizeinBytes = 16;

			xMeshGeometry *pActual = &XMeshDataBase[0]->Geometry[i];

			//	video::CTechnique_ *technique = new video::TechniqueGL();

			if (pActual->Info.SkinMeshHeader.NumBones > 0) {

			}
			else {

			}

			if (pActual->VertexAttributes&xMeshGeometry::HAS_POSITION) {
				VertexSizeinBytes += VectorSizeinBytes;

			}

			if (pActual->VertexAttributes&xMeshGeometry::HAS_NORMAL) {
				VertexSizeinBytes += VectorSizeinBytes;

			}

			if (pActual->VertexAttributes&xMeshGeometry::HAS_TANGENT) {
				VertexSizeinBytes += VectorSizeinBytes;

			}

			if (pActual->VertexAttributes&xMeshGeometry::HAS_BINORMAL) {
				VertexSizeinBytes += VectorSizeinBytes;

			}

			if (pActual->VertexAttributes&xMeshGeometry::HAS_TEXCOORD0) {
				VertexSizeinBytes += 8;

			}

			if (pActual->VertexAttributes&xMeshGeometry::HAS_TEXCOORD1) {
				VertexSizeinBytes += 8;

			}

			if (pActual->VertexAttributes&xMeshGeometry::HAS_TEXCOORD2) {
				VertexSizeinBytes += 8;

			}

			if (pActual->VertexAttributes&xMeshGeometry::HAS_TEXCOORD3) {
				VertexSizeinBytes += 8;

			}

			xFinalGeometry tmpGeo;
			pActual->VertexSize = tmpGeo.VertexSize = VertexSizeinBytes;
			unsigned int NumFloatsPerVertex = VertexSizeinBytes / 4;
			unsigned int NumFloatsPerGeometry = NumFloatsPerVertex*pActual->NumVertices;
			unsigned int NumComponents = VectorSizeinBytes / 4;

			tmpGeo.NumVertex = pActual->NumVertices;
			tmpGeo.pData = new float[NumFloatsPerGeometry];
			tmpGeo.pDataDest = new float[NumFloatsPerGeometry];

			unsigned int counter = 0;
			for (unsigned int j = 0; j < pActual->NumVertices; j++) {

				if (pActual->VertexAttributes&xMeshGeometry::HAS_POSITION) {
					for (unsigned int k = 0; k < NumComponents; k++) {
						tmpGeo.pData[counter++] = pActual->Positions[j].v[k];
					}
				}

				if (pActual->VertexAttributes&xMeshGeometry::HAS_NORMAL) {
					for (unsigned int k = 0; k < NumComponents; k++) {
						tmpGeo.pData[counter++] = pActual->Normals[j].v[k];
					}
				}

				if (pActual->VertexAttributes&xMeshGeometry::HAS_TANGENT) {
					for (unsigned int k = 0; k < NumComponents; k++) {
						tmpGeo.pData[counter++] = pActual->Tangents[j].v[k];
					}
				}

				if (pActual->VertexAttributes&xMeshGeometry::HAS_BINORMAL) {
					for (unsigned int k = 0; k < NumComponents; k++) {
						tmpGeo.pData[counter++] = pActual->Binormals[j].v[k];
					}
				}

				if (pActual->VertexAttributes&xMeshGeometry::HAS_TEXCOORD0) {
					tmpGeo.pData[counter++] = pActual->TexCoordinates[0][j].x;
					tmpGeo.pData[counter++] = pActual->TexCoordinates[0][j].y;
				}

				if (pActual->VertexAttributes&xMeshGeometry::HAS_TEXCOORD1) {
					tmpGeo.pData[counter++] = pActual->TexCoordinates[1][j].x;
					tmpGeo.pData[counter++] = pActual->TexCoordinates[1][j].y;
				}

				if (pActual->VertexAttributes&xMeshGeometry::HAS_TEXCOORD2) {
					tmpGeo.pData[counter++] = pActual->TexCoordinates[2][j].x;
					tmpGeo.pData[counter++] = pActual->TexCoordinates[2][j].y;
				}

				if (pActual->VertexAttributes&xMeshGeometry::HAS_TEXCOORD3) {
					tmpGeo.pData[counter++] = pActual->TexCoordinates[3][j].x;
					tmpGeo.pData[counter++] = pActual->TexCoordinates[3][j].y;
				}

			}

			for (unsigned int j = 0; j < counter; j++) {
				tmpGeo.pDataDest[j] = tmpGeo.pData[j];
			}


			xDWORD NumMaterials = pActual->MaterialList.Materials.size();
			tmpGeo.Subsets.reserve(NumMaterials);

			xDWORD NumFaceIndices = pActual->MaterialList.FaceIndices.size();


			for (unsigned int j = 0; j < NumMaterials; j++) {

			//	video::Material_ mat;
			//	GatherMaterials(mat, pActual->MaterialList.Materials[j]);
				xSubsetInfo tmpSubset;
				tmpSubset.NumTris = 0;
				for (unsigned int k = 0; k < NumFaceIndices; k++) {
					if (pActual->MaterialList.FaceIndices[k] == j) {
						tmpSubset.NumTris++;
					}
				}
				tmpSubset.NumVertex = tmpSubset.NumTris * 3;
				unsigned short *tmpIndexex = new unsigned short[tmpSubset.NumVertex];
				counter = 0;
				bool first = false;
				for (unsigned int k = 0; k < NumFaceIndices; k++) {
					if (pActual->MaterialList.FaceIndices[k] == j) {
						unsigned int index = k * 3;
						if (!first) {
							tmpSubset.TriStart = k;
							tmpSubset.VertexStart = index;
							first = true;
						}
						tmpIndexex[counter++] = pActual->Triangles[index];
						tmpIndexex[counter++] = pActual->Triangles[index + 1];
						tmpIndexex[counter++] = pActual->Triangles[index + 2];
					}
				}



				tmpSubset.VertexAttrib = pActual->VertexAttributes;
				tmpSubset.VertexSize = pActual->VertexSize;
				tmpSubset.bAlignedVertex = true;
				tmpGeo.Subsets.push_back(tmpSubset);
				delete[] tmpIndexex;
			}
			MeshInfo.push_back(tmpGeo);
		}
	}
#if !USE_STRING_STREAM
	unsigned int	XDataBase::GetxTemplateTypeChar(std::string &retName) {
#if PROFILE_BRACES
		PROFILING_SCOPE("GetTemplateType");
#endif
		unsigned int current_index = index;

		unsigned int first_letter_space = 0;
		unsigned int size_word_1 = 0;
		unsigned int size_word_2 = 0;
		unsigned int index_init = index;
		char cWord_A[64];
		char cWord_B[32];


		while (pData[current_index] != '{') {
			current_index++;

			if (pData[current_index] == '}') {
				index = current_index;
				return STD_BREAK;
			}

		}

		current_index--;

		while (pData[current_index] == ' ') {
			first_letter_space = current_index;
			current_index--;
		}

		while (pData[current_index] != ' ' && pData[current_index] != '\n') {
			current_index--;
		}

		size_word_1 = first_letter_space - current_index;
		memcpy(cWord_A, &pData[current_index + 1], size_word_1);
		cWord_A[size_word_1 - 1] = '\0';

		while (pData[current_index] == ' ') {
			first_letter_space = current_index;
			current_index--;
		}

		if (pData[current_index] == '\n') {
			for (unsigned int i = 0; i < STD_X_REF; i++) {
				if (strstr(xTemplatesc_Str[i], cWord_A) != 0) {

					if (strcmp(xTemplatesc_Str[i], cWord_A) != 0)
						continue;

					retName = std::string(cWord_A, size_word_1);
#if DEBUG_GET_BRACE
					LogPrintDebug("One Word Found [%s]", cWord_A);
#endif
					index = index_init;
					advance_to_next_space();

					return i;
				}
			}

#if DEBUG_GET_BRACE
			LogPrintWarning("One Word NOT Found [%s]", cWord_A);
#endif

			index = index_init;
			advance_to_next_space();
			return STD_NOT;
		}

		while (pData[current_index] != ' ' && pData[current_index] != '\n') {
			current_index--;
		}


		size_word_2 = first_letter_space - current_index;
		memcpy(cWord_B, &pData[current_index + 1], size_word_2);
		cWord_B[size_word_2 - 1] = '\0';

		for (unsigned int i = 0; i < STD_X_REF; i++) {
			if (strstr(xTemplatesc_Str[i], cWord_B) != 0) {
				retName = std::string(cWord_A, size_word_1);

				if (strcmp(xTemplatesc_Str[i], cWord_B) != 0)
					continue;
#if DEBUG_GET_BRACE
				LogPrintDebug("Two Words Found [%s] [%s]", cWord_B, cWord_A);
#endif
				index = index_init;
				advance_to_next_space();
				return i;
			}
		}

#if DEBUG_GET_BRACE
		LogPrintWarning("Two Words NOT Found [%s] [%s]", cWord_B, cWord_A);
#endif
		index = index_init;
		advance_to_next_space();
		return STD_NOT;
	}
#endif

	int	XDataBase::GetxTemplateType(std::string Line, std::string *retName) {
		std::string::size_type ret = std::string::npos;
		for (int i = 0; i < xF::STD_X_REF; i++) {
			ret = Line.find(xTemplatesStr[i]);
			if (ret != std::string::npos) {
				if (retName) {
					std::size_t dif = Line.find_last_of(" ") - Line.find(" ", ret);
					*retName = Line.substr(ret + xTemplatesStr[i].size(), --dif);
				}
				return i;
			}
		}

		ret = Line.find("{");
		if ((ret != std::string::npos) && (Line.find("}") != std::string::npos)) {
			if (retName != 0) {
				std::size_t dif = Line.find_last_of(" ") - Line.find(" ", ret);
				*retName = Line.substr(++(++ret), --dif);
			}
			return xF::STD_X_REF;
		}

		return STD_NOT;
	}

	bool	XDataBase::Parse(std::string name) {
#if PROFILE_PARSING
		PROFILING_SCOPE("Parse")
#endif
		while (!m_Stack.empty()) {
			m_Stack.pop();
		}

		xMeshContainer	*tmp = new xMeshContainer();
		XMeshDataBase.push_back(tmp);
		m_pActualMesh = XMeshDataBase.back();
		m_pActualMesh->FileName = name;

#if USE_STRING_STREAM
		std::string Line;
		while (!m_ActualStream.eof()) {
		std::getline(m_ActualStream, Line);
		if (Line.find("{") != std::string::npos) {
			std::string rets;
			unsigned short Ret = GetxTemplateType(Line, &rets);

#else

		while(pData[index]!='\0'){
		advance_to_next_open_brace();{

			if (pData[index] == '\0')
				break;

			std::string rets;
			unsigned int Ret = GetxTemplateTypeChar(rets);
#endif
			switch (Ret) {

				case xF::STD_X_FRAME: {
#if	DEBUG_COUTS
					LogPrintDebug("Found Frame [%s]", rets.c_str());
#endif
					xBone tmp;
					tmp.Name = rets;
					tmp.Dad = 0;
					m_pActualMesh->Skeleton.Bones.push_back(tmp);
					ProcessFrameBlock(rets);
				}break;

				case xF::STD_X_TICKSPERSECCOND: {
#if	DEBUG_COUTS
					LogPrintDebug("Found AnimTicksPerSecond [%s]", rets.c_str());
#endif
					ProcessTicksPerSecond(&m_pActualMesh->Animation);
				}break;

				case STD_X_ANIMATIONSET: {
#if	DEBUG_COUTS
					LogPrintDebug("Found AnimationSet [%s]", rets.c_str());
#endif
					ProcessAnimationSet(&m_pActualMesh->Animation, rets);
				}break;
#if !USE_STRING_STREAM
				default: {
					advance_to_next_close_brace();
				}break;
#endif
				}//switch
			}//if
		}



		for (unsigned int i = 0; i < m_pActualMesh->Skeleton.Bones.size(); i++) {
			for (unsigned int j = 0; j < m_pActualMesh->Skeleton.Bones[i].Sons.size(); j++) {
				for (unsigned int k = 0; k < m_pActualMesh->Skeleton.Bones[i].Sons.size(); k++) {
					if (m_pActualMesh->Skeleton.Bones[i].Sons[j] != m_pActualMesh->Skeleton.Bones[i].Sons[k]) {
						m_pActualMesh->Skeleton.Bones[m_pActualMesh->Skeleton.Bones[i].Sons[j]].Brothers.push_back(m_pActualMesh->Skeleton.Bones[i].Sons[k]);
						//	m_pActualMesh->SkeletonAnimated.Bones[m_pActualMesh->SkeletonAnimated.Bones[i].Sons[j]].Brothers.push_back(m_pActualMesh->SkeletonAnimated.Bones[i].Sons[k]);

					}
				}
			}
		}

		m_pActualMesh->Skeleton.NumBones = m_pActualMesh->Skeleton.Bones.size();
		m_pActualMesh->SkeletonAnimated.NumBones = m_pActualMesh->Skeleton.NumBones;
		m_pActualMesh->SkeletonAnimated.Bones = std::vector<xBone>(m_pActualMesh->Skeleton.Bones);

		for (unsigned int i = 0; i < m_pActualMesh->Geometry.size(); i++) {
			for (unsigned int j = 0; j < m_pActualMesh->Geometry[i].Info.SkinMeshHeader.NumBones; j++) {
				for (unsigned int k = 0; k < m_pActualMesh->Skeleton.Bones.size(); k++) {
					if (m_pActualMesh->Geometry[i].Info.SkinWeights[j].NodeName.compare(m_pActualMesh->Skeleton.Bones[k].Name) == 0) {
						m_pActualMesh->Geometry[i].Info.SkinWeights[j].MatrixCombined = &m_pActualMesh->Skeleton.Bones[k].Combined;
						m_pActualMesh->Geometry[i].Info.SkinWeights[j].MatrixCombinedAnimation = &m_pActualMesh->SkeletonAnimated.Bones[k].Combined;
					}
				}
			}
		}


		return true;
	}


	void XDataBase::ProcessFrameBlock(std::string &actual) {
#if PROFILE_FRAME_BONE
		PROFILING_SCOPE("ProcessFrameBlock")
#endif
		m_Stack.push(actual);

#if USE_STRING_STREAM
		std::string Line;
		while (Line.find("}") == std::string::npos) {
			std::getline(m_ActualStream, Line);
			if (Line.find("{") != std::string::npos) {
				std::string rets;
				unsigned short Ret = GetxTemplateType(Line, &rets);
#else
		while(pData[index]!='}'){
			if (pData[index] != '{') {
				std::string rets;
				unsigned int Ret = GetxTemplateTypeChar(rets);
#endif
				if (Ret == STD_BREAK)
					break;

				switch (Ret){
					case xF::STD_X_FRAME: {

						xBone tmp;
						tmp.Name = rets;
						for (unsigned int i = 0; i < m_pActualMesh->Skeleton.Bones.size(); i++) {
							if (m_pActualMesh->Skeleton.Bones[i].Name.compare(m_Stack.top()) == 0) {
								tmp.Dad = i;
								m_pActualMesh->Skeleton.Bones[i].Sons.push_back(m_pActualMesh->Skeleton.Bones.size());
								//	m_pActualMesh->SkeletonAnimated.Bones[i].Sons.push_back(m_pActualMesh->SkeletonAnimated.Bones.size());

								break;
							}
						}
						m_pActualMesh->Skeleton.Bones.push_back(tmp);
						//	m_pActualMesh->SkeletonAnimated.Bones.push_back(tmp);
						#if !USE_STRING_STREAM
						advance_to_next_open_brace();
						#endif
						ProcessFrameBlock(rets);

					}break;
					case xF::STD_X_MESH: {
	#if	DEBUG_COUTS
						LogPrintDebug("Found x Mesh [%s]", rets.c_str());
	#endif
						ProcessMeshBlock(rets);

					}break;

					case xF::STD_X_OBJ_CMMTX: {
						//	ProcessMatrix(&m_pActualMesh->Skeleton.Bones.back().Bone);
	#if USE_STRING_STREAM
						GetNextEndBracket();
	#else

	#endif

					}break;

					case xF::STD_X_FRAME_TRANSFORM_MTX: {
						//	std::streampos PosStream = m_ActualStream.tellg();
						ProcessMatrix(&m_pActualMesh->Skeleton.Bones.back().Bone);
						//	m_ActualStream.seekg(PosStream);
						//	ProcessMatrix(&m_pActualMesh->SkeletonAnimated.Bones.back().Bone);

						//system("pause");
	#if USE_STRING_STREAM
						GetNextEndBracket();
	#else

	#endif

					}break;

					default: {

					}break;
				}
#if !USE_STRING_STREAM
				advance_to_next_close_brace_pre();
#endif
			}
			index++;
		}

		if (m_Stack.size() > 0)
			m_Stack.pop();
	}

	void XDataBase::ProcessMeshBlock(std::string actual) {
#if PROFILE_MESH_BLOCK
		PROFILING_SCOPE("ProcessMeshBlock")
#endif

		xF::xMeshGeometry	tmp;
		m_pActualMesh->Geometry.push_back(tmp);

		xMeshGeometry *ptr = &m_pActualMesh->Geometry.back();
		ptr->Name = actual;
		ptr->RelativeMatrix = m_pActualMesh->Skeleton.Bones.back().Bone;

#if USE_STRING_STREAM
		m_ActualStream >> ptr->NumVertices >> c_temp;
#if USE_VECTOR_RESERVE_AND_PUSH
		ptr->Positions.reserve(ptr->NumVertices);
#elif USE_VECTOR_ARRAY_MODE
		ptr->Positions = std::vector<XVECTOR3>(ptr->NumVertices);
#endif

		float x = 0.0f, y = 0.0f, z = 0.0f;
		for (std_uint i = 0; i < ptr->NumVertices; i++) {
			m_ActualStream >> x >> c_temp >> y >> c_temp >> z >> c_temp >> c_temp;
#if USE_VECTOR_RESERVE_AND_PUSH
			ptr->Positions.push_back(XVECTOR3(x, y, z));
#elif USE_VECTOR_ARRAY_MODE
			ptr->Positions[i].x = x;
			ptr->Positions[i].y = y;
			ptr->Positions[i].z = z;
#endif
		}

		m_ActualStream >> ptr->NumTriangles >> c_temp;

#if USE_VECTOR_RESERVE_AND_PUSH
		ptr->Triangles.reserve(3 * ptr->NumTriangles);
#elif USE_VECTOR_ARRAY_MODE
		ptr->Triangles = std::vector<xWORD>(3 * ptr->NumTriangles);
#endif
		unsigned int counter = 0;
		unsigned short a, b, c;
		for (unsigned int i = 0; i < ptr->NumTriangles; i++) {
			m_ActualStream >> c_temp >> c_temp >> a >> c_temp >> b >> c_temp >> c >> c_temp >> c_temp;
#if USE_VECTOR_RESERVE_AND_PUSH
			ptr->Triangles.push_back(a);
			ptr->Triangles.push_back(b);
			ptr->Triangles.push_back(c);
#elif USE_VECTOR_ARRAY_MODE
			ptr->Triangles[counter++] = a;
			ptr->Triangles[counter++] = b;
			ptr->Triangles[counter++] = c;
#endif
		}

#else
#if PROFILE_VERTICES_BLOCK
{		TimeEvent v("Vertex Block");
#endif
		int current_index = index;
		int token = 0;
		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == ' ')
				token = current_index;
		}
		char cNumVerts[16];
		cNumVerts[15] = '\0';
		unsigned int size_g = current_index - token;
		memcpy(cNumVerts, &pData[token + 1], size_g);
		cNumVerts[size_g - 1] = '\0';
		ptr->NumVertices = static_cast<xDWORD>(atoi(cNumVerts));

		current_index++;
#if USE_VECTOR_RESERVE_AND_PUSH
		ptr->Positions.reserve(ptr->NumVertices);
#elif USE_VECTOR_ARRAY_MODE
		ptr->Positions = std::vector<XVECTOR3>(ptr->NumVertices);
#endif

		char cVertComponent[16];
		cVertComponent[15] = '\0';
		int bracket_cont = 0;
		int component_count = 0;
		for (unsigned int i = 0; i < ptr->NumVertices; i++) {
			bracket_cont = 0;
			component_count = 0;
			while (pData[current_index] != ',') {

				if (pData[current_index] == ' ')
					token = current_index;

				if (pData[current_index] == ';') {
					bracket_cont++;
					size_g = current_index - token;
					memcpy(cVertComponent, &pData[token + 1], size_g);
					cVertComponent[size_g - 1] = '\0';
					if (component_count < 3) {
						ptr->Positions[i].v[component_count++] = static_cast<float>(atof(cVertComponent));
					}
					token = current_index;
					if (bracket_cont == 4)
						break;
				}
				current_index++;
			}
			current_index++;
		}
#if PROFILE_VERTICES_BLOCK
}
#endif
		current_index++;

#if DEBUG_VERTICES
		for (unsigned int i = 0; i < ptr->NumVertices; i++) {
			LogPrintDebug("[%f;%f;%f;,]", ptr->Positions[i].x, ptr->Positions[i].y, ptr->Positions[i].z);
		}
#endif

		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == ' ')
				token = current_index;
		}
#if PROFILE_INDICES_BLOCK
{		TimeEvent t("Indices Block");
#endif
		size_g = current_index - token;
		memcpy(cNumVerts, &pData[token + 1], size_g);
		cNumVerts[size_g - 1] = '\0';
		ptr->NumTriangles = static_cast<xDWORD>(atoi(cNumVerts));


		while (pData[current_index] != ' ') {
			current_index++;
		}

#if USE_VECTOR_RESERVE_AND_PUSH
		ptr->Triangles.reserve(3 * ptr->NumTriangles);
#elif USE_VECTOR_ARRAY_MODE
		ptr->Triangles = std::vector<xWORD>(3 * ptr->NumTriangles);
#endif
		char cTriang[16];
		cTriang[15] = '\0';
		int cont = 0;
		int delim = 0;
		for (unsigned int i = 0; i < ptr->NumTriangles; i++) {
			delim = 0;
			while (delim < 5) {
				if (pData[current_index] == ',' || pData[current_index] == ';') {
					if (delim != 0 && delim != 4) {
						size_g = current_index - token;
						memcpy(cTriang, &pData[token + 1], current_index - token);
						cTriang[size_g - 1] = '\0';
						ptr->Triangles[cont++] = static_cast<unsigned short>(atoi(cTriang));
					}
					token = current_index;

					delim++;
				}
				current_index++;
			}
		}
		current_index++;
#if PROFILE_INDICES_BLOCK
}
#endif

#if DEBUG_INDICES
		for (unsigned int i = 0; i < ptr->Triangles.size(); i++) {
			LogPrintDebug("[%d]", ptr->Triangles[i]);
		}
#endif
		index = current_index;

#endif



#if USE_STRING_STREAM
		xSTRING Line;
		//std::getline(m_ActualStream,Line);
		while (Line.find("}") == std::string::npos) {
			std::getline(m_ActualStream, Line);
			if (Line.find("{") != std::string::npos) {
				std::string rets;
				unsigned int Ret = GetxTemplateType(Line, &rets);
#else
		while (pData[index] != '}') {
			if (pData[index] != '{') {
				std::string rets;
				unsigned int Ret = GetxTemplateTypeChar(rets);
#endif
				if (Ret == STD_BREAK)
					break;

				switch (Ret)
				{
				case xF::STD_X_MESH_NORMALS: {
#if	DEBUG_COUTS
					LogPrintDebug("Found MeshNormals [%s]", rets.c_str());
#endif
					ProcessNormalsBlock(ptr);

				}break;

				case xF::STD_X_MESH_TEXCOORD: {
#if	DEBUG_COUTS
					LogPrintDebug("Found TextureCoords [%s]", rets.c_str());
#endif
					ProcessTexCoordinatesBlock(ptr);
					/*
					advance_to_next_close_brace_pre();

					index--;
					index--;
					index--;
					*/
				}break;

				case xF::STD_X_DECLDATA: {
#if	DEBUG_COUTS
					std::cout << "Found Mesh decl data: " << std::endl;
#endif
				//	system("pause");
					ProcessDeclDataBlock(ptr);
					//	GetNextEndBracket();
				}break;

				case xF::STD_X_SKIN_HEADER: {
#if	DEBUG_COUTS
					LogPrintDebug("Found SkinMeshHeader [%s]", rets.c_str());
#endif

					ProcessSkinHeader(ptr);

				}break;

				case xF::STD_X_SKIN_WEIGHTS: {
#if	DEBUG_COUTS
					LogPrintDebug("Found SkinWeights [%s]", rets.c_str());
#endif

					ProcessSkinWeights(ptr);
				}break;

				case xF::STD_X_MATERIALS_LIST: {
#if	DEBUG_COUTS
					LogPrintDebug("Found MeshMaterialList [%s]", rets.c_str());
#endif
					ProcessMaterialBlock(ptr);
				}break;
				}
#if !USE_STRING_STREAM
				advance_to_next_close_brace_pre();
#endif
			}

			index++;
		}
		#if DEBUG_END_BLOCKS
		LogPrintDebug("End of ProcessMeshBlock");
		#endif
	}

	void XDataBase::ProcessTicksPerSecond(xF::xAnimationInfo* pAnimation) {
		#if PROFILE_TICKS
		PROFILING_SCOPE("ProcessTicksPerSecond")
		#endif
#if USE_STRING_STREAM
		std::string Temp;
		m_ActualStream >> Temp;
		Temp = Temp.substr(0, Temp.size() - 1);
		std::stringstream toInt(Temp);
		toInt >> pAnimation->TicksPerSecond;
		pAnimation->isAnimInfo = true;
#else
		int current_index = index;
		int token = 0;
		while (pData[current_index] != ';') {
			if (pData[current_index] == ' ')
				token = current_index;

			current_index++;
		}
		char cTicksPerSecond[16];
		cTicksPerSecond[15] = '\0';
		unsigned int size_g = current_index - token;
		memcpy(cTicksPerSecond, &pData[token + 1], size_g);
		cTicksPerSecond[size_g - 1] = '\0';
		pAnimation->TicksPerSecond = static_cast<xDWORD>(atoi(cTicksPerSecond));
		pAnimation->isAnimInfo = true;
#if DEBUG_TICKS_PER_SECOND
		LogPrintDebug("Ticks per Second: [%d]", pAnimation->TicksPerSecond);
#endif

#endif
	}

	void XDataBase::ProcessAnimationSet(xF::xAnimationInfo* pAnimation, const std::string name) {
#if PROFILE_ANIMATION_SET
		PROFILING_SCOPE("ProcessAnimationSet")
#endif

#if USE_STRING_STREAM
			xF::xAnimationSet tmp;
		pAnimation->Animations.push_back(tmp);
		xF::xAnimationSet* pActualAnimationSet = &pAnimation->Animations.back();

		pActualAnimationSet->Name = name;

		xSTRING Line;
		while (Line.find("}") == std::string::npos) {
			std::getline(m_ActualStream, Line);
			if (Line.find("{") != std::string::npos) {
				xSTRING rets;
				switch (GetxTemplateType(Line, &rets)) {
				case xF::STD_X_ANIMATION: {
#if DEBUG_COUTS
					std::cout << "Found Animation for: " << rets << std::endl;
#endif
					ProcessAnimation(pActualAnimationSet);
				}break;
				}
			}
		}

#else
		xF::xAnimationSet tmp;
		pAnimation->Animations.push_back(tmp);
		xF::xAnimationSet* pActualAnimationSet = &pAnimation->Animations.back();
		pActualAnimationSet->Name = name;

		while (pData[index] != '}') {
			if (pData[index] != '{') {
				std::string rets;
				unsigned int Ret = GetxTemplateTypeChar(rets);

				if (Ret == STD_BREAK)
					break;

				switch (Ret) {
					case xF::STD_X_ANIMATION: {
	#if DEBUG_ANIMATION
						LogPrintDebug("Found Animation [%s] ", rets.c_str());
	#endif
						ProcessAnimation(pActualAnimationSet);

					}break;
				}

				advance_to_next_close_brace_pre();
			}
			index++;
		}



#endif
	}
#if !USE_STRING_STREAM && USE_PREFETCH_ANIMATION_SETS_NUM
	unsigned int XDataBase::prefetch_animation_num() {
		PROFILING_SCOPE("prefetch_animation_num");

			unsigned int current_index = index;
			unsigned int animation_sets = 0;
			bool ended = false;
			while (!ended) {

				while (pData[index] != '{') {

					if (pData[index] == '\0') {
						ended = true;
						break;
					}

					index++;
				}

				if (ended)
					break;

				std::string rets;
				unsigned int Ret = GetxTemplateTypeChar(rets);

				if (Ret == STD_X_ANIMATIONSET)
					animation_sets++;



				advance_to_next_close_brace();
			}

			index = current_index;

		return animation_sets;
	}
#endif
	void XDataBase::ProcessAnimation(xF::xAnimationSet* out) {
#if PROFILE_ANIMATION
		PROFILING_SCOPE("ProcessAnimation")
#endif

#if USE_STRING_STREAM
		xF::xSTRING Line;
		while ((Line.find("{") == std::string::npos) && (Line.find("}") == std::string::npos)) {
			std::getline(m_ActualStream, Line);
		}
		unsigned int FBracket = Line.find("{");	unsigned int LBracket = Line.find("}");	unsigned int dif = LBracket - FBracket;
		Line = Line.substr(++FBracket, --dif);	std::string::iterator end_pos = std::remove(Line.begin(), Line.end(), ' ');
		Line.erase(end_pos, Line.end());

		xF::xAnimationBone	tmp;
		out->BonesRef.push_back(tmp);
		xF::xAnimationBone* pCurrentAnimBone = &out->BonesRef.back();
		pCurrentAnimBone->BoneName = Line;

		while (Line.find("}") == std::string::npos) {
			std::getline(m_ActualStream, Line);
			if (Line.find("{") != std::string::npos) {
				xSTRING rets;
				switch (GetxTemplateType(Line, &rets)) {
				case xF::STD_X_ANIMATION_KEY: {
#if DEBUG_COUTS
					cout << "Found Animation Key: " << rets << endl;
#endif
					char type;
					m_ActualStream >> type >> c_temp;

					switch (type) {
					case '0': {
#if DEBUG_COUTS
						cout << "Type Rotation" << endl;
#endif
						ProcessAnimationKey_Rotation(pCurrentAnimBone);
					}break;
					case '1': {
#if DEBUG_COUTS
						cout << "Type Scale" << endl;
#endif
						ProcessAnimationKey_Scale(pCurrentAnimBone);
					}break;
					case '2': {
#if DEBUG_COUTS
						cout << "Type Position" << endl;
#endif
						ProcessAnimationKey_Position(pCurrentAnimBone);
					}break;
					} // switch(type){


				}break; // case STD_X_ANIMATION_KEY:{
				case xF::STD_X_ANIMATION_OPTIONS: {
#if DEBUG_COUTS
					cout << "Found Animation Options: " << rets << endl;
#endif
					GetNextEndBracket();
				}break;
				} // switch(GetTemplateType(Line,&rets)){
			}
		}
#else
		while(pData[index] != '{') {
			index++;
		}
		index++;
		while (pData[index] != '{'){
			index++;
		}
		index++;
		while (pData[index] == ' ') {
			index++;
		}
		unsigned int token = index;
		index++;
		while (pData[index] != ' ') {
			index++;
		}
		index++;
		char cBoneName[32];
		cBoneName[31] = '\0';
		unsigned int g_size = index - token;
		memcpy(cBoneName,&pData[token], g_size);
		cBoneName[g_size-1] = '\0';

		xF::xAnimationBone	tmp;
		out->BonesRef.push_back(tmp);
		xF::xAnimationBone* pCurrentAnimBone = &out->BonesRef.back();

		for(std::size_t i =0;i<m_pActualMesh->Skeleton.Bones.size();i++){
			if(strcmp(cBoneName, m_pActualMesh->Skeleton.Bones[i].Name.c_str())==0){
				pCurrentAnimBone->BoneID = i;
				break;
			}
		}

		pCurrentAnimBone->BoneName = std::string(cBoneName, g_size - 1);


#if DEBUG_ANIMATION
		LogPrintDebug("Bone name: [%s]",pCurrentAnimBone->BoneName.c_str());
#endif
		while (pData[index] != '}') {
			index++;
		}
		index++;

		while (pData[index] != '}') {
			if (pData[index] != '{') {
				std::string rets;
				unsigned int Ret = GetxTemplateTypeChar(rets);

				if (Ret == STD_BREAK)
					break;

				switch (Ret) {
					case xF::STD_X_ANIMATION_OPTIONS: {
	#if DEBUG_ANIMATION
					LogPrintDebug("Found AnimationOptions [%s] ", rets.c_str());
	#endif


					}break;

					case xF::STD_X_ANIMATION_KEY: {
	#if DEBUG_ANIMATION
					LogPrintDebug("Found AnimationKey [%s] ", rets.c_str());
	#endif

						unsigned int token = 0;
						while (pData[index] != ';') {
							if(pData[index] == ' ')
								token = index;
							index++;
						}
						char cIngeger[16];
						cIngeger[15] = '\0';
						g_size = index - token;
						memcpy(cIngeger,&pData[token+1], g_size);
						cIngeger[g_size-1] = '\0';
						unsigned int type = static_cast<xDWORD>(atoi(cIngeger));

#if DEBUG_ANIMATION
						LogPrintDebug("Found AnimationKey type [%d] ", type);
#endif
							switch (type) {
								case 0: {
		#if DEBUG_ANIMATION
									LogPrintDebug("Found AnimationKey type Rotation");
		#endif
									ProcessAnimationKey_Rotation(pCurrentAnimBone);

								}break;
								case 1: {
		#if DEBUG_ANIMATION
									LogPrintDebug("Found AnimationKey type Scale");
		#endif
									ProcessAnimationKey_Scale(pCurrentAnimBone);

								}break;
								case 2: {
		#if DEBUG_ANIMATION
									LogPrintDebug("Found AnimationKey type Position");
		#endif
									ProcessAnimationKey_Position(pCurrentAnimBone);

								}break;
							}

					}break;
				}

				advance_to_next_close_brace_pre();
			}
			index++;
		}

#endif
	}

	void XDataBase::ProcessAnimationKey_Rotation(xF::xAnimationBone* out) {
#if PROFILE_ANIMATION_ROTATION
		PROFILING_SCOPE("ProcessAnimationKey_Rotation")
#endif
#if USE_STRING_STREAM
		int size_vec = 0;
		m_ActualStream >> size_vec >> c_temp;

#if USE_VECTOR_RESERVE_AND_PUSH
		out->RotationKeys.reserve(size_vec);
#elif USE_VECTOR_ARRAY_MODE
		out->RotationKeys = std::vector<xRotationKey>(size_vec);
#endif
		xRotationKey tmp;
		for (int i = 0; i < size_vec; i++) {
			m_ActualStream >> tmp.t.i_atTime >> c_temp >> c_temp >> c_temp >> tmp.Rot.w >> c_temp >> tmp.Rot.x >> c_temp >> tmp.Rot.y >> c_temp >> tmp.Rot.z >> c_temp >> c_temp >> c_temp;
#if DEBUG_COUTS
			cout << "time: " << tmp.t.i_atTime << " w: " << tmp.Rot.w << " x: " << tmp.Rot.x << " y: " << tmp.Rot.y << " z: " << tmp.Rot.z << endl;
#endif
#if USE_VECTOR_RESERVE_AND_PUSH
			out->RotationKeys.push_back(tmp);
#elif USE_VECTOR_ARRAY_MODE
			out->RotationKeys[i].t.i_atTime = tmp.t.i_atTime;
			out->RotationKeys[i].Rot.x = tmp.Rot.x;
			out->RotationKeys[i].Rot.y = tmp.Rot.y;
			out->RotationKeys[i].Rot.z = tmp.Rot.z;
			out->RotationKeys[i].Rot.w = tmp.Rot.w;
#endif
		}
		m_ActualStream >> c_temp;
#else

		if(pData[index] == ';')
			index++;

		unsigned int token = 0;
		while (pData[index] != ';') {
			if (pData[index] == ' ')
				token = index;
			index++;
		}

		char cIngeger[16];
		cIngeger[15] = '\0';
		unsigned int g_size = index - token;
		memcpy(cIngeger, &pData[token + 1], g_size);
		cIngeger[g_size - 1] = '\0';
		unsigned int num_keys = static_cast<xDWORD>(atoi(cIngeger));

#if USE_VECTOR_RESERVE_AND_PUSH
		out->RotationKeys.reserve(size_vec);
#elif USE_VECTOR_ARRAY_MODE
		out->RotationKeys = std::vector<xRotationKey>(num_keys);
#endif

		while (pData[index] != ';') {
			index++;
		}
		index++;
		while (pData[index] != ';') {
			index++;
			if(pData[index] == ' ')
				token = index;
		}

		char cVar[16];
		cVar[15] = '\0';
		unsigned int current_index = index;
		unsigned int num_components = 0;
		for(unsigned int i=0;i<num_keys;i++){
			num_components = 0;
			while(num_components < 6) {

				while (pData[current_index] != ';' && num_components < 2) {

					current_index++;
				}

				while (pData[current_index] != ',' && num_components >= 2 && num_components != 5) {

					current_index++;
				}

				while (pData[current_index] != ';' && num_components == 5) {
					current_index++;
				}

				if (pData[current_index] == ';' && num_components == 5) {
					g_size = current_index - token;
					memcpy(cVar, &pData[token + 1], g_size);
					cVar[g_size - 1] = '\0';
					out->RotationKeys[i].Rot.v[3] = static_cast<float>(atof(cVar));
					token = current_index;
					if(i != (num_keys - 1)){
						while (pData[current_index] != ',') {
							current_index++;
						}
					}
					current_index++;
					while (pData[current_index] != ';') {
						current_index++;
						if (pData[current_index] == ' ')
							token = current_index;
					}
					break;
				}

				if (pData[current_index] == ',' && num_components == 4) {

					g_size = current_index - token;
					memcpy(cVar, &pData[token + 1], g_size);
					cVar[g_size - 1] = '\0';
					out->RotationKeys[i].Rot.v[2] = static_cast<float>(atof(cVar));
					token = current_index;
					num_components = 5;
				}

				if (pData[current_index] == ',' && num_components == 3) {

					g_size = current_index - token;
					memcpy(cVar, &pData[token + 1], g_size);
					cVar[g_size - 1] = '\0';
					out->RotationKeys[i].Rot.v[1] = static_cast<float>(atof(cVar));
					token = current_index;
					num_components = 4;
				}

				if (pData[current_index] == ',' && num_components == 2) {

					g_size = current_index - token;
					memcpy(cVar, &pData[token + 1], g_size);
					cVar[g_size - 1] = '\0';
					out->RotationKeys[i].Rot.v[0] = static_cast<float>(atof(cVar));
					token = current_index;
					num_components = 3;
				}

				if (pData[current_index] == ';' && num_components == 1) {
					token = current_index;
					num_components = 2;
				}

				if(pData[current_index] == ';' && num_components == 0){
					char cIngeger[16];
					cIngeger[15] = '\0';
					g_size = current_index - token;
					memcpy(cIngeger, &pData[token + 1], g_size);
					cIngeger[g_size - 1] = '\0';
					out->RotationKeys[i].t.i_atTime = static_cast<xDWORD>(atoi(cIngeger));
					token = current_index;
					num_components=1;
				}

				current_index++;
			}
		}
		index = current_index;

#if DEBUG_ANIMATION_KEYS
		for(unsigned int i=0;i<out->RotationKeys.size();i++){
			LogPrintDebug("Rotation key time [%d] keys [%f] [%f] [%f] [%f] ", out->RotationKeys[i].t.i_atTime, out->RotationKeys[i].Rot.x, out->RotationKeys[i].Rot.y, out->RotationKeys[i].Rot.z, out->RotationKeys[i].Rot.w);
		}
#endif

#endif

	}

	void XDataBase::ProcessAnimationKey_Scale(xF::xAnimationBone* out) {
#if PROFILE_ANIMATION_SCALE
		PROFILING_SCOPE("ProcessAnimationKey_Scale")
#endif
#if USE_STRING_STREAM
		int size_vec = 0;
		m_ActualStream >> size_vec >> c_temp;
#if USE_VECTOR_RESERVE_AND_PUSH
		out->ScaleKeys.reserve(size_vec);
#elif USE_VECTOR_ARRAY_MODE
		out->ScaleKeys = std::vector<xScaleKey>(size_vec);
#endif
		xScaleKey tmp;
		for (int i = 0; i < size_vec; i++) {
			m_ActualStream >> tmp.t.i_atTime >> c_temp >> c_temp >> c_temp >> tmp.Scale.x >> c_temp >> tmp.Scale.y >> c_temp >> tmp.Scale.z >> c_temp >> c_temp >> c_temp;
#if DEBUG_COUTS
			cout << "time: " << tmp.t.i_atTime << " x: " << tmp.Scale.x << " y: " << tmp.Scale.y << " z: " << tmp.Scale.z << endl;
#endif
#if USE_VECTOR_RESERVE_AND_PUSH
			out->ScaleKeys.push_back(tmp);
#elif USE_VECTOR_ARRAY_MODE
			out->ScaleKeys[i].t.i_atTime = tmp.t.i_atTime;
			out->ScaleKeys[i].Scale.x = tmp.Scale.x;
			out->ScaleKeys[i].Scale.y = tmp.Scale.y;
			out->ScaleKeys[i].Scale.z = tmp.Scale.z;
#endif
		}

		m_ActualStream >> c_temp;
#else
		if (pData[index] == ';')
			index++;

		unsigned int token = 0;
		while (pData[index] != ';') {
			if (pData[index] == ' ')
				token = index;
			index++;
		}

		char cIngeger[16];
		cIngeger[15] = '\0';
		unsigned int g_size = index - token;
		memcpy(cIngeger, &pData[token + 1], g_size);
		cIngeger[g_size - 1] = '\0';
		unsigned int num_keys = static_cast<xDWORD>(atoi(cIngeger));

#if USE_VECTOR_RESERVE_AND_PUSH
		out->ScaleKeys.reserve(size_vec);
#elif USE_VECTOR_ARRAY_MODE
		out->ScaleKeys = std::vector<xScaleKey>(num_keys);
#endif

		while (pData[index] != ';') {
			index++;
		}
		index++;
		while (pData[index] != ';') {
			index++;
			if (pData[index] == ' ')
				token = index;
		}


		char cVar[16];
		cVar[15] = '\0';
		unsigned int current_index = index;
		unsigned int num_components = 0;
		for (unsigned int i = 0; i < num_keys; i++) {
			num_components = 0;
			while (num_components < 5) {

				while (pData[current_index] != ';' && num_components < 2) {

					current_index++;
				}

				while (pData[current_index] != ',' && num_components >= 2 && num_components != 4) {

					current_index++;
				}

				while (pData[current_index] != ';' && num_components == 4) {
					current_index++;
				}

				if (pData[current_index] == ';' && num_components == 4) {
					g_size = current_index - token;
					memcpy(cVar, &pData[token + 1], g_size);
					cVar[g_size - 1] = '\0';
					out->ScaleKeys[i].Scale.v[2] = static_cast<float>(atof(cVar));
					token = current_index;
					if (i != (num_keys - 1)) {
						while (pData[current_index] != ',') {
							current_index++;
						}
					}
					current_index++;
					while (pData[current_index] != ';') {
						current_index++;
						if (pData[current_index] == ' ')
							token = current_index;
					}
					break;
				}

				if (pData[current_index] == ',' && num_components == 3) {
					g_size = current_index - token;
					memcpy(cVar, &pData[token + 1], g_size);
					cVar[g_size - 1] = '\0';
					out->ScaleKeys[i].Scale.v[1] = static_cast<float>(atof(cVar));
					token = current_index;
					num_components = 4;
				}

				if (pData[current_index] == ',' && num_components == 2) {

					g_size = current_index - token;
					memcpy(cVar, &pData[token + 1], g_size);
					cVar[g_size - 1] = '\0';
					out->ScaleKeys[i].Scale.v[0] = static_cast<float>(atof(cVar));
					token = current_index;
					num_components = 3;
				}

				if (pData[current_index] == ';' && num_components == 1) {
					token = current_index;
					num_components = 2;
				}

				if (pData[current_index] == ';' && num_components == 0) {
					char cIngeger[16];
					cIngeger[15] = '\0';
					g_size = current_index - token;
					memcpy(cIngeger, &pData[token + 1], g_size);
					cIngeger[g_size - 1] = '\0';
					out->ScaleKeys[i].t.i_atTime = static_cast<xDWORD>(atoi(cIngeger));
					token = current_index;
					num_components = 1;
				}

				current_index++;
			}
		}
		index = current_index;

#if DEBUG_ANIMATION_KEYS
		for (unsigned int i = 0; i < out->ScaleKeys.size(); i++) {
			LogPrintDebug("Scale key time [%d] keys [%f] [%f] [%f] [%f] ", out->ScaleKeys[i].t.i_atTime, out->ScaleKeys[i].Scale.x, out->ScaleKeys[i].Scale.y, out->ScaleKeys[i].Scale.z, out->ScaleKeys[i].Scale.w);
		}
#endif

#endif
	}

	void XDataBase::ProcessAnimationKey_Position(xF::xAnimationBone* out) {
	#if PROFILE_ANIMATION_POSITION
		PROFILING_SCOPE("ProcessAnimationKey_Position")
	#endif
#if USE_STRING_STREAM
		int size_vec = 0;
		m_ActualStream >> size_vec >> c_temp;
#if USE_VECTOR_RESERVE_AND_PUSH
		out->PositionKeys.reserve(size_vec);
#elif USE_VECTOR_ARRAY_MODE
		out->PositionKeys = std::vector<xPositionKey>(size_vec);
#endif
		xPositionKey tmp;
		for (int i = 0; i < size_vec; i++) {
			m_ActualStream >> tmp.t.i_atTime >> c_temp >> c_temp >> c_temp >> tmp.Position.x >> c_temp >> tmp.Position.y >> c_temp >> tmp.Position.z >> c_temp >> c_temp >> c_temp;
#if DEBUG_COUTS
			cout << "time: " << tmp.t.i_atTime << " x: " << tmp.Position.x << " y: " << tmp.Position.y << " z: " << tmp.Position.z << endl;
#endif

#if USE_VECTOR_RESERVE_AND_PUSH
			out->PositionKeys.push_back(tmp);
#elif USE_VECTOR_ARRAY_MODE
			out->PositionKeys[i].t.i_atTime = tmp.t.i_atTime;
			out->PositionKeys[i].Position.x = tmp.Position.x;
			out->PositionKeys[i].Position.y = tmp.Position.y;
			out->PositionKeys[i].Position.z = tmp.Position.z;
#endif
		}

		m_ActualStream >> c_temp;
#else
			if (pData[index] == ';')
				index++;

		unsigned int token = 0;
		while (pData[index] != ';') {
			if (pData[index] == ' ')
				token = index;
			index++;
		}

		char cIngeger[16];
		cIngeger[15] = '\0';
		unsigned int g_size = index - token;
		memcpy(cIngeger, &pData[token + 1], g_size);
		cIngeger[g_size - 1] = '\0';
		unsigned int num_keys = static_cast<xDWORD>(atoi(cIngeger));

#if USE_VECTOR_RESERVE_AND_PUSH
		out->PositionKeys.reserve(size_vec);
#elif USE_VECTOR_ARRAY_MODE
		out->PositionKeys = std::vector<xPositionKey>(num_keys);
#endif

		while (pData[index] != ';') {
			index++;
		}
		index++;
		while (pData[index] != ';') {
			index++;
			if (pData[index] == ' ')
				token = index;
		}


		char cVar[16];
		cVar[15] = '\0';
		unsigned int current_index = index;
		unsigned int num_components = 0;
		for (unsigned int i = 0; i < num_keys; i++) {
			num_components = 0;
			while (num_components < 5) {

				while (pData[current_index] != ';' && num_components < 2) {

					current_index++;
				}

				while (pData[current_index] != ',' && num_components >= 2 && num_components != 4) {

					current_index++;
				}

				while (pData[current_index] != ';' && num_components == 4) {
					current_index++;
				}

				if (pData[current_index] == ';' && num_components == 4) {
					g_size = current_index - token;
					memcpy(cVar, &pData[token + 1], g_size);
					cVar[g_size - 1] = '\0';
					out->PositionKeys[i].Position.v[2] = static_cast<float>(atof(cVar));
					token = current_index;
					if (i != (num_keys - 1)) {
						while (pData[current_index] != ',') {
							current_index++;
						}
					}
					current_index++;
					while (pData[current_index] != ';') {
						current_index++;
						if (pData[current_index] == ' ')
							token = current_index;
					}
					break;
				}

				if (pData[current_index] == ',' && num_components == 3) {
					g_size = current_index - token;
					memcpy(cVar, &pData[token + 1], g_size);
					cVar[g_size - 1] = '\0';
					out->PositionKeys[i].Position.v[1] = static_cast<float>(atof(cVar));
					token = current_index;
					num_components = 4;
				}

				if (pData[current_index] == ',' && num_components == 2) {

					g_size = current_index - token;
					memcpy(cVar, &pData[token + 1], g_size);
					cVar[g_size - 1] = '\0';
					out->PositionKeys[i].Position.v[0] = static_cast<float>(atof(cVar));
					token = current_index;
					num_components = 3;
				}

				if (pData[current_index] == ';' && num_components == 1) {
					token = current_index;
					num_components = 2;
				}

				if (pData[current_index] == ';' && num_components == 0) {
					char cIngeger[16];
					cIngeger[15] = '\0';
					g_size = current_index - token;
					memcpy(cIngeger, &pData[token + 1], g_size);
					cIngeger[g_size - 1] = '\0';
					out->PositionKeys[i].t.i_atTime = static_cast<xDWORD>(atoi(cIngeger));
					token = current_index;
					num_components = 1;
				}

				current_index++;
			}
		}
		index = current_index;

#if DEBUG_ANIMATION_KEYS
		for (unsigned int i = 0; i < out->PositionKeys.size(); i++) {
			LogPrintDebug("Position key time [%d] keys [%f] [%f] [%f] [%f] ", out->PositionKeys[i].t.i_atTime, out->PositionKeys[i].Position.x, out->PositionKeys[i].Position.y, out->PositionKeys[i].Position.z, out->PositionKeys[i].Position.w);
		}
#endif

#endif
	}

	void XDataBase::ProcessSkinHeader(xF::xMeshGeometry* pGeometry) {
#if PROFILE_SKIN_HEADER
		PROFILING_SCOPE("ProcessSkinHeader")
#endif
#if USE_STRING_STREAM
		m_ActualStream >> pGeometry->Info.SkinMeshHeader.MaxNumWeightPerVertex >> c_temp;
		m_ActualStream >> pGeometry->Info.SkinMeshHeader.MaxNumWeightPerFace >> c_temp;
		m_ActualStream >> pGeometry->Info.SkinMeshHeader.NumBones >> c_temp;

		if (pGeometry->Info.SkinMeshHeader.MaxNumWeightPerVertex == 4) {
			pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_SKININDEXES0;
			pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_SKINWEIGHTS0;
		}
		else if (pGeometry->Info.SkinMeshHeader.MaxNumWeightPerVertex == 8) {
			pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_SKININDEXES0;
			pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_SKINWEIGHTS0;
			pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_SKININDEXES1;
			pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_SKINWEIGHTS1;
		}
#if USE_VECTOR_RESERVE_AND_PUSH
		pGeometry->Info.SkinWeights.reserve(pGeometry->Info.SkinMeshHeader.NumBones);
		for (std::size_t i = 0; i < pGeometry->Info.SkinMeshHeader.NumBones; i++) {
			xSkinWeights tmp;
			pGeometry->Info.SkinWeights.push_back(tmp);
		}
#elif USE_VECTOR_ARRAY_MODE
		pGeometry->Info.SkinWeights = std::vector<xSkinWeights>(pGeometry->Info.SkinMeshHeader.NumBones);
#endif

		pGeometry->Info.SkinMeshHeader.NumBonesProcess = 0;

		GetNextEndBracket();
#else

		int current_index = index;
		int token = 0;
		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == ' ')
				token = current_index;
		}
		char cInteger[16];
		unsigned int g_size = current_index - token;
		memcpy(cInteger, &pData[token + 1], g_size);
		cInteger[g_size-1] = '\0';
		pGeometry->Info.SkinMeshHeader.MaxNumWeightPerVertex = static_cast<xWORD>(atoi(cInteger));

		current_index++;

		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == ' ')
				token = current_index;
		}

		g_size = current_index - token;
		memcpy(cInteger, &pData[token + 1], g_size);
		cInteger[g_size - 1] = '\0';
		pGeometry->Info.SkinMeshHeader.MaxNumWeightPerFace = static_cast<xWORD>(atoi(cInteger));

		current_index++;

		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == ' ')
				token = current_index;
		}

		g_size = current_index - token;
		memcpy(cInteger, &pData[token + 1], g_size);
		cInteger[g_size - 1] = '\0';
		pGeometry->Info.SkinMeshHeader.NumBones = static_cast<xWORD>(atoi(cInteger));

#if DEBUG_COUTS
		LogPrintDebug("MaxNumWeightPerVertex[%d] MaxNumWeightPerFace[%d] NumBones[%d]", pGeometry->Info.SkinMeshHeader.MaxNumWeightPerVertex, pGeometry->Info.SkinMeshHeader.MaxNumWeightPerFace, pGeometry->Info.SkinMeshHeader.NumBones);
#endif
		if (pGeometry->Info.SkinMeshHeader.MaxNumWeightPerVertex == 4) {
			pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_SKININDEXES0;
			pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_SKINWEIGHTS0;
		}
		else if (pGeometry->Info.SkinMeshHeader.MaxNumWeightPerVertex == 8) {
			pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_SKININDEXES0;
			pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_SKINWEIGHTS0;
			pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_SKININDEXES1;
			pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_SKINWEIGHTS1;
		}
#if USE_VECTOR_RESERVE_AND_PUSH
		pGeometry->Info.SkinWeights.reserve(pGeometry->Info.SkinMeshHeader.NumBones);
		for (std::size_t i = 0; i < pGeometry->Info.SkinMeshHeader.NumBones; i++) {
			xSkinWeights tmp;
			pGeometry->Info.SkinWeights.push_back(tmp);
		}
#elif USE_VECTOR_ARRAY_MODE
		pGeometry->Info.SkinWeights = std::vector<xSkinWeights>(pGeometry->Info.SkinMeshHeader.NumBones);
#endif

		pGeometry->Info.SkinMeshHeader.NumBonesProcess = 0;

		index = current_index;



#endif
	}

	void  XDataBase::ProcessSkinWeights(xF::xMeshGeometry* pGeometry) {
#if PROFILE_SKIN_WEIGHTS
		PROFILING_SCOPE("ProcessSkinWeights")
#endif
#if USE_STRING_STREAM
		xF::xSkinWeights	*pSkin = &pGeometry->Info.SkinWeights[pGeometry->Info.SkinMeshHeader.NumBonesProcess];

		m_ActualStream >> c_temp >> pSkin->NodeName;

		pSkin->NodeName = pSkin->NodeName.substr(0, pSkin->NodeName.size() - 2);

		xDWORD	NumWeights;
		m_ActualStream >> NumWeights >> c_temp;

#if USE_VECTOR_RESERVE_AND_PUSH
		pSkin->VertexIndices.reserve(NumWeights);
		pSkin->Weights.reserve(NumWeights);
#elif USE_VECTOR_ARRAY_MODE
		pSkin->VertexIndices = std::vector<xDWORD>(NumWeights);
		pSkin->Weights = std::vector<xFLOAT>(NumWeights);
#endif
		xDWORD tmp_d;
		for (unsigned int i = 0; i < NumWeights; i++) {
			m_ActualStream >> tmp_d >> c_temp;
#if USE_VECTOR_RESERVE_AND_PUSH
			pSkin->VertexIndices.push_back(tmp_d);
#elif USE_VECTOR_ARRAY_MODE
			pSkin->VertexIndices[i] = tmp_d;
#endif
		}

		xFLOAT tmp_f;
		for (unsigned int i = 0; i < NumWeights; i++) {
			m_ActualStream >> tmp_f >> c_temp;
#if USE_VECTOR_RESERVE_AND_PUSH
			pSkin->Weights.push_back(tmp_f);
#elif USE_VECTOR_ARRAY_MODE
			pSkin->Weights[i] = tmp_f;
#endif
		}

		for (int i = 0; i < 16; i++) {
			m_ActualStream >> pSkin->MatrixOffset.mat[i] >> c_temp;
		}

		pGeometry->Info.SkinMeshHeader.NumBonesProcess++;

		GetNextEndBracket();
#else
		xF::xSkinWeights	*pSkin = &pGeometry->Info.SkinWeights[pGeometry->Info.SkinMeshHeader.NumBonesProcess];

		int current_index = index;
		int token = 0;
		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == '"' && token==0)
				token = current_index;
		}
		token++;
		pSkin->NodeName = std::string(&pData[token], (current_index-1) - token);
		current_index++;

		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == ' ')
				token = current_index;
		}

		char cInteger[16];
		cInteger[15] = '\0';
		unsigned int g_size = current_index - token;
		memcpy(cInteger, &pData[token + 1], g_size);
		cInteger[g_size-1] = '\0';
		xDWORD	NumWeights = static_cast<xDWORD>(atoi(cInteger));

		current_index++;

#if USE_VECTOR_RESERVE_AND_PUSH
		pSkin->VertexIndices.reserve(NumWeights);
		pSkin->Weights.reserve(NumWeights);
#elif USE_VECTOR_ARRAY_MODE
		pSkin->VertexIndices = std::vector<xDWORD>(NumWeights);
		pSkin->Weights = std::vector<xFLOAT>(NumWeights);
#endif
		token = 0;
		int counter = 0;
		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == ' ')
				token = current_index;

			if (pData[current_index] == ',' || pData[current_index] == ';') {
				g_size = current_index - token;
				memcpy(cInteger, &pData[token + 1], g_size);
				cInteger[g_size - 1] = '\0';
				pSkin->VertexIndices[counter++] = static_cast<xDWORD>(atoi(cInteger));
			}
		}

		current_index++;
		counter = 0;
		char cFloat[16];
		cFloat[15] = '\0';
		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == ' ')
				token = current_index;

			if (pData[current_index] == ',' || pData[current_index] == ';') {
				g_size = current_index - token;
				memcpy(cFloat, &pData[token + 1], g_size);
				cFloat[g_size - 1] = '\0';
				pSkin->Weights[counter++] = static_cast<float>(atof(cFloat));
			}
		}

		current_index++;
		counter = 0;
		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == ' ')
				token = current_index;

			if (pData[current_index] == ',' || pData[current_index] == ';') {
				g_size = current_index - token;
				memcpy(cFloat, &pData[token + 1], g_size);
				cFloat[g_size - 1] = '\0';
				pSkin->MatrixOffset.mat[counter++] = static_cast<float>(atof(cFloat));
				token = current_index;
				if(counter==16)
					break;
			}
		}

#if DEBUG_WEIGHTS
		LogPrintDebug("Node Name:[%s] Num Weights [%d]", pSkin->NodeName.c_str(), NumWeights);

		for (std::size_t i = 0; i < pSkin->VertexIndices.size(); i++) {
			LogPrintDebug("Index [%d] value [%d]", i, pSkin->VertexIndices[i] );
		}

		for (std::size_t i = 0; i < pSkin->Weights.size(); i++) {
			LogPrintDebug("Weight [%d] value [%f]", i, pSkin->Weights[i]);
		}

		for (std::size_t i = 0; i < 16; i++) {
			LogPrintDebug("Mat element [%d] value [%f]", i, pSkin->MatrixOffset.mat[i]);
		}
#endif

		index = current_index;



#endif
	}
#if !USE_STRING_STREAM
	void XDataBase::PrintNextCharsAndPause() {
		std::string str_ = std::string(&pData[index], 25);
		LogPrintDebug("NEXT[%s]", str_.c_str());
		system("pause");
	}
#endif
	void XDataBase::ProcessMaterialBlock(xF::xMeshGeometry *pGeometry) {
#if PROFILE_MATERIAL_BLOCK
		PROFILING_SCOPE("ProcessMaterialBlock")
#endif
#if USE_STRING_STREAM
		xDWORD	NumMaterials = 0;
		xDWORD	NumFaceIndices = 0;

		m_ActualStream >> NumMaterials >> c_temp;

#if USE_VECTOR_RESERVE_AND_PUSH
		pGeometry->MaterialList.Materials.reserve(NumMaterials);
		xMaterial tmp_m;
		for (unsigned int i = 0; i < NumMaterials; i++) {
			pGeometry->MaterialList.Materials.push_back(tmp_m);
		}
#elif USE_VECTOR_ARRAY_MODE
		pGeometry->MaterialList.Materials = std::vector<xMaterial>(NumMaterials);
#endif

		m_ActualStream >> NumFaceIndices >> c_temp;


#if USE_VECTOR_RESERVE_AND_PUSH
		pGeometry->MaterialList.FaceIndices.reserve(NumFaceIndices);
#elif USE_VECTOR_ARRAY_MODE
		pGeometry->MaterialList.FaceIndices = std::vector<xDWORD>(NumFaceIndices);
#endif
		xDWORD tmp_d;
		for (unsigned int i = 0; i < NumFaceIndices; i++) {

			m_ActualStream >> tmp_d >> c_temp;
#if USE_VECTOR_RESERVE_AND_PUSH
			pGeometry->MaterialList.FaceIndices.push_back(tmp_d);
#elif USE_VECTOR_ARRAY_MODE
			pGeometry->MaterialList.FaceIndices[i] = tmp_d;
#endif
		}

		xF::xSTRING Line;
		while (Line.find("}") == std::string::npos) {
			std::getline(m_ActualStream, Line);
			if (Line.find("{") != std::string::npos) {
				xSTRING rets;
				switch (GetxTemplateType(Line, &rets))
				{
				case xF::STD_X_MATERIAL: {
#if DEBUG_COUTS
					std::cout << "Found Material : " << rets << " num: " << pGeometry->MaterialList.NumMatProcess << std::endl;
#endif
					pGeometry->MaterialList.Materials[pGeometry->MaterialList.NumMatProcess].Name = rets;
					ProcessMaterial(&pGeometry->MaterialList.Materials[pGeometry->MaterialList.NumMatProcess]);
					pGeometry->MaterialList.NumMatProcess++;
				}break;

				case xF::STD_X_REF: {
#if DEBUG_COUTS
					std::cout << "Found Reference : " << rets << std::endl;
#endif
					Line = "";
				}break;

				}
			}
		}
#else
		xDWORD	NumMaterials = 0;
		xDWORD	NumFaceIndices = 0;
		int current_index = index;
		int token = 0;
		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == ' ')
				token = current_index;
		}
		char cInteger[16];
		cInteger[15] = '\0';
		unsigned int g_size = current_index - token;
		memcpy(cInteger, &pData[token + 1], g_size);
		cInteger[g_size - 1] = '\0';
		NumMaterials = static_cast<xDWORD>(atoi(cInteger));
		current_index++;

		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == ' ')
				token = current_index;
		}

		g_size = current_index - token;
		memcpy(cInteger, &pData[token + 1], g_size);
		cInteger[g_size - 1] = '\0';
		NumFaceIndices = static_cast<xDWORD>(atoi(cInteger));

		pGeometry->MaterialList.Materials = std::vector<xMaterial>(NumMaterials);
		pGeometry->MaterialList.FaceIndices = std::vector<xDWORD>(NumFaceIndices);

		current_index++;

		int counter = 0;
		while (pData[current_index] != ';') {
			current_index++;

			if (pData[current_index] == ' ' ) {
				token = current_index;
			}

			if (pData[current_index] == ',' || pData[current_index] == ';') {
				g_size = current_index - token;
				memcpy(cInteger, &pData[token + 1], g_size);
				cInteger[g_size - 1] = '\0';
				pGeometry->MaterialList.FaceIndices[counter++] = static_cast<xDWORD>(atoi(cInteger));
			}

		}
		current_index++;

		index = current_index;
#if DEBUG_NUMBER_MATERIALS
		LogPrintDebug("Num materials [%d] Num faceindices [%d]", NumMaterials,NumFaceIndices);
#endif

#if DEBUG_MATERIAL_INDICES
		for (std::size_t i = 0; i < pGeometry->MaterialList.FaceIndices.size(); i++) {
			LogPrintDebug("Index [%d] value [%d]", i, pGeometry->MaterialList.FaceIndices[i]);
		}
#endif

		while (pData[index] != '}') {
			if (pData[index] != '{') {
				std::string rets;
				unsigned int Ret = GetxTemplateTypeChar(rets);

				if(Ret==STD_BREAK)
					break;

				switch (Ret) {
				case xF::STD_X_MATERIAL: {
#if DEBUG_COUTS
					LogPrintDebug("Found Material [%s] Material Index: [%d]", rets.c_str(), pGeometry->MaterialList.NumMatProcess);
#endif
					pGeometry->MaterialList.Materials[pGeometry->MaterialList.NumMatProcess].Name = rets;
					ProcessMaterial(&pGeometry->MaterialList.Materials[pGeometry->MaterialList.NumMatProcess]);
					pGeometry->MaterialList.NumMatProcess++;



				}break;

				case xF::STD_X_REF: {
#if DEBUG_COUTS
					LogPrintDebug("Found Reference [%s]", rets.c_str());
#endif

				}break;
				}

				advance_to_next_close_brace_pre();
			}
			index++;
		}


#endif
#if DEBUG_END_BLOCKS
		LogPrintDebug("End mesh");
#endif

	}

	void XDataBase::ProcessMaterial(xMaterial* out) {
#if PROFILE_MATERIAL
		PROFILING_SCOPE("ProcessMaterial")
#endif
#if USE_STRING_STREAM
		m_ActualStream >> out->FaceColor.r >> c_temp >> out->FaceColor.g >> c_temp >> out->FaceColor.b >> c_temp >> out->FaceColor.a >> c_temp >> c_temp;

		m_ActualStream >> out->Power >> c_temp;

		m_ActualStream >> out->Specular.r >> c_temp >> out->Specular.g >> c_temp >> out->Specular.b >> c_temp >> c_temp;

		m_ActualStream >> out->Emissive.r >> c_temp >> out->Emissive.g >> c_temp >> out->Emissive.b >> c_temp >> c_temp;


		xF::xSTRING Line;
		while (Line.find("}") == std::string::npos) {
			std::getline(m_ActualStream, Line);
			if (Line.find("{") != std::string::npos) {
				xSTRING rets;
				switch (GetxTemplateType(Line, &rets))
				{
				case xF::STD_X_EFFECT_INSTANCE: {
#if DEBUG_COUTS
					std::cout << "Found Effect : " << rets << std::endl;
#endif
					out->bEffects = true;
					ProcessEffectInstance(&out->EffectInstance);
				}break;

				case xF::STD_X_TEXTURE: {
#if DEBUG_COUTS
					std::cout << "Material Found Texture : " << rets << std::endl;
#endif
					GetNextEndBracket();
				}break;
				}
			}
		}
#else
		int current_index = index;
		int token = 0;
		int colo_count = 0;
		int bracket_count = 0;
		char cFloat[16];
		cFloat[15] = '\0';
		unsigned g_size = 0;
		while (bracket_count<4) {
			current_index++;
			if (pData[current_index] == ' ')
				token = current_index;

			if (pData[current_index] == ';') {
				g_size = current_index - token;
				memcpy(cFloat, &pData[token + 1], g_size);
				cFloat[g_size-1] = '\0';
				out->FaceColor.v[colo_count++] = static_cast<float>(atof(cFloat));
				token = current_index;
				bracket_count++;
			}
		}

		current_index++;
		current_index++;

		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == ' ')
				token = current_index;
		}
		g_size = current_index - token;
		memcpy(cFloat, &pData[token + 1], g_size);
		cFloat[g_size - 1] = '\0';
		out->Power = static_cast<float>(atof(cFloat));

		current_index++;
		colo_count = 0;
		bracket_count = 0;
		while (bracket_count < 3) {
			current_index++;
			if (pData[current_index] == ' ')
				token = current_index;

			if (pData[current_index] == ';') {
				g_size = current_index - token;
				memcpy(cFloat, &pData[token + 1], g_size);
				cFloat[g_size - 1] = '\0';
				out->Specular.v[colo_count++] = static_cast<float>(atof(cFloat));
				token = current_index;
				bracket_count++;
			}
		}
		out->Specular.a = 1.0f;

		current_index++;
		colo_count = 0;
		bracket_count = 0;
		while (bracket_count < 3) {
			current_index++;
			if (pData[current_index] == ' ')
				token = current_index;

			if (pData[current_index] == ';') {
				g_size = current_index - token;
				memcpy(cFloat, &pData[token + 1], g_size);
				cFloat[g_size - 1] = '\0';
				out->Emissive.v[colo_count++] = static_cast<float>(atof(cFloat));
				token = current_index;
				bracket_count++;
			}
		}
		out->Emissive.a = 1.0f;
#if DEBUG_MATERIAL_COLORS
		LogPrintDebug("Color [%f]  [%f]  [%f]  [%f]", out->FaceColor.r, out->FaceColor.g, out->FaceColor.b, out->FaceColor.a);
		LogPrintDebug("Spec Power [%f] ", out->Power);
		LogPrintDebug("Spec Color [%f]  [%f]  [%f]  [%f]", out->Specular.r, out->Specular.g, out->Specular.b, out->Specular.a);
		LogPrintDebug("Emissive Color [%f]  [%f]  [%f]  [%f]", out->Emissive.r, out->Emissive.g, out->Emissive.b, out->Emissive.a);
#endif
		index = current_index;

		while (pData[index] != '}') {
				if (pData[index] != '{') {
				std::string rets;
				unsigned int Ret = GetxTemplateTypeChar(rets);

				if(Ret==STD_BREAK)
					break;

				switch (Ret) {
					case xF::STD_X_EFFECT_INSTANCE: {
	#if DEBUG_EFFECT_INSTANCE
						LogPrintDebug("Found EffectInstance [%s]", rets.c_str());
	#endif
						out->bEffects = true;
						ProcessEffectInstance(&out->EffectInstance);

					}break;

					case xF::STD_X_TEXTURE: {
	#if DEBUG_COUTS
						LogPrintDebug("Found Texture [%s]", rets.c_str());
	#endif

					}break;
				}

				advance_to_next_close_brace_pre();
			}
			index++;
		}
#endif
#if DEBUG_END_BLOCKS
		LogPrintDebug("End of ProcessMaterial");
#endif
	}

	void XDataBase::ProcessEffectInstance(xF::xEffectInstance *out) {
#if PROFILE_EFFECT_INSTANCE
		PROFILING_SCOPE("ProcessEffectInstance")
#endif
#if USE_STRING_STREAM
		m_ActualStream >> c_temp >> out->ShaderFileName;

		out->ShaderFileName = out->ShaderFileName.substr(0, out->ShaderFileName.size() - 2);


		std::streampos PosStream = m_ActualStream.tellg();

		out->NumDefaults = 0;

		xF::xSTRING Line;
		while (Line.find("}") == std::string::npos) {
			std::getline(m_ActualStream, Line);
			if (Line.find("{") != std::string::npos) {
				xF::xSTRING rets;
				std_uint T = GetxTemplateType(Line, &rets);
				if (T == xF::STD_X_EFFECT_PARAMS_DWORD || T == xF::STD_X_EFFECT_PARAMS_FLOAT || T == xF::STD_X_EFFECT_PARAMS_STRING) {
					out->NumDefaults++;
					GetNextEndBracket();
				}
			}
		}

		m_ActualStream.seekg(PosStream);
		Line = "";
#if USE_VECTOR_RESERVE_AND_PUSH
		out->pDefaults.reserve(out->NumDefaults);
		for (std::size_t i = 0; i < out->NumDefaults; i++) {
			xEffectDefault tmp;
			out->pDefaults.push_back(tmp);
		}
#elif USE_VECTOR_ARRAY_MODE
		out->pDefaults = std::vector<xEffectDefault>(out->NumDefaults);
#endif

		out->NumProcess = 0;

		while (Line.find("}") == std::string::npos) {
			std::getline(m_ActualStream, Line);
			if (Line.find("{") != std::string::npos) {
				xF::xSTRING rets;
				switch (GetxTemplateType(Line, &rets))
				{
				case xF::STD_X_EFFECT_PARAMS_DWORD: {
#if DEBUG_COUTS
					std::cout << "Found DWORD " << std::endl;
#endif
					ProcessEffectDwords(&out->pDefaults[out->NumProcess++]);
				}break;

				case xF::STD_X_EFFECT_PARAMS_FLOAT: {
#if DEBUG_COUTS
					std::cout << "Found FLOAT " << std::endl;
#endif
					ProcessEffectFloats(&out->pDefaults[out->NumProcess++]);
				}break;

				case xF::STD_X_EFFECT_PARAMS_STRING: {
#if DEBUG_COUTS
					std::cout << "Found STRING " << std::endl;
#endif
					ProcessEffectString(&out->pDefaults[out->NumProcess++]);
				}break;
				}
			}
		}
#else
		int current_index = index;
		int token = 0;
		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == '"' && token == 0)
				token = current_index;
		}
		token++;
		out->ShaderFileName = std::string(&pData[token], (current_index - 1) - token);
		current_index++;

		index = current_index;

		out->NumDefaults = 0;
		while (pData[index] != '}') {
			if (pData[index] != '{') {
				std::string rets;
				unsigned int Ret = GetxTemplateTypeChar(rets);

				if(Ret==STD_BREAK)
					break;

				if (Ret == xF::STD_X_EFFECT_PARAMS_DWORD || Ret == xF::STD_X_EFFECT_PARAMS_FLOAT || Ret == xF::STD_X_EFFECT_PARAMS_STRING) {
					out->NumDefaults++;
				}

				while (pData[index] != '}') {
					index++;
				}
			}
			index++;
		}

		out->pDefaults = std::vector<xEffectDefault>(out->NumDefaults);
#if DEBUG_EFFECT_INSTANCE
		LogPrintDebug("Shader Name [%s] num defaults: [%d]", out->ShaderFileName.c_str(), out->NumDefaults);
#endif
		index = current_index;
		out->NumProcess = 0;

		while (pData[index] != '}') {
			if (pData[index] != '{') {
				std::string rets;
				unsigned int Ret = GetxTemplateTypeChar(rets);

				if (Ret == STD_BREAK)
					break;

				switch (Ret) {
					case xF::STD_X_EFFECT_PARAMS_DWORD: {
	#if DEBUG_EFFECT_DWORDS
						LogPrintDebug("Found EffectParamDWord [%s] index [%d]", rets.c_str(), out->NumProcess);
	#endif
						ProcessEffectDwords(&out->pDefaults[out->NumProcess++]);


					}break;

					case xF::STD_X_EFFECT_PARAMS_FLOAT: {
	#if DEBUG_EFFECT_FLOATS
						LogPrintDebug("Found EffectParamFloats [%s] index [%d]", rets.c_str(), out->NumProcess);
	#endif
						ProcessEffectFloats(&out->pDefaults[out->NumProcess++]);


					}break;

					case xF::STD_X_EFFECT_PARAMS_STRING: {
	#if DEBUG_EFFECT_STRING
						LogPrintDebug("Found EffectParamString [%s] index [%d]", rets.c_str(), out->NumProcess);
	#endif
						ProcessEffectString(&out->pDefaults[out->NumProcess++]);



					}break;
				}

				advance_to_next_close_brace_pre();
			}
			index++;
		}


#endif
#if DEBUG_END_BLOCKS
		LogPrintDebug("End of effect instance");
#endif
	}


	void XDataBase::ProcessEffectString(xF::xEffectDefault *out) {
#if PROFILE_EFFECT_STRING
		PROFILING_SCOPE("ProcessEffectString")
#endif
#if USE_STRING_STREAM
		out->Type = xF::STDX_STRINGS;

		m_ActualStream >> c_temp >> out->NameParam;

		out->NameParam = out->NameParam.substr(0, out->NameParam.size() - 2);

		m_ActualStream >> c_temp >> out->CaseString;

		out->CaseString = out->CaseString.substr(0, out->CaseString.size() - 2);

		GetNextEndBracket();
#else
		out->Type = xF::STDX_STRINGS;

		int current_index = index;
		int token = 0;
		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == '"' && token == 0)
				token = current_index;
		}

		token++;
		out->NameParam = std::string(&pData[token], (current_index - 1) - token);

		current_index++;
		token = 0;
		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == '"' && token == 0)
				token = current_index;
		}

		token++;
		out->CaseString = std::string(&pData[token], (current_index - 1) - token);

#if DEBUG_EFFECT_STRING
		LogPrintDebug("Name String [%s] value [%s]", out->NameParam.c_str(), out->CaseString.c_str());
#endif

		index = current_index;
#endif
	}

	void XDataBase::ProcessEffectFloats(xF::xEffectDefault *out) {
#if PROFILE_EFFECT_FLOATS
		PROFILING_SCOPE("ProcessEffectFloats")
#endif
#if USE_STRING_STREAM
		out->Type = xF::STDX_FLOATS;

		m_ActualStream >> c_temp >> out->NameParam;

		out->NameParam = out->NameParam.substr(0, out->NameParam.size() - 2);

		xDWORD NumElements = 0;
		m_ActualStream >> NumElements >> c_temp;

#if USE_VECTOR_RESERVE_AND_PUSH
		if (NumElements > 0)
			out->CaseFloat.reserve(NumElements);
#elif USE_VECTOR_ARRAY_MODE
		if (NumElements > 0)
			out->CaseFloat = std::vector<xFLOAT>(NumElements);
#endif
		xFLOAT tmp_f;
		for (std_uint i = 0; i < NumElements; i++) {
			m_ActualStream >> tmp_f >> c_temp;
#if USE_VECTOR_RESERVE_AND_PUSH
			out->CaseFloat.push_back(tmp_f);
#elif USE_VECTOR_ARRAY_MODE
			out->CaseFloat[i] = tmp_f;
#endif
		}


		GetNextEndBracket();
#else
		out->Type = xF::STDX_FLOATS;

		int current_index = index;
		int token = 0;
		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == '"' && token == 0)
				token = current_index;
		}
		token++;
		out->NameParam = std::string(&pData[token], (current_index - 1) - token);
		current_index++;

		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == ' ')
				token = current_index;
		}
		xDWORD NumElements = 0;
		char cInteger[16];
		cInteger[15] = '\0';
		unsigned int g_size = current_index - token;
		memcpy(cInteger, &pData[token + 1],g_size);
		cInteger[g_size-1] = '\0';
		NumElements = static_cast<xDWORD>(atoi(cInteger));
		current_index++;

		if (NumElements > 0)
			out->CaseFloat = std::vector<xFLOAT>(NumElements);

		char cFloat[16];
		cFloat[15] = '\0';
		unsigned int cont = 0;
		while (cont<NumElements){

			if (pData[current_index] == ' ')
				token = current_index;

			if (pData[current_index] == ',' || pData[current_index] == ';') {
				g_size = current_index - token;
				memcpy(cFloat, &pData[token + 1], g_size);
				cFloat[g_size - 1] = '\0';
				out->CaseFloat[cont++] = static_cast<xFLOAT>(atof(cFloat));
			}
			current_index++;
		}

#if DEBUG_EFFECT_FLOATS
		for (std::size_t i = 0; i < out->CaseFloat.size(); i++) {
			LogPrintDebug("Name Floats Param [%s] value [%f]", out->NameParam.c_str(), out->CaseFloat[i]);
		}
#endif

		index = current_index;

#endif
	}

	void XDataBase::ProcessEffectDwords(xEffectDefault *out) {
#if PROFILE_EFFECT_DWORDS
		PROFILING_SCOPE("ProcessEffectDwords")
#endif
#if USE_STRING_STREAM
		out->Type = xF::STDX_DWORDS;

		m_ActualStream >> c_temp >> out->NameParam;

		out->NameParam = out->NameParam.substr(0, out->NameParam.size() - 2);

		m_ActualStream >> out->CaseDWORD >> c_temp;

		GetNextEndBracket();
#else
		out->Type = xF::STDX_DWORDS;

		int current_index = index;
		int token = 0;
		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == '"' && token == 0)
				token = current_index;
		}
		token++;
		out->NameParam = std::string(&pData[token], (current_index - 1) - token);
		current_index++;

		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == ' ')
				token = current_index;
		}
		char cInteger[16];
		cInteger[15] = '\0';
		unsigned int g_size = current_index - token;
		memcpy(cInteger, &pData[token + 1], g_size);
		cInteger[g_size - 1] = '\0';
		out->CaseDWORD = static_cast<xDWORD>(atoi(cInteger));

#if DEBUG_EFFECT_DWORDS
		LogPrintDebug("Name Dword Param [%s] value [%d]", out->NameParam.c_str(), out->CaseDWORD);
#endif

		index = current_index;

#endif
	}

	void XDataBase::ProcessNormalsBlock(xF::xMeshGeometry *pGeometry) {
#if PROFILE_NORMALS_BLOCK
		PROFILING_SCOPE("ProcessNormalsBlock")
#endif
#if USE_STRING_STREAM
		xDWORD NumVertices = 0;
		m_ActualStream >> NumVertices >> c_temp;
#if USE_VECTOR_RESERVE_AND_PUSH
		pGeometry->Normals.reserve(NumVertices);
#elif USE_VECTOR_ARRAY_MODE
		pGeometry->Normals = std::vector<XVECTOR3>(NumVertices);
#endif
		float x = 0.0f, y = 0.0f, z = 0.0f;
		for (std_uint i = 0; i < NumVertices; i++) {
			m_ActualStream >> x >> c_temp >> y >> c_temp >> z >> c_temp >> c_temp;
#if USE_VECTOR_RESERVE_AND_PUSH
			pGeometry->Normals.push_back(XVECTOR3(x, y, z));
#elif USE_VECTOR_ARRAY_MODE
			pGeometry->Normals[i].x = x;
			pGeometry->Normals[i].y = y;
			pGeometry->Normals[i].z = z;
#endif
		}
		pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_NORMAL;

		GetNextEndBracket();
#else
		xDWORD NumVertices = 0;

		int current_index = index;
		int token = 0;
		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == ' ')
				token = current_index;
		}
		char cNumVerts[16];
		cNumVerts[15] = '\0';
		unsigned int g_size = current_index - token;
		memcpy(cNumVerts, &pData[token + 1], g_size);
		cNumVerts[g_size-1] = '\0';
		NumVertices = static_cast<xDWORD>(atoi(cNumVerts));
		current_index++;

#if USE_VECTOR_RESERVE_AND_PUSH
		pGeometry->Normals.reserve(NumVertices);
#elif USE_VECTOR_ARRAY_MODE
		pGeometry->Normals = std::vector<XVECTOR3>(NumVertices);
#endif

		char cVertComponent[15];
		cVertComponent[14] = '\0';
		int bracket_cont = 0;
		int component_count = 0;
		for (unsigned int i = 0; i < NumVertices; i++) {
			bracket_cont = 0;
			component_count = 0;
			while (pData[current_index] != ',') {

				if (pData[current_index] == ' ')
					token = current_index;

				if (pData[current_index] == ';' && bracket_cont < 3) {
					bracket_cont++;
					g_size = current_index - token;
					memcpy(cVertComponent, &pData[token + 1], g_size);
					cVertComponent[g_size - 1] = '\0';
					if (component_count < 3) {
						pGeometry->Normals[i].v[component_count++] = static_cast<float>(atof(cVertComponent));
					}
					token = current_index;
				}
				current_index++;
			}
			current_index++;
		}
		current_index++;

		pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_NORMAL;

#if DEBUG_NORMALS
		for (unsigned int i = 0; i < NumVertices; i++) {
			LogPrintDebug("[%f;%f;%f;,]", pGeometry->Normals[i].x, pGeometry->Normals[i].y, pGeometry->Normals[i].z);
		}
#endif

		index = current_index;


#endif

	}

	void  XDataBase::ProcessTexCoordinatesBlock(xF::xMeshGeometry *pGeometry) {
#if PROFILE_TEXCOORD_BLOCK
		PROFILING_SCOPE("ProcessTexCoordinatesBlock")
#endif
		xDWORD NumTexcoords = 0;

#if USE_STRING_STREAM
		float x = 0.0f, y = 0.0f;

		m_ActualStream >> NumTexcoords >> c_temp;
#if USE_VECTOR_RESERVE_AND_PUSH
		pGeometry->TexCoordinates[pGeometry->NumChannelsTexCoords].reserve(NumTexcoords);
#elif USE_VECTOR_ARRAY_MODE
		pGeometry->TexCoordinates[pGeometry->NumChannelsTexCoords] = std::vector<XVECTOR2>(NumTexcoords);
#endif
		for (std_uint i = 0; i < pGeometry->Normals.size(); i++) {
			m_ActualStream >> x >> c_temp >> y >> c_temp >> c_temp;
#if USE_VECTOR_RESERVE_AND_PUSH
			pGeometry->TexCoordinates[pGeometry->NumChannelsTexCoords].push_back(XVECTOR2(x, y));
#elif USE_VECTOR_ARRAY_MODE
			pGeometry->TexCoordinates[pGeometry->NumChannelsTexCoords][i].x = x;
			pGeometry->TexCoordinates[pGeometry->NumChannelsTexCoords][i].y = y;
#endif
		}

		pGeometry->NumChannelsTexCoords++;

		if (pGeometry->NumChannelsTexCoords == 1) {
			pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_TEXCOORD0;
		}
		else if (pGeometry->NumChannelsTexCoords == 2) {
			pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_TEXCOORD1;
		}
		else if (pGeometry->NumChannelsTexCoords == 3) {
			pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_TEXCOORD2;
		}
		else if (pGeometry->NumChannelsTexCoords == 4) {
			pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_TEXCOORD3;
		}




		GetNextEndBracket();
#else
		int current_index = index;
		int token = 0;
		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == ' ')
				token = current_index;
		}
		char cNumVerts[16];
		cNumVerts[15] = '\0';
		unsigned int g_size = current_index - token;
		memcpy(cNumVerts, &pData[token + 1], g_size);
		cNumVerts[g_size - 1] = '\0';
		NumTexcoords = static_cast<xDWORD>(atoi(cNumVerts));
		current_index++;


#if USE_VECTOR_RESERVE_AND_PUSH
		pGeometry->TexCoordinates[pGeometry->NumChannelsTexCoords].reserve(NumTexcoords);
#elif USE_VECTOR_ARRAY_MODE
		pGeometry->TexCoordinates[pGeometry->NumChannelsTexCoords] = std::vector<XVECTOR2>(NumTexcoords);
#endif

		char cVertComponent[16];
		cVertComponent[15] = '\0';
		int cont_brackets = 0;
		int cont_components = 0;
		for (unsigned int i = 0; i < NumTexcoords; i++) {
			cont_brackets = 0;
			cont_components = 0;
			while (pData[current_index] != ',') {

				if (pData[current_index] == ' ')
					token = current_index;

				if (pData[current_index] == ';') {
					cont_brackets++;
					g_size = current_index - token;
					memcpy(cVertComponent, &pData[token + 1], g_size);
					cVertComponent[g_size - 1] = '\0';

					if (cont_components < 2) {
						pGeometry->TexCoordinates[pGeometry->NumChannelsTexCoords][i].v[cont_components++] = static_cast<float>(atof(cVertComponent));
					}

					token = current_index;

					if (cont_brackets == 3)
						break;
				}
				current_index++;
			}
			current_index++;
		}

#if DEBUG_TEXCOORDS
		for (unsigned int i = 0; i < NumTexcoords; i++) {
			LogPrintDebug("[%f;%f;,]", pGeometry->TexCoordinates[pGeometry->NumChannelsTexCoords][i].x, pGeometry->TexCoordinates[pGeometry->NumChannelsTexCoords][i].y);
		}
#endif

		pGeometry->NumChannelsTexCoords++;

		if (pGeometry->NumChannelsTexCoords == 1) {
			pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_TEXCOORD0;
		}
		else if (pGeometry->NumChannelsTexCoords == 2) {
			pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_TEXCOORD1;
	}
		else if (pGeometry->NumChannelsTexCoords == 3) {
			pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_TEXCOORD2;
		}
		else if (pGeometry->NumChannelsTexCoords == 4) {
			pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_TEXCOORD3;
		}

		index = current_index;


#endif
	}

	void XDataBase::ProcessDeclDataBlock(xF::xMeshGeometry *pGeometry) {
#if PROFILE_DECL_DATA_BLOCK
		PROFILING_SCOPE("ProcessDeclDataBlock")
#endif
#if USE_STRING_STREAM
		xF::xDeclData	Data;
		xDWORD NumElements = 0;
		xDWORD NumValues = 0;
		m_ActualStream >> NumElements >> c_temp;
#if USE_VECTOR_RESERVE_AND_PUSH
		Data.Elements.reserve(NumElements);
#elif USE_VECTOR_ARRAY_MODE
		Data.Elements = std::vector<xVertexElement>(NumElements);
#endif
		xVertexElement tmp_xv;
		for (unsigned int i = 0; i < NumElements; i++) {

			m_ActualStream  >> tmp_xv.Type >> c_temp
				>> tmp_xv.Method >> c_temp
				>> tmp_xv.Usage >> c_temp
				>> tmp_xv.UsageIndex >> c_temp >> c_temp;
#if USE_VECTOR_RESERVE_AND_PUSH
			Data.Elements.push_back(tmp_xv);
#elif USE_VECTOR_ARRAY_MODE
			Data.Elements[i].Type = tmp_xv.Type;
			Data.Elements[i].Method = tmp_xv.Method;
			Data.Elements[i].Usage = tmp_xv.Usage;
			Data.Elements[i].UsageIndex = tmp_xv.UsageIndex;
#endif
		}

		m_ActualStream >> NumValues >> c_temp;
#if USE_VECTOR_RESERVE_AND_PUSH
		Data.Values.reserve(NumValues);
#elif USE_VECTOR_ARRAY_MODE
		Data.Values = std::vector<xDWORD>(NumValues);
#endif
		xDWORD	tmp_d;
		for (unsigned int i = 0; i < NumValues; i++) {
			m_ActualStream >> tmp_d >> c_temp;
#if USE_VECTOR_RESERVE_AND_PUSH
			Data.Values.push_back(tmp_d);
#elif USE_VECTOR_ARRAY_MODE
			Data.Values[i] = tmp_d;
#endif
		}
		
		#else
		int current_index = index;
		int token = 0, last_token = 0;
		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == ' ')
				token = current_index;
		}
		current_index++;

		xF::xDeclData	Data;
		xDWORD NumElements = 0;
		xDWORD NumValues = 0;

		char cNumVerts[16];
		cNumVerts[15] = '\0';
		unsigned int g_size = current_index - token;
		memcpy(cNumVerts, &pData[token + 1], g_size);
		cNumVerts[g_size - 1] = '\0';
		NumElements = static_cast<xDWORD>(atoi(cNumVerts));

#if USE_VECTOR_RESERVE_AND_PUSH
		Data.Elements.reserve(NumElements);
#elif USE_VECTOR_ARRAY_MODE
		Data.Elements = std::vector<xVertexElement>(NumElements);
#endif

		for (unsigned int i = 0; i < NumElements; i++) {
			while (pData[current_index] != ';') {
				current_index++;
				if (pData[current_index] == ' ')
					token = current_index;
			}			
			g_size = current_index - token;
			memcpy(cNumVerts, &pData[token + 1], g_size);
			cNumVerts[g_size - 1] = '\0';
			Data.Elements[i].Type = static_cast<xDWORD>(atoi(cNumVerts));

			token = current_index;
			current_index++;
			while (pData[current_index] != ';') {
				current_index++;
			}			
			g_size = current_index - token;
			memcpy(cNumVerts, &pData[token + 1], g_size);
			cNumVerts[g_size - 1] = '\0';
			Data.Elements[i].Method = static_cast<xDWORD>(atoi(cNumVerts));

			token = current_index;
			current_index++;
			while (pData[current_index] != ';') {
				current_index++;
			}			
			g_size = current_index - token;
			memcpy(cNumVerts, &pData[token + 1], g_size);
			cNumVerts[g_size - 1] = '\0';
			Data.Elements[i].Usage = static_cast<xDWORD>(atoi(cNumVerts));

			token = current_index;
			current_index++;
			while (pData[current_index] != ';') {
				current_index++;
			}
			g_size = current_index - token;
			memcpy(cNumVerts, &pData[token + 1], g_size);
			cNumVerts[g_size - 1] = '\0';
			Data.Elements[i].UsageIndex = static_cast<xDWORD>(atoi(cNumVerts));
			current_index++;
		}
		current_index++;
		while (pData[current_index] != ';') {
			current_index++;
			if (pData[current_index] == ' ')
				token = current_index;
		}
		g_size = current_index - token;
		memcpy(cNumVerts, &pData[token + 1], g_size);
		cNumVerts[g_size - 1] = '\0';
		NumValues = static_cast<xDWORD>(atoi(cNumVerts));
		current_index++;

#if USE_VECTOR_RESERVE_AND_PUSH
		Data.Values.reserve(NumValues);
#elif USE_VECTOR_ARRAY_MODE
		Data.Values = std::vector<xDWORD>(NumValues);
#endif

		xDWORD	tmp_d;
		char tok = ',';
		for (unsigned int i = 0; i < NumValues; i++) {

			if (i == NumValues - 1)
				tok = ';';

			while (pData[current_index] != tok) {
				current_index++;
				if (pData[current_index] == ' ')
					token = current_index;
			}

			g_size = current_index - token;
			memcpy(cNumVerts, &pData[token + 1], g_size);
			cNumVerts[g_size - 1] = '\0';
			tmp_d = static_cast<xDWORD>(atoll(cNumVerts));

#if USE_VECTOR_RESERVE_AND_PUSH
			Data.Values.push_back(tmp_d);
#elif USE_VECTOR_ARRAY_MODE
			Data.Values[i] = tmp_d;
#endif
			current_index++;
		}

		#endif

		xDWORD NumVertices = pGeometry->Positions.size();
#if USE_VECTOR_RESERVE_AND_PUSH
		float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
#endif
		for (unsigned int i = 0; i < NumElements; i++) {
			if (Data.Elements[i].Usage == xF::STDDECLUSAGE_TANGENT) {
#if USE_VECTOR_RESERVE_AND_PUSH
				pGeometry->Tangents.reserve(NumVertices);
#elif USE_VECTOR_ARRAY_MODE
				pGeometry->Tangents = std::vector<XVECTOR3>(NumVertices);
#endif
				int Offset = 3;
				for (unsigned int j = 0; j < NumVertices; j++) {
#if USE_VECTOR_RESERVE_AND_PUSH
					x = *(float*)(&Data.Values[Offset++]);
					y = *(float*)(&Data.Values[Offset++]);
					z = *(float*)(&Data.Values[Offset++]);
					pGeometry->Tangents.push_back(XVECTOR3(x, y, z));
#elif USE_VECTOR_ARRAY_MODE
					pGeometry->Tangents[j].x = *(float*)(&Data.Values[Offset++]);
					pGeometry->Tangents[j].y = *(float*)(&Data.Values[Offset++]);
					pGeometry->Tangents[j].z = *(float*)(&Data.Values[Offset++]);
					Offset += 3;
#endif
				}
				pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_TANGENT;
			}
			else if (Data.Elements[i].Usage == xF::STDDECLUSAGE_BINORMAL) {
#if USE_VECTOR_RESERVE_AND_PUSH
				pGeometry->Binormals.reserve(NumVertices);
#elif USE_VECTOR_ARRAY_MODE
				pGeometry->Binormals = std::vector<XVECTOR3>(NumVertices);
#endif
				int Offset = 0;
				for (unsigned int j = 0; j < NumVertices; j++) {
#if USE_VECTOR_RESERVE_AND_PUSH
					x = *(float*)(&Data.Values[Offset++]);
					y = *(float*)(&Data.Values[Offset++]);
					z = *(float*)(&Data.Values[Offset++]);
					pGeometry->Binormals.push_back(XVECTOR3(x, y, z));
#elif USE_VECTOR_ARRAY_MODE
					pGeometry->Binormals[j].x = *(float*)(&Data.Values[Offset++]);
					pGeometry->Binormals[j].y = *(float*)(&Data.Values[Offset++]);
					pGeometry->Binormals[j].z = *(float*)(&Data.Values[Offset++]);
					Offset += 3;
#endif
				}
				pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_BINORMAL;
			}
			else if (Data.Elements[i].Usage == xF::STDDECLUSAGE_TEXCOORD) {
#if USE_VECTOR_RESERVE_AND_PUSH
				float x = 0.0f, y = 0.0f;
				pGeometry->TexCoordinates[pGeometry->NumChannelsTexCoords].reserve(NumVertices);
#elif USE_VECTOR_ARRAY_MODE
				pGeometry->TexCoordinates[pGeometry->NumChannelsTexCoords] = std::vector<XVECTOR2>(NumVertices);
#endif
				int Offset = 0;
				for (unsigned int j = 0; j < NumVertices; j++) {
#if USE_VECTOR_RESERVE_AND_PUSH
					x = *(float*)(&Data.Values[Offset++]);
					y = *(float*)(&Data.Values[Offset++]);
					pGeometry->TexCoordinates[pGeometry->NumChannelsTexCoords].push_back(XVECTOR2(x, y));
#elif USE_VECTOR_ARRAY_MODE
					pGeometry->TexCoordinates[pGeometry->NumChannelsTexCoords][j].x = *(float*)(&Data.Values[Offset++]);
					pGeometry->TexCoordinates[pGeometry->NumChannelsTexCoords][j].y = *(float*)(&Data.Values[Offset++]);
#endif
				}

				pGeometry->NumChannelsTexCoords++;

				if (pGeometry->NumChannelsTexCoords == 1) {
					pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_TEXCOORD0;
				}
				else if (pGeometry->NumChannelsTexCoords == 2) {
					pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_TEXCOORD1;
				}
				else if (pGeometry->NumChannelsTexCoords == 3) {
					pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_TEXCOORD2;
				}
				else if (pGeometry->NumChannelsTexCoords == 4) {
					pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_TEXCOORD3;
				}

			}
			else if (Data.Elements[i].Usage == xF::STDDECLUSAGE_COLOR) {
#if USE_VECTOR_RESERVE_AND_PUSH
				pGeometry->VertexColors.reserve(NumVertices);
#elif USE_VECTOR_ARRAY_MODE
				pGeometry->VertexColors = std::vector<XVECTOR3>(NumVertices);
#endif
				int Offset = 0;
				for (unsigned int j = 0; j < NumVertices; j++) {
#if USE_VECTOR_RESERVE_AND_PUSH
					x = *(float*)(&Data.Values[Offset++]);
					y = *(float*)(&Data.Values[Offset++]);
					z = *(float*)(&Data.Values[Offset++]);
					if (Data.Elements[i].Type == xF::STDDECLTYPE_FLOAT4) {
						w = *(float*)(&Data.Values[Offset++]);
					}
					else {
						w = 0.0f;
					}
					pGeometry->VertexColors.push_back(XVECTOR3(x, y, z, w));
#elif USE_VECTOR_ARRAY_MODE
					pGeometry->VertexColors[j].x = *(float*)(&Data.Values[Offset++]);
					pGeometry->VertexColors[j].y = *(float*)(&Data.Values[Offset++]);
					pGeometry->VertexColors[j].z = *(float*)(&Data.Values[Offset++]);
					if (Data.Elements[i].Type == xF::STDDECLTYPE_FLOAT4) {
						pGeometry->VertexColors[j].w = *(float*)(&Data.Values[Offset++]);
					}
					else {
						pGeometry->VertexColors[j].w = 0.0f;
					}
#endif
				}
				pGeometry->VertexAttributes |= xF::xMeshGeometry::HAS_VERTEXCOLOR;
			}
		}

		#if USE_STRING_STREAM
			GetNextEndBracket();
		#else
			index = current_index;
		#endif
	}

	void XDataBase::GetNextEndBracket() {
		std::string Line;
		std::getline(m_ActualStream, Line);
		while (Line.find("}") == std::string::npos) {
			std::getline(m_ActualStream, Line);
		}
	}

	void XDataBase::ProcessMatrix(XMATRIX44* out) {
#if PROFILE_MATRICES
		PROFILING_SCOPE("ProcessMatrix")
#endif

#if USE_STRING_STREAM
			char tmp;
		for (int i = 0; i < 16; i++) {
			m_ActualStream >> out->mat[i] >> tmp;
		}
#else
		unsigned int current_index = index;
		unsigned int token = 0;
		char cFloat[16];
		cFloat[15] = '\0';
		unsigned int cont = 0;
		unsigned int size_f = 0;
		while (cont < 16) {

			if (pData[current_index] == ' ')
				token = current_index;

			if (pData[current_index] == ',' || pData[current_index] == ';') {
				size_f = current_index - token;
				memcpy(cFloat, &pData[token + 1], size_f);
				cFloat[size_f-1] = '\0';
				out->mat[cont++] = static_cast<xFLOAT>(atof(cFloat));
				token = current_index;
			}
			current_index++;
		}

		index = current_index;
	#if DEBUG_MATRICES
			for (unsigned int i = 0; i < 16; i++) {
				LogPrintDebug("[%f]", out->mat[i]);
			}
	#endif
#endif

	}
}
