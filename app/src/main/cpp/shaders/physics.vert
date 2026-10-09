#version 450

layout(push_constant) uniform ObjectState {
    vec4 camera;     // x: elapsed time, y: viewport aspect
    vec4 transform;  // xyz: center, w: radius
    vec4 material;   // rgb: color, w: 0 sphere, 1 floor, 2 picked sphere
} objectState;

layout(location = 0) out vec3 worldNormal;
layout(location = 1) out vec3 baseColor;
layout(location = 2) out vec3 worldPosition;
layout(location = 3) flat out int objectKind;

void main() {
    int kind = int(objectState.material.w + 0.5);
    vec3 normal;
    vec3 world;

    // Floor: two triangles, rendered by the same pipeline as the spheres.
    if (kind == 1) {
        const vec2 corners[6] = vec2[](
            vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
            vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0)
        );
        vec2 uv = corners[gl_VertexIndex];
        world = vec3(uv.x * 2.25, -1.90, -5.95 + uv.y * 2.35);
        normal = vec3(0.0, 1.0, 0.0);
    } else {
        // Parametric sphere generated entirely on the GPU:
        // 16 longitude x 12 latitude cells, two triangles per cell.
        int cell = gl_VertexIndex / 6;
        int longitudeCell = cell % 16;
        int latitudeCell = cell / 16;
        int corner = gl_VertexIndex % 6;
        float u = float(longitudeCell + ((corner == 1 || corner == 2 || corner == 4) ? 1 : 0)) / 16.0;
        float v = float(latitudeCell + ((corner == 2 || corner == 4 || corner == 5) ? 1 : 0)) / 12.0;
        float longitude = u * 6.28318530718;
        float latitude = v * 3.14159265359;
        normal = vec3(sin(latitude) * cos(longitude),
                      cos(latitude),
                      sin(latitude) * sin(longitude));
        world = objectState.transform.xyz + normal * objectState.transform.w;
    }

    float aspect = max(objectState.camera.y, 0.2);
    float focal = 1.8;
    // OpenGL ES and Vulkan use different clip-space depth ranges.
    // Vulkan: z normalized into [0, 1].
    float z = -1.001001 * world.z - 0.1001001;
    gl_Position = vec4(
        focal * world.x / aspect,
        -focal * world.y,
        z,
        -world.z
    );
    worldNormal = normal;
    baseColor = objectState.material.rgb;
    worldPosition = world;
    objectKind = kind;
}
