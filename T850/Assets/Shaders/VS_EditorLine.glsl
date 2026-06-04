// Editor line shader (GLSL). Uniform declaration ORDER MUST MATCH the HLSL
// cbuffer field order in VS_EditorLine.hlsl, because the GL backend reflects
// uniforms positionally to map them onto the constant-buffer byte layout.
//   WVP         -> bytes [0..64)
//   LineColor   -> bytes [64..80)
//   DepthParams -> bytes [80..96), declared in FS_EditorLine.glsl
//
// LineColor is consumed by both the VS (passed through as a varying) and
// the FS, but is declared *only here* — the FS reads `vColor`. This avoids
// the parser double-counting the uniform across stages, which would shift
// the byte offsets out of sync with the constant buffer.
#ifdef ES_30
	in highp vec4 MyVertex;
	out highp vec4 vColor;
	out highp float vClipDepth;
#else
	attribute highp vec4 MyVertex;
	varying highp vec4 vColor;
	varying highp float vClipDepth;
#endif

uniform highp mat4 WVP;
uniform highp vec4 LineColor;

void main(){
	gl_Position = WVP * MyVertex;
	vColor = LineColor;
	vClipDepth = gl_Position.z / gl_Position.w;
}
