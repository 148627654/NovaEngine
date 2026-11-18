#pragma once
#include <string>
#include <vector>
#include <nlohmann/json_fwd.hpp>
#include <NovaEngine/Scene/Component.h>
#include "AnimatorController.h"

namespace NovaEngine {

// 动画状态机组件（V0.5-9）：参数驱动状态过渡 + 过渡混合；v1 列表式配置（内联 JSON，无独立资产）。
class Animator : public Component {
public:
    explicit Animator(GameObject* owner);
    std::vector<AnimatorState> states;
    int defaultState = 0;
    std::vector<AnimatorTransition> transitions;
    std::vector<AnimatorParameter> parameters;

    void setFloat(const std::string& name, float v);    // 脚本 API（Play 中；对齐 Unity 心智）
    void setBool(const std::string& name, bool v);
    void setTrigger(const std::string& name);
    float getFloat(const std::string& name) const;
    const std::string& currentStateName() const;
    void play(const std::string& stateName);   // 直接跳转（无混合）

    void onUpdate(float dt) override;          // 过渡判定/参数消费/混合推进/骨骼矩阵上传
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

}  // namespace NovaEngine
