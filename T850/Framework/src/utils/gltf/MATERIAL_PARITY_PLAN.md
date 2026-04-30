# glTF Material Parity Plan

Goal: render glTF 2.0 materials close to Khronos Sample Renderer behavior, while keeping T850's post-processing free to differ.

## Ground Rules

- Keep material color math in linear space. Decode color textures/factors before lighting, tone map in linear HDR, and encode to sRGB only at final display output.
- Prefer runtime material flags and always-bound safe textures for new optional maps. `ShaderKey` is already out of spare feature bits after clearcoat maps.
- Apply `KHR_texture_transform` per textureInfo. Do not share transforms between maps unless the glTF textureInfo is actually shared.
- Preserve API parity first: D3D11, D3D12, GL, then Vulkan.
- Add one material feature at a time with dump comparisons on a targeted Khronos sample and the Porsche GLB.

## Phase 1: Low-Risk Core Gaps

Status: implemented in loader/runtime shaders with fixed texture slots 20-23 and GBuffer COLOR6 carrying dielectric F0 plus occlusion for deferred lighting. Dielectric F0 is derived from `KHR_materials_ior` before applying `KHR_materials_specular`. Needs visual sample validation against Khronos captures.

1. Occlusion texture
   - Loader records `occlusionMap`, texCoord, strength, and UV transform.
   - Runtime samples occlusion from the red channel.
   - Deferred and forward paths apply it to indirect lighting only.
   - Validate with Khronos `MaterialsOcclusion` samples.

2. `KHR_materials_specular` textures
   - Loader records `specularTexture` and `specularColorTexture` names, texCoord, transforms, and runtime map flags.
   - `specularTexture` modulates specular factor from its alpha channel.
   - `specularColorTexture` is decoded to linear before modulating dielectric F0.
   - Validate with Khronos `MaterialsSpecular` samples.

3. Transmission texture
   - Loader records `transmissionTexture` binding, texCoord, transform, and runtime flag.
   - Forward transmission samples from the red channel and multiplies by `transmissionFactor`.
   - Keep the existing screen-space approximation until volume/refraction is upgraded.
   - Validate with `MaterialsTransmission` samples.

## Phase 2: Clearcoat Completion

4. Clearcoat normal texture
   - Factor and roughness textures are already wired.
   - Add loader/render state for `clearcoatNormalTexture`, including scale, texCoord, and transform.
   - Decide representation before implementation:
     - Deferred option: add/repurpose GBuffer storage for a separate clearcoat normal.
     - Forward-only option: use clearcoat normal only in transparent/forward materials and document the deferred limitation.
   - Khronos behavior expects the clearcoat normal to affect only the clearcoat lobe, not the base layer normal.
   - Validate with `MaterialsClearcoat` samples.

## Phase 3: Larger BSDF Extensions

5. Volume and attenuation
   - Parse/use `thicknessFactor`, `thicknessTexture`, `attenuationDistance`, and `attenuationColor`.
   - Needs better transmission integration than the current screen-space blend.
   - Validate with `MaterialsVolume` samples.

6. Diffuse transmission
   - Add diffuse transmission factor/color and textures.
   - Integrate with the diffuse lobe separately from specular transmission.
   - Validate with `MaterialsDiffuseTransmission` samples.

7. Iridescence
   - Add factor, IOR, thickness range, and textures.
   - Requires shader-side thin-film Fresnel approximation and LUT/reference matching.
   - Validate with `MaterialsIridescence` samples.

8. Anisotropy
   - Add anisotropy strength, rotation, and texture.
   - Requires tangent-space anisotropic GGX in direct and IBL paths.
   - Validate with `MaterialsAnisotropy` samples.

9. Dispersion
   - Depends on stronger transmission/volume support.
   - Implement after volume so wavelength splitting has a correct medium to affect.

## Phase 4: Legacy/Compatibility

10. `KHR_materials_pbrSpecularGlossiness` texture path
    - Scalar diffuse/spec/gloss conversion exists, but `specularGlossinessTexture` is not fully rendered.
    - Decide whether to convert to metallic-roughness at load time or keep a separate shader path.
    - Prefer load-time conversion if it stays faithful enough and avoids another shader feature family.

## Validation Checklist Per Feature

- Add loader defaults and typed material fields.
- Add subset state, texture binding, CBuffer rows, and shader runtime flags.
- Add HLSL and GLSL behavior in both deferred and forward paths when applicable.
- Confirm D3D12 PSO state ordering is unaffected.
- Run Release x64 build.
- Capture D3D11, D3D12, and GL dumps for one targeted Khronos sample plus Porsche when relevant.
- Compare GBuffer depth/PBR/albedo/normal and final LDR output, allowing only post-processing differences where expected.