// SSAO.cpp —— 骨架日桩（V0.5-5）：接口冻结于 §25.3；采样/模糊实现归 c3（本文件为 c3 独占）。
// render 恒返回 0 → SceneRenderer 走 setAmbientOcclusionTexture(0)（combine 乘 1，AO 无贡献）。
#include <NovaEngine/Render/SSAO.h>

namespace NovaEngine {

void SSAO::init() {}
void SSAO::shutdown() {}
void SSAO::resize(int w, int h) { (void)w; (void)h; }

uint32_t SSAO::render(uint32_t normalTex, uint32_t linearDepthTex, const PostProcessSettings& s) {
    (void)normalTex; (void)linearDepthTex; (void)s;
    return 0;   // 骨架日空实现：无 AO 纹理（调用方置 0 = combine 乘 1）
}

}  // namespace NovaEngine
