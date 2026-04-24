#ifdef ES_30
precision mediump float;
#endif

uniform highp vec4 DiffuseColor;

#ifdef USE_TEXCOORD0
	#ifdef ES_30
		in highp vec2 vecUVCoords;
	#else
		varying highp vec2 vecUVCoords;
	#endif
#endif

#ifdef USE_NORMALS
	#ifdef ES_30
		in highp vec4 hnormal;
	#else
		varying highp vec4 hnormal;
	#endif
#endif

#ifdef USE_TANGENTS
	#ifdef ES_30
		in highp vec4 htangent;
	#else
		varying highp vec4 htangent;
	#endif
#endif

#ifdef USE_BINORMALS
	#ifdef ES_30
		in highp vec4 hbinormal;
	#else
		varying highp vec4 hbinormal;
	#endif
#endif

#ifdef ES_30
	in highp vec4 Pos;
	in highp vec4 WorldPos;
	layout(location = 0) out highp vec4 colorOut;
#else
	varying highp vec4 Pos;
	varying highp vec4 WorldPos;
#endif

uniform highp sampler2D depthTex;

void main(){
	// Manual depth test against GBuffer's linear depth (COLOR4)
	vec2 screenUV = gl_FragCoord.xy * vec2(1.0/CameraInfo.z, 1.0/CameraInfo.w);
	float sceneDepth = texture(depthTex, screenUV).r;
	float wireDepth = Pos.z / CameraInfo.y;
	if (sceneDepth > 0.0001 && wireDepth > sceneDepth * 1.005)
		discard;
#ifdef ES_30
	colorOut = DiffuseColor;
#else
	gl_FragColor = DiffuseColor;
#endif
}
