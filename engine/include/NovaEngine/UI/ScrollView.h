#pragma once
#include <nlohmann/json_fwd.hpp>
#include <glm/vec2.hpp>
#include "../Scene/Component.h"

namespace NovaEngine {

// 滚动容器（V0.5-13 v1）：内容 = 直接子物体（GameObject 子树）的 UI 组件；
// 可视区 = anchor/offset/size 矩形；渲染期 scissor 裁剪（嵌套取交集）；滚轮滚动。
// v1 不做内容自动重排——内容布局由创作者摆放，滚动仅平移视口。
class ScrollView : public Component {
public:
    explicit ScrollView(GameObject* owner);

    glm::vec2 anchor{0.5f}, offset{0.0f}, size{300.0f, 200.0f};   // 可视区
    bool scrollHorizontal = false;
    bool scrollVertical = true;
    float scrollSensitivity = 20.0f;     // 滚轮每格像素（Input::scrollDelta 消费）
    glm::vec2 scrollOffset{0.0f};        // 像素（正 = 内容向右/下移）；序列化（位置记忆）

    glm::vec2 contentSize() const;       // 内容包围盒（渲染期重算：子物体 UI rect 并集；脚本查询）

    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;

    // ---- 运行期缓存（非序列化，同 Button::m_* 裁定）：UIRenderer 渲染/命中遍历期重算 ----
    glm::vec2 m_contentSize{0.0f};       // 逻辑像素；contentSize() 查询缓存
};

}  // namespace NovaEngine
