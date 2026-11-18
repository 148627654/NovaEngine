#version 450 core
// particle.vert —— 粒子 billboard 顶点着色器（V0.5-6，§25.4）：
// 用相机右/上向量把单位 quad（三角形带 4 顶点）展开成面向相机的 billboard。
// 实例属性：世界位置 / 已插值颜色 / 世界尺寸 / 纹理条索引（divisor 1，CPU 侧合并上传）。
// CameraBlock UBO（binding 0：uViewProj + uCamPos，Renderer::beginFrame 每帧填充）复用。

layout(location = 0) in vec2 aCorner;          // quad 角（-1..1；三角形带 0..3）
layout(location = 1) in vec3 aInstancePos;     // 实例属性：世界位置
layout(location = 2) in vec4 aInstanceColor;   // 实例属性：寿命插值后的颜色
layout(location = 3) in float aInstanceSize;   // 实例属性：世界单位尺寸
layout(location = 4) in float aInstanceTile;   // 实例属性：纹理条索引（0..tiles-1）

layout(std140, binding = 0) uniform CameraBlock {
    mat4 uViewProj;
    vec4 uCamPos;
};

uniform vec3 uCamRight;   // 相机右向量（view 矩阵第 1 列；CPU 侧每帧设置）
uniform vec3 uCamUp;      // 相机上向量（view 矩阵第 2 列）

out vec2 vUV;             // quad 内 UV（0..1；y=1 在 quad 顶）
out vec4 vColor;
flat out float vTile;     // flat：同实例四顶点同值

void main() {
    // 尺寸语义 = quad 总宽：corner ∈ [-1,1]，乘 0.5 使宽度恰为 aInstanceSize
    vec3 worldPos = aInstancePos
                  + uCamRight * (aCorner.x * aInstanceSize * 0.5)
                  + uCamUp    * (aCorner.y * aInstanceSize * 0.5);
    vUV = aCorner * 0.5 + 0.5;
    vColor = aInstanceColor;
    vTile = aInstanceTile;
    gl_Position = uViewProj * vec4(worldPos, 1.0);
}
