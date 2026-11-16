// UIRenderer.cpp —— 运行时 UI 批渲染与输入（V0.3-3）。零 ImGui 依赖。
//
// == 渲染路径 ==
// 3D pass + 后处理之后、调用方「当前绑定的 FBO」内绘制：本文件不绑定/切换 FBO、
// 不改视口（视口由调用方按目标分辨率设好）。GL 状态（程序/VAO/VBO/纹理/深度/混合/
// 剔除）进出 render 全部保存-恢复，不打扰 3D 与后处理。
//
// == 坐标系（裁定）==
// 正交投影 0..w × 0..h 像素；左上原点、Y 向下——与 Input::mousePosition 窗口坐标
// 约定一致，processInput 收到的鼠标坐标可直接比对。NDC 换算在 ui.vert 内完成。
// 布局：Image/Button 元素中心 = anchor(0..1) × 屏幕 + offset × uiScale；
//       Text 起点（左上）= anchor × 屏幕 + offset × uiScale；
//       size / fontSize / slice9 均 × uiScale（anchor 不缩放——锚点贴屏幕角）；
//       Canvas.uiScale 为 v1 预留（Inspector 可调，渲染/命中同步生效）。
//
// == 批处理（裁定）==
// per-Canvas、per-texture 批：单 Canvas 内全部字形/白块/按钮共用字体图集 = 1 draw
// call；每张精灵贴图 +1 draw call（跨贴图批排序会改变严格画家顺序——v1 已知取舍，
// 同贴图内保持遍历序）。Canvas 自身不绘制；嵌套 Canvas 子树交给其自身 pass，避免
// 双重绘制。
//
// == 字形生命周期 ==
// FontAtlas 图集 512→1024 重建会使旧 Glyph* 失效。规避：本文件每帧、每字形重新取
// glyph()，且每 Canvas 先做一遍「预烘焙 pass」（对全部 Text 逐码点 glyph() 一次，
// 触发可能的重建），再取第二遍构建 quad——保证同帧内 UV 一致、无中途重建。
//
// == 输入 ==
// processInput 接线（分侧）：Player 主循环应在 scene.update 之前调用（脚本 onUpdate
// 查询当帧状态）；编辑器 SceneView 为渲染期调用（脚本侧按钮状态滞后一帧——已知取舍，
// 由调用点注释文档化）。
// 矩形命中 = anchor/size 反推屏幕 rect（与渲染同一公式）；Button hover/press 状态
// 每帧整体复位后重算；onClick 边沿触发（leftPressed && hit）。Text/Image 不响应。
// 输入新鲜标志 s_inputThisFrame：render 消费后复位——Edit 预览（无 processInput）
// 一律基色，杜绝「退出 Play 后 hover 色残留」。
//
// == v1 已知限制（注释存档）==
// - 不做字距（kern）；单行 + '\n' 换行；超宽不裁剪
// - 图集满（1024²）后新字形缺失渲染（FontAtlas 已 warn 一次）
// （Text.fontPath 已于 v1.1 生效：每字体独立图集）

#include <NovaEngine/UI/UIRenderer.h>

#include <NovaEngine/UI/FontAtlas.h>

#include <NovaEngine/Assets/AssetDatabase.h>
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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace NovaEngine {

namespace {

// 内部保留码点（与 FontAtlas.cpp 一一对应，冻结头无常量位——两侧注释互指）
constexpr uint32_t kWhiteBlockCp = 0xFFFFFF;    // 1x1 白块（纯色 Image / Button 共用）
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
glm::vec2 s_screenSize{0.0f};    // 最近一次 render 的目标分辨率（processInput 命中用）
float s_dpiScale = 1.0f;         // HiDPI：uiScale 全局乘数（窗口逻辑→帧缓冲像素比；默认 1）

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
    // 顶点序 TL/TR/BL, TR/BR/BL（默认逆时针正面）
    const UIVertex tl{x0, y0, u0, v0, c.r, c.g, c.b, c.a};
    const UIVertex tr{x1, y0, u1, v0, c.r, c.g, c.b, c.a};
    const UIVertex bl{x0, y1, u0, v1, c.r, c.g, c.b, c.a};
    const UIVertex br{x1, y1, u1, v1, c.r, c.g, c.b, c.a};
    out.insert(out.end(), {tl, tr, bl, tr, br, bl});
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

// 最近的 Canvas 祖先（渲染与输入共用同一布局公式，保证命中与显示一致）
const Canvas* findCanvasAncestor(const GameObject* go) {
    for (const GameObject* p = go->parent(); p; p = p->parent())
        if (const Canvas* c = p->getComponent<Canvas>())
            return c;
    return nullptr;
}

float uiScaleOf(const GameObject* node) {
    const Canvas* c = findCanvasAncestor(node);
    const float base = c ? std::clamp(c->uiScale, 0.01f, 100.0f) : 1.0f;
    // HiDPI：× 窗口逻辑→帧缓冲像素比——UI 保持逻辑尺寸，物理像素下渲染更清晰
    return base * s_dpiScale;
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

// ---- 三类组件的 quad 生成（输出到对应贴图批；坐标/尺寸已按 uiScale 缩放）----

// per-texture 批容器：批内保持遍历序（画家顺序），跨贴图排序为 v1 已知取舍
struct BatchSink {
    std::vector<uint32_t>& order;
    std::unordered_map<uint32_t, size_t>& index;
    std::vector<std::vector<UIVertex>>& buckets;
    std::vector<UIVertex>& operator[](uint32_t tex) {
        const auto [it, inserted] = index.try_emplace(tex, buckets.size());
        if (inserted) { buckets.emplace_back(); order.push_back(tex); }
        return buckets[it->second];
    }
};

void emitImage(BatchSink& sink, const Image& img, float us, const Glyph* white,
               uint32_t atlasTex) {
    if (img.size.x <= 0.0f || img.size.y <= 0.0f) return;
    const glm::vec2 center = anchorPoint(img.anchor, img.offset, us);
    const float w = img.size.x * us, h = img.size.y * us;
    const float x0 = center.x - w * 0.5f, y0 = center.y - h * 0.5f;

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

    // 9-slice（仅精灵贴图有意义；白块 1x1 拆不出边距；尺寸小于边距和时退化为单 quad）
    const glm::vec4 m = img.slice9 * us;
    const bool slice = useSprite && (m.x > 0.0f || m.y > 0.0f || m.z > 0.0f || m.w > 0.0f) &&
                       (w > m.x + m.z) && (h > m.y + m.w);
    if (!slice) {
        pushQuad(sink[tex], x0, y0, x0 + w, y0 + h, u0, v0, u1, v1, img.color);
        return;
    }
    const float suL = m.x / static_cast<float>(texW);
    const float suR = m.z / static_cast<float>(texW);
    const float svT = m.y / static_cast<float>(texH);
    const float svB = m.w / static_cast<float>(texH);
    const float xs[4] = {x0, x0 + m.x, x0 + w - m.z, x0 + w};
    const float ys[4] = {y0, y0 + m.y, y0 + h - m.w, y0 + h};
    const float us_[4] = {u0, u0 + suL, u1 - suR, u1};
    const float vs_[4] = {v0, v0 + svT, v1 - svB, v1};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            pushQuad(sink[tex], xs[c], ys[r], xs[c + 1], ys[r + 1], us_[c], vs_[r], us_[c + 1],
                     vs_[r + 1], img.color);
}

void emitText(BatchSink& sink, const Text& t, float us) {
    if (t.text.empty() || t.fontSize <= 0.0f) return;
    // 每文本独立字体图集（fontPath 空 = 默认链；v1.1）
    const uint32_t atlasTex = FontAtlas::textureId(t.fontPath);
    if (!atlasTex) return;
    // 行高探针：基线与行高（每帧重取——图集重建后旧指针失效的规避契约）
    const Glyph* probe = FontAtlas::glyph(kMetricsProbeCp, t.fontPath);
    const float scale = (t.fontSize * us) / kRefPx;
    const glm::vec2 pen0 = anchorPoint(t.anchor, t.offset, us);
    float penX = pen0.x;
    float baseline = pen0.y + (probe ? probe->y1 * scale : 0.0f);
    const float lineH = probe ? (probe->y1 - probe->y0) * scale : 0.0f;

    for (size_t i = 0; i < t.text.size();) {
        const uint32_t cp = utf8Decode(t.text, i);
        if (cp == '\n') {
            penX = pen0.x;
            baseline += lineH;
            continue;
        }
        const Glyph* g = FontAtlas::glyph(cp, t.fontPath);
        if (!g || g->w <= 0.0f || g->h <= 0.0f) {  // 缺失/空白：仅推进（图集满时跳字符）
            penX += g ? g->advance * scale : t.fontSize * us * 0.25f;
            continue;
        }
        const float qx0 = penX + g->x0 * scale;
        const float qy0 = baseline - g->y1 * scale;
        pushQuad(sink[atlasTex], qx0, qy0, qx0 + g->w * scale, qy0 + g->h * scale, g->u0, g->v0,
                 g->u1, g->v1, t.color);
        penX += g->advance * scale;
    }
}

void emitButton(BatchSink& sink, const Button& b, float us, const Glyph* white,
                uint32_t atlasTex, bool useInputState) {
    if (b.size.x <= 0.0f || b.size.y <= 0.0f) return;
    glm::vec4 c = b.color;
    if (useInputState) {  // 仅 Play（processInput 已刷新）；Edit 预览一律基色
        if (b.m_pressed) c = b.pressedColor;
        else if (b.m_hovered) c = b.hoverColor;
    }
    const glm::vec2 center = anchorPoint(b.anchor, b.offset, us);
    const float w = b.size.x * us, h = b.size.y * us;
    // Button 无 spritePath（白块纯色）；slice9 对白块无意义 → 单 quad
    pushQuad(sink[atlasTex], center.x - w * 0.5f, center.y - h * 0.5f, center.x + w * 0.5f,
             center.y + h * 0.5f, white->u0, white->v0, white->u1, white->v1, c);
}

}  // namespace

void UIRenderer::init() {
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
    s_screenSize = glm::vec2(static_cast<float>(screenW), static_cast<float>(screenH));

    // ---- 保存调用方 GL 状态（结束时逐一恢复）----
    GLint prevProgram = 0, prevVao = 0, prevArrayBuf = 0, prevTex = 0;
    GLint prevActiveTex = 0, prevBlendSrc = 0, prevBlendDst = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuf);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDst);
    const GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean prevBlend = glIsEnabled(GL_BLEND);
    const GLboolean prevCull = glIsEnabled(GL_CULL_FACE);

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

        // per-texture 批（插入序即遍历序；同贴图内保持画家顺序）
        std::vector<uint32_t> bucketOrder;
        std::unordered_map<uint32_t, size_t> bucketIndex;
        std::vector<std::vector<UIVertex>> buckets;
        BatchSink sink{bucketOrder, bucketIndex, buckets};

        for (const GameObject* n : nodes) {
            const float us = uiScaleOf(n);
            if (const Image* img = n->getComponent<Image>())
                emitImage(sink, *img, us, white, atlasTex);
            if (const Text* t = n->getComponent<Text>())
                emitText(sink, *t, us);   // v1.1：文本内部按 fontPath 选图集
            if (const Button* b = n->getComponent<Button>())
                emitButton(sink, *b, us, white, atlasTex, useInputState);
        }

        size_t total = 0;
        for (const auto& b : buckets) total += b.size();
        if (!total) continue;

        // 每帧 map 重写：orphan（容量不变时）后 glBufferSubData 上传本 Canvas 全部批
        glBufferData(GL_ARRAY_BUFFER, s_vboCapacity * sizeof(UIVertex), nullptr, GL_DYNAMIC_DRAW);
        ensureVboCapacity(total);
        if (total > s_vboCapacity) continue;  // 扩容后容量仍不足（理论不可达，防御）
        size_t offset = 0;
        for (const auto& b : buckets) {
            if (b.empty()) continue;
            glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(offset * sizeof(UIVertex)),
                            static_cast<GLsizeiptr>(b.size() * sizeof(UIVertex)), b.data());
            offset += b.size();
        }

        offset = 0;
        for (size_t bi = 0; bi < buckets.size(); ++bi) {
            if (buckets[bi].empty()) continue;
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, bucketOrder[bi]);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(offset),
                         static_cast<GLsizei>(buckets[bi].size()));
            offset += buckets[bi].size();
        }
    }

    // ---- 恢复调用方状态 ----
    glActiveTexture(static_cast<GLenum>(prevActiveTex));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuf));
    glBindVertexArray(static_cast<GLuint>(prevVao));
    glUseProgram(static_cast<GLuint>(prevProgram));
    if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFunc(static_cast<GLenum>(prevBlendSrc), static_cast<GLenum>(prevBlendDst));
    if (prevCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
}

void UIRenderer::processInput(const Scene& scene, const glm::vec2& mouse, bool leftDown,
                              bool leftPressed) {
    if (s_disabled) return;
    if (s_screenSize.x <= 0.0f || s_screenSize.y <= 0.0f) return;  // render 未运行过：无布局基准
    s_inputThisFrame = true;

    std::vector<Button*> buttons = scene.findComponents<Button>();
    // 每帧整体复位，再重算命中（旧状态不留存）
    for (Button* b : buttons) {
        b->m_hovered = false;
        b->m_pressed = false;
        b->m_pressedEdge = false;
    }
    for (Button* b : buttons) {
        const GameObject* go = b->gameObject();
        if (!go || !go->activeInHierarchy()) continue;
        const Canvas* canvas = findCanvasAncestor(go);
        if (!canvas) continue;  // Canvas 之外的 Button 不参与渲染也不参与命中
        const float us = uiScaleOf(go);
        // 与 emitButton 同一公式：中心 = anchor×屏幕 + offset×uiScale，rect = 中心 ± size/2
        const glm::vec2 center = anchorPoint(b->anchor, b->offset, us);
        const float hw = b->size.x * us * 0.5f, hh = b->size.y * us * 0.5f;
        const bool hit = mouse.x >= center.x - hw && mouse.x <= center.x + hw &&
                         mouse.y >= center.y - hh && mouse.y <= center.y + hh;
        b->m_hovered = hit;
        b->m_pressed = hit && leftDown;
        b->m_pressedEdge = hit && leftPressed;  // 按下边沿（onClick 触发沿）
        if (b->m_pressedEdge && b->onClick) b->onClick();
    }
}

}  // namespace NovaEngine
