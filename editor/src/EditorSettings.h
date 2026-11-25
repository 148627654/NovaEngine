#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <imgui.h>

namespace NovaEditor {

// 编辑器偏好设置（V1.0-12，§28.2）：
// 存储 = %APPDATA%/NovaEngine/editor-settings.json（version 1）——
// 吸附值 / 主题 / 快捷键 / 最近文件 / 多视口。
// 与音频设置严格分离（§26 冻结裁定）：音频 = 引擎级随 assets/ 走（audio-settings.novaaudio），
// 编辑器偏好 = 用户目录，两者不合并。
//
// 生命周期：
//   1. main() 在创建 ImGui 上下文后立即 instance().load()——multiViewport 标志须在
//      ImGui 配置期（首个 NewFrame 之前）生效；
//   2. EditorApplication::init() 把设置应用到运行态（EditorContext.snapSettings / 主题 /
//      ImGuiStyle 覆盖 / recentFiles）；
//   3. EditorApplication::update() 每帧做运行态 → 设置数据回写同步（dirty 标记），
//      按 0.5s 节流落盘；shutdown() 强制落盘。
class EditorSettings {
public:
    struct Data {
        int version = 1;
        // 吸附（Inspector 无选中时 Snap Settings 面板同源读写；GizmoController 消费）
        float snapMove = 0.5f;     // 平移吸附步长（米，逐轴）
        float snapRotate = 15.0f;  // 旋转吸附步长（度）
        float snapScale = 0.5f;    // 缩放吸附步长
        // 主题（dark / light / classic）+ ImGuiStyle 自定义覆盖（v1：颜色全表 + 圆角）
        std::string theme = "dark";
        bool styleCustom = false;
        std::vector<uint32_t> styleColors;   // ImGuiCol_COUNT 打包 ImU32；空 = 未自定义
        float windowRounding = -1.0f;        // <0 = 不覆盖
        float frameRounding = -1.0f;         // <0 = 不覆盖
        // 快捷键：动作名 → chord 字符串（"Ctrl+P" / "Ctrl+Shift+R" / "F10"）。
        // 动作集合 = 静态核心表（Play/Stop、Save、Undo、Redo、New、Open、Step、Reload Scripts），
        // 不可移除（defaultKeybindings 恒为全量）；chord 可重绑，冲突检测见 SettingsPanel。
        std::unordered_map<std::string, std::string> keybindings;
        // 最近文件（≤10，新在前；绝对路径；加载时做文件存在性过滤）
        std::vector<std::string> recentFiles;
        // 多视口（ImGui ViewportsEnable）：重启编辑器生效（main 在 ImGui 配置期读取）
        bool multiViewport = false;
    };

    static EditorSettings& instance();

    static std::filesystem::path filePath();  // %APPDATA%/NovaEngine/editor-settings.json

    void load();      // 缺失/损坏 → 默认值 + warn（不崩溃，不影响启动）
    void save() const;  // 目录不存在则创建；写失败 warn
    void markDirty() { m_dirty = true; }
    bool dirty() const { return m_dirty; }
    void clearDirty() { m_dirty = false; }

    // ---- 快捷键静态表与 chord 工具（V1.0-12）----
    // 核心动作 → 默认 chord（动作集合固定不可移除；chord 可重绑）
    static const std::vector<std::pair<std::string, std::string>>& defaultKeybindings();
    // 当前生效 chord（data.keybindings 缺失/非法 → 默认值兜底；action 未知 → 空串）
    static std::string chordFor(const std::string& action);
    // chord 解析："Ctrl+Shift+R" → 修饰键 + 键码（Key 枚举底层值）。非法返回 false。
    static bool parseChord(const std::string& chord, bool& ctrl, bool& shift, bool& alt, int& key);
    // 当前输入帧是否命中 chord（修饰键按住 + 主键本帧按下；空串恒 false）
    static bool chordMatches(const std::string& chord);
    // 冲突检测：返回 data 中已占用该 chord 的其他动作名（无冲突返回空串；chord 非法返回原样提示）
    static std::string checkConflict(const Data& d, const std::string& action,
                                     const std::string& chord);

    // ---- 主题应用（ImGui 上下文存活期内调用）----
    static void applyTheme(const std::string& theme);        // 预置：dark/light/classic
    static void applyStyleCustom(const Data& d);             // 自定义颜色/圆角覆盖（styleCustom 时）
    // 把当前 ImGuiStyle 捕获进 d（SettingsPanel「保存当前样式」按钮用）
    static void captureStyle(Data& d);

    Data data;

private:
    EditorSettings() = default;
    bool m_dirty = false;
};

}  // namespace NovaEditor
