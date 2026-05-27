#ifdef ES_30
	precision mediump float;
	layout(location = 0) out highp vec4 colorOut;
#endif
uniform highp vec4 LineColor;
void main(){
#ifdef ES_30
	colorOut = LineColor;		
#else
	gl_FragColor = LineColor;	
#endif
}
