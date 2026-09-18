struct GodRaysConstants {
    wvpInverse: mat4x4<f32>,
    wvpLight: mat4x4<f32>,
    cameraPosition: vec4<f32>,
    sunDirectionAndBias: vec4<f32>,
    volumeCenterAndEnabled: vec4<f32>,
    volumeHalfExtentsAndFactor: vec4<f32>,
    outputSizeStepsAndEnabled: vec4<f32>,
}

@group(0) @binding(0) var<uniform> constants: GodRaysConstants;
@group(0) @binding(1) var sceneDepth: texture_2d<f32>;
@group(0) @binding(2) var shadowDepth: texture_2d<f32>;
@group(0) @binding(3) var sceneDepthSampler: sampler;
@group(0) @binding(4) var shadowDepthSampler: sampler;
@group(0) @binding(5) var outputTexture: texture_storage_2d<rgba8unorm, write>;

fn ComputeScattering(lightDotView: f32) -> f32 {
    let scattering = -0.2;
    return (1.0 - scattering * scattering) / (4.0 * 3.14159265359 *
        pow(1.0 + scattering * scattering - 2.0 * scattering * lightDotView, 1.5));
}

fn IntersectGodRaysBox(origin: vec3<f32>, direction: vec3<f32>,
    boxMin: vec3<f32>, boxMax: vec3<f32>) -> vec2<f32> {
    let epsilon = vec3<f32>(0.000001);
    let safeDirection = select(direction, select(epsilon, -epsilon, direction < vec3<f32>(0.0)),
        abs(direction) < epsilon);
    let first = (boxMin - origin) / safeDirection;
    let second = (boxMax - origin) / safeDirection;
    let near = min(first, second);
    let far = max(first, second);
    return vec2<f32>(max(max(near.x, near.y), near.z), min(min(far.x, far.y), far.z));
}

@compute @workgroup_size(8, 8, 1)
fn CS(@builtin(global_invocation_id) dispatchId: vec3<u32>) {
    let outputSize = vec2<u32>(constants.outputSizeStepsAndEnabled.xy);
    if (any(dispatchId.xy >= outputSize)) { return; }
    let empty = vec4<f32>(0.0, 0.0, 0.0, 1.0);
    if (constants.outputSizeStepsAndEnabled.w < 0.5) {
        textureStore(outputTexture, dispatchId.xy, empty);
        return;
    }
    let uv = (vec2<f32>(dispatchId.xy) + 0.5) / vec2<f32>(outputSize);
    let depth = textureSampleLevel(sceneDepth, sceneDepthSampler, uv, 0.0).r;
    if (depth <= 0.0001) {
        textureStore(outputTexture, dispatchId.xy, empty);
        return;
    }
    let clipPos = vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    let homogeneous = constants.wvpInverse * vec4<f32>(clipPos, depth, 1.0);
    let position = vec4<f32>(homogeneous.xyz / homogeneous.w, 1.0);
    let steps = max(i32(constants.outputSizeStepsAndEnabled.z), 2);
    let ray = position - constants.cameraPosition;
    let rayDir = normalize(ray);
    let rayLength = length(ray.xyz);
    var nearPosition = constants.cameraPosition;
    var farPosition = position;
    if (constants.volumeCenterAndEnabled.w > 0.5) {
        let halfExtents = max(abs(constants.volumeHalfExtentsAndFactor.xyz), vec3<f32>(0.001));
        var interval = IntersectGodRaysBox(constants.cameraPosition.xyz, rayDir.xyz,
            constants.volumeCenterAndEnabled.xyz - halfExtents, constants.volumeCenterAndEnabled.xyz + halfExtents);
        if (interval.y < max(interval.x, 0.0)) {
            textureStore(outputTexture, dispatchId.xy, empty);
            return;
        }
        interval = clamp(interval, vec2<f32>(0.0), vec2<f32>(rayLength));
        if (interval.y <= interval.x) {
            textureStore(outputTexture, dispatchId.xy, empty);
            return;
        }
        nearPosition = constants.cameraPosition + rayDir * interval.x;
        farPosition = constants.cameraPosition + rayDir * interval.y;
    }
    var accumulatedFog = vec3<f32>(0.0);
    let sunDirection = normalize(constants.sunDirectionAndBias.xyz);
    let shadowBias = max(constants.sunDirectionAndBias.w, 0.0);
    for (var index = 0; index < steps; index++) {
        let worldPosition = mix(farPosition, nearPosition, f32(index) / f32(steps - 1));
        let lightPosition = constants.wvpLight * worldPosition;
        let lightClip = lightPosition.xyz / lightPosition.w;
        let shadowUv = lightClip.xy * vec2<f32>(0.5, -0.5) + 0.5;
        if (all(shadowUv > vec2<f32>(0.0)) && all(shadowUv < vec2<f32>(1.0)) &&
            lightClip.z > 0.0 && lightClip.z < 1.0) {
            let sampledDepth = textureSampleLevel(shadowDepth, shadowDepthSampler, shadowUv, 0.0).r - shadowBias;
            if (lightClip.z >= sampledDepth) {
                accumulatedFog += vec3<f32>(0.9803, 0.8392, 0.6470) * ComputeScattering(dot(rayDir.xyz, sunDirection));
            }
        }
    }
    accumulatedFog = pow(accumulatedFog / f32(steps), vec3<f32>(0.4545));
    textureStore(outputTexture, dispatchId.xy, vec4<f32>(accumulatedFog * constants.volumeHalfExtentsAndFactor.w, 1.0));
}