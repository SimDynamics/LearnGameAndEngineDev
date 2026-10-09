#version 450

layout(location = 0) in vec4 triangleColor;
layout(location = 0) out vec4 outputColor;

void main() {
    outputColor = triangleColor;
}
