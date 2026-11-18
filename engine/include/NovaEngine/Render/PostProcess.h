#pragma once
#include <cstdint>

namespace NovaEngine {

// 后处理设置（V0.3-14，波 3）：存于 Scene::postProcess，序列化 environment.postProcess 段
struct PostProcessSettings {
    bool enabled = false;
    float bloomThreshold = 1.0f;   // 亮度提取阈值（线性空间）
    float bloomIntensity = 0.5f;
    int bloomLevels = 3;           // 降采样级数（1/2 → 1/4 → 1/8）
    float exposure = 1.0f;
    bool ssaoEnabled = false;      // 追加（V0.5-5）：环境段缺省 false（旧文件零变化）
    float ssaoRadius = 0.5f;       // 采样半径（米，view-space）
    float ssaoStrength = 1.0f;     // 遮蔽强度
    int ssaoSampleCount = 16;      // 半球采样数（8/16/32）
};

// 后处理链（V0.3-14，波 3）：Bloom（brightpass → 降采样高斯 → 升采样合成）+ Narkowicz ACES + gamma
// 公共头无 GL 依赖；全部实现进 .cpp
class PostProcess {
public:
    static void init();              // 后处理 shader + 全屏 VAO；失败 → 强制 disabled 降级（Logger::warn）
    static void shutdown();
    static void resize(int w, int h);                     // 重建 ping-pong 半分辨率纹理
    static bool hdrSupported();                           // RGB16F 探测（一次性缓存；失败 → 调用方回退 RGBA8）
    // 输入 HDR resolve 纹理 → Bloom → ACES+gamma → 输出到当前绑定 FBO（视口已由调用方设置）
    static void apply(uint32_t hdrColorTexture, const PostProcessSettings& s);
    // 追加（V0.5-5 SSAO）：combine 合成用 AO 纹理（0 = 无 AO，combine 跳过合成乘 1）
    static void setAmbientOcclusionTexture(uint32_t tex);
};

}  // namespace NovaEngine
