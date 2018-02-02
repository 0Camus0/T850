#ifdef ES_30
	in highp vec4 Vertex;
	in highp vec4 Normal;
	in highp vec2 UV;
#else
	attribute highp vec4 Vertex;
	attribute highp vec4 Normal;
	attribute highp vec2 UV;
#endif

#ifdef ES_30
	out highp vec2 vecUVCoords;
#endif

uniform highp mat4 WVP;
uniform highp mat4 World;

void main(){
	vecUVCoords	= UV;
	vecUVCoords.y = 1.0 - vecUVCoords.y;
	gl_Position = WVP*Vertex;
}