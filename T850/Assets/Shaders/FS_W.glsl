#ifdef ES_30
	precision mediump float;
	layout(location = 0) out highp vec4 colorOut;
#endif
void main(){
#ifdef ES_30
	colorOut = vec4(1.0,0.0,1.0,1.0);		
#else
	gl_FragColor = vec4(1.0,0.0,1.0,1.0);	
#endif
}