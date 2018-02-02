#ifndef T800_CIL_H
#define T800_CIL_H


#include <string>
#include <fstream>
#include <iostream>
#include <cstddef>
#include <algorithm>
using namespace std;

#define CIL_CALL_STB 1
#define CIL_LOG_OUTPUT 1

// Formats
#define CIL_PVRTC_2BPP_RGB_FMT		0
#define CIL_PVRTC_2BPP_RGBA_FMT		1
#define CIL_PVRTC_4BPP_RGB_FMT		2
#define CIL_PVRTC_4BPP_RGBA_FMT		3
#define CIL_PVRTCII_2BPP_RGB_FMT	4
#define CIL_PVRTCII_4BPP_RGB_FMT	5
#define CIL_ETC1_FMT				6
#define CIL_DXT1_FMT				7
#define CIL_DXT3_FMT				9
#define CIL_DXT5_FMT				11
#define CIL_ETC2_FMT				23 //ETC2 RGBA not RGBA, for that we have ETC1
#define CIL_RGBA32_FMT

//Channel types
#define CIL_CHT_UNSIGNED_BYTE_NORM			0
#define CIL_CHT_SIGNED_BYTE_NORM			1
#define CIL_CHT_UNSIGNED_BYTE				2
#define CIL_CHT_SIGNED_BYTE					3
#define CIL_CHT_UNSIGNED_SHORT_NORM			4
#define CIL_CHT_SIGNED_SHORT_NORM			5
#define CIL_CHT_UNSIGNED_SHORT				6
#define CIL_CHT_SIGNED_SHORT				7
#define CIL_CHT_UNSIGNED_INT_NORM			8
#define CIL_CHT_SIGNED_INT_NORM				9
#define CIL_CHT_UNSIGNED_INT				10
#define CIL_CHT_SIGNED_INT					11
#define CIL_CHT_FLOAT						12

// Return Errors
#define CIL_NOT_FOUND				0x0E2
#define CIL_CORRUPT					0x0E3
#define CIL_NO_MEMORY				0x0E4
#define CIL_PVR_V2_NOT_SUPPORTED	0x0E5
#define CIL_NOT_SUPPORTED_FILE		0x0E6
#define CIL_DDS_MALFORMED			0x0E7
#define CIL_FORWARD_TO_STB			0x0E8
#define CIL_NO_ERROR				0

// DDS Specific 
#define CIL_DDS_CAPS			0x00000001l
#define CIL_DDS_HEIGHT			0x00000002l
#define CIL_DDS_WIDTH			0x00000004l
#define CIL_DDS_PITCH			0x00000008l
#define CIL_DDS_PIXELFORMAT		0x00001000l
#define CIL_DDS_MIPMAPCOUNT		0x00020000l
#define CIL_DDS_LINEARSIZE		0x00080000l
#define CIL_DDS_DEPTH			0x00800000l
#define CIL_DDS_ALPHAPIXELS		0x00000001l
#define CIL_DDS_FOURCC			0x00000004l
#define CIL_DDS_RGB				0x00000040l
#define CIL_DDS_RGBA			0x00000041l
#define CIL_DDS_COMPLEX			0x00000008l
#define CIL_DDS_TEXTURE			0x00001000l
#define CIL_DDS_MIPMAP			0x00400000l
#define CIL_DDS_CUBEMAP			0x00000200l
#define CIL_DDS_CUBEMAP_POSITIVEX  0x00000400l
#define CIL_DDS_CUBEMAP_NEGATIVEX  0x00000800l
#define CIL_DDS_CUBEMAP_POSITIVEY  0x00001000l
#define CIL_DDS_CUBEMAP_NEGATIVEY  0x00002000l
#define CIL_DDS_CUBEMAP_POSITIVEZ  0x00004000l
#define CIL_DDS_CUBEMAP_NEGATIVEZ  0x00008000l
#define CIL_DDS_CUBEMAP_ALL_FACES  0x0000FC00l
#define CIL_DDS_VOLUME   0x00200000l
#define CIL_FOURCC_DXT1  0x31545844l 
#define CIL_FOURCC_DXT3  0x33545844l 
#define CIL_FOURCC_DXT5  0x35545844l 
#define CIL_FOURCC_RAW	 0

// KTX formats
#define CIL_ETC1_RGB8_OES						0x8D64
#define CIL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG		0x8C00
#define CIL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG		0x8C01
#define CIL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG	0x8C02
#define CIL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG	0x8C03
#define CIL_COMPRESSED_RGBA_PVRTC_2BPPV2_IMG	0x9137
#define CIL_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG	0x9138
#define CIL_COMPRESSED_RGBA8_ETC2_EAC			0x9278
#define CIL_COMPRESSED_RGB_S3TC_DXT1_EXT		0x83F0
#define CIL_COMPRESSED_RGBA_S3TC_DXT1_EXT		0x83F1
#define CIL_COMPRESSED_RGBA_S3TC_DXT3_EXT		0x83F2
#define CIL_COMPRESSED_RGBA_S3TC_DXT5_EXT		0x83F3

// Props
#define CIL_ALPHA				(1 << 0)
#define CIL_RGB					(1 << 1)
#define CIL_RGBA				(1 << 2)
#define CIL_PFMT_SIGNED			(1 << 3)
#define CIL_PFMT_UNSIGNED		(1 << 4)
#define CIL_PFMT_NORMALIZED		(1 << 5)
#define CIL_PFMT_UNNORMALIZED	(1 << 6)
#define CIL_PFMT_BYTE			(1 << 7)
#define CIL_PFMT_SHORT			(1 << 8)
#define CIL_PFMT_INT			(1 << 9)
#define CIL_PFMT_FLOAT			(1 << 10)
#define CIL_CUBE_MAP			(1 << 11)
#define CIL_COMPRESSED			(1 << 12)
#define CIL_BPP_2				(1 << 13)
#define CIL_BPP_4				(1 << 14)
#define CIL_BPP_8				(1 << 15)
#define CIL_LOADED_WITH_STB		(1 << 16)
#define CIL_PNG					(1 << 17)
#define CIL_DDS					(1 << 18)
#define CIL_TGA					(1 << 19)
#define CIL_PVR					(1 << 20)
#define CIL_KTX					(1 << 21)
#define CIL_RAW					(1 << 22)
#define CIL_ETC1				(1 << 23)
#define CIL_ETC2				(1 << 24)
#define CIL_DXT1				(1 << 25)
#define CIL_DXT5				(1 << 26)
#define CIL_PVRTC2				(1 << 27)
#define CIL_PVRTC4				(1 << 28)
#define CIL_PVRTCII2			(1 << 29)
#define CIL_PVRTCII4			(1 << 30)

struct pvr_v3_header {
	uint32_t		version;
	uint32_t		flags;
	uint32_t		pix_format_0;
	uint32_t		pix_format_1;
	uint32_t		color_space;
	uint32_t		channel_type;
	uint32_t		height;
	uint32_t		width;
	uint32_t		depth;
	uint32_t		surfaces;
	uint32_t		faces;
	uint32_t		mipmaps_c;
	uint32_t		metadata_size;
};

struct pvr_metadata {
	unsigned char	fourcc[4];
	uint32_t		key;
	uint32_t		size;
};


struct ktx_header {
	unsigned char   identifier[12];
	uint32_t		endianess;
	uint32_t		gltype;
	uint32_t		gltypesize;
	uint32_t		glformat;
	uint32_t		glinternalformat;
	uint32_t		glbaseinternafFormat;
	uint32_t		width;
	uint32_t		height;
	uint32_t		depth;
	uint32_t		surfaces;
	uint32_t		faces;
	uint32_t		mipmaps_c;
	uint32_t		keyvaluedatasize;
};

struct DDS_PIXELFORMAT {
	unsigned int dwSize;
	unsigned int dwFlags;
	unsigned int dwFourCC;
	unsigned int dwRGBBitCount;
	unsigned int dwRBitMask;
	unsigned int dwGBitMask;
	unsigned int dwBBitMask;
	unsigned int dwABitMask;
};

struct DDS_HEADER {
	unsigned int           dwSize;
	unsigned int           dwFlags;
	unsigned int           dwHeight;
	unsigned int           dwWidth;
	unsigned int           dwPitchOrLinearSize;
	unsigned int           dwDepth;
	unsigned int           dwMipMapCount;
	unsigned int           dwReserved1[11];
	DDS_PIXELFORMAT		   ddspf;
	unsigned int           dwCaps;
	unsigned int           dwCaps2;
	unsigned int           dwCaps3;
	unsigned int           dwCaps4;
	unsigned int           dwReserved2;
};

void checkformat(ifstream &in_, unsigned int &prop);
void pvr_set_pix_format(uint32_t& pix_format, unsigned int &prop);
void pvr_set_channel_type(uint32_t& c_type, unsigned int &prop);
unsigned char*	load_pvr(ifstream &in_, int &x, int &y, unsigned int &mipmaps, unsigned int &prop, unsigned int &buffersize);
void ktx_set_pix_format(unsigned int &format, unsigned int &prop);
unsigned char*	load_ktx(ifstream &in_, int &x, int &y, unsigned int &mipmaps, unsigned int &prop, unsigned int &buffersize);
void dds_set_pix_format(unsigned int &format, unsigned int &bppinfo, unsigned int &prop);
unsigned char*	load_dds(ifstream &in_, int &x, int &y, unsigned int &mipmaps, unsigned int &prop, unsigned int &buffersize);
void cil_free_buffer(unsigned char *pbuff,unsigned int prop=0);
unsigned char*	cil_load(const char* filename, int *x, int *y, unsigned int *mipmaps, unsigned int *props, unsigned int *buffersize, unsigned int ForceResizeFactor = 0);

#endif