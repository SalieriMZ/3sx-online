#version 110

uniform sampler2D uPalette;
uniform sampler2D uIndexTex;

varying vec4 vColor;
varying vec2 vTexCoord;

// uIndexTex is a luminance texture; each texel's red channel is the 8-bit index
// scaled to [0,1]. uPalette is a 256x1 RGBA texture (256 entries).
void main() {
    float index = texture2D(uIndexTex, vTexCoord).r * 255.0;
    vec2 palUV = vec2((index + 0.5) / 256.0, 0.5);
    vec4 c = texture2D(uPalette, palUV) * vColor;
    if (c.a == 0.0) {
        discard;
    }
    gl_FragColor = c;
}
