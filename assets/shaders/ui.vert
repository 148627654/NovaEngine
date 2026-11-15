#version 450 core
// ui.vert —— 运行时 UI 顶点（V0.3-3）
// 输入：屏幕像素坐标（左上原点、Y 向下——与 Input::mousePosition 窗口坐标约定一致）
// 正交投影在 shader 内完成：pos / uScreenSize → NDC（Y 翻转），无需 CPU 矩阵。

layout(location = 0) in vec2 aPos;    // 屏幕像素
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

uniform vec2 uScreenSize;             // 目标分辨率（像素；调用方当前 FBO 的布局尺寸）

out vec2 vUV;
out vec4 vColor;

void main() {
    vUV = aUV;
    vColor = aColor;
    vec2 ndc = vec2(aPos.x / uScreenSize.x * 2.0 - 1.0,
                    1.0 - aPos.y / uScreenSize.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
