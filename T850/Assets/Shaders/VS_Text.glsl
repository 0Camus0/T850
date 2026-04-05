#ifdef ES_30
	in highp vec4 Vertex;
	in highp vec2 UV;
#else
	attribute highp vec4 Vertex;
	attribute highp vec2 UV;
#endif

#ifdef ES_30
	out highp vec2 vecUVCoords;
#else
	varying highp vec2 vecUVCoords;
#endif

uniform highp vec4 color;

void main(){
	vecUVCoords = UV;	
	gl_Position = Vertex;
}