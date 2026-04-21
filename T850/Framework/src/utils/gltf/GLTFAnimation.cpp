/*********************************************************
 * glTF 2.0 — skin / animation conversion.
 *
 * Phase 1 stub: skinning and animations are deferred to Phase 2
 * (see plan §9). This translation unit exists today so the build
 * system entries are stable across phases.
 *
 * The Phase-2 implementation will:
 *   - Walk doc.skins[*].joints, mapping joint node hierarchy onto
 *     xSkeleton.Bones with Dad indices set from parent links.
 *   - Read inverseBindMatrices into xSkinWeights.MatrixOffset.
 *   - Read JOINTS_0 / WEIGHTS_0 attributes and fan them out into
 *     xSkinInfo.SkinWeights[joint].VertexIndices/Weights.
 *   - Convert AnimationChannels into xAnimationBone.PositionKeys,
 *     RotationKeys, ScaleKeys with TicksPerSecond = 4800.
 *   - LINEAR keyframes pass through; STEP duplicates each key at
 *     t and t+ε; CUBICSPLINE is baked to LINEAR at TicksPerSecond
 *     resolution so the existing runtime LERP is correct.
 *********************************************************/

#include <utils/gltf/GLTFLoader.h>
#include <utils/gltf/GLTFTypes.h>
#include <utils/XDataBase.h>

namespace t800 {
namespace gltf {

void BuildSkinsAndAnimations(const Document& /*doc*/,
                             xF::XDataBase& /*out*/) {
  // Phase 2.
}

} // namespace gltf
} // namespace t800
