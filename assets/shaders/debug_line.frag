#version 450 core

// 调试线框片元：纯色 uColor（默认白；碰撞体按运动类型着色由调用方传入）

uniform vec3 uColor;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vec4(uColor, 1.0);
}
