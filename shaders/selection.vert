#version 450

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform PushConstants {
    vec4 blockPos;  // xyz = world position of block origin, w = unused
} pc;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
} camera;

void main() {
    vec3 worldPos = inPosition + pc.blockPos.xyz;
    gl_Position = camera.proj * camera.view * vec4(worldPos, 1.0);
}