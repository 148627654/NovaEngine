#pragma once
#include <cstdint>
#include <NovaEngine/Render/PostProcess.h>   // PostProcessSettings（采样参数来源，§25.3）

namespace NovaEngine {

// 屏幕空间 AO（V0.5-5）：输入 = 场景法线纹理（RGBA16F）+ 线性深度（RGBA16F，view-space -z）；
// 半球采样 → 半分辨率 AO → 复用 post.vert+post_blur.frag 高斯模糊（H/V 各一次）
// → 供 PostProcess combine 合成。参数取自 PostProcessSettings（环境段，见下）。
class SSAO {
public:
    static void init(); static void shutdown();
    static void resize(int w, int h);    // 半分辨率 AO 纹理重建
    static uint32_t render(uint32_t normalTex, uint32_t linearDepthTex, const PostProcessSettings& s);
};

}  // namespace NovaEngine
