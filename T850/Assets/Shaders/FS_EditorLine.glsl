// Editor line FS — the LineColor uniform is declared only in VS_EditorLine.glsl
// and forwarded here via the `vColor` varying. See VS_EditorLine.glsl for the
// rationale (avoid double-reflection of the same uniform across stages).
#ifdef ES_30
	precision mediump float;
	in highp vec4 vColor;
	layout(location = 0) out highp vec4 colorOut;
#else
	varying highp vec4 vColor;
#endif

uniform sampler2D depthTex;
uniform sampler2D depthTex2;
uniform highp vec4 DepthParams;

highp float SampleDepth(sampler2D tex, highp vec2 uv) {
#ifdef ES_30
	return texture(tex, uv).r;
#else
	return texture2D(tex, uv).r;
#endif
}

void main(){
	highp vec2 screenUV = clamp(gl_FragCoord.xy * DepthParams.xy, 0.0, 1.0);
	highp float sceneDepth = max(SampleDepth(depthTex, screenUV),
	                             SampleDepth(depthTex2, screenUV));
	highp float wireDepth = gl_FragCoord.z;
	if (sceneDepth > 0.0001 && wireDepth < sceneDepth * (1.0 - DepthParams.w)) {
		discard;
	}
#ifdef ES_30
	colorOut = vColor;
#else
	gl_FragColor = vColor;
#endif
}
