#ifdef ES_30
	in highp vec4 Vertex;
#else
	attribute highp vec4 Vertex;
#endif

#ifdef USE_NORMALS
	#ifdef ES_30
		in highp vec4 Normal;
	#else
		attribute highp vec4 Normal;
	#endif
#endif

#ifdef USE_TANGENTS
	#ifdef ES_30
		in highp vec4 Tangent;
	#else
		attribute highp vec4 Tangent;
	#endif
#endif

#ifdef USE_BINORMALS
	#ifdef ES_30
		in highp vec4 Binormal;
	#else
		attribute highp vec4 Binormal;
	#endif
#endif

#ifdef USE_TEXCOORD0
	#ifdef ES_30
		in highp vec2 UV;
	#else
		attribute highp vec2 UV;
	#endif
#endif

#ifdef USE_SKINNING
	#ifdef ES_30
		in highp vec4 Joints;
		in highp vec4 Weights;
	#else
		attribute highp vec4 Joints;
		attribute highp vec4 Weights;
	#endif
#endif


#ifdef USE_NORMALS
	#ifdef ES_30
		out highp vec4 hnormal;
	#else
		varying highp vec4 hnormal;
	#endif
#endif

#ifdef USE_TANGENTS
	#ifdef ES_30
		out highp vec4 htangent;
	#else
		varying highp vec4 htangent;
	#endif
#endif

#ifdef USE_BINORMALS
	#ifdef ES_30
		out highp vec4 hbinormal;
	#else
		varying highp vec4 hbinormal;
	#endif
#endif

#ifdef USE_TEXCOORD0
	#ifdef ES_30
		out highp vec2 vecUVCoords;
	#else
		varying highp vec2 vecUVCoords;
	#endif
#endif

#ifdef ES_30
	out highp vec4 Pos;
	out highp vec4 WorldPos;
#else
	varying highp vec4 Pos;
	varying highp vec4 WorldPos;
#endif

uniform highp mat4 WVP;
uniform highp mat4 World;
uniform highp mat4 WorldView;
uniform highp vec4 LightPos;
uniform highp vec4 LightColor;
uniform highp vec4 CameraPosition;
uniform highp vec4 CameraInfo;
uniform highp vec4 AmbientColor;
uniform highp vec4 DiffuseColor;
uniform highp vec4 SpecularColor;
uniform highp vec4 PBRParams;
uniform highp vec4 Intensities;
uniform highp vec4 ParallaxSettings;
uniform highp vec4 ParallaxShadowSettings;
uniform highp vec4 Light0Direction;
#ifdef USE_SKINNING
uniform highp mat4 BoneMatrices[256];
#endif

void main(){
#ifdef USE_SKINNING
	ivec4 idx = ivec4(Joints);
	mat4 skinMatrix = BoneMatrices[idx.x] * Weights.x
	                + BoneMatrices[idx.y] * Weights.y
	                + BoneMatrices[idx.z] * Weights.z
	                + BoneMatrices[idx.w] * Weights.w;
	vec4 skinnedPos = skinMatrix * Vertex;
#ifdef USE_NORMALS
	vec3 skinnedNormal = mat3(skinMatrix) * vec3(Normal);
#endif
#ifdef USE_TANGENTS
	vec3 skinnedTangent = mat3(skinMatrix) * vec3(Tangent);
#endif
#ifdef USE_BINORMALS
	vec3 skinnedBinormal = mat3(skinMatrix) * vec3(Binormal);
#endif
#else
	vec4 skinnedPos = Vertex;
#ifdef USE_NORMALS
	vec3 skinnedNormal = vec3(Normal);
#endif
#ifdef USE_TANGENTS
	vec3 skinnedTangent = vec3(Tangent);
#endif
#ifdef USE_BINORMALS
	vec3 skinnedBinormal = vec3(Binormal);
#endif
#endif

#ifdef SHADOW_MAP_PASS
		Pos = WVP*skinnedPos;
		gl_Position = Pos;
#else
		mat3 RotWorld = mat3(World);
	#ifdef USE_NORMALS
		hnormal	= vec4(normalize(RotWorld*skinnedNormal),1.0);
	#endif

	#ifdef USE_TANGENTS
		htangent	= vec4(normalize(RotWorld*skinnedTangent),1.0);
	#endif

	#ifdef USE_BINORMALS
		hbinormal	= vec4(normalize(RotWorld*skinnedBinormal),1.0);
	#endif

	#ifdef NON_LINEAR_DEPTH
		Pos 	 = WVP*skinnedPos;
	#else
		Pos 	 = WorldView*skinnedPos;
	#endif
		WorldPos = World*skinnedPos;
		
	#ifdef USE_TEXCOORD0
		vecUVCoords = UV;
		vecUVCoords.y = vecUVCoords.y;
	#endif

	#ifdef NON_LINEAR_DEPTH
		gl_Position = Pos;
	#else
		gl_Position = WVP*skinnedPos;
	#endif
#endif
}