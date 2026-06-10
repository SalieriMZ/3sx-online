#version 110

uniform sampler2D uPalette;
uniform sampler2D uIndexTex;

varying vec4 vColor;
varying vec2 vTexCoord;

// 4bpp index unpack is disabled on Vita until the Sony Cg compiler quirks
// are mapped out (multi-line expressions + chained vglMul + divisions all
// trip its parser inside this single function). For now, fall back to the
// 8bpp path so glLinkProgram succeeds; visuals on 4bpp textures will be
// wrong but the boot path stops crashing.
void main() {
    float index = texture2D(uIndexTex, vTexCoord).r * 255.0;
    float palU = (index + 0.5) * 0.00390625;
    vec4 c = texture2D(uPalette, vec2(palU, 0.5)) * vColor;
    if (c.a == 0.0) {
        discard;
    }
    gl_FragColor = c;
}
