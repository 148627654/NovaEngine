#pragma once
#include <functional>
#include <nlohmann/json_fwd.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include "Component.h"

namespace NovaEngine {

// 运行时 UI 按钮（V0.3-3）：含 Image 全部字段（组合而非继承）
class Button : public Component {
public:
    explicit Button(GameObject* owner);

    glm::vec4 color{1.0f}, hoverColor{0.9f}, pressedColor{0.8f};
    glm::vec4 slice9{0.0f};
    glm::vec2 anchor{0.5f}, offset{0.0f}, size{100.0f};
    std::function<void()> onClick;                // 脚本赋值（C++ 内嵌脚本无 Inspector 事件绑定）
    bool hovered() const;                         // 脚本 onUpdate 查询
    bool pressedThisFrame() const;

    // ---- 运行期输入状态（V0.3-3 裁定：非序列化字段存组件内，序列化不写）----
    // UIRenderer::processInput 每帧整体复位后重算；render 仅在本帧有 processInput
    // （Play）时消费三色组，Edit 预览一律基色（防退出 Play 后 hover 色残留）。
    bool m_hovered = false;         // 最近一次 processInput 指针命中
    bool m_pressed = false;         // 左键按住且命中
    bool m_pressedEdge = false;     // 本帧按下边沿（onClick 已触发）

    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

}  // namespace NovaEngine
