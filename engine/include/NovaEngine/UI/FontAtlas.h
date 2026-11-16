#pragma once
#include <cstdint>
#include <string>

namespace NovaEngine {

// 字形图集条目（像素坐标，纹理坐标由 UIRenderer 换算）
struct Glyph {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;   // 图集 UV
    float w = 0, h = 0;                     // 位图尺寸
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // 相对基线的放置框
    float advance = 0;
};

// stb_truetype 字体图集（V0.3-3）：按需烘焙（新字形追加，满则扩容重建）；
// 默认系统字体 msyh.ttc，失败降级内置 ASCII；含 1x1 白块（纯色 Image 共用同批）
class FontAtlas {
public:
    // 字形参考像素高（唯一出处）：UIRenderer 排版缩放 = fontSize/kRefPx，
    // 烘焙与排版两侧必须一致（2026-11-16 曾因 FontAtlas/UIRenderer 各自
    // 维护一份 64/128 不一致导致全部文字 2 倍大，收敛为单一常量）
    static constexpr float kRefPx = 128.0f;

    static FontAtlas& defaultAtlas();
    static const Glyph* glyph(uint32_t codepoint);   // 未找到返回 nullptr
    static uint32_t textureId();
    // 2026-11-16 新增（v1.1 每文本独立字体，增量接口，既有签名不变）：
    // fontPath 空 = 默认链字体；非空 = 指定字体文件（如 C:/Windows/Fonts/times.ttf），
    // 每字体独立图集/纹理。指定字体加载失败自动回退默认图集（中文将显示为 ?）。
    static const Glyph* glyph(uint32_t codepoint, const std::string& fontPath);
    static uint32_t textureId(const std::string& fontPath);
    // 2026-11-16 新增：烘焙批次结束后重建 mipmap 链（采样走 GL_LINEAR_MIPMAP_LINEAR，
    // 小字号降采样不再发虚）。UIRenderer 每 Canvas 预烘焙 pass 后调用；
    // 冻结头既有接口（glyph/textureId）签名不变，纯增量。
    static void refreshMipmaps();
};

}  // namespace NovaEngine
