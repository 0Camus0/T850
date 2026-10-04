struct ArithmeticConstants {
    elementCount: u32,
    addend: u32,
    multiplier: u32,
    xorMask: u32,
}

@group(0) @binding(0) var<uniform> constants: ArithmeticConstants;
@group(0) @binding(1) var<storage, read_write> output: array<u32>;
#ifdef COMPUTE_READ_INPUT
@group(0) @binding(2) var<storage, read> input: array<u32>;
#endif

@compute @workgroup_size(64, 1, 1)
fn CS(@builtin(global_invocation_id) dispatchId: vec3<u32>) {
    if (dispatchId.x >= constants.elementCount) { return; }
#ifdef COMPUTE_READ_INPUT
    let value = (input[dispatchId.x] + constants.addend) * constants.multiplier;
#else
    let value = (dispatchId.x + constants.addend) * constants.multiplier;
#endif
    output[dispatchId.x] = value ^ constants.xorMask;
}