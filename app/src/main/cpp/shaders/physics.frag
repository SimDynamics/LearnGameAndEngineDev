#version 450

layout(location = 0) in vec3 worldNormal;
layout(location = 1) in vec3 baseColor;
layout(location = 2) in vec3 worldPosition;
layout(location = 3) flat in int objectKind;
layout(location = 0) out vec4 outputColor;

void main() {
    vec3 light = normalize(vec3(-0.55, 0.9, 0.75));
    float diffuse = max(dot(normalize(worldNormal), light), 0.0);
    vec3 color = baseColor * (0.28 + 0.72 * diffuse);
    if (objectKind == 1) {
        vec2 grid = abs(fract(worldPosition.xz * 1.35) - 0.5);
        float line = 1.0 - smoothstep(0.465, 0.495, max(grid.x, grid.y));
        color = mix(vec3(0.09, 0.17, 0.23), vec3(0.14, 0.23, 0.31), line);
    } else if (objectKind == 2) {
        color += vec3(0.25, 0.22, 0.06);
    }
    outputColor = vec4(color, 1.0);
}
