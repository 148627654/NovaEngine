#version 450 core

// 选中描边：顶点沿法线膨胀约模型尺寸 4%（对象空间，随模型缩放）
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;      // 未使用（与 Mesh 的 VAO 布局对齐）
layout(location = 3) in vec3 aTangent; // 未使用

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform float uInflate;   // 膨胀量（Renderer 按 AABB 最大边长 * 4% 计算）

void main() {
    vec3 pos = aPosition + aNormal * uInflate;
    gl_Position = uViewProj * uModel * vec4(pos, 1.0);
}
