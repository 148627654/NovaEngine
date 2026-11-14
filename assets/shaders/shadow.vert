#version 450 core

// 阴影深度 pass（V0.2-6）：顶点属性 layout 0-3 与 Mesh 交错布局一致，复用同一 VAO

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;    // 未使用（与 Mesh VAO 布局对齐）
layout(location = 2) in vec2 aUV;        // 未使用
layout(location = 3) in vec3 aTangent;   // 未使用

uniform mat4 uLightViewProj;   // 光空间矩阵（computeLightViewProj，已纹素对齐）
uniform mat4 uModel;

void main() {
    gl_Position = uLightViewProj * uModel * vec4(aPosition, 1.0);
}
