// FontAtlas.cpp —— stb_truetype 字形图集（V0.3-3）
//
// 全项目唯一定义点：STB_TRUETYPE_IMPLEMENTATION 只在本文件定义（grep 校验过，
// 其余 .cpp 一律 #include <stb_truetype.h> 头文件路径，不得再定义实现宏）。
//
// 设计要点：
//  1) 默认字体 C:/Windows/Fonts/msyh.ttc（微软雅黑 TTC 集合，取第 0 面），
//     AssetStream::readAllBytes 读入内存后 stbtt_InitFont；失败降级内置 5x7 ASCII
//     位图字体（程序化 8 倍放大烘焙进同一图集），Logger::warn 提示。
//  2) 按需烘焙：glyph(codepoint) 首次请求才光栅化；全部字形按固定参考像素高
//     64px（kRefPx）烘焙，UIRenderer 按 fontSize/64 缩放排版——冻结头 glyph() 签名
//     无字号参数，参考高固定是唯一可行方案（64px 对 CJK 细节与 2 倍放大都够用）。
//  3) 图集 512² 起步，放满则 1024² 整体重建（旧 Glyph* 全部失效——规避方式：
//     调用方每帧、每字形重新调 glyph()；UIRenderer 额外做“预烘焙 pass”保证同帧
//     字形 UV 一致，见 UIRenderer.cpp 顶部注释）。1024² 再满：新字形返回 nullptr，
//     调用方跳过渲染该字符，仅 warn 一次。
//  4) 纹理为单通道 R8 + 全通道 swizzle→RED：采样 texel=(r,r,r,r)，shader 里
//     texel.a 即字形覆盖率（ui.frag 注释见）；精灵贴图（普通 RGBA8）同采样器，
//     texel.a 为贴图 alpha——两种纹理共用一份 shader。
//  5) 内部保留码点（不进字体、不参与文本）：
//     0xFFFFFF 白块：1x1 白像素，纯色 Image / Button 与字形同图集 → 同批、同 draw call
//     0xFFFFFE 行高探针：零尺寸字形，y1=ascend、y0=descend（参考像素），
//                     UIRenderer 用它取基线与行高（冻结头无 metrics 接口，走字形通道）

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <NovaEngine/UI/FontAtlas.h>

#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Core/Logger.h>

#include <glad/gl.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace NovaEngine {

namespace {

constexpr uint32_t kWhiteBlockCp = 0xFFFFFF;    // 内部保留：1x1 白块（纯色 Image/Button 共用）
constexpr uint32_t kMetricsProbeCp = 0xFFFFFE;  // 内部保留：行高探针（零尺寸，只带 metrics）
constexpr float kRefPx = 64.0f;                 // 全部字形按 64px 参考高烘焙
constexpr int kPad = 2;                         // 字形四周留 2px 空（线性过滤防邻字渗色）
constexpr int kAtlasMax = 1024;                 // 图集上限（满则新字形缺失）

// 内置降级字体：5x7 ASCII（32..126），每字节一列，bit r = 第 r 行（bit0 = 顶行）
constexpr int kFontW = 5, kFontH = 7, kFontScale = 8;  // 放大 8 倍 → 40x56 参考字形
constexpr uint8_t kFont5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00},  // ' '
    {0x00,0x00,0x5F,0x00,0x00},  // '!'
    {0x00,0x03,0x00,0x03,0x00},  // '"'
    {0x14,0x1F,0x1F,0x1F,0x14},  // '#'
    {0x24,0x2A,0x7F,0x2A,0x24},  // '$'
    {0x63,0x13,0x08,0x64,0x63},  // '%'
    {0x36,0x49,0x55,0x32,0x10},  // '&'
    {0x00,0x00,0x03,0x00,0x00},  // '\''
    {0x0C,0x12,0x20,0x00,0x00},  // '('
    {0x00,0x00,0x20,0x12,0x0C},  // ')'
    {0x00,0x05,0x02,0x05,0x00},  // '*'
    {0x00,0x08,0x1C,0x08,0x00},  // '+'
    {0x00,0x00,0x20,0x10,0x00},  // ','
    {0x00,0x08,0x08,0x08,0x00},  // '-'
    {0x00,0x00,0x20,0x00,0x00},  // '.'
    {0x20,0x10,0x08,0x04,0x02},  // '/'
    {0x1E,0x31,0x25,0x25,0x1E},  // '0'
    {0x00,0x22,0x3F,0x20,0x00},  // '1'
    {0x22,0x21,0x31,0x29,0x25},  // '2'
    {0x22,0x49,0x49,0x49,0x32},  // '3'
    {0x18,0x14,0x12,0x3F,0x10},  // '4'
    {0x17,0x25,0x25,0x25,0x19},  // '5'
    {0x1E,0x29,0x29,0x29,0x12},  // '6'
    {0x21,0x11,0x09,0x05,0x03},  // '7'
    {0x36,0x49,0x49,0x49,0x36},  // '8'
    {0x26,0x49,0x49,0x49,0x1E},  // '9'
    {0x00,0x00,0x24,0x00,0x00},  // ':'
    {0x00,0x00,0x24,0x10,0x00},  // ';'
    {0x08,0x14,0x22,0x41,0x00},  // '<'
    {0x00,0x14,0x14,0x14,0x00},  // '='
    {0x00,0x41,0x22,0x14,0x08},  // '>'
    {0x02,0x01,0x51,0x09,0x06},  // '?'
    {0x3E,0x41,0x7D,0x5D,0x1E},  // '@'
    {0x3E,0x09,0x09,0x09,0x3E},  // 'A'
    {0x3F,0x29,0x29,0x29,0x16},  // 'B'
    {0x3E,0x41,0x41,0x41,0x22},  // 'C'
    {0x3F,0x21,0x21,0x21,0x1E},  // 'D'
    {0x3F,0x29,0x29,0x29,0x21},  // 'E'
    {0x3F,0x09,0x09,0x09,0x01},  // 'F'
    {0x1E,0x21,0x29,0x29,0x1A},  // 'G'
    {0x3F,0x08,0x08,0x08,0x3F},  // 'H'
    {0x00,0x21,0x3F,0x21,0x00},  // 'I'
    {0x10,0x20,0x21,0x1F,0x01},  // 'J'
    {0x3F,0x04,0x08,0x12,0x21},  // 'K'
    {0x3F,0x20,0x20,0x20,0x20},  // 'L'
    {0x3F,0x02,0x06,0x02,0x3F},  // 'M'
    {0x3F,0x02,0x04,0x08,0x3F},  // 'N'
    {0x1E,0x21,0x21,0x21,0x1E},  // 'O'
    {0x3F,0x09,0x09,0x0F,0x06},  // 'P'
    {0x1E,0x21,0x31,0x01,0x3E},  // 'Q'
    {0x3F,0x09,0x09,0x1F,0x26},  // 'R'
    {0x26,0x49,0x49,0x49,0x32},  // 'S'
    {0x01,0x01,0x7F,0x01,0x01},  // 'T'
    {0x1F,0x20,0x20,0x20,0x1F},  // 'U'
    {0x0F,0x10,0x20,0x10,0x0F},  // 'V'
    {0x1F,0x20,0x18,0x20,0x1F},  // 'W'
    {0x23,0x14,0x08,0x14,0x23},  // 'X'
    {0x03,0x04,0x38,0x04,0x03},  // 'Y'
    {0x21,0x31,0x29,0x25,0x23},  // 'Z'
    {0x00,0x7F,0x21,0x21,0x21},  // '['
    {0x02,0x04,0x08,0x10,0x20},  // '\'
    {0x00,0x21,0x21,0x7F,0x21},  // ']'
    {0x04,0x02,0x01,0x02,0x04},  // '^'
    {0x40,0x40,0x40,0x40,0x40},  // '_'
    {0x00,0x01,0x02,0x00,0x00},  // '`'
    {0x20,0x54,0x5C,0x54,0x78},  // 'a'
    {0x3F,0x44,0x44,0x44,0x38},  // 'b'
    {0x38,0x44,0x44,0x44,0x28},  // 'c'
    {0x38,0x44,0x44,0x44,0x3B},  // 'd'
    {0x38,0x44,0x54,0x54,0x18},  // 'e'
    {0x08,0x7E,0x05,0x05,0x02},  // 'f'
    {0x0C,0x52,0x52,0x5E,0x2C},  // 'g'
    {0x7F,0x04,0x04,0x04,0x78},  // 'h'
    {0x00,0x44,0x7D,0x44,0x00},  // 'i'
    {0x00,0x40,0x44,0x3D,0x04},  // 'j'
    {0x7F,0x10,0x24,0x6C,0x44},  // 'k'
    {0x00,0x40,0x7F,0x40,0x00},  // 'l'
    {0x7C,0x04,0x7C,0x04,0x78},  // 'm'
    {0x7C,0x04,0x04,0x04,0x78},  // 'n'
    {0x38,0x44,0x44,0x44,0x38},  // 'o'
    {0x7C,0x24,0x24,0x3C,0x18},  // 'p'
    {0x18,0x24,0x24,0x3C,0x58},  // 'q'
    {0x7C,0x04,0x04,0x04,0x08},  // 'r'
    {0x48,0x54,0x54,0x54,0x24},  // 's'
    {0x08,0x08,0x3E,0x48,0x40},  // 't'
    {0x3C,0x40,0x40,0x40,0x3C},  // 'u'
    {0x1C,0x20,0x40,0x20,0x1C},  // 'v'
    {0x3C,0x40,0x70,0x40,0x3C},  // 'w'
    {0x44,0x28,0x10,0x28,0x44},  // 'x'
    {0x1C,0x20,0x20,0x3C,0x7C},  // 'y'
    {0x44,0x64,0x54,0x4C,0x44},  // 'z'
    {0x04,0x04,0x1B,0x11,0x00},  // '{'
    {0x00,0x00,0x7F,0x00,0x00},  // '|'
    {0x00,0x11,0x1B,0x04,0x04},  // '}'
    {0x08,0x04,0x08,0x08,0x04},  // '~'
};

// 图集内部实现（函数内静态防初始化顺序问题；FontAtlas 冻结头无成员，全部状态收敛于此）
struct AtlasImpl {
    std::vector<uint8_t> pixels;  // R8，size²（顶行在前，与屏幕 Y 向下约定一致）
    int size = 512;
    int cursorX = kPad, cursorY = kPad, rowHeight = 0;
    bool loaded = false;
    bool builtin = false;         // 降级内置 5x7 字体模式
    bool warnedFull = false;
    uint32_t texId = 0;
    stbtt_fontinfo font{};                       // 仅 !builtin 有效
    std::vector<uint8_t> fontData;               // 字体文件字节（stbtt 内部指针需存活）
    std::unordered_map<uint32_t, Glyph> glyphs;  // 码点 → 字形（值语义稳定；重建时整表替换）

    void ensureLoaded() {
        if (loaded) return;
        loaded = true;
        // DevOps 修复：pixels 此前未分配（size=512 但 vector 为空），首次白块烘焙
        // pixels[...] 越界写 + glTexImage2D 读空缓冲 → 播放器启动挂死。此处按初始 512² 分配。
        pixels.assign(static_cast<size_t>(size) * size, 0);
        std::vector<uint8_t> data;
        if (AssetStream::readAllBytes("C:/Windows/Fonts/msyh.ttc", data) && !data.empty()) {
            // TTC 集合：取第 0 面（微软雅黑）
            const int off = stbtt_GetFontOffsetForIndex(data.data(), 0);
            if (off >= 0 && stbtt_InitFont(&font, data.data(), off)) {
                fontData = std::move(data);
                return;
            }
            Logger::warn("[FontAtlas] msyh.ttc 解析失败，降级内置 ASCII 位图字体");
        } else {
            Logger::warn("[FontAtlas] 无法加载 C:/Windows/Fonts/msyh.ttc，降级内置 ASCII 位图字体");
        }
        builtin = true;
    }

    // 简易行式摆放：行内放不下换行，换行仍放不下返回 false（零尺寸恒成功、不占空间）
    bool place(int w, int h, int& outX, int& outY) {
        if (w <= 0 || h <= 0) { outX = 0; outY = 0; return true; }
        if (cursorX + w + kPad > size) { cursorX = kPad; cursorY += rowHeight + kPad; rowHeight = 0; }
        if (cursorY + h + kPad > size) return false;
        outX = cursorX; outY = cursorY;
        cursorX += w + kPad;
        rowHeight = std::max(rowHeight, h);
        return true;
    }

    // GL 纹理：R8 + 全通道 swizzle→RED（采样 texel=(r,r,r,r)，texel.a 即覆盖率）。
    // glad 空指针守卫：GL 上下文未就绪时延后创建（UIRenderer::init 在上下文就绪后触发首次烘焙）
    void ensureTexture() {
        if (texId) return;
        if (!glad_glGenTextures) return;
        glGenTextures(1, &texId);
        glBindTexture(GL_TEXTURE_2D, texId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, size, size, 0, GL_RED, GL_UNSIGNED_BYTE,
                     pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void uploadRegion(int x, int y, int w, int h, const uint8_t* data) {
        ensureTexture();
        if (!texId || w <= 0 || h <= 0) return;
        glBindTexture(GL_TEXTURE_2D, texId);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RED, GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void recreateTexture() {
        ensureTexture();
        if (!texId) return;
        glBindTexture(GL_TEXTURE_2D, texId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, size, size, 0, GL_RED, GL_UNSIGNED_BYTE,
                     pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // 512→1024 整体重建：清空重摆全部已缓存字形。旧 Glyph* 全部失效——
    // 契约：调用方每帧重取 glyph()（UIRenderer 预烘焙 pass 保证同帧一致，见其顶部注释）
    bool growTo1024() {
        if (size >= kAtlasMax) {
            if (!warnedFull) {
                Logger::warn("[FontAtlas] 图集已满（{}x{}），后续新字形将缺失渲染", kAtlasMax,
                             kAtlasMax);
                warnedFull = true;
            }
            return false;
        }
        Logger::warn("[FontAtlas] 字形图集 512→1024 扩容重建：旧 Glyph* 全部失效（调用方须每帧重取 glyph()）");
        size = kAtlasMax;
        pixels.assign(static_cast<size_t>(size) * size, 0);
        cursorX = cursorY = kPad;
        rowHeight = 0;
        const auto old = std::move(glyphs);
        glyphs.clear();
        for (const auto& [cp, g] : old) {
            (void)g;
            bake(cp);  // 重新光栅化 + 摆放（失败仅丢失该字形，见 bake）
        }
        recreateTexture();
        return true;
    }

    Glyph& insert(uint32_t cp, Glyph g) {
        return glyphs.emplace(cp, g).first->second;
    }

    // 白块：1x1 白像素（纯色 Image / Button 与字形同图集 → 同批同 draw call）
    const Glyph* bakeWhiteBlock() {
        int px = 0, py = 0;
        if (!place(1, 1, px, py)) {
            if (!growTo1024()) return nullptr;
            if (!place(1, 1, px, py)) return nullptr;
        }
        pixels[static_cast<size_t>(py) * size + px] = 255;
        uploadRegion(px, py, 1, 1, pixels.data() + static_cast<size_t>(py) * size + px);
        Glyph g{};
        g.u0 = static_cast<float>(px) / size;
        g.v0 = static_cast<float>(py) / size;
        g.u1 = static_cast<float>(px + 1) / size;
        g.v1 = static_cast<float>(py + 1) / size;
        g.w = 1; g.h = 1;
        g.x1 = 1; g.y1 = 1;
        g.advance = 1;
        return &insert(kWhiteBlockCp, g);
    }

    // 行高探针：零尺寸字形，仅带 metrics（y1=ascend、y0=descend，参考像素）——
    // 冻结头无 ascent/descent 接口，UIRenderer 经 glyph(kMetricsProbeCp) 取基线与行高
    const Glyph* bakeMetricsProbe() {
        Glyph g{};
        if (builtin) {
            g.y0 = -1.0f * kFontScale;                      // 基线下 1 行
            g.y1 = static_cast<float>(kFontH) * kFontScale; // 基线上 7 行
        } else {
            int ascent = 0, descent = 0, lineGap = 0;
            stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
            const float scale = stbtt_ScaleForPixelHeight(&font, kRefPx);
            g.y0 = descent * scale;
            g.y1 = ascent * scale;
        }
        if (const Glyph* wb = bakeWhiteBlock()) {  // UV 借用白块（w==0 永不绘制）
            g.u0 = wb->u0; g.v0 = wb->v0; g.u1 = wb->u1; g.v1 = wb->v1;
        }
        return &insert(kMetricsProbeCp, g);
    }

    // 内置 5x7 字体：每源像素放大 8 倍 → 40x56 参考字形；无位图（空格）仅 advance
    const Glyph* bakeBuiltin(uint32_t cp) {
        if (cp < 32 || cp > 126) {
            if (cp == U'?') return nullptr;
            return bake(U'?');  // 缺字回退 '?'
        }
        const uint8_t* bits = kFont5x7[cp - 32];
        int count = 0;
        for (int c = 0; c < kFontW; ++c)
            for (int r = 0; r < kFontH; ++r)
                if (bits[c] & (1 << r)) ++count;

        Glyph g{};
        g.advance = static_cast<float>(6 * kFontScale);  // 5 列 + 1 列间距，等宽
        g.y0 = -1.0f * kFontScale;
        g.y1 = static_cast<float>(kFontH) * kFontScale;
        g.x1 = static_cast<float>(kFontW) * kFontScale;
        if (!count) {  // 空格：无位图
            if (const Glyph* wb = bakeWhiteBlock()) {
                g.u0 = wb->u0; g.v0 = wb->v0; g.u1 = wb->u1; g.v1 = wb->v1;
            }
            return &insert(cp, g);
        }

        const int w = kFontW * kFontScale, h = kFontH * kFontScale;
        int px = 0, py = 0;
        if (!place(w, h, px, py)) {
            if (!growTo1024()) return nullptr;
            return bake(cp);  // 重建后重试（递归深度 1）
        }
        // 先写图集缓存，再拷连续临时块上传（glTexSubImage2D 要求连续行）
        for (int c = 0; c < kFontW; ++c)
            for (int r = 0; r < kFontH; ++r)
                if (bits[c] & (1 << r))
                    for (int dy = 0; dy < kFontScale; ++dy)
                        for (int dx = 0; dx < kFontScale; ++dx)
                            pixels[static_cast<size_t>(py + r * kFontScale + dy) * size +
                                   (px + c * kFontScale + dx)] = 255;
        std::vector<uint8_t> tmp(static_cast<size_t>(w) * h);
        for (int row = 0; row < h; ++row)
            for (int col = 0; col < w; ++col)
                tmp[static_cast<size_t>(row) * w + col] =
                    pixels[static_cast<size_t>(py + row) * size + (px + col)];
        uploadRegion(px, py, w, h, tmp.data());

        g.w = static_cast<float>(w);
        g.h = static_cast<float>(h);
        g.u0 = static_cast<float>(px) / size;
        g.v0 = static_cast<float>(py) / size;
        g.u1 = static_cast<float>(px + w) / size;
        g.v1 = static_cast<float>(py + h) / size;
        return &insert(cp, g);
    }

    const Glyph* bakeStb(uint32_t cp) {
        const int gi = stbtt_FindGlyphIndex(&font, static_cast<int>(cp));
        if (gi == 0) {
            if (cp == U'?') return nullptr;
            return bake(U'?');  // 缺字回退 '?'
        }
        const float scale = stbtt_ScaleForPixelHeight(&font, kRefPx);

        // 2026-11-16 修复：GetCodepoint* 系列参数是「码点」（内部自会 FindGlyphIndex），
        // 原实现误传字形索引 gi → 双重映射 → 烘焙出码点值恰好等于字形索引的错误字形
        //（数字不是数字、字母不是字母、汉字不是汉字）。gi 仅用于缺字判断。
        const int codepoint = static_cast<int>(cp);
        int w = 0, h = 0, xoff = 0, yoff = 0;
        unsigned char* bmp = stbtt_GetCodepointBitmap(&font, scale, scale, codepoint, &w, &h, &xoff, &yoff);
        if (!bmp) return nullptr;

        int advW = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&font, codepoint, &advW, &lsb);
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBox(&font, codepoint, &x0, &y0, &x1, &y1);

        Glyph g{};
        // 布局 metrics 用包围盒（y 向上、相对基线）；位图与包围盒差亚像素级，忽略
        g.x0 = x0 * scale; g.y0 = y0 * scale;
        g.x1 = x1 * scale; g.y1 = y1 * scale;
        g.advance = advW * scale;  // v1 不做字距（CJK 为主，注释见 UIRenderer）

        if (w > 0 && h > 0) {
            int px = 0, py = 0;
            if (!place(w, h, px, py)) {
                stbtt_FreeBitmap(bmp, nullptr);
                if (!growTo1024()) return nullptr;
                return bake(cp);  // 重建后重试（递归深度 1）
            }
            // 位图行序顶行在前，与图集/屏幕 Y 向下一致
            for (int row = 0; row < h; ++row)
                for (int col = 0; col < w; ++col)
                    pixels[static_cast<size_t>(py + row) * size + (px + col)] =
                        bmp[static_cast<size_t>(row) * w + col];
            uploadRegion(px, py, w, h, bmp);
            stbtt_FreeBitmap(bmp, nullptr);
            g.w = static_cast<float>(w);
            g.h = static_cast<float>(h);
            g.u0 = static_cast<float>(px) / size;
            g.v0 = static_cast<float>(py) / size;
            g.u1 = static_cast<float>(px + w) / size;
            g.v1 = static_cast<float>(py + h) / size;
        } else {
            stbtt_FreeBitmap(bmp, nullptr);
            if (const Glyph* wb = bakeWhiteBlock()) {  // 零尺寸（空格等）：UV 借用白块
                g.u0 = wb->u0; g.v0 = wb->v0; g.u1 = wb->u1; g.v1 = wb->v1;
            }
        }
        return &insert(cp, g);
    }

    const Glyph* bake(uint32_t cp) {
        ensureLoaded();
        if (const auto it = glyphs.find(cp); it != glyphs.end()) return &it->second;
        if (cp == kWhiteBlockCp) return bakeWhiteBlock();
        if (cp == kMetricsProbeCp) return bakeMetricsProbe();
        if (builtin) return bakeBuiltin(cp);
        return bakeStb(cp);
    }
};

AtlasImpl& impl() {
    static AtlasImpl s;
    return s;
}

}  // namespace

FontAtlas& FontAtlas::defaultAtlas() {
    // 触发字体加载与首次烘焙（白块/探针由 UIRenderer::init 显式请求）；
    // FontAtlas 冻结头为静态 API 壳，实例仅作访问载体
    impl().ensureLoaded();
    static FontAtlas s_instance;
    return s_instance;
}

const Glyph* FontAtlas::glyph(uint32_t codepoint) {
    return impl().bake(codepoint);
}

uint32_t FontAtlas::textureId() {
    return impl().texId;
}

}  // namespace NovaEngine
