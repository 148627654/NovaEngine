#pragma once
#include <glm/vec2.hpp>

namespace NovaEngine {

class Scene;

// 运行时 UI 批处理渲染（V0.3-3）：单动态 VBO（每帧 map 顶点），per-atlas 批 →
// 每 Canvas 一 draw call；深度测试关、混合开、正交投影。零 ImGui 依赖。
class UIRenderer {
public:
    static void init();                            // ui.vert/.frag + 动态 quad VBO；失败降级 no-op
    static void shutdown();
    // 3D pass + 后处理之后、目标 FBO 内绘制全部 Canvas 树（Edit 预览 + Play 同路径）
    static void render(const Scene& scene, int screenW, int screenH);
    // Play 输入：鼠标（已换算游戏分辨率坐标）→ 矩形命中 → Button hover/press + onClick 边沿触发
    static void processInput(const Scene& scene, const glm::vec2& mouse, bool leftDown, bool leftPressed);
};

}  // namespace NovaEngine
