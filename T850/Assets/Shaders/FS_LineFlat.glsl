// Flat line FS — always visible, no depth testing.
// The LineColor uniform is declared only in VS_EditorLine.glsl
// and forwarded here via the vColor varying.
#ifdef ES_30
        precision mediump float;
        in highp vec4 vColor;
        layout(location = 0) out highp vec4 colorOut;
#else
        varying highp vec4 vColor;
#endif

void main(){
#ifdef ES_30
        colorOut = vColor;
#else
        gl_FragColor = vColor;
#endif
}
