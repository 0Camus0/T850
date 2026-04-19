// Editor line shader — used by the T8ditor host for grid / gizmo / wireframe
// overlays. Single color per draw via the constant buffer; wireframe meshes
// vary the color from the CPU side. Kept editor-only so Framework remains
// UI-toolkit-agnostic.
cbuffer ConstantBuffer{
    float4x4 WVP;
    float4   LineColor;
}

struct VS_INPUT{
    float4 position : POSITION;
};

struct VS_OUTPUT{
    float4 hposition : SV_POSITION;
};

VS_OUTPUT VS( VS_INPUT input ){
    VS_OUTPUT OUT;
    OUT.hposition = mul(WVP, input.position);
    return OUT;
}
