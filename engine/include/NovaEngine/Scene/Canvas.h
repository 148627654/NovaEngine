#pragma once
#include <nlohmann/json_fwd.hpp>
#include "Component.h"

namespace NovaEngine {

// 运行时 UI 子树根组件（V0.3-3）：屏幕空间恒全屏，Transform 忽略
class Canvas : public Component {
public:
    explicit Canvas(GameObject* owner);

    float uiScale = 1.0f;                         // v1 预留

    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

}  // namespace NovaEngine
