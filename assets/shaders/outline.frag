#version 450 core

// 选中描边：纯色输出（编辑器默认橙色）

uniform vec3 uColor;

out vec4 fragColor;

void main() {
    fragColor = vec4(uColor, 1.0);
}
