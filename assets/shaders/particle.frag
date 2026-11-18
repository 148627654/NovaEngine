#version 450 core
// particle.frag —— 粒子片元着色器（V0.5-6，§25.4）：纹理条（flipbook）采样 × 插值颜色。
// 混合模式在 CPU 侧设置（blendAdditive → GL_SRC_ALPHA, GL_ONE；否则标准 alpha 混合）。
// 行序约定：条第 0 行 = 图片最上一行（stb 加载后 v=0 为图片顶部；quad 顶 vUV.y=1
// 采样图片顶部——单图 tileX=tileY=1 时图片正立显示）。

in vec2 vUV;
in vec4 vColor;
flat in float vTile;

uniform sampler2D uTex;
uniform int uTileX;   // 纹理条列数（>=1）
uniform int uTileY;   // 纹理条行数（>=1）

out vec4 oColor;

void main() {
    int cols = max(uTileX, 1);
    int rows = max(uTileY, 1);
    int t = clamp(int(floor(vTile)), 0, cols * rows - 1);
    int col = t % cols;
    int row = t / cols;
    vec2 uv;
    uv.x = (vUV.x + float(col)) / float(cols);
    uv.y = (1.0 - vUV.y + float(row)) / float(rows);
    oColor = texture(uTex, uv) * vColor;
}
