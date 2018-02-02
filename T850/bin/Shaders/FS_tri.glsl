#ifdef ES_30
	int highp vec3 VertexColor;
	layout(location = 0) out highp vec4 colorOut;
#else
	varying highp vec3 VertexColor;
#endif

void main(){
#ifdef ES_30
	colorOut = vec4(VertexColor,1.0);		
#else
	gl_FragColor = vec4(VertexColor,1.0);	
#endif
}