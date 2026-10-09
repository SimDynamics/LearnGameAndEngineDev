#version 450

layout(location = 0) in vec3 worldNormal;
layout(location = 1) in vec3 baseColor;
layout(location = 0) out vec4 outputColor;

void main() {
    vec3 lightDirection = normalize(vec3(-0.55, 0.8, 1.0));
    float diffuse = max(dot(normalize(worldNormal), lightDirection), 0.0);
    outputColor = vec4(baseColor * (0.26 + diffuse * 0.74), 1.0);
}
