
#ifndef XDATABASE_H
#define XDATABASE_H


#define USE_STRING_STREAM 0

#include <Config.h>

#include <utils/xDefs.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <stack>
#include <algorithm>
using namespace std;

namespace xF {

	class XDataBase
	{
	public:
    XDataBase(XDataBase&& other) = default;
		XDataBase() {}
		~XDataBase() {
			for (unsigned int i = 0; i < XMeshDataBase.size(); i++) {
				delete XMeshDataBase[i];

			}
			XMeshDataBase.clear();
		}
		bool	LoadXFile(const std::string	&FileName);
		bool	LoadXFromMemory(char* pData, const unsigned int &size);

		std::vector<xMeshContainer*>	XMeshDataBase;
		std::vector<xFinalGeometry>		MeshInfo;
    std::string m_name;

	private:
		void			CreateSubSets();
		bool			Parse(std::string);
		int	GetxTemplateType(std::string Line, std::string *retName = 0);

		void		ProcessFrameBlock(std::string &actual);
		void		ProcessMeshBlock(std::string actual);
		void		ProcessNormalsBlock(xF::xMeshGeometry *pGeometry);
		void		ProcessTexCoordinatesBlock(xF::xMeshGeometry *pGeometry);
		void		ProcessDeclDataBlock(xF::xMeshGeometry *pGeometry);
		void		ProcessMaterialBlock(xF::xMeshGeometry *pGeometry);
		void		ProcessSkinHeader(xF::xMeshGeometry* pGeometry);
		void		ProcessSkinWeights(xF::xMeshGeometry* pGeometry);

		void		ProcessTicksPerSecond(xF::xAnimationInfo* pAnimation);
		void		ProcessAnimationSet(xF::xAnimationInfo* pAnimation, const std::string name);

		void		GetNextEndBracket();

		void		ProcessAnimation(xF::xAnimationSet* out);
		void		ProcessAnimationKey_Rotation(xF::xAnimationBone* out);
		void		ProcessAnimationKey_Scale(xF::xAnimationBone* out);
		void		ProcessAnimationKey_Position(xF::xAnimationBone* out);

		void		ProcessMatrix(XMATRIX44* out);
		void		ProcessMaterial(xF::xMaterial* out);
		void		ProcessEffectInstance(xF::xEffectInstance *out);
		void		ProcessEffectString(xF::xEffectDefault *out);
		void		ProcessEffectFloats(xF::xEffectDefault *out);
		void		ProcessEffectDwords(xF::xEffectDefault *out);

#if !USE_STRING_STREAM
		void			advance_to_next_open_brace();
		void			advance_to_next_close_brace();
		void			advance_to_next_space();
		unsigned int	GetxTemplateTypeChar(std::string &retName);


		void		PrintNextCharsAndPause();

		void			advance_to_next_close_brace_pre();
		unsigned int	prefetch_animation_num();
#endif

		std::stringstream		 m_ActualStream;
		std::stack<std::string>	 m_Stack;
		xMeshContainer			*m_pActualMesh;
		char					 *pData;
		unsigned int			 index;
		// ...
		char					c_temp;

	};
}


#endif
