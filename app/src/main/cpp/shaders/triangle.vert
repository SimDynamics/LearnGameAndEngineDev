#version 450

layout(push_constant) uniform Animation {
    float seconds;
} animation;

layout(location = 0) out vec4 triangleColor;

void main() {
    const vec2 vertices[3] = vec2[](
        vec2(0.0, -0.7),
        vec2(-0.7, 0.5),
        vec2(0.7, 0.5)
    );

    vec2 point = vertices[gl_VertexIndex];

    float angle = animation.seconds * 0.8;
    float cosine = cos(angle);
    float sine = sin(angle);

    vec2 rotated = vec2(
        cosine * point.x - sine * point.y,
        sine * point.x + cosine * point.y
    );

    gl_Position = vec4(rotated, 0.0, 1.0);
    triangleColor = vec4(0.98, 0.58, 0.16, 1.0);
}
