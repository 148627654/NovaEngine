#version 450 core
// sprite.vert —— 2D 精灵顶点（V0.5-10，c0 骨架日最小可用版；c8 后续可改进本文件）
// 对象空间 quad（2D 平面 = 世界 XY，尺寸 = 贴图像素 / pixelsPerUnit，SceneRenderer 侧生成顶点）
// + 世界变换（uModel）；复用 ui.frag（vColor × 贴图 alpha，SRC_ALPHA 混合）。
// CameraBlock UBO（binding 0）与 Renderer::beginFrame 每帧填充的相机块同布局。

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
