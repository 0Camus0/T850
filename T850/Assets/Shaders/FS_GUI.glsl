uniform highp vec4 color;

#ifdef ES_30
	precision mediump float;
	in highp vec2 vecUVCoords;
#else
	varying highp vec2 vecUVCoords;
#endif

#ifdef ES_30
	layout(location = 0) out highp vec4 colorOut;
#endif

uniform mediump sampler2D tex0;

void main(){
	lowp vec2 coords = vecUVCoords;
	coords.y = 1.0 - coords.y;
	#ifdef ES_30
		vec4 texColor = texture(tex0, coords);
		colorOut = vec4(texColor.rgb * color.xyz, texColor.a);
	#else
		vec4 texColor = texture2D(tex0, coords);
		gl_FragColor = vec4(texColor.rgb * color.xyz, texColor.a);
	#endif
}
