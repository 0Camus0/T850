uniform highp vec4 color;

#ifdef ES_30
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
		colorOut = vec4(color.xyz,texture(tex0,coords).a);
	#else
		gl_FragColor = vec4(color.xyz,texture2D(tex0,coords).a);
	#endif
}



