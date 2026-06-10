#version 110

attribute vec3 aPos;
attribute vec2 aTexCoord;
attribute vec4 aColor;

varying vec2 vTexCoord;
varying vec4 vColor;

void main() {
    gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);
    vTexCoord = aTexCoord;
    vColor = aColor;
}
