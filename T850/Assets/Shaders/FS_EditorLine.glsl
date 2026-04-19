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

void main(){
#ifdef ES_30
	colorOut = vColor;
#else
	gl_FragColor = vColor;
#endif
}

