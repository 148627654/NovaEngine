#pragma once
#include <glm/vec2.hpp>

namespace NovaEngine {

class Scene;
class GameObject;
class Component;

// 指针命中结果（最上层 UI 元素；Image/Text 也参与命中 = 不透明覆盖语义）
struct UIHitResult {
    GameObject* gameObject = nullptr;   // 命中组件所在 GO
    Component* component = nullptr;     // 命中的 UI 组件（Image/Text/Button/Slider/ScrollView）
};

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
    // Edit 态滚轮（V0.5 验收补强）：仅 ScrollView 滚动（Button/Slider 不参与）；
    // 消费返回 true——调用方据此抑制编辑器相机缩放
    static bool processWheelInput(const Scene& scene, const glm::vec2& mouse);
    // 2026-11-16 新增（HiDPI 适配，增量接口）：uiScale 全局乘数 =
    // 窗口逻辑→帧缓冲像素比（150% 显示器 = 1.5）。调用方每帧在 render 之前设置
    //（编辑器 SceneView 用 ImGui FramebufferScale；播放器用 fbW/winW）。默认 1.0。
    static void setDpiScale(float scale);
    // ===== V1.0-12 §28.2.1 追加（resize/DPI 鼠标映射统一）=====
    // 窗口逻辑坐标（GLFW 光标坐标，左上原点；调用方已减去视口偏移/letterbox 起点）
    // → 布局空间（逻辑像素 = 物理 FBO / setDpiScale）的一次性换算，内容缩放参与：
    //   layout = windowPoint × contentScale / dpiScale。
    // contentScale = 窗口内容缩放（Window::getContentScale / glfwGetWindowContentScale；
    // GLFW_SCALE_TO_MONITOR 下与 ImGui 帧缓冲缩放比、fb/win 比值同源）。
    // 缺省（≤0）取内部 dpiScale——调用方每帧先 setDpiScale(contentScale) 时两者
    // 一致、换算恒等（布局空间 = 窗口逻辑像素）；不一致（如 dpi 设置滞后一帧）时
    // 公式自动补偿，保证与本模块内部 ÷dpiScale 的输入契约自洽。
    // 追加式：既有 processInput/processWheelInput/hitTest 签名与行为不变。
    static glm::vec2 layoutFromWindowPoint(const glm::vec2& windowPoint,
                                           float contentScale = -1.0f);

    // ---- V0.5-13 追加（命中 / 冒泡 /「Play 中 UI 优先于场景拾取」）----
    // 最上层命中：全部 UI 元素 rect（与 render 同一布局公式）中 DFS 画家序最后命中者
    static bool hitTest(const Scene& scene, const glm::vec2& mouse, UIHitResult& out);
    // 任一 UI 元素覆盖该点（「UI 优先于场景拾取」的引擎侧判定入口）
    static bool isPointerOverUI(const Scene& scene, const glm::vec2& mouse);
    // 最近一次 processInput 是否有交互组件消费（onClick/拖拽/滚动）——调用方据此抑制
    // 场景拾取/其它指针行为（编辑器 SceneView Play 分支接线见 §24.5）
    static bool lastPointerConsumed();
};

}  // namespace NovaEngine
