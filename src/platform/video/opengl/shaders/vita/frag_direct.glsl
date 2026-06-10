#version 110

varying vec4 vColor;
varying vec2 vTexCoord;

uniform sampler2D uTexture;

void main() {
    vec4 c = texture2D(uTexture, vTexCoord) * vColor;
    if (c.a == 0.0) {
        discard;
    }
    gl_FragColor = c;
}
