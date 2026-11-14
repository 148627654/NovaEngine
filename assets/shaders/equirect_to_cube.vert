#version 450 core

// 等距柱状 → cubemap 转换（Cubemap::createFromEquirect 内部使用）：
// 相机在原点渲染单位立方体，片元以局部坐标（= 方向向量）采样 equirect 贴图。

layout(location = 0) in vec3 aPosition;

uniform mat4 uView;
uniform mat4 uProj;

out vec3 vLocalPos;

void main() {
    vLocalPos = aPosition;
    gl_Position = uProj * uView * vec4(aPosition, 1.0);
}
