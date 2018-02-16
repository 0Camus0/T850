#include <utils/cil.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

void checkformat(ifstream &in_, unsigned int &prop) {
	std::streampos begPos = in_.tellg();

	in_.seekg(begPos);
	char	dds[4];
	in_.read((char*)dds, 3);
	dds[3] = '\0';
	if (strcmp(dds, "DDS") == 0) {
		prop |= CIL_DDS;
		in_.seekg(begPos);
		return;
	}

	in_.seekg(begPos);
	unsigned char	ktx[5];
	in_.read((char*)ktx, 4);
	ktx[0] = ' ';
	ktx[4] = '\0';
	if (strcmp((char*)ktx, " KTX") == 0) {
		prop |= CIL_KTX;
		in_.seekg(begPos);
		return;
	}

	in_.seekg(begPos);
	char pvr[4];
	in_.read((char*)&pvr, 3);
	pvr[3] = '\0';
	if (strcmp((char*)pvr, "PVR") == 0) {
		prop |= CIL_PVR;
		in_.seekg(begPos);
		return;
	}

#if CIL_CALL_STB
	prop = CIL_FORWARD_TO_STB;
#else
	prop = CIL_NOT_SUPPORTED_FILE;
#endif
}

void	pvr_set_pix_format(uint32_t& pix_format, unsigned int &prop) {
	switch (pix_format) {
	case  CIL_PVRTC_2BPP_RGB_FMT: {
		prop |= CIL_PVRTC2;
		prop |= CIL_RGB;
		prop |= CIL_BPP_2;
		prop |= CIL_COMPRESSED;
	}break;
	case  CIL_PVRTC_2BPP_RGBA_FMT: {
		prop |= CIL_PVRTC2;
		prop |= CIL_RGBA;
		prop |= CIL_BPP_2;
		prop |= CIL_COMPRESSED;
	}break;
	case  CIL_PVRTC_4BPP_RGB_FMT: {
		prop |= CIL_PVRTC4;
		prop |= CIL_RGB;
		prop |= CIL_BPP_4;
		prop |= CIL_COMPRESSED;
	}break;
	case  CIL_PVRTC_4BPP_RGBA_FMT: {
		prop |= CIL_PVRTC4;
		prop |= CIL_RGBA;
		prop |= CIL_BPP_4;
		prop |= CIL_COMPRESSED;
	}break;
	case  CIL_PVRTCII_2BPP_RGB_FMT: {
		prop |= CIL_PVRTCII2;
		prop |= CIL_RGBA;
		prop |= CIL_BPP_2;
		prop |= CIL_COMPRESSED;
	}break;
	case  CIL_PVRTCII_4BPP_RGB_FMT: {
		prop |= CIL_PVRTCII4;
		prop |= CIL_RGBA;
		prop |= CIL_BPP_4;
		prop |= CIL_COMPRESSED;
	}break;
	case  CIL_ETC1_FMT: {
		prop |= CIL_ETC1;
		prop |= CIL_RGB;
		prop |= CIL_BPP_4;
		prop |= CIL_COMPRESSED;
	}break;
	case  CIL_DXT1_FMT: {
		prop |= CIL_DXT1;
		prop |= CIL_RGB;
		prop |= CIL_BPP_4;
		prop |= CIL_COMPRESSED;
	}break;
	case  CIL_DXT5_FMT: {
		prop |= CIL_DXT5;
		prop |= CIL_RGBA;
		prop |= CIL_BPP_8;
		prop |= CIL_COMPRESSED;
	}break;
	case  CIL_ETC2_FMT: {
		prop |= CIL_ETC2;
		prop |= CIL_RGBA;
		prop |= CIL_BPP_8;
		prop |= CIL_COMPRESSED;
	}break;
	}
}

void pvr_set_channel_type(uint32_t& c_type, unsigned int &prop) {
	switch (c_type) {
	case  CIL_CHT_UNSIGNED_BYTE_NORM: {
		prop |= CIL_PFMT_UNSIGNED;
		prop |= CIL_PFMT_BYTE;
		prop |= CIL_PFMT_NORMALIZED;
	}break;
	case  CIL_CHT_SIGNED_BYTE_NORM: {
		prop |= CIL_PFMT_SIGNED;
		prop |= CIL_PFMT_BYTE;
		prop |= CIL_PFMT_NORMALIZED;
	}break;
	case  CIL_CHT_UNSIGNED_BYTE: {
		prop |= CIL_PFMT_UNSIGNED;
		prop |= CIL_PFMT_BYTE;
		prop |= CIL_PFMT_UNNORMALIZED;
	}break;
	case  CIL_CHT_SIGNED_BYTE: {
		prop |= CIL_PFMT_SIGNED;
		prop |= CIL_PFMT_BYTE;
		prop |= CIL_PFMT_UNNORMALIZED;
	}break;
	case  CIL_CHT_UNSIGNED_SHORT_NORM: {
		prop |= CIL_PFMT_UNSIGNED;
		prop |= CIL_PFMT_SHORT;
		prop |= CIL_PFMT_NORMALIZED;
	}break;
	case  CIL_CHT_SIGNED_SHORT_NORM: {
		prop |= CIL_PFMT_SIGNED;
		prop |= CIL_PFMT_SHORT;
		prop |= CIL_PFMT_NORMALIZED;
	}break;
	case  CIL_CHT_UNSIGNED_SHORT: {
		prop |= CIL_PFMT_UNSIGNED;
		prop |= CIL_PFMT_SHORT;
		prop |= CIL_PFMT_UNNORMALIZED;
	}break;
	case  CIL_CHT_SIGNED_SHORT: {
		prop |= CIL_PFMT_SIGNED;
		prop |= CIL_PFMT_SHORT;
		prop |= CIL_PFMT_UNNORMALIZED;
	}break;
	case  CIL_CHT_UNSIGNED_INT_NORM: {
		prop |= CIL_PFMT_UNSIGNED;
		prop |= CIL_PFMT_INT;
		prop |= CIL_PFMT_NORMALIZED;
	}break;
	case  CIL_CHT_SIGNED_INT_NORM: {
		prop |= CIL_PFMT_SIGNED;
		prop |= CIL_PFMT_INT;
		prop |= CIL_PFMT_NORMALIZED;
	}break;
	case  CIL_CHT_UNSIGNED_INT: {
		prop |= CIL_PFMT_UNSIGNED;
		prop |= CIL_PFMT_INT;
		prop |= CIL_PFMT_UNNORMALIZED;
	}break;
	case  CIL_CHT_SIGNED_INT: {
		prop |= CIL_PFMT_SIGNED;
		prop |= CIL_PFMT_INT;
		prop |= CIL_PFMT_UNNORMALIZED;
	}break;
	case  CIL_CHT_FLOAT: {
		prop |= CIL_PFMT_FLOAT;
	}break;
	}
}

unsigned char*	load_pvr(ifstream &in_, int &x, int &y, unsigned int &mipmaps, unsigned int &prop, unsigned int &buffersize) {
	pvr_v3_header header;
	in_.seekg(0);
	in_.read((char*)&header, sizeof(pvr_v3_header));

	if (header.version == 52) {
		prop = CIL_PVR_V2_NOT_SUPPORTED;
		return 0;
	}

#if CIL_LOG_OUTPUT
	cout << "PVR Data: " << endl
		<< "Version: " << header.version << endl
		<< "Pixel format: " << header.pix_format_0 << endl
		<< "Channel type: " << header.channel_type << endl
		<< "Height: " << header.height << endl
		<< "Width: " << header.width << endl
		<< "Depth: " << header.depth << endl
		<< "Surfaces: " << header.surfaces << endl
		<< "Faces: " << header.faces << endl
		<< "Mipmap count: " << header.mipmaps_c << endl
		<< "Size meta: " << header.metadata_size << endl;
#endif

	x = header.width;
	y = header.height;
	mipmaps = header.mipmaps_c;

	if (header.faces == 6)
		prop |= CIL_CUBE_MAP;

	pvr_set_pix_format(header.pix_format_0, prop);
	pvr_set_channel_type(header.channel_type, prop);

	pvr_metadata meta;
	in_.read((char*)&meta, sizeof(pvr_metadata));

	unsigned char*	metadata = new unsigned char[meta.size + 1];
	in_.read((char*)&metadata[0], meta.size);
	metadata[meta.size] = '\0';

#if CIL_LOG_OUTPUT
	cout << "Metadata: " << endl

		<< "Fourcc 0: " << meta.fourcc[0] << endl
		<< "Fourcc 1: " << meta.fourcc[1] << endl
		<< "Fourcc 2: " << meta.fourcc[2] << endl
		<< "Fourcc 3: " << (int)meta.fourcc[3] << endl
		<< "Key: " << meta.key << endl
		<< "Data size: " << meta.size << endl;
	for (unsigned int i = 0; i < meta.size; i++) {
		cout << "Meta " << i << ": " << (int)metadata[i] << endl;
	}
#endif
	delete[] metadata;

	int currentWidth = header.width, currentHeight = header.height;
	int final_size = 0;
	int blockSize = (prop & CIL_BPP_4) ? 16 : 32;
	int bpp = (prop & CIL_BPP_4) ? 4 : 2;
	int widthBlocks = (prop & CIL_BPP_4) ? (currentWidth / 4) : (currentWidth / 8);
	int heightBlocks = currentHeight / 4;
	int current_size = 0;
	for (unsigned int i = 0; i < header.mipmaps_c; i++) {

		widthBlocks = widthBlocks < 2 ? 2 : widthBlocks;
		heightBlocks = heightBlocks < 2 ? 2 : heightBlocks;

		if (prop&CIL_ETC1) {
			current_size = (currentHeight*currentWidth*bpp) / 8;
			current_size = max(current_size, 8);
		}
		else {
			current_size = widthBlocks * heightBlocks * ((blockSize * bpp) / 8);
		}
		for (unsigned int f = 0; f < header.faces; f++) {
			final_size += current_size;
		}
		currentWidth = max(currentWidth >> 1, 1);
		currentHeight = max(currentHeight >> 1, 1);

		widthBlocks = (prop & CIL_BPP_4) ? (currentWidth / 4) : (currentWidth / 8);
		heightBlocks = currentHeight / 4;
	}

	buffersize = final_size;
	unsigned char *buffer = new unsigned char[buffersize];

	if (buffer == 0) {
		prop = CIL_NO_MEMORY;
		return 0;
	}

	in_.read((char*)&buffer[0], buffersize);

	return buffer;
}

void ktx_set_pix_format(unsigned int &format, unsigned int &prop) {

	switch (format) {
	case CIL_ETC1_RGB8_OES: {
		prop |= CIL_ETC1;
		prop |= CIL_RGB;
		prop |= CIL_BPP_4;
		prop |= CIL_COMPRESSED;
	}break;
	case CIL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG: {
		prop |= CIL_PVRTC4;
		prop |= CIL_RGB;
		prop |= CIL_BPP_4;
		prop |= CIL_COMPRESSED;
	}break;
	case CIL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG: {
		prop |= CIL_PVRTC2;
		prop |= CIL_RGB;
		prop |= CIL_BPP_2;
		prop |= CIL_COMPRESSED;
	}break;
	case CIL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG: {
		prop |= CIL_PVRTC4;
		prop |= CIL_RGBA;
		prop |= CIL_BPP_4;
		prop |= CIL_COMPRESSED;
	}break;
	case CIL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG: {
		prop |= CIL_PVRTC2;
		prop |= CIL_RGBA;
		prop |= CIL_BPP_2;
		prop |= CIL_COMPRESSED;
	}break;
	case CIL_COMPRESSED_RGBA_PVRTC_2BPPV2_IMG: {
		prop |= CIL_PVRTCII2;
		prop |= CIL_RGBA;
		prop |= CIL_BPP_2;
		prop |= CIL_COMPRESSED;
	}break;
	case CIL_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG: {
		prop |= CIL_PVRTCII4;
		prop |= CIL_RGBA;
		prop |= CIL_BPP_4;
		prop |= CIL_COMPRESSED;
	}break;
	case CIL_COMPRESSED_RGBA8_ETC2_EAC: {
		prop |= CIL_ETC2;
		prop |= CIL_RGBA;
		prop |= CIL_BPP_8;
		prop |= CIL_COMPRESSED;
	}break;
	case CIL_COMPRESSED_RGB_S3TC_DXT1_EXT: {
		prop |= CIL_DXT1;
		prop |= CIL_RGB;
		prop |= CIL_BPP_4;
		prop |= CIL_COMPRESSED;
	}break;
	case CIL_COMPRESSED_RGBA_S3TC_DXT1_EXT: {
		prop |= CIL_DXT1;
		prop |= CIL_RGBA;
		prop |= CIL_BPP_4;
		prop |= CIL_COMPRESSED;
	}break;
	case CIL_COMPRESSED_RGBA_S3TC_DXT5_EXT: {
		prop |= CIL_DXT5;
		prop |= CIL_RGBA;
		prop |= CIL_BPP_8;
		prop |= CIL_COMPRESSED;
	}break;
	}
}

unsigned char*	load_ktx(ifstream &in_, int &x, int &y, unsigned int &mipmaps, unsigned int &prop, unsigned int &buffersize) {
	ktx_header	header;
	in_.seekg(0);
	in_.read((char*)&header, sizeof(ktx_header));

#if CIL_LOG_OUTPUT
	cout << "KTX Data: " << endl
		<< "GLType: " << header.gltype << endl
		<< "GLFormat: " << header.glformat << endl
		<< "GLInternalFormat: " << header.glinternalformat << endl
		<< "GLTypeSize: " << header.gltypesize << endl
		<< "Height: " << header.height << endl
		<< "Width: " << header.width << endl
		<< "Depth: " << header.depth << endl
		<< "Surfaces: " << header.surfaces << endl
		<< "Faces: " << header.faces << endl
		<< "Mipmap count: " << header.mipmaps_c << endl
		<< "Size Key: " << header.keyvaluedatasize << endl;
#endif
	if (header.mipmaps_c == 0)
		header.mipmaps_c = 1;

	x = header.width;
	y = header.height;
	mipmaps = header.mipmaps_c;


	if (header.faces == 6)
		prop |= CIL_CUBE_MAP;


	ktx_set_pix_format(header.glinternalformat, prop);

	if (header.keyvaluedatasize > 0) {
		unsigned char*	metadata = new unsigned char[header.keyvaluedatasize + 1];
		in_.read((char*)&metadata[0], header.keyvaluedatasize);
		metadata[header.keyvaluedatasize] = '\0';
		delete[] metadata;
	}

	streampos actual = in_.tellg();
	unsigned int totalSize = 0;
	for (unsigned int i = 0; i < header.mipmaps_c; i++) {
		unsigned int size = 0;
		in_.read((char*)&size, sizeof(unsigned int));
		size = size*header.faces;
		in_.seekg(in_.tellg() + streampos(size));
		totalSize += size;
	}


	buffersize = totalSize;

	unsigned char * pBuffer = new unsigned char[totalSize];
	unsigned char *pHead = pBuffer;
	if (pBuffer == 0) {
		prop = CIL_NO_MEMORY;
		return 0;
	}

	in_.seekg(actual);
	for (unsigned int i = 0; i < header.mipmaps_c; i++) {
		unsigned int size = 0;
		in_.read((char*)&size, sizeof(unsigned int));
		for (unsigned int f = 0; f < header.faces; f++) {
			in_.read((char*)pBuffer, size);
			pBuffer += size;
		}
	}
	pBuffer = pHead;

	return pBuffer;
}

void dds_set_pix_format(unsigned int &format, unsigned int &bppinfo, unsigned int &prop) {
	switch (format) {
	case CIL_FOURCC_RAW: {
		prop |= CIL_RAW;
		if (bppinfo == 24)
			prop |= CIL_RGB;
		else
			prop |= CIL_RGBA;
	}break;
	case CIL_FOURCC_DXT1: {
		prop |= CIL_DXT1;
		prop |= CIL_BPP_4;
		prop |= CIL_COMPRESSED;
	}break;
	case CIL_FOURCC_DXT3: {
		prop |= CIL_DXT5;
		prop |= CIL_BPP_8;
		prop |= CIL_COMPRESSED;
	}break;
	case CIL_FOURCC_DXT5: {
		prop |= CIL_DXT5;
		prop |= CIL_BPP_8;
		prop |= CIL_COMPRESSED;
	}break;
	}
}

unsigned char*	load_dds(ifstream &in_, int &x, int &y, unsigned int &mipmaps, unsigned int &prop, unsigned int &buffersize) {
	char ddstr[4];
	DDS_HEADER header;
	in_.seekg(0, std::ios::end);
	unsigned int FileSize = static_cast<unsigned int>(in_.tellg());
	in_.seekg(0, std::ios::beg);
	in_.read((char*)ddstr, 4);
	FileSize -= 4;
	in_.read((char*)&header, sizeof(DDS_HEADER));
	FileSize -= sizeof(DDS_HEADER);

	if (header.dwSize != 124) {
		in_.close();
		prop = CIL_DDS_MALFORMED;
		return 0;
	}

#if CIL_LOG_OUTPUT
	cout << "DDS Data: " << endl
		<< "	dwSize: " << header.dwSize << endl
		<< "	dwFlags: " << header.dwFlags << endl
		<< "	dwHeight: " << header.dwHeight << endl
		<< "	dwWidth: " << header.dwWidth << endl
		<< "	dwPitchOrLinearSize: " << header.dwPitchOrLinearSize << endl
		<< "	dwDepth: " << header.dwDepth << endl
		<< "	dwMipMapCount: " << header.dwMipMapCount << endl
		<< "	dwCaps: " << header.dwCaps << endl
		<< "	dwCaps2: " << header.dwCaps2 << endl
		<< "	dwCaps3: " << header.dwCaps3 << endl
		<< "	dwCaps4: " << header.dwCaps4 << endl
		<< "	dwReserved2: " << header.dwMipMapCount << endl << endl;

	cout << "DDS Pixel Format: " << endl
		<< "	dwSize: " << header.ddspf.dwSize << endl
		<< "	dwFlags: " << header.ddspf.dwFlags << endl
		<< "	dwFourCC: " << header.ddspf.dwFourCC << endl
		<< "	dwRGBBitCount: " << header.ddspf.dwRGBBitCount << endl
		<< "	dwRBitMask: " << header.ddspf.dwRBitMask << endl
		<< "	dwGBitMask: " << header.ddspf.dwGBitMask << endl
		<< "	dwBBitMask: " << header.ddspf.dwBBitMask << endl
		<< "	dwABitMask: " << header.ddspf.dwABitMask << endl;

	char *FourCC;
	FourCC = (char*)&header.ddspf.dwFourCC;
	cout << "	FOURCC: " << FourCC << endl;
#endif
	x = header.dwWidth;
	y = header.dwHeight;
	mipmaps = header.dwMipMapCount;

	if (header.dwCaps2 & CIL_DDS_CUBEMAP) {
		prop |= CIL_CUBE_MAP;
	}

	dds_set_pix_format(header.ddspf.dwFourCC, header.ddspf.dwRGBBitCount, prop);

	int numFaces = (prop&CIL_CUBE_MAP) ? 6 : 1;
	int finalSize = 0;
	int widthBlocks = x;
	int heightBlocks = y;
	int bpp = 8;
	if (prop&CIL_COMPRESSED) {
		int blockSize = (prop & CIL_BPP_4) ? 8 : 16;
		if (prop&CIL_DXT1)
			bpp = 4;
		for (int i = 0; i < numFaces; i++) {
			widthBlocks = x;
			heightBlocks = y;
			for (unsigned int j = 0; j < mipmaps; j++) {
				int current_size = (widthBlocks*heightBlocks*bpp) / 8;
				current_size = max(current_size, blockSize);
				finalSize += current_size;
				widthBlocks >>= 1;
				heightBlocks >>= 1;
			}
		}
	}
	else {
		bpp = (prop&CIL_RGB) ? 24 : 32;
		mipmaps = mipmaps == 0 ? 1 : mipmaps;
		for (int i = 0; i < numFaces; i++) {
			widthBlocks = x;
			heightBlocks = y;
			for (unsigned int j = 0; j < mipmaps; j++) {
				int current_size = (widthBlocks*heightBlocks*bpp) / 8;
				finalSize += current_size;
				widthBlocks >>= 1;
				heightBlocks >>= 1;
			}
		}
	}

	buffersize = finalSize;
	FileSize -= finalSize;

	if (FileSize != 0) {
		in_.close();
		exit(666);
	}

	unsigned char *buffer = new unsigned char[buffersize];

	if (buffer == 0) {
		prop = CIL_NO_MEMORY;
		return 0;
	}

	in_.read((char*)&buffer[0], buffersize);

	return buffer;
}

void cil_free_buffer(unsigned char *pbuff, unsigned int prop) {
	if (prop&CIL_LOADED_WITH_STB) {
		stbi_image_free(pbuff);
	}
	else {
		delete[] pbuff;
		pbuff = 0;
	}
}

unsigned char*	cil_load(const char* filename, int *x, int *y, unsigned int *mipmaps, unsigned int *props, unsigned int *buffersize, unsigned int ForceResizeFactor) {

	ifstream in_(filename, ios::binary | ios::in);

	if (!in_.good()) {
		in_.close();
		*props = CIL_NOT_FOUND;
		return 0;
	}

	int x_ = 0, y_ = 0;
	unsigned int props_ = 0;
	unsigned int buffer_size_ = 0;
	unsigned int mipmaps_;
	checkformat(in_, props_);

	if (props_&CIL_PVR) {
		unsigned char * buffer = load_pvr(in_, x_, y_, mipmaps_, props_, buffer_size_);
		*props = props_;
		*x = x_;
		*y = y_;
		*buffersize = buffer_size_;
		*mipmaps = mipmaps_;
		in_.close();
		return buffer;
	}
	else if (props_&CIL_KTX) {
		unsigned char * buffer = load_ktx(in_, x_, y_, mipmaps_, props_, buffer_size_);
		*props = props_;
		*x = x_;
		*y = y_;
		*buffersize = buffer_size_;
		*mipmaps = mipmaps_;
		in_.close();
		return buffer;
	}
	else if (props_&CIL_DDS) {
		unsigned char * buffer = load_dds(in_, x_, y_, mipmaps_, props_, buffer_size_);
		*props = props_;
		*x = x_;
		*y = y_;
		*buffersize = buffer_size_;
		*mipmaps = mipmaps_;
		in_.close();
		return buffer;
	}
#if CIL_CALL_STB
	else if (props_ == CIL_FORWARD_TO_STB) {
		props_ = CIL_LOADED_WITH_STB | CIL_RAW; 
		in_.close();
		int channels;
		unsigned char * buffer = stbi_load(filename, x, y, &channels, 4);
		props_ |= (channels == 3) ? CIL_RGB : CIL_RGBA;
		*mipmaps = 1;
		*buffersize = (*x)*(*y) * channels;
		*props = props_;
#if FORCE_LOW_RES_TEXTURES
		if (buffer) {

			int nx = *x / FORCED_FACTOR;
			int ny = *y / FORCED_FACTOR;

			unsigned char* resizedBuf = (unsigned char*)STBI_MALLOC(nx*ny * 4 + 1);

			resizedBuf[nx*ny * 4] = '\0';

			int result = stbir_resize_uint8(buffer, *x, *y, 0, resizedBuf, nx, ny, 0, 4);

			stbi_image_free(buffer);

			*buffersize = nx*ny * 4;
			buffer = resizedBuf;
			*x = nx;
			*y = ny;
			channels = 4;
		}
#endif
		return buffer;
	}
#else
	else if (props_ == CIL_NOT_SUPPORTED_FILE) {
		in_.close();
		return 0;
	}
#endif

	return 0;
}
