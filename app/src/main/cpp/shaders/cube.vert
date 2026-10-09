#version 450

// Each face generates two triangles procedurally from gl_VertexIndex.
// The Android native renderer does not need a vertex buffer for this demo.
layout(push_constant) uniform CameraState {
    float seconds;
    float aspectRatio;
} camera;

layout(location = 0) out vec3 worldNormal;
layout(location = 1) out vec3 baseColor;

void main() {
    const vec2 corners[6] = vec2[](
        vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
        vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0)
    );

    const vec3 normals[6] = vec3[](
        vec3(0, 0, 1), vec3(0, 0, -1),
        vec3(1, 0, 0), vec3(-1, 0, 0),
        vec3(0, 1, 0), vec3(0, -1, 0)
    );

    const vec3 tangents[6] = vec3[](
        vec3(1, 0, 0), vec3(-1, 0, 0),
        vec3(0, 0, -1), vec3(0, 0, 1),
        vec3(1, 0, 0), vec3(1, 0, 0)
    );

    const vec3 bitangents[6] = vec3[](
        vec3(0, 1, 0), vec3(0, 1, 0),
        vec3(0, 1, 0), vec3(0, 1, 0),
        vec3(0, 0, -1), vec3(0, 0, 1)
    );

    int face = gl_VertexIndex / 6;
    vec2 uv = corners[gl_VertexIndex % 6];

    vec3 normal = normals[face];
    vec3 point = 0.72 * (normal + tangents[face] * uv.x + bitangents[face] * uv.y);

    float yaw = camera.seconds * 0.65;
    float cy = cos(yaw);
    float sy = sin(yaw);
    mat3 rotateY = mat3(
        vec3(cy, 0.0, -sy),
        vec3(0.0, 1.0, 0.0),
        vec3(sy, 0.0, cy)
    );
    float pitch = 0.42 + camera.seconds * 0.32;
    float cx = cos(pitch);
    float sx = sin(pitch);
    mat3 rotateX = mat3(
        vec3(1.0, 0.0, 0.0),
        vec3(0.0, cx, sx),
        vec3(0.0, -sx, cx)
    );

    mat3 rotation = rotateX * rotateY;
    vec3 cameraPoint = rotation * point + vec3(0.0, 0.0, -4.0);

    // Near = 0.1, far = 100, perspective focal scale = 1.8.
    float focal = 1.8;
    float aspect = max(camera.aspectRatio, 0.25);
    // Vulkan expects depth in [0, 1], unlike OpenGL ES [-1, 1].
    float z = -1.001001 * cameraPoint.z - 0.1001001;

    // Vulkan's viewport Y points down, so match the OpenGL ES orientation.
    gl_Position = vec4(
        focal * cameraPoint.x / aspect,
        -focal * cameraPoint.y,
        z,
        -cameraPoint.z
    );
    worldNormal = rotation * normal;
    baseColor = vec3(0.19, 0.52, 0.91) + abs(normal) * vec3(0.56, 0.28, 0.05);
}
