#version 450 core
// ui.frag —— 运行时 UI 片元（V0.3-3）
// 单采样器双用途：
//   字体图集（R8 + 全通道 swizzle→RED，FontAtlas.cpp 创建时设置）：
//     texel = (r,r,r,r)，texel.a = 字形覆盖率（r → alpha）
//   精灵贴图（普通 RGBA8）：texel.a = 贴图 alpha
//   白块路径与字形同图集同采样器（纯色 Image / Button 与文本同批同 draw call）。
// 输出 = 顶点色 × alpha，配合 UIRenderer 的 SRC_ALPHA / ONE_MINUS_SRC_ALPHA 混合。

in vec2 vUV;
in vec4 vColor;

uniform sampler2D uTex;

out vec4 fragColor;

void main() {
    float a = texture(uTex, vUV).a * vColor.a;
    fragColor = vec4(vColor.rgb, a);
}
