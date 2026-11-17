// UIRenderer.cpp —— 运行时 UI 批渲染与输入（V0.3-3；V0.5-13 扩展 fill/wrap/Slider/ScrollView/命中冒泡）。
// 零 ImGui 依赖。
//
// == 渲染路径 ==
// 3D pass + 后处理之后、调用方「当前绑定的 FBO」内绘制：本文件不绑定/切换 FBO、
// 不改视口（视口由调用方按目标分辨率设好）。GL 状态（程序/VAO/VBO/纹理/深度/混合/
// 剔除/scissor）进出 render 全部保存-恢复，不打扰 3D 与后处理。
//
// == 坐标系（裁定）==
// 正交投影 0..w × 0..h 像素；左上原点、Y 向下——与 Input::mousePosition 窗口坐标
// 约定一致，processInput 收到的鼠标坐标可直接比对。NDC 换算在 ui.vert 内完成。
// 布局：Image/Button/Slider/ScrollView 元素中心 = anchor(0..1) × 屏幕 + offset × uiScale；
//       Text 起点（左上）= anchor × 屏幕 + offset × uiScale；
//       size / fontSize / slice9 / fillThickness / handleSize 均 × uiScale
//       （anchor 不缩放——锚点贴屏幕角）；Canvas.uiScale 为 v1 预留（Inspector 可调，
//       渲染/命中同步生效）。
// ScrollView：可视区 = 其 anchor/offset/size 矩形；子树的 quad 平移 scrollOffset × uiScale
// （逻辑像素偏移，与 offset 同单位）、裁剪 = 可视区 ∩ 祖先 ScrollView 交集（GL scissor）。
//
// == 批处理（裁定）==
// per-Canvas、per-(贴图,裁剪) 批：单 Canvas 内全部字形/白块/按钮共用字体图集 = 1 draw
// call；每张精灵贴图 +1 draw call；ScrollView 子树按裁剪矩形分组（组间顺序按首次出现，
// 与跨贴图排序同为 v1 已知取舍）。Canvas 自身不绘制；嵌套 Canvas 子树交给其自身 pass，
// 避免双重绘制。
//
// == 布局与命中（V0.5-13 核心纪律）==
// 渲染发射、hitTest、contentSize 三处共用同一 walker（walkUIChildren）与同一布局
// helper（centerRect/textBlockRect/translateRect）：命中矩形与显示矩形同一公式，
// 杜绝「点击漂移」。Image fill = 几何+UV 双向裁切（保比例）：Horizontal 裁右、
// Vertical 自底（裁上）、Radial 自顶部顺时针扇形（三角扇近似，Radial90 顶点在
// 右上角；fillClockwise=false 反向）。9-slice 先 fill 后 slice（裁切尺寸 < 边距和
// 时退化单 quad）。Text wrap 按码点折行（CJK 安全、保留 '\n'），hAlign 相对
// wrapWidth（wrap）或最宽行（不 wrap）。
//
// == 字形生命周期 ==
// FontAtlas 图集 512→1024 重建会使旧 Glyph* 失效。规避：本文件每帧、每字形重新取
// glyph()，且每 Canvas 先做一遍「预烘焙 pass」（对全部 Text 逐码点 glyph() 一次，
// 触发可能的重建），再取第二遍构建 quad——保证同帧内 UV 一致、无中途重建。
// 例外：layoutText（processInput 命中路径，先于 render）按「取即用」纪律使用 Glyph
// 指针——探针/字形指针绝不跨 glyph() 调用保留。
//
// == 输入 ==
// processInput 接线（分侧）：Player 主循环应在 scene.update 之前调用（脚本 onUpdate
// 查询当帧状态）；编辑器 SceneView 为渲染期调用（脚本侧按钮状态滞后一帧——已知取舍，
// 由调用点注释文档化）。V0.5-13 冒泡裁定：hitTest 取最上层 UI 元素（DFS 画家序最后
// 命中者，Image/Text 也参与命中）→ 自 winner GO 沿父链就近交付交互组件
// （Button/Slider/ScrollView）→ 未消费继续向上冒泡 → 仅链上组件更新状态、其余复位。
// ScrollView 滚轮在 processInput 内读 Input::scrollDelta()。Edit 态不调 processInput
// → UI 预览不挡场景拾取（Play 中经 lastPointerConsumed/isPointerOverUI「UI 优先」）。
// 输入新鲜标志 s_inputThisFrame：render 消费后复位——Edit 预览（无 processInput）
// 一律基色，杜绝「退出 Play 后 hover 色残留」。
//
// == v1 已知限制（注释存档）==
// - 不做字距（kern）；超宽不裁剪（wrap=false 时）
// - 图集满（1024²）后新字形缺失渲染（FontAtlas 已 warn 一次）
// - 滚动指示条纯视觉不可拖；ScrollView 不做内容自动重排（布局由创作者摆放）
// - Radial fill 与 9-slice 不叠加（扇形本身保持比例，slice 无意义）
// （Text.fontPath 已于 v1.1 生效：每字体独立图集）

#include <NovaEngine/UI/UIRenderer.h>

#include <NovaEngine/UI/FontAtlas.h>
#include <NovaEngine/UI/ScrollView.h>
#include <NovaEngine/UI/Slider.h>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Input.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Render/Shader.h>
#include <NovaEngine/Render/Texture.h>
#include <NovaEngine/Scene/Button.h>
#include <NovaEngine/Scene/Canvas.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Image.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/Text.h>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace NovaEngine {

namespace {

// 内部保留码点（与 FontAtlas.cpp 一一对应，冻结头无常量位——两侧注释互指）
constexpr uint32_t kWhiteBlockCp = 0xFFFFFF;    // 1x1 白块（纯色 Image / Button / Slider 共用）
constexpr uint32_t kMetricsProbeCp = 0xFFFFFE;  // 行高探针：y1=ascend、y0=descend（参考像素）
// 2026-11-16：与 FontAtlas 参考高必须一致——两侧曾各维护一份常量（64/128 不一致
// 导致全部文字渲染成 2 倍大、看起来像乱码）；现统一引用 FontAtlas::kRefPx 唯一出处
constexpr float kRefPx = FontAtlas::kRefPx;    // FontAtlas 全部字形按 128px 参考高烘焙

// 顶点：屏幕像素（左上原点 Y 向下）+ 图集/贴图 UV + 直通色
struct UIVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
};
constexpr size_t kInitialVboCapacity = 4096;  // 顶点数（682 quad）起步，按需 ×2 扩容

// ---- 全局状态（引擎单例生命周期；shutdown 后允许重新 init）----
bool s_disabled = false;      // init 失败降级 no-op：全部入口早退
bool s_initialized = false;
bool s_inputThisFrame = false;   // 本帧 processInput 已刷新按钮状态（render 消费后复位）
glm::vec2 s_screenSize{0.0f};          // 最近一次 render 的布局空间（逻辑像素；= 物理 / dpi）
glm::vec2 s_physicalScreenSize{0.0f};  // 最近一次 render 的物理目标（帧缓冲像素）
float s_dpiScale = 1.0f;               // HiDPI：逻辑→物理像素比（发射期统一乘；默认 1）
bool s_lastPointerConsumed = false;  // 最近一次 processInput 是否有交互组件消费（onClick/拖拽/滚动）

std::shared_ptr<Shader> s_shader;
uint32_t s_vao = 0, s_vbo = 0;
size_t s_vboCapacity = kInitialVboCapacity;
int32_t s_locScreenSize = -1, s_locTex = -1;

// 精灵贴图强引用缓存：AssetDatabase 的纹理缓存是 weak_ptr（调用方丢引用即销毁重传），
// UI 精灵按路径持有，避免每帧重复解码上传
std::unordered_map<std::string, std::shared_ptr<Texture>> s_spriteCache;
std::unordered_set<std::string> s_warnedSpritePaths;  // 加载失败仅 warn 一次

void pushQuad(std::vector<UIVertex>& out, float x0, float y0, float x1, float y1,
              float u0, float v0, float u1, float v1, const glm::vec4& c) {
    // 顶点序 TL/TR/BL, TR/BR/BL（默认逆时针正面；本 pass 剔除关闭，绕向无关）
    // HiDPI：布局坐标为逻辑像素，发射期统一 × dpi → 物理像素（顶点唯一出口）
    const float k = s_dpiScale;
    x0 *= k; y0 *= k; x1 *= k; y1 *= k;
    const UIVertex tl{x0, y0, u0, v0, c.r, c.g, c.b, c.a};
    const UIVertex tr{x1, y0, u1, v0, c.r, c.g, c.b, c.a};
    const UIVertex bl{x0, y1, u0, v1, c.r, c.g, c.b, c.a};
    const UIVertex br{x1, y1, u1, v1, c.r, c.g, c.b, c.a};
    out.insert(out.end(), {tl, tr, bl, tr, br, bl});
}

void pushTri(std::vector<UIVertex>& out, const glm::vec2& p0, const glm::vec2& t0,
             const glm::vec2& p1, const glm::vec2& t1, const glm::vec2& p2, const glm::vec2& t2,
             const glm::vec4& c) {
    // HiDPI：布局坐标（逻辑）→ 物理像素（与 pushQuad 同策）
    const float k = s_dpiScale;
    out.insert(out.end(), {
        UIVertex{p0.x * k, p0.y * k, t0.x, t0.y, c.r, c.g, c.b, c.a},
        UIVertex{p1.x * k, p1.y * k, t1.x, t1.y, c.r, c.g, c.b, c.a},
        UIVertex{p2.x * k, p2.y * k, t2.x, t2.y, c.r, c.g, c.b, c.a}});
}

// 简易 UTF-8 解码：返回码点并前进；非法序列返回 U+FFFD 并前进 1（Text 含中文）
uint32_t utf8Decode(const std::string& s, size_t& i) {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const unsigned char c = p[i];
    if (c < 0x80) { i += 1; return c; }
    uint32_t cp = 0;
    int n = 0;
    if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 3; }
    else { i += 1; return 0xFFFD; }
    for (int k = 1; k <= n; ++k) {
        if (i + k >= s.size() || (p[i + k] & 0xC0) != 0x80) { i += 1; return 0xFFFD; }
        cp = (cp << 6) | (p[i + k] & 0x3F);
    }
    i += static_cast<size_t>(n) + 1;
    return cp;
}

// 最近的 Canvas 祖先（渲染与输入共用同一布局公式，保证命中与显示一致）。
// 含 go 自身：Canvas 根节点自带 ScrollView 时（全屏滚动容器）取自身 Canvas.uiScale——
// 否则其可视区/裁剪按 1.0 缩放而子树按 canvas.uiScale 缩放，uiScale≠1 时 scissor 错位
//（评审 P2 修复；普通节点自身无 Canvas，第一轮遍历即空，行为与旧实现逐位一致）
const Canvas* findCanvasAncestor(const GameObject* go) {
    for (const GameObject* p = go; p; p = p->parent())
        if (const Canvas* c = p->getComponent<Canvas>())
            return c;
    return nullptr;
}

float uiScaleOf(const GameObject* node) {
    const Canvas* c = findCanvasAncestor(node);
    const float base = c ? std::clamp(c->uiScale, 0.01f, 100.0f) : 1.0f;
    // HiDPI（2026-11-17 修复）：uiScale 只含 Canvas 缩放——dpi 全局乘数移至发射期
    //（pushQuad/pushTri 统一乘）。布局空间 = 逻辑像素（s_screenSize = 物理 / dpi），
    // 否则 anchor×物理尺寸 与 offset×dpi 混用，150% 显示器上整个 UI 放大 1.5 倍溢出视口。
    return base;
}

glm::vec2 anchorPoint(const glm::vec2& anchor, const glm::vec2& offset, float uiScale) {
    return anchor * s_screenSize + offset * uiScale;
}

// 收集 Canvas 的 UI 节点树（跳过非激活与嵌套 Canvas 子树——后者由其自身 pass 绘制）
void collectUINodes(const GameObject* node, std::vector<const GameObject*>& out) {
    for (const auto& child : node->children()) {
        if (!child->activeInHierarchy()) continue;
        if (child->getComponent<Canvas>()) continue;
        out.push_back(child.get());
        collectUINodes(child.get(), out);
    }
}

void ensureVboCapacity(size_t verts) {
    if (verts <= s_vboCapacity) return;
    while (s_vboCapacity < verts) s_vboCapacity *= 2;
    glBufferData(GL_ARRAY_BUFFER, s_vboCapacity * sizeof(UIVertex), nullptr, GL_DYNAMIC_DRAW);
}

std::shared_ptr<Texture> spriteTexture(const std::string& path) {
    auto it = s_spriteCache.find(path);
    if (it != s_spriteCache.end()) return it->second;
    // UI 精灵按原样采样（srgb=false）：UI 在 LDR 输出阶段绘制，直接输出作者色
    auto tex = AssetDatabase::instance().getTexture(path, /*srgb=*/false);
    s_spriteCache.emplace(path, tex);
    if (!tex && s_warnedSpritePaths.insert(path).second)
        Logger::warn("[UIRenderer] UI 精灵加载失败（回退纯色）: {}", path);
    return tex;
}

// ============================================================
// 布局公式（渲染发射 / 命中测试 / contentSize 共用唯一出处）
// rect = (x0, y0, x1, y1)，layout 像素（已含 uiScale 与 dpi），左上原点
// ============================================================

// 中心锚矩形（Image/Button/Slider/ScrollView 视口共用；与 §坐标系 裁定一致）
glm::vec4 centerRect(const glm::vec2& anchor, const glm::vec2& offset, const glm::vec2& size,
                     float us) {
    const glm::vec2 c = anchorPoint(anchor, offset, us);
    const float hw = size.x * us * 0.5f, hh = size.y * us * 0.5f;
    return glm::vec4(c.x - hw, c.y - hh, c.x + hw, c.y + hh);
}

glm::vec4 translateRect(const glm::vec4& r, const glm::vec2& t) {
    return glm::vec4(r.x + t.x, r.y + t.y, r.z + t.x, r.w + t.y);
}

bool rectValid(const glm::vec4& r) { return r.z > r.x && r.w > r.y; }

bool pointInRect(const glm::vec2& p, const glm::vec4& r) {
    return p.x >= r.x && p.x <= r.z && p.y >= r.y && p.y <= r.w;
}

glm::vec4 intersectRect(const glm::vec4& a, const glm::vec4& b) {
    return glm::vec4(std::max(a.x, b.x), std::max(a.y, b.y), std::min(a.z, b.z),
                     std::min(a.w, b.w));
}

// ============================================================
// 文本排版（渲染与命中共用）：按码点折行（CJK 安全）、保留 '\n'、hAlign 对齐
// wrap=false 且 hAlign=Left 时，与旧单遍渲染逐位一致（同一累加序）
// ============================================================

struct TextLine {
    size_t startByte, endByte;   // [start, end) 码点字节区间（不含结尾 '\n'）
    float width;                 // 行宽（layout 像素）
};

struct TextLayout {
    std::vector<TextLine> lines;
    float maxLineW = 0.0f;       // 最宽行
    float lineH = 0.0f;          // 行高（layout 像素）
};

// 单码点推进宽度：缺失字形回退 0.25×fontSize（与旧渲染一致）
float glyphAdvanceWidth(uint32_t cp, const std::string& fontPath, float scale, float fontSizeUs) {
    const Glyph* g = FontAtlas::glyph(cp, fontPath);
    return g ? g->advance * scale : fontSizeUs * 0.25f;
}

TextLayout layoutText(const Text& t, float us, float effWrap /*<=0 = 不折行*/) {
    TextLayout L;
    const float scale = (t.fontSize * us) / kRefPx;
    {
        // 行高探针「取即用」：layout 可能触发图集扩容重建（processInput 先于 render 的
        // 首帧烘焙新字形），探针指针不得跨后续 glyph() 调用保留
        const Glyph* probe = FontAtlas::glyph(kMetricsProbeCp, t.fontPath);
        L.lineH = probe ? (probe->y1 - probe->y0) * scale : 0.0f;
    }
    size_t i = 0, lineStart = 0;
    float lineW = 0.0f;
    while (i < t.text.size()) {
        const size_t cpStart = i;
        const uint32_t cp = utf8Decode(t.text, i);
        if (cp == '\n') {   // 显式换行保留（连续 '\n' 产生空行）
            L.lines.push_back({lineStart, cpStart, lineW});
            lineStart = i;
            lineW = 0.0f;
            continue;
        }
        const float adv = glyphAdvanceWidth(cp, t.fontPath, scale, t.fontSize * us);
        if (effWrap > 0.0f && lineW + adv > effWrap && lineW > 0.0f) {
            // 折行：该码点归下一行；行内已有内容才断 → 超宽单字不卡死
            L.lines.push_back({lineStart, cpStart, lineW});
            lineStart = cpStart;
            lineW = adv;
        } else {
            lineW += adv;
        }
    }
    L.lines.push_back({lineStart, i, lineW});
    for (const TextLine& ln : L.lines)
        L.maxLineW = std::max(L.maxLineW, ln.width);
    return L;
}

// 文本块矩形（含平移）：命中与 contentSize 用；空文本/字体不可用 → 无效矩形
glm::vec4 textBlockRect(const Text& t, float us, const glm::vec2& trans) {
    if (t.text.empty() || t.fontSize <= 0.0f) return glm::vec4(0.0f);
    const float effWrap = t.wrap ? (t.wrapWidth > 0.0f ? t.wrapWidth * us : s_screenSize.x)
                                 : 0.0f;
    const glm::vec2 pen0 = anchorPoint(t.anchor, t.offset, us) + trans;
    const TextLayout L = layoutText(t, us, effWrap);
    const float blockW = t.wrap ? effWrap : L.maxLineW;
    const float blockH = static_cast<float>(L.lines.size()) * L.lineH;
    if (blockW <= 0.0f || blockH <= 0.0f) return glm::vec4(0.0f);
    return glm::vec4(pen0.x, pen0.y, pen0.x + blockW, pen0.y + blockH);
}

// ============================================================
// per-(贴图,裁剪) 批容器：插入序即遍历序（同贴图+同裁剪内保持画家顺序）。
// 裁剪索引 -1 = 无裁剪；ScrollView scissor 分组后组间顺序按首次出现
//（与跨贴图排序同为 v1 已知取舍）
// ============================================================

struct BatchKey {
    uint32_t tex;
    int clip;
    bool operator==(const BatchKey&) const = default;
};

struct BatchKeyHash {
    size_t operator()(const BatchKey& k) const {
        return (static_cast<size_t>(k.tex) * 0x9E3779B1u) ^ static_cast<size_t>(k.clip + 1);
    }
};

struct BatchSink {
    std::vector<BatchKey>& order;
    std::unordered_map<BatchKey, size_t, BatchKeyHash>& index;
    std::vector<std::vector<UIVertex>>& buckets;
    std::vector<UIVertex>& operator[](BatchKey k) {
        const auto [it, inserted] = index.try_emplace(k, buckets.size());
        if (inserted) { buckets.emplace_back(); order.push_back(k); }
        return buckets[it->second];
    }
};

// 发射上下文：当前裁剪（ScrollView scissor 索引）
struct EmitCtx {
    BatchSink& sink;
    int clip = -1;
    std::vector<UIVertex>& operator[](uint32_t tex) { return sink[{tex, clip}]; }
};

// 裁剪矩形表（同值复用索引；rects 由同一条祖先链确定性算出，浮点逐位相等安全）
int clipIndexOf(std::vector<glm::vec4>& rects, const glm::vec4& r) {
    for (size_t i = 0; i < rects.size(); ++i)
        if (rects[i] == r) return static_cast<int>(i);
    rects.push_back(r);
    return static_cast<int>(rects.size()) - 1;
}

// ---- 组件 quad 生成（坐标/尺寸已按 uiScale 缩放；rect 已含 ScrollView 平移）----

// 纯色或精灵拉伸矩形（Slider 三部件 / 滚动指示条共用；无 9-slice）
void emitRectQuad(EmitCtx& ctx, const std::string& spritePath, const glm::vec4& color,
                  float x0, float y0, float x1, float y1, const Glyph* white, uint32_t atlasTex) {
    if (x1 <= x0 || y1 <= y0) return;
    uint32_t tex = atlasTex;
    float u0 = white->u0, v0 = white->v0, u1 = white->u1, v1 = white->v1;
    if (!spritePath.empty()) {
        if (auto t = spriteTexture(spritePath)) {
            tex = t->handle();
            u0 = 0.0f; v0 = 0.0f; u1 = 1.0f; v1 = 1.0f;
        }
    }
    if (!tex) return;
    pushQuad(ctx[tex], x0, y0, x1, y1, u0, v0, u1, v1, color);
}

// Radial fill：自顶部顺时针扇形（45° 步进三角扇 = 象限网格近似；Radial90 扇弧顶点
// 过右上角）。归一化坐标同时插值屏幕与 UV → 贴图 1:1 保比例。与 9-slice 不叠加
//（扇形本身保持比例，slice 无意义）。
void emitRadialFill(EmitCtx& ctx, const Image& img, float x0, float y0, float x1, float y1,
                    float u0, float v0, float u1, float v1, uint32_t tex, float amount) {
    // 满量扫掠角：amount ∈ [0,1] 线性映射到该型满扇形（Radial90 = 右上象限 90°、
    // Radial180 = 半圆、Radial360 = 整圆）；45° 步进量化 = 契约「按象限网格近似」。
    // （评审 P1 修复：旧式 floor(amount*360/step)*step 使 Radial360 在 amount<1 时
    // 恒为 0（满量前不可见）、Radial90/180 超界扫过 180°/360°）
    const float maxSweep = img.fillType == ImageFillType::Radial90   ? 90.0f
                         : img.fillType == ImageFillType::Radial180 ? 180.0f
                                                                    : 360.0f;
    const float sweep =
        std::min(std::floor(amount * maxSweep / 45.0f) * 45.0f, maxSweep);
    if (sweep <= 0.0f) return;
    const float dir = img.fillClockwise ? 1.0f : -1.0f;   // false = 自顶部逆时针
    const auto screen = [&](const glm::vec2& p) {
        return glm::vec2(x0 + p.x * (x1 - x0), y0 + p.y * (y1 - y0));
    };
    const auto texuv = [&](const glm::vec2& p) {
        return glm::vec2(u0 + p.x * (u1 - u0), v0 + p.y * (v1 - v0));
    };
    const auto uvAt = [&](float deg) {   // deg=0 顶部、90 右、180 底、270 左（图像空间 y 向下）
        const float a = glm::radians(dir * deg);
        return glm::vec2(0.5f + 0.5f * std::sin(a), 0.5f - 0.5f * std::cos(a));
    };
    if (sweep >= 360.0f) {   // 满圆 = 原矩形单 quad
        pushQuad(ctx[tex], x0, y0, x1, y1, u0, v0, u1, v1, img.color);
        return;
    }
    std::vector<glm::vec2> pts;
    pts.push_back(glm::vec2(0.5f));   // 扇心
    for (float d = 0.0f; d < sweep - 1e-4f; d += 45.0f)
        pts.push_back(uvAt(d));
    pts.push_back(uvAt(sweep));
    for (size_t k = 1; k + 1 < pts.size(); ++k)
        pushTri(ctx[tex], screen(pts[0]), texuv(pts[0]), screen(pts[k]), texuv(pts[k]),
                screen(pts[k + 1]), texuv(pts[k + 1]), img.color);
}

void emitImage(EmitCtx& ctx, const Image& img, float us, const Glyph* white, uint32_t atlasTex,
               const glm::vec4& rect) {
    uint32_t tex = atlasTex;
    int texW = 1, texH = 1;
    bool useSprite = false;
    float u0 = white->u0, v0 = white->v0, u1 = white->u1, v1 = white->v1;  // 默认白块 UV
    if (!img.spritePath.empty()) {
        if (auto t = spriteTexture(img.spritePath)) {
            tex = t->handle();
            texW = std::max(1, t->width());
            texH = std::max(1, t->height());
            useSprite = true;
            u0 = 0.0f; v0 = 0.0f; u1 = 1.0f; v1 = 1.0f;
        }
    }
    if (!tex) return;

    const float amount = std::clamp(img.fillAmount, 0.0f, 1.0f);

    // Radial：扇形路径（见 emitRadialFill）
    if (img.fillType == ImageFillType::Radial90 || img.fillType == ImageFillType::Radial180 ||
        img.fillType == ImageFillType::Radial360) {
        emitRadialFill(ctx, img, rect.x, rect.y, rect.z, rect.w, u0, v0, u1, v1, tex, amount);
        return;
    }

    // Horizontal / Vertical：几何+UV 同步裁切（保比例；amount=1 与旧行为逐位一致）
    float fx0 = rect.x, fy0 = rect.y, fx1 = rect.z, fy1 = rect.w;
    float fu0 = u0, fv0 = v0, fu1 = u1, fv1 = v1;
    if (img.fillType == ImageFillType::Horizontal) {   // 裁右：保留左侧 [0, amount]
        fx1 = rect.x + (rect.z - rect.x) * amount;
        fu1 = u0 + (u1 - u0) * amount;
    } else if (img.fillType == ImageFillType::Vertical) {  // 裁上：自底填充（v 大端 = 图像底部）
        fy0 = rect.w - (rect.w - rect.y) * amount;
        fv0 = v1 - (v1 - v0) * amount;
    }
    if (fx1 <= fx0 || fy1 <= fy0) return;   // amount=0 → 不可见

    // 9-slice（仅精灵贴图有意义；先 fill 后 slice：边距采样裁切后的贴图区域；
    // 裁切尺寸 < 边距和 → 退化单 quad，沿用旧规则）
    const glm::vec4 m = img.slice9 * us;
    const bool slice = useSprite && (m.x > 0.0f || m.y > 0.0f || m.z > 0.0f || m.w > 0.0f) &&
                       ((fx1 - fx0) > m.x + m.z) && ((fy1 - fy0) > m.y + m.w);
    if (!slice) {
        pushQuad(ctx[tex], fx0, fy0, fx1, fy1, fu0, fv0, fu1, fv1, img.color);
        return;
    }
    const float suL = m.x / static_cast<float>(texW);
    const float suR = m.z / static_cast<float>(texW);
    const float svT = m.y / static_cast<float>(texH);
    const float svB = m.w / static_cast<float>(texH);
    const float xs[4] = {fx0, fx0 + m.x, fx1 - m.z, fx1};
    const float ys[4] = {fy0, fy0 + m.y, fy1 - m.w, fy1};
    const float us_[4] = {fu0, fu0 + suL, fu1 - suR, fu1};
    const float vs_[4] = {fv0, fv0 + svT, fv1 - svB, fv1};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            pushQuad(ctx[tex], xs[c], ys[r], xs[c + 1], ys[r + 1], us_[c], vs_[r], us_[c + 1],
                     vs_[r + 1], img.color);
}

void emitText(EmitCtx& ctx, const Text& t, float us, const glm::vec4& rect) {
    // rect = textBlockRect 产物（含内容平移）；行数据重新排版（与命中同一 layoutText）。
    // wrap=false + hAlign=Left 时：行宽对齐量为 0、累加序不变 → 与旧渲染逐位一致。
    const uint32_t atlasTex = FontAtlas::textureId(t.fontPath);
    if (!atlasTex) return;
    const Glyph* probe = FontAtlas::glyph(kMetricsProbeCp, t.fontPath);
    const float scale = (t.fontSize * us) / kRefPx;
    const float effWrap = t.wrap ? (t.wrapWidth > 0.0f ? t.wrapWidth * us : s_screenSize.x) : 0.0f;
    const TextLayout L = layoutText(t, us, effWrap);
    const float baseW = t.wrap ? effWrap : L.maxLineW;   // 对齐基准：wrapWidth 或最宽行
    const glm::vec2 pen0(rect.x, rect.y);
    float baseline = pen0.y + (probe ? probe->y1 * scale : 0.0f);   // 与旧实现同起点
    for (const TextLine& ln : L.lines) {
        float align = 0.0f;
        if (t.hAlign == TextHAlign::Center) align = (baseW - ln.width) * 0.5f;
        else if (t.hAlign == TextHAlign::Right) align = baseW - ln.width;
        float penX = pen0.x + align;
        for (size_t i = ln.startByte; i < ln.endByte;) {
            const uint32_t cp = utf8Decode(t.text, i);
            const Glyph* g = FontAtlas::glyph(cp, t.fontPath);
            if (!g || g->w <= 0.0f || g->h <= 0.0f) {  // 缺失/空白：仅推进（图集满时跳字符）
                penX += g ? g->advance * scale : t.fontSize * us * 0.25f;
                continue;
            }
            const float qx0 = penX + g->x0 * scale;
            const float qy0 = baseline - g->y1 * scale;
            pushQuad(ctx[atlasTex], qx0, qy0, qx0 + g->w * scale, qy0 + g->h * scale, g->u0, g->v0,
                     g->u1, g->v1, t.color);
            penX += g->advance * scale;
        }
        baseline += L.lineH;   // 累加式推进（与旧实现一致，浮点逐位相同）
    }
}

void emitButton(EmitCtx& ctx, const Button& b, const Glyph* white, uint32_t atlasTex,
                bool useInputState, const glm::vec4& rect) {
    glm::vec4 c = b.color;
    if (useInputState) {  // 仅 Play（processInput 已刷新）；Edit 预览一律基色
        if (b.m_pressed) c = b.pressedColor;
        else if (b.m_hovered) c = b.hoverColor;
    }
    // Button 无 spritePath（白块纯色）；slice9 对白块无意义 → 单 quad
    pushQuad(ctx[atlasTex], rect.x, rect.y, rect.z, rect.w, white->u0, white->v0, white->u1,
             white->v1, c);
}

// 手柄归一化位置 t ∈ [0,1]（min>=max 防御 → 0）
float sliderT(const Slider& s) {
    const float lo = std::min(s.minValue, s.maxValue), hi = std::max(s.minValue, s.maxValue);
    const float span = hi - lo;
    if (span <= 0.0f) return 0.0f;
    return std::clamp((s.value - lo) / span, 0.0f, 1.0f);
}

void emitSlider(EmitCtx& ctx, const Slider& s, float us, const Glyph* white, uint32_t atlasTex,
                const glm::vec4& rect) {
    const float x0 = rect.x, x1 = rect.z;
    const float yc = (rect.y + rect.w) * 0.5f;
    const float halfT = std::max(0.0f, s.fillThickness) * us * 0.5f;
    const float hx = x0 + sliderT(s) * (x1 - x0);
    if (halfT > 0.0f) {   // 轨道背景（厚度条垂直居中）
        emitRectQuad(ctx, s.backgroundSpritePath, s.backgroundColor, x0, yc - halfT, x1,
                     yc + halfT, white, atlasTex);
        if (hx > x0)   // 填充（左缘 → 手柄）
            emitRectQuad(ctx, s.fillSpritePath, s.fillColor, x0, yc - halfT, hx, yc + halfT, white,
                         atlasTex);
    }
    const float hw2 = std::max(0.0f, s.handleSize.x) * us * 0.5f;
    const float hh2 = std::max(0.0f, s.handleSize.y) * us * 0.5f;
    if (hw2 > 0.0f && hh2 > 0.0f)   // 手柄（可超出轨道厚度，垂直居中）
        emitRectQuad(ctx, s.handleSpritePath, s.handleColor, hx - hw2, yc - hh2, hx + hw2,
                     yc + hh2, white, atlasTex);
}

// 滚动指示条（右侧/底部细条：显示内容比例与位置；纯视觉 v1 不可拖）
constexpr float kScrollBarThickness = 4.0f;   // 逻辑像素
const glm::vec4 kScrollBarColor{0.75f, 0.75f, 0.75f, 0.5f};

// 可视区外框（V0.5 验收补强）：背景填充 + 四边描边。
// 在 enter 回调发射（先于子树）→ 批内发射序靠前，内容绘制在其上。
void emitScrollViewFrame(EmitCtx& ctx, const ScrollView& sv, float us, const glm::vec4& view,
                         const Glyph* white, uint32_t atlasTex) {
    if (view.z - view.x <= 0.0f || view.w - view.y <= 0.0f) return;
    if (sv.bgColor.a > 0.001f)
        emitRectQuad(ctx, "", sv.bgColor, view.x, view.y, view.z, view.w, white, atlasTex);
    const float bt = std::max(sv.borderThickness, 0.0f) * us;
    if (bt <= 0.0f || sv.borderColor.a <= 0.001f) return;
    emitRectQuad(ctx, "", sv.borderColor, view.x, view.y, view.z, view.y + bt, white, atlasTex);
    emitRectQuad(ctx, "", sv.borderColor, view.x, view.w - bt, view.z, view.w, white, atlasTex);
    emitRectQuad(ctx, "", sv.borderColor, view.x, view.y, view.x + bt, view.w, white, atlasTex);
    emitRectQuad(ctx, "", sv.borderColor, view.z - bt, view.y, view.z, view.w, white, atlasTex);
}

void emitScrollBars(EmitCtx& ctx, const ScrollView& sv, float us, const glm::vec4& view,
                    const Glyph* white, uint32_t atlasTex) {
    const float vw = view.z - view.x, vh = view.w - view.y;
    if (vw <= 0.0f || vh <= 0.0f) return;
    if (sv.scrollVertical && sv.m_contentSize.y > sv.size.y) {
        const float contentH = std::max(sv.m_contentSize.y * us, 1e-3f);
        const float barH = std::max(vh * vh / contentH, 8.0f * us);
        const float maxOff = sv.m_contentSize.y - sv.size.y;
        const float f = maxOff > 0.0f ? std::clamp(sv.scrollOffset.y / maxOff, 0.0f, 1.0f) : 0.0f;
        const float barY = view.y + (vh - barH) * f;
        const float bw = kScrollBarThickness * us;
        emitRectQuad(ctx, "", kScrollBarColor, view.z - bw, barY, view.z, barY + barH, white,
                     atlasTex);
    }
    if (sv.scrollHorizontal && sv.m_contentSize.x > sv.size.x) {
        const float contentW = std::max(sv.m_contentSize.x * us, 1e-3f);
        const float barW = std::max(vw * vw / contentW, 8.0f * us);
        const float maxOff = sv.m_contentSize.x - sv.size.x;
        const float f = maxOff > 0.0f ? std::clamp(sv.scrollOffset.x / maxOff, 0.0f, 1.0f) : 0.0f;
        const float barX = view.x + (vw - barW) * f;
        const float bh = kScrollBarThickness * us;
        emitRectQuad(ctx, "", kScrollBarColor, barX, view.w - bh, barX + barW, view.w, white,
                     atlasTex);
    }
}

// ============================================================
// 布局遍历 walker：渲染发射 / 命中测试 / contentSize 三用同一实现。
// ScrollView 节点：自身元素用节点 ctx（祖先平移/裁剪），子树 ctx 追加
// scrollOffset 平移与可视区裁剪；嵌套 ScrollView 裁剪取交集。
// ============================================================

struct ContentBox {   // contentSize 并集盒
    bool valid = false;
    glm::vec2 mn{0.0f}, mx{0.0f};
    void add(const glm::vec4& r) {
        if (!valid) {
            mn = glm::vec2(r.x, r.y);
            mx = glm::vec2(r.z, r.w);
            valid = true;
            return;
        }
        mn = glm::min(mn, glm::vec2(r.x, r.y));
        mx = glm::max(mx, glm::vec2(r.z, r.w));
    }
    glm::vec2 size() const { return valid ? mx - mn : glm::vec2(0.0f); }
};

// 冒泡链条目：路径上每个 GO 的交互组件与矩形（矩形已含 ScrollView 平移）
struct UIChainEntry {
    const GameObject* go = nullptr;
    Component* button = nullptr;
    glm::vec4 buttonRect{0.0f};
    Component* slider = nullptr;
    glm::vec4 sliderRect{0.0f};
    Component* scrollView = nullptr;
    glm::vec4 scrollViewRect{0.0f};
};

struct UIWalkState {
    glm::vec2 translate{0.0f};        // ScrollView 祖先累积内容平移（layout 像素）
    bool hasClip = false;
    glm::vec4 clip{0.0f};             // 裁剪交集
    bool clipEmpty = false;           // 交集为空 → 子树不可见/不可命中
    std::vector<std::pair<const ScrollView*, ContentBox>> collectors;   // contentSize 收集栈
    std::vector<UIChainEntry> chain;  // 命中测试用：根 → 当前节点的交互链
};

using UIWalkEnterFn = std::function<void(const GameObject*, UIWalkState&)>;
using UIWalkLeaveFn = std::function<void(const GameObject*, const UIWalkState&,
                                         const UIWalkState&, const ContentBox&)>;

// 节点全部 UI 元素矩形（与渲染同一布局公式；无效矩形 = 全 0）
struct NodeUIElements {
    Component* imageComp = nullptr;
    glm::vec4 image{0.0f};
    Component* textComp = nullptr;
    glm::vec4 text{0.0f};
    Component* buttonComp = nullptr;
    glm::vec4 button{0.0f};
    Component* sliderComp = nullptr;
    glm::vec4 slider{0.0f};
    Component* scrollViewComp = nullptr;
    glm::vec4 scrollView{0.0f};
};

NodeUIElements collectNodeElements(const GameObject* node, const glm::vec2& trans) {
    NodeUIElements e;
    const float us = uiScaleOf(node);
    if (const Image* img = node->getComponent<Image>();
        img && img->size.x > 0.0f && img->size.y > 0.0f) {
        e.imageComp = const_cast<Image*>(img);
        e.image = translateRect(centerRect(img->anchor, img->offset, img->size, us), trans);
    }
    if (const Text* t = node->getComponent<Text>(); t) {
        const glm::vec4 r = textBlockRect(*t, us, trans);
        if (rectValid(r)) {
            e.textComp = const_cast<Text*>(t);
            e.text = r;
        }
    }
    if (const Button* b = node->getComponent<Button>();
        b && b->size.x > 0.0f && b->size.y > 0.0f) {
        e.buttonComp = const_cast<Button*>(b);
        e.button = translateRect(centerRect(b->anchor, b->offset, b->size, us), trans);
    }
    if (const Slider* s = node->getComponent<Slider>();
        s && s->size.x > 0.0f && s->size.y > 0.0f) {
        e.sliderComp = const_cast<Slider*>(s);
        e.slider = translateRect(centerRect(s->anchor, s->offset, s->size, us), trans);
    }
    if (const ScrollView* sv = node->getComponent<ScrollView>();
        sv && sv->size.x > 0.0f && sv->size.y > 0.0f) {
        e.scrollViewComp = const_cast<ScrollView*>(sv);
        e.scrollView = translateRect(centerRect(sv->anchor, sv->offset, sv->size, us), trans);
    }
    return e;
}

// 子树上下文：本节点是 ScrollView → 平移 + scrollOffset、裁剪 ∩ 可视区（空交集保持为空）
void applyScrollViewCtx(UIWalkState& st, const ScrollView& sv, const GameObject* node) {
    const float us = uiScaleOf(node);
    const glm::vec4 view =
        translateRect(centerRect(sv.anchor, sv.offset, sv.size, us), st.translate);
    glm::vec2 off{0.0f};
    if (sv.scrollHorizontal) off.x = sv.scrollOffset.x * us;
    if (sv.scrollVertical) off.y = sv.scrollOffset.y * us;
    st.translate += off;
    if (st.clipEmpty) {
        /* 祖先交集已空 → 保持空 */
    } else if (st.hasClip) {
        st.clip = intersectRect(st.clip, view);
        st.clipEmpty = !rectValid(st.clip);
    } else {
        st.hasClip = true;
        st.clip = view;
        st.clipEmpty = !rectValid(view);
    }
}

void walkUIChildren(const GameObject* parent, const UIWalkState& in, const UIWalkEnterFn& enter,
                    const UIWalkLeaveFn& leave) {
    for (const auto& child : parent->children()) {
        if (!child->activeInHierarchy()) continue;
        if (child->getComponent<Canvas>()) continue;   // 嵌套 Canvas 子树由其自身 pass 绘制
        const GameObject* node = child.get();
        UIWalkState nodeSt = in;          // 本节点 ctx（祖先平移/裁剪）
        enter(node, nodeSt);
        UIWalkState childSt = nodeSt;     // 子树 ctx
        const ScrollView* sv = node->getComponent<ScrollView>();
        if (sv) {
            applyScrollViewCtx(childSt, *sv, node);
            childSt.collectors.push_back({sv, ContentBox{}});
        }
        walkUIChildren(node, childSt, enter, leave);
        if (sv) {
            const ContentBox box = childSt.collectors.back().second;
            childSt.collectors.pop_back();
            leave(node, nodeSt, childSt, box);
        }
    }
}

// Canvas 根入口：root 自身组件不参与（与 collectUINodes 一致）；root 带 ScrollView =
// 全屏内容滚动（v1 顺带支持，可视区按其 anchor/size 矩形）
void walkCanvas(const GameObject* root, UIWalkState& st, const UIWalkEnterFn& enter,
                const UIWalkLeaveFn& leave) {
    const ScrollView* rootSv = root->getComponent<ScrollView>();
    if (rootSv) {
        applyScrollViewCtx(st, *rootSv, root);
        st.collectors.push_back({rootSv, ContentBox{}});
        // 根级 ScrollView 也进冒泡链（评审 P2 修复：此前 root 不进 enter → 链中缺失，
        // 指针命中其内容时滚轮无处交付）。rect = 可视区（无祖先平移，不含自身滚动量）
        UIChainEntry rootEntry;
        rootEntry.go = root;
        rootEntry.scrollView = const_cast<ScrollView*>(rootSv);
        rootEntry.scrollViewRect = translateRect(
            centerRect(rootSv->anchor, rootSv->offset, rootSv->size, uiScaleOf(root)),
            glm::vec2(0.0f));
        st.chain.push_back(rootEntry);
    }
    walkUIChildren(root, st, enter, leave);
    if (rootSv) {
        const ContentBox box = st.collectors.back().second;
        st.collectors.pop_back();
        leave(root, UIWalkState{}, st, box);
    }
}

// ---- contentSize 落地与 scrollOffset 钳制 ----

// 并集盒 → 逻辑像素写入 m_contentSize（UIRenderer 渲染/命中遍历期重算；脚本查询缓存）
void finalizeContentSize(const ScrollView& sv, const ContentBox& box) {
    const GameObject* go = sv.gameObject();
    if (!go) return;
    const float us = uiScaleOf(go);
    const glm::vec2 s = box.size();
    const_cast<ScrollView&>(sv).m_contentSize = glm::vec2(s.x / us, s.y / us);
}

// scrollOffset 钳制 [0, max(0, content-view)]（按启用轴；禁用轴保持存值——渲染不应用）
void clampScrollOffset(const ScrollView& sv) {
    auto& s = const_cast<ScrollView&>(sv);
    if (s.scrollHorizontal)
        s.scrollOffset.x =
            std::clamp(s.scrollOffset.x, 0.0f, std::max(0.0f, s.m_contentSize.x - s.size.x));
    if (s.scrollVertical)
        s.scrollOffset.y =
            std::clamp(s.scrollOffset.y, 0.0f, std::max(0.0f, s.m_contentSize.y - s.size.y));
}

// ============================================================
// 命中（hitTest 内部实现）：与渲染同一 walker/同一布局公式；顺带重算 contentSize
//（processInput 滚轮钳制用当帧值）。返回 DFS 画家序最后命中者 + 其根→节点冒泡链。
// ============================================================

bool hitTestWithChain(const Scene& scene, const glm::vec2& mouse, UIHitResult& out,
                      std::vector<UIChainEntry>& outChain) {
    bool found = false;
    for (const Canvas* canvas : scene.findComponents<Canvas>()) {
        const GameObject* root = canvas->gameObject();
        if (!root || !root->activeInHierarchy()) continue;
        UIWalkState st;
        const UIWalkEnterFn enter = [&](const GameObject* node, UIWalkState& ws) {
            const NodeUIElements e = collectNodeElements(node, ws.translate);
            // contentSize 收集（与可见性无关——被裁剪的内容仍占滚动范围）
            for (auto& entry : ws.collectors) {
                for (const glm::vec4* r : {&e.image, &e.text, &e.button, &e.slider, &e.scrollView})
                    if (rectValid(*r)) entry.second.add(*r);
            }
            if (ws.clipEmpty) return;   // 裁剪交集为空 → 子树不可见也不可命中
            // 冒泡链记录（路径上每个含交互组件的节点；矩形已含平移）
            UIChainEntry entry;
            entry.go = node;
            if (e.buttonComp && rectValid(e.button)) {
                entry.button = e.buttonComp;
                entry.buttonRect = e.button;
            }
            if (e.sliderComp && rectValid(e.slider)) {
                entry.slider = e.sliderComp;
                entry.sliderRect = e.slider;
            }
            if (e.scrollViewComp && rectValid(e.scrollView)) {
                entry.scrollView = e.scrollViewComp;
                entry.scrollViewRect = e.scrollView;
            }
            if (entry.button || entry.slider || entry.scrollView)
                ws.chain.push_back(entry);
            // 命中判定：rect ∩ clip（可见部分）；顺序与发射一致
            //（Image → Text → Button → Slider → ScrollView；后命中者覆盖 = 最上层）
            const bool vis = !ws.hasClip || pointInRect(mouse, ws.clip);
            if (!vis) return;
            const auto tryHit = [&](const glm::vec4& r, Component* comp) {
                if (rectValid(r) && pointInRect(mouse, r)) {
                    found = true;
                    out = UIHitResult{const_cast<GameObject*>(node), comp};
                    outChain = ws.chain;
                }
            };
            tryHit(e.image, e.imageComp);
            tryHit(e.text, e.textComp);
            tryHit(e.button, e.buttonComp);
            tryHit(e.slider, e.sliderComp);
            tryHit(e.scrollView, e.scrollViewComp);
        };
        const UIWalkLeaveFn leave = [&](const GameObject* node, const UIWalkState&,
                                        const UIWalkState&, const ContentBox& box) {
            if (const ScrollView* sv = node->getComponent<ScrollView>()) {
                finalizeContentSize(*sv, box);
                clampScrollOffset(*sv);
            }
        };
        walkCanvas(root, st, enter, leave);
    }
    return found;
}

// ============================================================
// processInput 冒泡交付
// ============================================================

// Slider 拖拽捕获（按下帧建立、松开帧解除；拖拽中指针移动/离开控件不改变投递目标）。
// 只存 GO id 不信任原始指针：Play 快照还原 / 读档 = 同一 Scene 对象内销毁重建全部 GO
//（SceneSerializer::deserialize 原地重建），旧 Slider* 悬垂——校验一律经
// scene->findGameObject(id) 重查（评审 P1 修复：拖拽中退出 Play 再进入会解引用悬垂指针）
struct ActiveDrag {
    const Scene* scene = nullptr;
    uint64_t goId = 0;
    Slider* slider = nullptr;
    glm::vec4 rect{0.0f};
} s_activeDrag;

bool deliverButton(Button& b, const glm::vec4& rect, const glm::vec2& mouse, bool leftDown,
                   bool leftPressed) {
    const bool hit = pointInRect(mouse, rect);
    b.m_hovered = hit;
    b.m_pressed = hit && leftDown;
    b.m_pressedEdge = hit && leftPressed;   // 按下边沿（onClick 触发沿）
    if (b.m_pressedEdge && b.onClick) b.onClick();
    return hit && leftDown;   // 按下（含点击边沿）即消费
}

// 按下即拖拽：手柄跳到指针 X（值线性映射），onValueChanged 连续触发
void updateSliderFromPointer(Slider& s, const glm::vec4& rect, const glm::vec2& mouse) {
    const float span = rect.z - rect.x;
    const float t = span > 0.0f ? std::clamp((mouse.x - rect.x) / span, 0.0f, 1.0f) : 0.0f;
    const float lo = std::min(s.minValue, s.maxValue), hi = std::max(s.minValue, s.maxValue);
    float v = lo + t * (hi - lo);
    if (s.wholeNumbers) v = std::round(v);
    v = std::clamp(v, lo, hi);
    if (v != s.value) {
        s.value = v;
        if (s.onValueChanged) s.onValueChanged(v);
    }
}

bool deliverSliderStart(Slider& s, const glm::vec4& rect, const glm::vec2& mouse,
                        bool leftPressed) {
    if (!leftPressed || !pointInRect(mouse, rect)) return false;
    s.m_dragging = true;
    updateSliderFromPointer(s, rect, mouse);
    return true;
}

// 滚轮：本帧「生效」（实际改变偏移）才消费；未生效继续向上冒泡（嵌套滚透）
bool deliverScrollView(ScrollView& sv, const glm::vec4& rect, const glm::vec2& mouse) {
    if (!pointInRect(mouse, rect)) return false;
    const glm::vec2 d = Input::scrollDelta();
    if (d.x == 0.0f && d.y == 0.0f) return false;
    glm::vec2 next = sv.scrollOffset;
    const glm::vec2 maxOff{std::max(0.0f, sv.m_contentSize.x - sv.size.x),
                           std::max(0.0f, sv.m_contentSize.y - sv.size.y)};
    bool changed = false;
    if (sv.scrollHorizontal && d.x != 0.0f) {
        const float nx = std::clamp(next.x + d.x * sv.scrollSensitivity, 0.0f, maxOff.x);
        changed = changed || nx != next.x;
        next.x = nx;
    }
    if (sv.scrollVertical && d.y != 0.0f) {
        const float ny = std::clamp(next.y + d.y * sv.scrollSensitivity, 0.0f, maxOff.y);
        changed = changed || ny != next.y;
        next.y = ny;
    }
    sv.scrollOffset = next;
    return changed;
}

// 静态库链接防护（V0.5-13）：UIRenderer 对 Slider/ScrollView 仅按指针访问公共字段，
// MSVC 静态库可能不把两组件 .obj 拉入 Player/Editor 链接 → NE_REGISTER_COMPONENT
// 自注册初始器缺失。此 volatile 恒假分支产生对构造函数的引用，强制链接两 .obj
//（与 ComponentRegistry.cpp 的 s_linkGuardProbe 同纪律；该处清单由整合者按注释同步）。
void linkGuardNewUIComponents() {
    static volatile bool s_probe = false;
    if (s_probe) {
        (void)std::make_unique<Slider>(nullptr);
        (void)std::make_unique<ScrollView>(nullptr);
    }
}

}  // namespace

void UIRenderer::init() {
    linkGuardNewUIComponents();
    if (s_initialized || s_disabled) return;
    s_initialized = true;

    // GL 上下文未就绪（理论上 init 应晚于 Renderer::init；防御性降级）
    if (!glad_glCreateShader) {
        s_disabled = true;
        Logger::warn("[UIRenderer] init 时 GL 上下文未就绪，UI 渲染降级为 no-op");
        return;
    }

    s_shader = Shader::createFromFiles("assets/shaders/ui.vert", "assets/shaders/ui.frag");
    if (!s_shader) {  // 编译/链接失败已由 Shader 内部 error 日志
        s_disabled = true;
        return;
    }
    s_locScreenSize = glGetUniformLocation(s_shader->handle(), "uScreenSize");
    s_locTex = glGetUniformLocation(s_shader->handle(), "uTex");

    glGenVertexArrays(1, &s_vao);
    glGenBuffers(1, &s_vbo);
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, s_vboCapacity * sizeof(UIVertex), nullptr, GL_DYNAMIC_DRAW);
    const GLsizei stride = sizeof(UIVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(UIVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(UIVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(UIVertex, r)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // 触发默认图集加载 + 白块/行高探针烘焙（GL 纹理随首次烘焙创建）
    FontAtlas::defaultAtlas();
    FontAtlas::glyph(kWhiteBlockCp);
    FontAtlas::glyph(kMetricsProbeCp);
    if (!FontAtlas::textureId()) {
        s_disabled = true;
        Logger::error("[UIRenderer] 字体图集纹理创建失败，UI 渲染降级为 no-op");
    }
}

void UIRenderer::setDpiScale(float scale) {
    s_dpiScale = std::clamp(scale, 0.1f, 10.0f);
}

void UIRenderer::shutdown() {
    s_activeDrag = {};
    s_lastPointerConsumed = false;
    if (!glad_glDeleteBuffers) return;  // 上下文已销毁：句柄随上下文释放
    if (s_vao) { glDeleteVertexArrays(1, &s_vao); s_vao = 0; }
    if (s_vbo) { glDeleteBuffers(1, &s_vbo); s_vbo = 0; }
    s_shader.reset();
    s_spriteCache.clear();
    s_warnedSpritePaths.clear();
    s_disabled = false;    // 允许窗口重建后重新 init
    s_initialized = false;
    s_inputThisFrame = false;
}

void UIRenderer::render(const Scene& scene, int screenW, int screenH) {
    if (s_disabled || !s_shader) return;
    // 输入新鲜标志在尺寸校验之前消费：screenW/H 非法早退时若残留 true，会使
    // 退出 Play 后的首个有效帧误用旧 hover 色（评审 P2-6 修复）
    const bool useInputState = s_inputThisFrame;  // Play 帧 processInput 已刷新
    s_inputThisFrame = false;                     // Edit 预览（无 processInput）走基色
    if (screenW <= 0 || screenH <= 0) return;
    s_physicalScreenSize = glm::vec2(static_cast<float>(screenW), static_cast<float>(screenH));
    s_screenSize = s_physicalScreenSize / s_dpiScale;   // 布局空间 = 逻辑像素（HiDPI）

    // ---- 保存调用方 GL 状态（结束时逐一恢复；V0.5-13 +scissor）----
    GLint prevProgram = 0, prevVao = 0, prevArrayBuf = 0, prevTex = 0, prevTex0 = 0;
    GLint prevActiveTex = 0, prevBlendSrc = 0, prevBlendDst = 0;
    GLint prevScissorBox[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuf);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);
    // 固定 unit 0 绘制期间会改写 unit 0 绑定（精灵/图集纹理）：unit 0 与当前激活单元
    // 各保存一份，退出时逐一恢复（评审 P2 修复：prevActiveTex≠0 时 unit 0 绑定曾泄漏）
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex0);
    glActiveTexture(static_cast<GLenum>(prevActiveTex));
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDst);
    glGetIntegerv(GL_SCISSOR_BOX, prevScissorBox);
    const GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean prevBlend = glIsEnabled(GL_BLEND);
    const GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
    const GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    s_shader->bind();
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    // 固定 unit 0：精灵按需加载（AssetDatabase 建纹理）与 FontAtlas 子区域上传
    // 都绑定在当前激活单元上——提前固定后，保存/恢复 unit 0 绑定即精确无泄漏
    glActiveTexture(GL_TEXTURE0);
    glUniform2f(s_locScreenSize, static_cast<float>(screenW), static_cast<float>(screenH));
    glUniform1i(s_locTex, 0);

    // ---- 遍历全部 Canvas：每 Canvas 独立收集、预烘焙、批上传、绘制 ----
    for (const Canvas* canvas : scene.findComponents<Canvas>()) {
        const GameObject* root = canvas->gameObject();
        if (!root || !root->activeInHierarchy()) continue;

        std::vector<const GameObject*> nodes;
        collectUINodes(root, nodes);
        if (nodes.empty()) continue;

        // 预烘焙 pass：先触发全部字形烘焙（含可能的图集扩容重建），再构建 quad——
        // 保证同帧取到的 Glyph UV 一致（重建后旧指针失效的规避契约）。
        // v1.1：按 Text.fontPath 分别烘焙到对应字体图集
        for (const GameObject* n : nodes)
            if (const Text* t = n->getComponent<Text>())
                for (size_t i = 0; i < t->text.size();)
                    FontAtlas::glyph(utf8Decode(t->text, i), t->fontPath);

        const Glyph* white = FontAtlas::glyph(kWhiteBlockCp);
        const uint32_t atlasTex = FontAtlas::textureId();
        if (!white || !atlasTex) continue;
        // 预烘焙可能有新字形上传 → 重建 mip 链（MIN=GL_LINEAR_MIPMAP_LINEAR），
        // 保证本 Canvas 本帧采样的是最新 mip（否则新字形用旧 mip/无 mip 发虚）
        FontAtlas::refreshMipmaps();

        // per-(贴图,裁剪) 批（插入序即遍历序；同贴图+同裁剪内保持画家顺序）
        std::vector<BatchKey> bucketOrder;
        std::unordered_map<BatchKey, size_t, BatchKeyHash> bucketIndex;
        std::vector<std::vector<UIVertex>> buckets;
        std::vector<glm::vec4> clipRects;
        BatchSink sink{bucketOrder, bucketIndex, buckets};

        UIWalkState st;
        const UIWalkEnterFn enter = [&](const GameObject* node, UIWalkState& ws) {
            const NodeUIElements e = collectNodeElements(node, ws.translate);
            // contentSize 收集（与可见性无关；Edit 预览无 processInput 也保持新鲜值）
            for (auto& entry : ws.collectors) {
                for (const glm::vec4* r : {&e.image, &e.text, &e.button, &e.slider, &e.scrollView})
                    if (rectValid(*r)) entry.second.add(*r);
            }
            if (ws.clipEmpty) return;   // ScrollView 交集为空 → 子树整体不可见
            EmitCtx ctx{sink, ws.hasClip ? clipIndexOf(clipRects, ws.clip) : -1};
            const float us = uiScaleOf(node);
            if (rectValid(e.image))
                emitImage(ctx, *static_cast<Image*>(e.imageComp), us, white, atlasTex, e.image);
            if (rectValid(e.text))
                emitText(ctx, *static_cast<Text*>(e.textComp), us, e.text);
            if (rectValid(e.button))
                emitButton(ctx, *static_cast<Button*>(e.buttonComp), white, atlasTex, useInputState,
                           e.button);
            if (rectValid(e.slider))
                emitSlider(ctx, *static_cast<Slider*>(e.sliderComp), us, white, atlasTex, e.slider);
            if (rectValid(e.scrollView))
                emitScrollViewFrame(ctx, *static_cast<ScrollView*>(e.scrollViewComp), us,
                                    e.scrollView, white, atlasTex);
        };
        const UIWalkLeaveFn leave = [&](const GameObject* node, const UIWalkState& nodeCtx,
                                        const UIWalkState& childCtx, const ContentBox& box) {
            const ScrollView* sv = node->getComponent<ScrollView>();
            if (!sv) return;
            finalizeContentSize(*sv, box);
            clampScrollOffset(*sv);
            if (childCtx.clipEmpty) return;
            const float us = uiScaleOf(node);
            const glm::vec4 view =
                translateRect(centerRect(sv->anchor, sv->offset, sv->size, us), nodeCtx.translate);
            EmitCtx ctx{sink, childCtx.hasClip ? clipIndexOf(clipRects, childCtx.clip) : -1};
            emitScrollBars(ctx, *sv, us, view, white, atlasTex);
        };
        walkCanvas(root, st, enter, leave);

        size_t total = 0;
        for (const auto& b : buckets) total += b.size();
        if (!total) continue;

        // 每帧 map 重写：orphan（容量不变时）后 glBufferSubData 上传本 Canvas 全部批
        glBufferData(GL_ARRAY_BUFFER, s_vboCapacity * sizeof(UIVertex), nullptr, GL_DYNAMIC_DRAW);
        ensureVboCapacity(total);
        if (total > s_vboCapacity) continue;  // 扩容后容量仍不足（理论不可达，防御）
        size_t offset = 0;
        for (size_t bi = 0; bi < bucketOrder.size(); ++bi) {
            if (buckets[bi].empty()) continue;
            glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(offset * sizeof(UIVertex)),
                            static_cast<GLsizeiptr>(buckets[bi].size() * sizeof(UIVertex)),
                            buckets[bi].data());
            offset += buckets[bi].size();
        }

        offset = 0;
        for (size_t bi = 0; bi < bucketOrder.size(); ++bi) {
            if (buckets[bi].empty()) continue;
            const BatchKey k = bucketOrder[bi];
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, k.tex);
            if (k.clip >= 0) {   // ScrollView scissor（y 翻转：布局左上原点 → GL 左下原点）
                const glm::vec4 r = clipRects[static_cast<size_t>(k.clip)];
                const float dpi = s_dpiScale;
                glEnable(GL_SCISSOR_TEST);
                glScissor(static_cast<GLint>(std::floor(r.x * dpi)),
                          static_cast<GLint>(std::floor(s_physicalScreenSize.y - r.w * dpi)),
                          static_cast<GLsizei>(std::ceil((r.z - r.x) * dpi)),
                          static_cast<GLsizei>(std::ceil((r.w - r.y) * dpi)));
            } else {
                glDisable(GL_SCISSOR_TEST);
            }
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(offset),
                         static_cast<GLsizei>(buckets[bi].size()));
            offset += buckets[bi].size();
        }
    }

    // ---- 恢复调用方状态 ----
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex0));
    glActiveTexture(static_cast<GLenum>(prevActiveTex));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuf));
    glBindVertexArray(static_cast<GLuint>(prevVao));
    glUseProgram(static_cast<GLuint>(prevProgram));
    if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFunc(static_cast<GLenum>(prevBlendSrc), static_cast<GLenum>(prevBlendDst));
    if (prevCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (prevScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    glScissor(prevScissorBox[0], prevScissorBox[1], prevScissorBox[2], prevScissorBox[3]);
}

void UIRenderer::processInput(const Scene& scene, const glm::vec2& mouse, bool leftDown,
                              bool leftPressed) {
    if (s_disabled) { s_lastPointerConsumed = false; return; }
    if (s_screenSize.x <= 0.0f || s_screenSize.y <= 0.0f) {  // render 未运行过：无布局基准
        s_lastPointerConsumed = false;
        return;
    }
    s_inputThisFrame = true;
    const glm::vec2 m = mouse / s_dpiScale;   // 物理 FBO 坐标 → 逻辑布局坐标（HiDPI）

    // 1. 全部交互状态整体复位（随后仅冒泡链上的组件重算，链外一律复位——
    //    修复旧实现「遍历全部 Button 各自命中」的重叠多置位缺陷）
    for (Button* b : scene.findComponents<Button>()) {
        b->m_hovered = false;
        b->m_pressed = false;
        b->m_pressedEdge = false;
    }
    for (Slider* s : scene.findComponents<Slider>()) s->m_dragging = false;

    // 2. Slider 拖拽捕获（跨帧）：拖拽中指针移动/离开控件不改变投递目标（连续回调）
    if (s_activeDrag.slider) {
        // 校验经 GO id 重查：场景重建后原始 Slider* 悬垂，先 findGameObject 再取组件，
        // 指针不匹配（GO 已重建为新组件）即丢弃捕获（评审 P1 修复，见 ActiveDrag 注释）
        GameObject* dragGo = (s_activeDrag.scene == &scene)
            ? scene.findGameObject(s_activeDrag.goId) : nullptr;
        const bool valid = dragGo && dragGo->getComponent<Slider>() == s_activeDrag.slider;
        if (!valid) {
            s_activeDrag = {};   // 场景已切换/GO 已销毁重建：丢弃捕获
        } else if (leftDown) {
            s_activeDrag.slider->m_dragging = true;
            updateSliderFromPointer(*s_activeDrag.slider, s_activeDrag.rect, m);
            s_lastPointerConsumed = true;   // 拖拽中即消费（Play 中 UI 优先于场景拾取）
            return;
        } else {
            s_activeDrag = {};   // 松开帧：捕获结束，本帧其余交互照常处理
        }
    }

    // 3. 命中 winner = 最上层 UI 元素（hitTest 与渲染同一布局公式；顺带重算 contentSize）
    UIHitResult hit;
    std::vector<UIChainEntry> chain;
    if (!hitTestWithChain(scene, m, hit, chain)) {
        s_lastPointerConsumed = false;   // 指针未落在任何 UI 上：无消费
        return;
    }

    // 4. 冒泡：自 winner GO 沿父链就近交付交互组件（Button/Slider/ScrollView）；
    //    已消费 → 停（父链更上层不再收到本帧输入）
    bool consumed = false;
    for (auto it = chain.rbegin(); it != chain.rend() && !consumed; ++it) {
        const UIChainEntry& e = *it;
        if (e.button &&
            deliverButton(*static_cast<Button*>(e.button), e.buttonRect, m, leftDown,
                          leftPressed)) {
            consumed = true;
            break;
        }
        if (e.slider &&
            deliverSliderStart(*static_cast<Slider*>(e.slider), e.sliderRect, m,
                               leftPressed)) {
            s_activeDrag = {&scene, e.go->id(), static_cast<Slider*>(e.slider), e.sliderRect};
            consumed = true;
            break;
        }
        if (e.scrollView &&
            deliverScrollView(*static_cast<ScrollView*>(e.scrollView), e.scrollViewRect, m)) {
            consumed = true;
            break;
        }
    }
    s_lastPointerConsumed = consumed;
}

bool UIRenderer::processWheelInput(const Scene& scene, const glm::vec2& mouse) {
    // Edit 态滚轮（V0.5 验收补强，2026-11-17）：仅投递 ScrollView 滚动——Button/Slider
    // 不参与（Edit 预览不可点按，保持「预览不挡拾取」裁定）。滚轮被 ScrollView 消费
    // 时返回 true，调用方据此抑制编辑器相机缩放（否则同一 tick 相机与列表一起动）。
    // 命中/冒泡与 processInput 同一实现（hitTestWithChain 顺带重算 contentSize，钳制新鲜）。
    if (s_disabled) return false;
    if (s_screenSize.x <= 0.0f || s_screenSize.y <= 0.0f) return false;  // render 未运行过
    const glm::vec2 m = mouse / s_dpiScale;   // 物理 FBO 坐标 → 逻辑布局坐标（HiDPI）
    UIHitResult hit;
    std::vector<UIChainEntry> chain;
    if (!hitTestWithChain(scene, m, hit, chain)) return false;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (it->scrollView &&
            deliverScrollView(*static_cast<ScrollView*>(it->scrollView), it->scrollViewRect, m))
            return true;
    }
    return false;
}

bool UIRenderer::hitTest(const Scene& scene, const glm::vec2& mouse, UIHitResult& out) {
    out = {};
    if (s_disabled) return false;
    if (s_screenSize.x <= 0.0f || s_screenSize.y <= 0.0f) return false;  // render 未运行过
    std::vector<UIChainEntry> chain;
    return hitTestWithChain(scene, mouse / s_dpiScale, out, chain);   // 物理 → 逻辑（HiDPI）
}

bool UIRenderer::isPointerOverUI(const Scene& scene, const glm::vec2& mouse) {
    UIHitResult r;
    return hitTest(scene, mouse, r);
}

bool UIRenderer::lastPointerConsumed() { return s_lastPointerConsumed; }

}  // namespace NovaEngine
