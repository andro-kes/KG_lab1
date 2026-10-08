#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(std140, set = 0, binding = 0) uniform Scene {
    mat4 mvp;
    vec4 tint;
} scene;

layout(location = 0) out vec3 color;

void main() {
    gl_Position = scene.mvp * vec4(inPosition, 1.0);
    color = inColor * scene.tint.rgb;
}
