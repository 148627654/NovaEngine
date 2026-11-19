#version 450 core
// sprite.vert —— 2D 精灵顶点（V0.5-10，c8 定稿）
// 对象空间 quad（2D 平面 = 世界 XY，尺寸 = 贴图像素 / pixelsPerUnit——顶点由 SceneRenderer
// 按像素尺寸生成）+ 世界变换（uModel）；复用 ui.frag（vColor × 贴图 alpha，SRC_ALPHA 混合）。
// flipX/flipY 在 CPU 侧翻转 UV（SceneRenderer 生成顶点时交换 u/v 端点），shader 无需感知。
// CameraBlock UBO（binding 0：uViewProj + uCamPos，Renderer::beginFrame 每帧填充）——
// 与 blinn_phong.vert / particle.vert 同布局；正交或透视投影矩阵对 shader 透明。
// 顶点布局（与 SceneRenderer 冻结约定一致，勿改）：
//   location 0 vec3 aPos（对象空间，z=0）
//   location 1 vec2 aUV
//   location 2 vec4 aColor（SpriteRenderer.color，逐顶点复制）

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

layout(std140, binding = 0) uniform CameraBlock {
    mat4 uViewProj;
    vec4 uCamPos;
};

uniform mat4 uModel;

out vec2 vUV;
out vec4 vColor;

void main() {
    vUV = aUV;
    vColor = aColor;
    gl_Position = uViewProj * uModel * vec4(aPos, 1.0);
}
