#ifdef ES_30
	in highp vec4 MyVertex;
#else
	attribute highp vec4 MyVertex;
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

uniform highp mat4 VP;
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
	gl_Position = VP * (skinMatrix * MyVertex);
#else
	gl_Position = VP*MyVertex;
#endif
}