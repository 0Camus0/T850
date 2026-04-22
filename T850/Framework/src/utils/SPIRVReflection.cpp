#include "pch.h"
#include "utils/SPIRVReflection.h"
#include <utils/Log.h>
#include <algorithm>
#include <unordered_map>
#include <cstring>

// SPIR-V opcodes we care about (from SPIR-V spec §3.32)
enum SpvOp_ {
  SpvOpName            = 5,
  SpvOpMemberName      = 6,
  SpvOpEntryPoint      = 15,
  SpvOpTypeVoid        = 19,
  SpvOpTypeBool        = 20,
  SpvOpTypeInt         = 21,
  SpvOpTypeFloat       = 22,
  SpvOpTypeVector      = 23,
  SpvOpTypeMatrix      = 24,
  SpvOpTypeImage       = 25,
  SpvOpTypeSampler     = 26,
  SpvOpTypeSampledImage= 27,
  SpvOpTypeArray       = 28,
  SpvOpTypeStruct      = 30,
  SpvOpTypePointer     = 32,
  SpvOpVariable        = 59,
  SpvOpDecorate        = 71,
  SpvOpMemberDecorate  = 72,
};

// SPIR-V decorations
enum SpvDecoration_ {
  SpvDecorationBinding       = 33,
  SpvDecorationDescriptorSet = 34,
  SpvDecorationLocation      = 30,
  SpvDecorationBuiltIn       = 11,
};

// SPIR-V storage classes
enum SpvStorageClass_ {
  SpvStorageClassInput           = 1,
  SpvStorageClassUniform         = 2,
  SpvStorageClassOutput          = 3,
  SpvStorageClassUniformConstant = 0,
  SpvStorageClassPushConstant    = 9,
};

namespace t800 {

bool SPIRVReflection::Parse(const uint32_t* code, size_t wordCount) {
  if (wordCount < 5) return false;

  // SPIR-V header: magic, version, generator, bound, reserved
  uint32_t magic = code[0];
  if (magic != 0x07230203) return false; // not SPIR-V

  uint32_t bound = code[3]; // upper bound of IDs

  // Maps for collecting info per ID
  struct IDInfo {
    std::string name;
    uint32_t binding      = ~0u;
    uint32_t set          = 0;
    uint32_t location     = ~0u;
    uint32_t storageClass = ~0u;
    uint32_t typeId       = 0;
    bool     isBuiltIn    = false;
  };
  std::unordered_map<uint32_t, IDInfo> ids;

  // Type info
  struct TypeInfo {
    enum Kind { Unknown, Void, Scalar, Vector, Matrix, Image, Sampler, SampledImage, Struct, Array, Pointer };
    Kind     kind = Unknown;
    uint32_t componentCount = 1; // for vectors
    uint32_t pointeeType = 0;   // for pointers
    uint32_t storageClass = ~0u;
    uint32_t elementType = 0;   // for arrays/vectors
    bool     isCubemap = false;  // true if Image with Dim=Cube
  };
  std::unordered_map<uint32_t, TypeInfo> types;

  // First pass: parse all instructions
  size_t i = 5; // skip header
  while (i < wordCount) {
    uint32_t instrWord = code[i];
    uint16_t opcode   = instrWord & 0xFFFF;
    uint16_t instrLen = (instrWord >> 16) & 0xFFFF;

    if (instrLen == 0) break; // malformed

    switch (opcode) {
      case SpvOpName: {
        if (instrLen >= 3) {
          uint32_t id = code[i + 1];
          ids[id].name = (const char*)&code[i + 2];
        }
        break;
      }

      case SpvOpDecorate: {
        if (instrLen >= 3) {
          uint32_t id = code[i + 1];
          uint32_t decoration = code[i + 2];
          if (decoration == SpvDecorationBinding && instrLen >= 4)
            ids[id].binding = code[i + 3];
          else if (decoration == SpvDecorationDescriptorSet && instrLen >= 4)
            ids[id].set = code[i + 3];
          else if (decoration == SpvDecorationLocation && instrLen >= 4)
            ids[id].location = code[i + 3];
          else if (decoration == SpvDecorationBuiltIn)
            ids[id].isBuiltIn = true;
        }
        break;
      }

      case SpvOpVariable: {
        if (instrLen >= 4) {
          uint32_t typeId = code[i + 1];
          uint32_t resultId = code[i + 2];
          uint32_t storageClass = code[i + 3];
          ids[resultId].storageClass = storageClass;
          ids[resultId].typeId = typeId;
        }
        break;
      }

      case SpvOpTypeVoid:
        if (instrLen >= 2) { types[code[i+1]].kind = TypeInfo::Void; }
        break;

      case SpvOpTypeFloat:
      case SpvOpTypeInt:
        if (instrLen >= 2) { types[code[i+1]].kind = TypeInfo::Scalar; types[code[i+1]].componentCount = 1; }
        break;

      case SpvOpTypeVector:
        if (instrLen >= 4) {
          types[code[i+1]].kind = TypeInfo::Vector;
          types[code[i+1]].elementType = code[i+2];
          types[code[i+1]].componentCount = code[i+3];
        }
        break;

      case SpvOpTypeMatrix:
        if (instrLen >= 4) {
          types[code[i+1]].kind = TypeInfo::Matrix;
          types[code[i+1]].elementType = code[i+2];
          types[code[i+1]].componentCount = code[i+3];
        }
        break;

      case SpvOpTypeImage:
        if (instrLen >= 2) {
          types[code[i+1]].kind = TypeInfo::Image;
          // Dim is at operand index 3 (word i+3): 0=1D, 1=2D, 2=3D, 3=Cube
          if (instrLen >= 4 && code[i+3] == 3)
            types[code[i+1]].isCubemap = true;
        }
        break;

      case SpvOpTypeSampler:
        if (instrLen >= 2) { types[code[i+1]].kind = TypeInfo::Sampler; }
        break;

      case SpvOpTypeSampledImage:
        if (instrLen >= 3) {
          types[code[i+1]].kind = TypeInfo::SampledImage;
          types[code[i+1]].elementType = code[i+2]; // underlying Image type
          // Propagate cubemap flag from the underlying Image type
          if (types.count(code[i+2]) && types[code[i+2]].isCubemap)
            types[code[i+1]].isCubemap = true;
        }
        break;

      case SpvOpTypeStruct:
        if (instrLen >= 2) { types[code[i+1]].kind = TypeInfo::Struct; }
        break;

      case SpvOpTypeArray:
        if (instrLen >= 3) {
          types[code[i+1]].kind = TypeInfo::Array;
          types[code[i+1]].elementType = code[i+2];
        }
        break;

      case SpvOpTypePointer:
        if (instrLen >= 4) {
          types[code[i+1]].kind = TypeInfo::Pointer;
          types[code[i+1]].storageClass = code[i+2];
          types[code[i+1]].pointeeType = code[i+3];
        }
        break;
    }

    i += instrLen;
  }

  // Second pass: classify variables
  uniformBuffers.clear();
  sampledImages.clear();
  stageInputs.clear();

  for (auto& [id, info] : ids) {
    if (info.storageClass == ~0u) continue; // not a variable

    // Resolve pointee type through the pointer
    uint32_t pointeeTypeId = 0;
    if (types.count(info.typeId) && types[info.typeId].kind == TypeInfo::Pointer)
      pointeeTypeId = types[info.typeId].pointeeType;

    TypeInfo::Kind pointeeKind = TypeInfo::Unknown;
    uint32_t vecComponents = 4;
    if (types.count(pointeeTypeId)) {
      pointeeKind = types[pointeeTypeId].kind;
      vecComponents = types[pointeeTypeId].componentCount;
    }

    // Debug: log every variable
    #if 0 // enable for raw SPIR-V variable dump
    T8_LOG_INFO("[SPIRVRefl] ID=%u name='%s' storage=%u binding=%u set=%u pointeeKind=%d",
           id, info.name.c_str(), info.storageClass, info.binding, info.set, (int)pointeeKind);
    #endif

    if (info.storageClass == SpvStorageClassUniform ||
        info.storageClass == SpvStorageClassUniformConstant) {
      // Distinguish UBO from sampled image by pointee type
      if (pointeeKind == TypeInfo::SampledImage ||
          pointeeKind == TypeInfo::Image) {
        // Image or SampledImage → texture binding
        SPIRVBinding b;
        b.name    = info.name;
        b.set     = info.set;
        b.binding = info.binding;
        b.id      = id;
        // Check cubemap: walk SampledImage → Image → check dim
        b.isCubemap = false;
        if (types.count(pointeeTypeId)) {
          auto& pt = types[pointeeTypeId];
          if (pt.isCubemap) {
            b.isCubemap = true;
          } else if (pt.kind == TypeInfo::SampledImage && pt.elementType != 0) {
            uint32_t imgTypeId = pt.elementType;
            if (types.count(imgTypeId) && types[imgTypeId].isCubemap)
              b.isCubemap = true;
          }
        }
        sampledImages.push_back(b);
      } else if (pointeeKind == TypeInfo::Sampler) {
        // Skip standalone samplers — they share binding with the paired Image
        // and will be combined into COMBINED_IMAGE_SAMPLER descriptors
      } else {
        // Struct/block → UBO
        SPIRVBinding b;
        b.name    = info.name;
        b.set     = info.set;
        b.binding = info.binding;
        b.id      = id;
        uniformBuffers.push_back(b);
      }
    }
    else if (info.storageClass == SpvStorageClassInput) {
      if (info.isBuiltIn) continue; // skip gl_VertexIndex etc.
      SPIRVInput inp;
      inp.name     = info.name;
      inp.location = info.location;
      inp.vecSize  = vecComponents;
      inp.id       = id;
      stageInputs.push_back(inp);
    }
  }

  // Sort inputs by location for consistent vertex layout
  std::sort(stageInputs.begin(), stageInputs.end(),
    [](const SPIRVInput& a, const SPIRVInput& b) { return a.location < b.location; });
  // Sort bindings by binding number
  std::sort(uniformBuffers.begin(), uniformBuffers.end(),
    [](const SPIRVBinding& a, const SPIRVBinding& b) { return a.binding < b.binding; });
  std::sort(sampledImages.begin(), sampledImages.end(),
    [](const SPIRVBinding& a, const SPIRVBinding& b) { return a.binding < b.binding; });

  return true;
}

const SPIRVBinding* SPIRVReflection::FindBinding(const std::string& name) const {
  for (auto& b : uniformBuffers) if (b.name == name) return &b;
  for (auto& b : sampledImages) if (b.name == name) return &b;
  return nullptr;
}

const SPIRVInput* SPIRVReflection::FindInput(const std::string& name) const {
  for (auto& inp : stageInputs) if (inp.name == name) return &inp;
  return nullptr;
}

void SPIRVReflection::ShiftUBOBindings(uint32_t* code, size_t wordCount, uint32_t uboBindingShift) {
  if (wordCount < 5 || code[0] != 0x07230203) return;

  // First pass: identify UBO variable IDs (storage class = Uniform = 2)
  std::unordered_map<uint32_t, bool> uboVarIds;
  size_t i = 5;
  while (i < wordCount) {
    uint32_t instrWord = code[i];
    uint16_t opcode   = instrWord & 0xFFFF;
    uint16_t instrLen = (instrWord >> 16) & 0xFFFF;
    if (instrLen == 0) break;

    if (opcode == 59 /* OpVariable */ && instrLen >= 4) {
      uint32_t resultId = code[i + 2];
      uint32_t storageClass = code[i + 3];
      if (storageClass == 2 /* Uniform */) {
        uboVarIds[resultId] = true;
      }
    }
    i += instrLen;
  }

  // Second pass: patch Binding decorations for UBO variables
  i = 5;
  while (i < wordCount) {
    uint32_t instrWord = code[i];
    uint16_t opcode   = instrWord & 0xFFFF;
    uint16_t instrLen = (instrWord >> 16) & 0xFFFF;
    if (instrLen == 0) break;

    if (opcode == 71 /* OpDecorate */ && instrLen >= 4) {
      uint32_t targetId = code[i + 1];
      uint32_t decoration = code[i + 2];
      if (decoration == 33 /* Binding */ && uboVarIds.count(targetId)) {
        code[i + 3] += uboBindingShift; // shift the binding value in-place
      }
    }
    i += instrLen;
  }
}

} // namespace t800
