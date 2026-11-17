#pragma once
#include <nlohmann/json_fwd.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
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
    // 像素；正 = 内容向上/左移（看更下面/右边的内容，滚轮向下为正方向）；序列化（位置记忆）
    glm::vec2 scrollOffset{0.0f};
    // 可视区外框（V0.5 验收补强）：背景填充 + 描边——无外框的纯裁剪窗口在场景里不可见，
    // 验收者找不到滚动区域。旧文件缺字段走默认值（序列化往返无损）。
    glm::vec4 bgColor{0.10f, 0.11f, 0.13f, 0.85f};
    glm::vec4 borderColor{0.42f, 0.45f, 0.52f, 1.0f};
    float borderThickness = 1.5f;        // 逻辑像素

    glm::vec2 contentSize() const;       // 内容包围盒（渲染期重算：子物体 UI rect 并集；脚本查询）

    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;

    // ---- 运行期缓存（非序列化，同 Button::m_* 裁定）：UIRenderer 渲染/命中遍历期重算 ----
    glm::vec2 m_contentSize{0.0f};       // 逻辑像素；contentSize() 查询缓存
    // 内容包围盒在画布空间的原位边界（平移前；逻辑像素 × uiScale 单位与 walker 一致）——
    // 滚动钳制依据：maxOffset = 内容底对齐视口底，minOffset = 内容顶对齐视口顶（≤0）
    glm::vec2 m_contentBoxMin{0.0f};
    glm::vec2 m_contentBoxMax{0.0f};
};

}  // namespace NovaEngine
