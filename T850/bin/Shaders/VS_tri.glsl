#ifdef ES_30
	in highp vec3 MyVertex;
	in lowp vec3 MyColor;
#else
	attribute highp vec3 MyVertex;
	attribute lowp vec3 MyColor;
#endif

#ifdef ES_30
	out highp vec3 VertexColor;
#else
	varying highp vec3 VertexColor;
#endif

uniform highp mat4 MyMatrix;

void main(){
	VertexColor	= MyColor;
	gl_Position = MyMatrix*vec4(MyVertex,1.0);
}