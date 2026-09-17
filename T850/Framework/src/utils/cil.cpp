#include <pch.h>
#include <utils/cil.h>
#include <utils/ResourceLocator.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <utils/Log.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <vector>
#include <limits>

bool t850::DecompressDXTToRGBA(const unsigned char* source, size_t sourceBytes,
														 uint32_t width, uint32_t height, uint32_t mipCount,
														 uint32_t faceCount, unsigned int properties,
														 std::vector<unsigned char>& output) {
	output.clear();
	const auto format = properties & (CIL_DXT1 | CIL_DXT3 | CIL_DXT5);
	if (!source || !width || !height || !mipCount || mipCount > 32 || (faceCount != 1 && faceCount != 6) ||
			(format != CIL_DXT1 && format != CIL_DXT3 && format != CIL_DXT5)) return false;
	const size_t blockBytes = format == CIL_DXT1 ? 8 : 16;
	size_t totalSource = 0;
	size_t totalOutput = 0;
	for (uint32_t mip = 0; mip < mipCount; ++mip) {
		const size_t mipWidth = std::max(1u, width >> mip);
		const size_t mipHeight = std::max(1u, height >> mip);
		const size_t columns = (mipWidth + 3) / 4;
		const size_t rows = (mipHeight + 3) / 4;
		if (mipWidth > std::numeric_limits<size_t>::max() / mipHeight / 4 / faceCount ||
				columns > std::numeric_limits<size_t>::max() / rows / blockBytes / faceCount) return false;
		const size_t encoded = columns * rows * blockBytes * faceCount;
		const size_t decoded = mipWidth * mipHeight * 4 * faceCount;
		if (encoded > sourceBytes - totalSource || decoded > std::numeric_limits<size_t>::max() - totalOutput) return false;
		totalSource += encoded;
		totalOutput += decoded;
	}
	output.resize(totalOutput);
	const auto decode565 = [](uint16_t value, unsigned char* color) {
		color[0] = static_cast<unsigned char>((((value >> 11) & 31) * 255 + 15) / 31);
		color[1] = static_cast<unsigned char>((((value >> 5) & 63) * 255 + 31) / 63);
		color[2] = static_cast<unsigned char>(((value & 31) * 255 + 15) / 31);
		color[3] = 255;
	};
	size_t sourceOffset = 0;
	size_t outputOffset = 0;
	for (uint32_t face = 0; face < faceCount; ++face) {
		for (uint32_t mip = 0; mip < mipCount; ++mip) {
			const uint32_t mipWidth = std::max(1u, width >> mip);
			const uint32_t mipHeight = std::max(1u, height >> mip);
			const uint32_t columns = (mipWidth + 3) / 4;
			const uint32_t rows = (mipHeight + 3) / 4;
			for (uint32_t row = 0; row < rows; ++row) {
				for (uint32_t column = 0; column < columns; ++column) {
					const auto* block = source + sourceOffset;
					sourceOffset += blockBytes;
					const auto* colorBlock = block + (format == CIL_DXT1 ? 0 : 8);
					const uint16_t first = colorBlock[0] | (colorBlock[1] << 8);
					const uint16_t second = colorBlock[2] | (colorBlock[3] << 8);
					unsigned char colors[4][4] = {};
					decode565(first, colors[0]);
					decode565(second, colors[1]);
					if (first > second || format != CIL_DXT1) {
						for (unsigned channel = 0; channel < 3; ++channel) {
							colors[2][channel] = static_cast<unsigned char>((2 * colors[0][channel] + colors[1][channel] + 1) / 3);
							colors[3][channel] = static_cast<unsigned char>((colors[0][channel] + 2 * colors[1][channel] + 1) / 3);
						}
						colors[2][3] = colors[3][3] = 255;
					} else {
						for (unsigned channel = 0; channel < 3; ++channel)
							colors[2][channel] = static_cast<unsigned char>((colors[0][channel] + colors[1][channel] + 1) / 2);
						colors[2][3] = 255;
					}
					uint64_t alphaBits = 0;
					unsigned char alpha[8] = {block[0], block[1]};
					if (format == CIL_DXT5) {
						const unsigned interpolated = alpha[0] > alpha[1] ? 6 : 4;
						for (unsigned index = 0; index < interpolated; ++index)
							alpha[index + 2] = static_cast<unsigned char>(((interpolated - index) * alpha[0] + (index + 1) * alpha[1] + (interpolated + 1) / 2) / (interpolated + 1));
						if (interpolated == 4) { alpha[6] = 0; alpha[7] = 255; }
						for (unsigned index = 0; index < 6; ++index) alphaBits |= static_cast<uint64_t>(block[index + 2]) << (index * 8);
					}
					const uint32_t codes = colorBlock[4] | (static_cast<uint32_t>(colorBlock[5]) << 8) |
						(static_cast<uint32_t>(colorBlock[6]) << 16) | (static_cast<uint32_t>(colorBlock[7]) << 24);
					for (unsigned localRow = 0; localRow < 4; ++localRow) {
						const unsigned pixelRow = row * 4 + localRow;
						if (pixelRow >= mipHeight) continue;
						for (unsigned localColumn = 0; localColumn < 4; ++localColumn) {
							const unsigned pixelColumn = column * 4 + localColumn;
							if (pixelColumn >= mipWidth) continue;
							const unsigned pixel = localRow * 4 + localColumn;
							auto* destination = output.data() + outputOffset + (static_cast<size_t>(pixelRow) * mipWidth + pixelColumn) * 4;
							std::memcpy(destination, colors[(codes >> (pixel * 2)) & 3], 4);
							if (format == CIL_DXT3) destination[3] = static_cast<unsigned char>(((block[pixel / 2] >> ((pixel & 1) * 4)) & 15) * 17);
							else if (format == CIL_DXT5) destination[3] = alpha[(alphaBits >> (pixel * 3)) & 7];
						}
					}
				}
			}
			outputOffset += static_cast<size_t>(mipWidth) * mipHeight * 4;
		}
	}
	return true;
}

#if defined(OS_WINDOWS)
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")
#endif

using std::ifstream;
using std::cout;
using std::endl;
using std::max;
using std::streampos;
using std::ios;

namespace {
#if defined(OS_WINDOWS)
uint16_t Float32ToFloat16(float value) {
	uint32_t bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));

	const uint32_t sign = (bits >> 16) & 0x8000u;
	int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xffu) - 127 + 15;
	uint32_t mantissa = bits & 0x7fffffu;

	if (exponent <= 0) {
		if (exponent < -10)
			return static_cast<uint16_t>(sign);
		mantissa = (mantissa | 0x800000u) >> (1 - exponent);
		return static_cast<uint16_t>(sign | ((mantissa + 0x1000u) >> 13));
	}

	if (exponent >= 31)
		return static_cast<uint16_t>(sign | 0x7c00u);

	mantissa += 0x1000u;
	if (mantissa & 0x800000u) {
		mantissa = 0;
		++exponent;
		if (exponent >= 31)
			return static_cast<uint16_t>(sign | 0x7c00u);
	}

	return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13));
}
#endif

bool Is16BitPngFile(const char* filename) {
	std::ifstream file(filename, std::ios::binary);
	if (!file.good())
		return false;

	unsigned char header[25] = {};
	file.read(reinterpret_cast<char*>(header), sizeof(header));
	if (file.gcount() < static_cast<std::streamsize>(sizeof(header)))
		return false;

	static const unsigned char pngSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	if (std::memcmp(header, pngSig, sizeof(pngSig)) != 0)
		return false;
	if (header[12] != 'I' || header[13] != 'H' || header[14] != 'D' || header[15] != 'R')
		return false;

	return header[24] > 8;
}

#if defined(OS_WINDOWS)
bool Load16BitPngAsRGBA16F(const char* filename, int* x, int* y, int* channels, unsigned int* buffersize, unsigned char** outBuffer) {
	*outBuffer = nullptr;

	HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	const bool shouldUninitialize = SUCCEEDED(coInit);
	if (FAILED(coInit) && coInit != RPC_E_CHANGED_MODE)
		return false;

	Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
	HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
																IID_PPV_ARGS(factory.GetAddressOf()));
	if (FAILED(hr)) {
		if (shouldUninitialize) CoUninitialize();
		return false;
	}

	const std::wstring widePath = std::filesystem::path(filename).wstring();
	Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
	hr = factory->CreateDecoderFromFilename(widePath.c_str(), nullptr, GENERIC_READ,
																					WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf());
	if (FAILED(hr)) {
		if (shouldUninitialize) CoUninitialize();
		return false;
	}

	Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
	hr = decoder->GetFrame(0, frame.GetAddressOf());
	if (FAILED(hr)) {
		if (shouldUninitialize) CoUninitialize();
		return false;
	}

	UINT width = 0;
	UINT height = 0;
	hr = frame->GetSize(&width, &height);
	if (FAILED(hr) || width == 0 || height == 0) {
		if (shouldUninitialize) CoUninitialize();
		return false;
	}

	Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
	hr = factory->CreateFormatConverter(converter.GetAddressOf());
	if (FAILED(hr)) {
		if (shouldUninitialize) CoUninitialize();
		return false;
	}

	hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat64bppRGBA,
														 WICBitmapDitherTypeNone, nullptr, 0.0,
														 WICBitmapPaletteTypeCustom);
	if (FAILED(hr)) {
		if (shouldUninitialize) CoUninitialize();
		return false;
	}

	const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
	std::vector<uint16_t> rgba64(pixelCount);
	const UINT stride = width * 4u * sizeof(uint16_t);
	const UINT byteCount = static_cast<UINT>(rgba64.size() * sizeof(uint16_t));
	hr = converter->CopyPixels(nullptr, stride, byteCount, reinterpret_cast<BYTE*>(rgba64.data()));
	if (FAILED(hr)) {
		if (shouldUninitialize) CoUninitialize();
		return false;
	}

	uint16_t* halfData = new uint16_t[pixelCount];
	for (size_t i = 0; i < pixelCount; ++i) {
		const float normalized = static_cast<float>(rgba64[i]) * (1.0f / 65535.0f);
		halfData[i] = Float32ToFloat16(normalized);
	}

	*x = static_cast<int>(width);
	*y = static_cast<int>(height);
	*channels = 4;
	*buffersize = static_cast<unsigned int>(pixelCount * sizeof(uint16_t));
	*outBuffer = reinterpret_cast<unsigned char*>(halfData);

	if (shouldUninitialize)
		CoUninitialize();
	return true;
}
#endif
}

void checkformat(std::istream &in_, unsigned int &prop) {
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

unsigned char*	load_pvr(std::istream &in_, int &x, int &y, unsigned int &mipmaps, unsigned int &prop, unsigned int &buffersize) {
	pvr_v3_header header;
	in_.seekg(0);
	in_.read((char*)&header, sizeof(pvr_v3_header));

	if (header.version == 52) {
		prop = CIL_PVR_V2_NOT_SUPPORTED;
		return 0;
	}

#if CIL_LOG_OUTPUT
	T8_LOG_VERBOSE("PVR Data: Version=%u PixFmt=%llu ChanType=%u %ux%ux%u Surfaces=%u Faces=%u Mips=%u MetaSize=%u",
		header.version, header.pix_format_0, header.channel_type,
		header.width, header.height, header.depth,
		header.surfaces, header.faces, header.mipmaps_c, header.metadata_size);
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
	T8_LOG_VERBOSE("PVR Metadata: fourcc=[%c%c%c%d] key=%u dataSize=%u",
		meta.fourcc[0], meta.fourcc[1], meta.fourcc[2], (int)meta.fourcc[3],
		meta.key, meta.size);
	for (unsigned int i = 0; i < meta.size; i++) {
		T8_LOG_VERBOSE("  Meta %u: %d", i, (int)metadata[i]);
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

unsigned char*	load_ktx(std::istream &in_, int &x, int &y, unsigned int &mipmaps, unsigned int &prop, unsigned int &buffersize) {
	ktx_header	header;
	in_.seekg(0);
	in_.read((char*)&header, sizeof(ktx_header));

#if CIL_LOG_OUTPUT
	T8_LOG_VERBOSE("KTX Data: GLType=%u GLFmt=%u GLIntFmt=%u TypeSize=%u %ux%ux%u Surfaces=%u Faces=%u Mips=%u KeySize=%u",
		header.gltype, header.glformat, header.glinternalformat, header.gltypesize,
		header.width, header.height, header.depth,
		header.surfaces, header.faces, header.mipmaps_c, header.keyvaluedatasize);
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
		prop |= CIL_DXT3;
		prop |= CIL_BPP_8;
		prop |= CIL_COMPRESSED;
	}break;
	case CIL_FOURCC_DXT5: {
		prop |= CIL_DXT5;
		prop |= CIL_BPP_8;
		prop |= CIL_COMPRESSED;
	}break;
	case CIL_FOURCC_RGBA16F: {
		prop |= CIL_RAW;
		prop |= CIL_RGBA;
		prop |= CIL_HALF_FLOAT;
	}break;
	}
}

unsigned char*	load_dds(std::istream &in_, int &x, int &y, unsigned int &mipmaps, unsigned int &prop, unsigned int &buffersize) {
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
		prop = CIL_DDS_MALFORMED;
		return 0;
	}

#if CIL_LOG_OUTPUT
	T8_LOG_VERBOSE("DDS Data: size=%u flags=0x%X %ux%u pitch=%u depth=%u mips=%u caps=%u,%u,%u,%u",
		header.dwSize, header.dwFlags, header.dwWidth, header.dwHeight,
		header.dwPitchOrLinearSize, header.dwDepth, header.dwMipMapCount,
		header.dwCaps, header.dwCaps2, header.dwCaps3, header.dwCaps4);
	T8_LOG_VERBOSE("DDS PixFmt: size=%u flags=0x%X fourCC=%u rgbBits=%u R=0x%X G=0x%X B=0x%X A=0x%X",
		header.ddspf.dwSize, header.ddspf.dwFlags, header.ddspf.dwFourCC,
		header.ddspf.dwRGBBitCount, header.ddspf.dwRBitMask,
		header.ddspf.dwGBitMask, header.ddspf.dwBBitMask, header.ddspf.dwABitMask);
	char *FourCC;
	FourCC = (char*)&header.ddspf.dwFourCC;
	T8_LOG_VERBOSE("DDS FOURCC: %s", FourCC);
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
		if (prop & CIL_HALF_FLOAT)
			bpp = 64;
		else
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

	std::vector<unsigned char> bytes;
	if (!t850::ResourceLocator::Instance().ReadBinary(filename, bytes)) {
		*props = CIL_NOT_FOUND;
		return 0;
	}

	if (bytes.empty()) {
		*props = CIL_NOT_FOUND;
		return 0;
	}
	std::string streamData(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	std::istringstream in_(streamData, std::ios::in | std::ios::binary);

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
		return buffer;
	}
	else if (props_&CIL_KTX) {
		unsigned char * buffer = load_ktx(in_, x_, y_, mipmaps_, props_, buffer_size_);
		*props = props_;
		*x = x_;
		*y = y_;
		*buffersize = buffer_size_;
		*mipmaps = mipmaps_;
		return buffer;
	}
	else if (props_&CIL_DDS) {
		unsigned char * buffer = load_dds(in_, x_, y_, mipmaps_, props_, buffer_size_);
		*props = props_;
		*x = x_;
		*y = y_;
		*buffersize = buffer_size_;
		*mipmaps = mipmaps_;
		return buffer;
	}
#if CIL_CALL_STB
	else if (props_ == CIL_FORWARD_TO_STB) {
		int channels;
		if (Is16BitPngFile(filename)) {
#if defined(OS_WINDOWS)
			unsigned char* halfData = nullptr;
			if (Load16BitPngAsRGBA16F(filename, x, y, &channels, &buffer_size_, &halfData)) {
				props_ = CIL_RAW | CIL_RGBA | CIL_HALF_FLOAT;
				*mipmaps = 1;
				*buffersize = buffer_size_;
				*props = props_;
				T8_LOG_INFO("CIL loaded 16-bit image as RGBA16F: '%s' (%dx%d, decodedChannels=%d)", filename, *x, *y, channels);
				return halfData;
			}
#endif
			T8_LOG_ERROR("CIL failed to load 16-bit PNG '%s'", filename);
			*props = CIL_NOT_SUPPORTED_FILE;
			return 0;
		}

		props_ = CIL_LOADED_WITH_STB | CIL_RAW;
		unsigned char * buffer = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), x, y, &channels, 4);
		props_ |= CIL_RGBA;            // stbi_load always returns 4 channels (forced above)
		*mipmaps = 1;
		*buffersize = (*x)*(*y) * 4;   // buffer is always 4 bytes/pixel
		*props = props_;
		unsigned int resizeFactor = ForceResizeFactor;
#ifdef FORCE_LOW_RES_TEXTURES
		if (resizeFactor == 0) resizeFactor = FORCED_FACTOR;
#endif
		if (buffer && resizeFactor > 1) {

			int nx = (std::max)(1, *x / static_cast<int>(resizeFactor));
			int ny = (std::max)(1, *y / static_cast<int>(resizeFactor));

			unsigned char* resizedBuf = (unsigned char*)STBI_MALLOC(nx*ny * 4 + 1);

			resizedBuf[nx*ny * 4] = '\0';

			stbir_resize_uint8(buffer, *x, *y, 0, resizedBuf, nx, ny, 0, 4);

			stbi_image_free(buffer);

			*buffersize = nx*ny * 4;
			buffer = resizedBuf;
			*x = nx;
			*y = ny;
			channels = 4;
		}
		return buffer;
	}
#else
	else if (props_ == CIL_NOT_SUPPORTED_FILE) {
		return 0;
	}
#endif

	return 0;
}
