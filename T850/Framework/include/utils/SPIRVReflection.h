#pragma once
// ─── Lightweight SPIR-V Reflection ────────────────────────
// Parses compiled SPIR-V to extract resource bindings and
// vertex input layouts. Portable, no dependencies beyond
// the SPIR-V spec constants.
//
// Usage:
//   SPIRVReflection refl;
//   refl.Parse(spirvData.data(), spirvData.size());
//   // refl.uniformBuffers, refl.sampledImages, refl.stageInputs

#include <vector>
#include <string>
#include <cstdint>

namespace t850 {

  struct SPIRVBinding {
    std::string name;
    uint32_t    set     = 0;
    uint32_t    binding = 0;
    uint32_t    id      = 0;    // SPIR-V result ID
    bool        isCubemap = false; // true if OpTypeImage has Dim=Cube
  };

  struct SPIRVInput {
    std::string name;
    uint32_t    location = 0;
    uint32_t    vecSize  = 4;   // number of components (1-4)
    uint32_t    id       = 0;
  };

  struct SPIRVReflection {
    std::vector<SPIRVBinding> uniformBuffers;   // UBOs (cbuffer)
    std::vector<SPIRVBinding> sampledImages;    // combined image samplers / textures
    std::vector<SPIRVInput>   stageInputs;      // vertex inputs (for VS only)

    // Parse a SPIR-V module. wordCount = number of uint32_t words.
    bool Parse(const uint32_t* code, size_t wordCount);

    // Find a binding by name
    const SPIRVBinding* FindBinding(const std::string& name) const;
    const SPIRVInput*   FindInput(const std::string& name) const;

    // Patch SPIR-V binary: shift UBO bindings to avoid collision with textures.
    // Modifies the code in-place. Adds uboBindingShift to all UBO Binding decorations.
    static void ShiftUBOBindings(uint32_t* code, size_t wordCount, uint32_t uboBindingShift);
  };

} // namespace t850
