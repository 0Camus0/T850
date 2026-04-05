#ifdef ES_30
	in highp vec4 MyVertex;
#else
	attribute highp vec4 MyVertex;
#endif

uniform highp mat4 VP;

void main(){
	gl_Position = VP*MyVertex;
}