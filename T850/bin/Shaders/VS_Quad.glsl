#ifdef ES_30
	in highp vec4 Vertex;
	in highp vec2 UV;
#else
	attribute highp vec4 Vertex;
	attribute highp vec2 UV;
#endif

#ifdef ES_30
	out highp vec2 vecUVCoords;
	out highp vec4 Pos;
	out highp vec4 PosCorner;
#else
	varying highp vec2 vecUVCoords;
	varying highp vec4 Pos;
	varying highp vec4 PosCorner;
#endif

uniform highp mat4 WVP;
uniform highp mat4 World;
uniform highp mat4 WorldView;
uniform highp mat4 WVPInverse;
uniform highp mat4 WVPLight;
uniform highp mat4 Projection;
uniform highp vec4 LightPositions[128];
uniform highp vec4 LightColors[128];
uniform highp vec4 LightRadius[32];
uniform highp vec4 CameraPosition;
uniform highp vec4 CameraInfo;
uniform highp vec4 LightCameraPosition;
uniform highp vec4 LightCameraInfo;
uniform highp vec4   brightness;
uniform highp vec4   toogles;

void main(){
	vecUVCoords = UV;	
	Pos = WVP*Vertex;
#ifdef NON_LINEAR_DEPTH
	PosCorner = vec4(Vertex.xy,1.0,1.0);
#else
	PosCorner = WVPInverse*vec4(Vertex.xy,1.0,1.0);
	PosCorner.xyz /= PosCorner.w;
	PosCorner = PosCorner - CameraPosition;
#endif
	gl_Position = Pos;
}