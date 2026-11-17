#pragma once
#include <functional>
#include <string>
#include <nlohmann/json_fwd.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include "../Scene/Component.h"

namespace NovaEngine {

// 横向滑条（V0.5-13 v1：横向；纵向列后续）。轨道 = size 矩形，手柄沿 X 滑动。
// 布局与 Button 同公式（anchor/offset/size，中心锚）；渲染与命中见 UIRenderer.cpp。
class Slider : public Component {
public:
    explicit Slider(GameObject* owner);

    // 视觉
    std::string backgroundSpritePath;   // 空 = 纯色背景
    std::string fillSpritePath;         // 空 = 纯色填充
    std::string handleSpritePath;       // 空 = 纯色手柄
    glm::vec4 backgroundColor{0.15f, 0.15f, 0.15f, 1.0f};
    glm::vec4 fillColor{0.9f, 0.9f, 0.9f, 1.0f};
    glm::vec4 handleColor{1.0f};
    float fillThickness = 6.0f;         // 轨道/填充条厚度（逻辑像素，垂直居中）
    glm::vec2 handleSize{16.0f, 24.0f}; // 手柄尺寸
    glm::vec2 anchor{0.5f}, offset{0.0f}, size{200.0f, 30.0f};   // 布局（与 Button 同公式）

    // 语义
    float minValue = 0.0f, maxValue = 1.0f;
    float value = 0.5f;                 // 序列化；钳制 [min,max]
    bool wholeNumbers = false;          // true = 值取整
    std::function<void(float)> onValueChanged;   // 脚本回调：拖拽中值变化即触发（连续）

    bool dragging() const;              // 运行期查询

    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;

    // ---- 运行期输入状态（非序列化，同 Button::m_* 裁定）----
    bool m_dragging = false;            // processInput 维护
};

}  // namespace NovaEngine
