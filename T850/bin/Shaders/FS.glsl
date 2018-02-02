uniform highp sampler2D diffuse;

#ifdef ES_30
	in highp vec2 vecUVCoords;
#else
	varying highp vec2 vecUVCoords;
#endif

#ifdef ES_30
	layout(location = 0) out highp vec4 colorOut;
#endif

void main(){
#ifdef ES_30
	highp vec3 vector = texture(diffuse,vecUVCoords).rgb;
	colorOut = vec4(vector,1.0);
#else
	highp vec3 vector = texture2D(diffuse,vecUVCoords).rgb;
	gl_FragColor = vec4(vector,1.0);
#endif
}