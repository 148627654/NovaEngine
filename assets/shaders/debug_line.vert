#version 450 core

// 调试线框（V0.3-1 物理碰撞体）：世界坐标线段端点（a,b 成对，GL_LINES）
// CameraBlock（binding 0：mat4 uViewProj + vec4 uCamPos）复用主相机 UBO
layout(std140, binding = 0) uniform CameraBlock {
    mat4 uViewProj;
    vec4 uCamPos;
};

layout(location = 0) in vec3 aPosition;

void main() {
    gl_Position = uViewProj * vec4(aPosition, 1.0);
}
