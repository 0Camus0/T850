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

#ifdef USE_TEXCOORD1
	#ifdef ES_30
		in highp vec2 UV1;
	#else
		attribute highp vec2 UV1;
	#endif
#endif

#ifdef USE_TEXCOORD2
	#ifdef ES_30
		in highp vec2 UV2;
	#else
		attribute highp vec2 UV2;
	#endif
#endif

#ifdef USE_TEXCOORD3
	#ifdef ES_30
		in highp vec2 UV3;
	#else
		attribute highp vec2 UV3;
	#endif
#endif

#if defined(USE_SKINNING) || defined(USE_SKINNING_QT) || defined(USE_SKINNING_TEXTURE)
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

#ifdef USE_TEXCOORD1
	#ifdef ES_30
		out highp vec2 vecUVCoords1;
	#else
		varying highp vec2 vecUVCoords1;
	#endif
#endif

#ifdef USE_TEXCOORD2
	#ifdef ES_30
		out highp vec2 vecUVCoords2;
	#else
		varying highp vec2 vecUVCoords2;
	#endif
#endif

#ifdef USE_TEXCOORD3
	#ifdef ES_30
		out highp vec2 vecUVCoords3;
	#else
		varying highp vec2 vecUVCoords3;
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
// Scene clock for vertex displacement. Declared unconditionally and directly after
// Light0Direction because GL assigns uniform offsets in declaration order: the same
// 16 bytes sit after Light0Dir in RenderMesh::CBuffer.
uniform highp vec4 TimeParams;
#ifdef FLUID_SURFACE
// The solver's surface height field. Fetched with texelFetch, so no sampler state.
uniform highp sampler2D FluidSurfaceTexture;
#endif
#ifdef USE_SKINNING_TEXTURE
uniform highp sampler2D u_BoneTex;
mat4 getBoneMatrix(int index) {
	int texSize = textureSize(u_BoneTex, 0).x;
	int pixelIndex = index * 4;
	mat4 result;
	for (int i = 0; i < 4; ++i) {
		int px = (pixelIndex + i) % texSize;
		int py = (pixelIndex + i) / texSize;
		result[i] = texelFetch(u_BoneTex, ivec2(px, py), 0);
	}
	return result;
}
#elif defined(USE_SKINNING_QT)
// Quaternion+Translation: 2 vec4/bone — 256 bones = 512 vec4 (fits GL limit)
uniform highp vec4 BoneQuats[256];
uniform highp vec4 BoneTrans[256];
#elif defined(USE_SKINNING)
// Matrix: 4 vec4/bone — capped at 128 bones on GL (512 vec4)
uniform highp mat4 BoneMatrices[128];
#endif

void main(){
#ifdef USE_SKINNING_TEXTURE
	ivec4 idx = ivec4(Joints);
	// Texture stores rows → GLSL reads into columns (auto-transpose)
	// so skinMatrix * Vertex gives correct row-vector result
	mat4 skinMatrix = getBoneMatrix(idx.x) * Weights.x
	                + getBoneMatrix(idx.y) * Weights.y
	                + getBoneMatrix(idx.z) * Weights.z
	                + getBoneMatrix(idx.w) * Weights.w;
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

#elif defined(USE_SKINNING_QT)
	ivec4 idx = ivec4(Joints);
	vec4 q = BoneQuats[idx.x] * Weights.x
	       + BoneQuats[idx.y] * Weights.y
	       + BoneQuats[idx.z] * Weights.z
	       + BoneQuats[idx.w] * Weights.w;
	q = normalize(q);
	vec3 t = BoneTrans[idx.x].xyz * Weights.x
	       + BoneTrans[idx.y].xyz * Weights.y
	       + BoneTrans[idx.z].xyz * Weights.z
	       + BoneTrans[idx.w].xyz * Weights.w;
	vec3 p = Vertex.xyz;
	vec3 u = q.xyz;
	float s = q.w;
	p = p + 2.0 * cross(u, cross(u, p) + s * p);
	vec4 skinnedPos = vec4(p + t, 1.0);
#ifdef USE_NORMALS
	vec3 n = Normal.xyz;
	vec3 skinnedNormal = n + 2.0 * cross(u, cross(u, n) + s * n);
#endif
#ifdef USE_TANGENTS
	vec3 tg = Tangent.xyz;
	vec3 skinnedTangent = tg + 2.0 * cross(u, cross(u, tg) + s * tg);
#endif
#ifdef USE_BINORMALS
	vec3 bn = Binormal.xyz;
	vec3 skinnedBinormal = bn + 2.0 * cross(u, cross(u, bn) + s * bn);
#endif

#elif defined(USE_SKINNING)
	ivec4 idx = min(ivec4(Joints), ivec4(127));
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

#ifdef USE_TEXCOORD0
	vecUVCoords = UV;
	vecUVCoords.y = vecUVCoords.y;
#endif

#ifdef USE_TEXCOORD1
	vecUVCoords1 = UV1;
	vecUVCoords1.y = vecUVCoords1.y;
#endif

#ifdef USE_TEXCOORD2
	vecUVCoords2 = UV2;
#endif

#ifdef USE_TEXCOORD3
	vecUVCoords3 = UV3;
#endif

#ifdef SHADOW_MAP_PASS
		Pos = WVP*skinnedPos;
		gl_Position = Pos;
#else
#ifdef FLUID_SURFACE
	// Displace the water surface by the solver's height field (metres).
	{
		highp ivec2 fluidSize = textureSize(FluidSurfaceTexture, 0);
		highp vec2 fluidUV = clamp(UV, 0.0, 1.0);
		// Bilinear reconstruction: the solver writes to a STORAGE texture, which cannot be
		// hardware-filtered, so a plain texelFetch snaps every vertex onto a texel. This
		// blend is what makes the mesh look continuous, and it must match the fragment
		// shader's convention exactly or the two disagree about where the surface is.
		highp vec2 gridPos = fluidUV * vec2(fluidSize) - 0.5;
		highp ivec2 base = clamp(ivec2(floor(gridPos)), ivec2(0),
		                         ivec2(max(fluidSize.x - 2, 0), max(fluidSize.y - 2, 0)));
		highp vec2 frac = clamp(gridPos - vec2(base), 0.0, 1.0);
		highp float h00 = texelFetch(FluidSurfaceTexture, base + ivec2(0, 0), 0).x;
		highp float h10 = texelFetch(FluidSurfaceTexture, base + ivec2(1, 0), 0).x;
		highp float h01 = texelFetch(FluidSurfaceTexture, base + ivec2(0, 1), 0).x;
		highp float h11 = texelFetch(FluidSurfaceTexture, base + ivec2(1, 1), 0).x;
		highp float fluidHeightValue = mix(mix(h00, h10, frac.x), mix(h01, h11, frac.x), frac.y);
		skinnedPos.y += fluidHeightValue * TimeParams.z;

		// Optional geometric ripple octaves: a shading-only normal leaves the silhouette
		// flat, which is the giveaway at grazing angles. TimeParams.w gates them so the
		// water can be driven purely by the solver.
		if (TimeParams.w > 0.5) {
			highp vec2 ripplePos = skinnedPos.xz;
			highp float rippleTime = TimeParams.x;
			highp float rippleScale = TimeParams.y;
			highp float rippleHeight =
				0.0160 * sin(dot(ripplePos, vec2( 0.86, 0.51)) * 10.134 + rippleTime * 5.573) +
				0.0090 * sin(dot(ripplePos, vec2(-0.60, 0.80)) * 15.322 + rippleTime * 11.492) +
				0.0060 * sin(dot(ripplePos, vec2( 0.31, -0.95)) * 23.271 + rippleTime * 24.434);
			skinnedPos.y += rippleHeight * rippleScale;
		}
	}
#endif
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

		Pos 	 = WVP*skinnedPos;
		WorldPos = World*skinnedPos;
		
		gl_Position = Pos;
#endif
}