#pragma once
#include <glm/vec3.hpp>
#include <nlohmann/json_fwd.hpp>
#include "Component.h"

namespace NovaEngine {

class DirectionalLight : public Component {
public:
    explicit DirectionalLight(GameObject* owner);

    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    // 世界方向 = transform()->forward()
    bool castShadows = true;       // V0.2-6：是否投射阴影
    float shadowBias = 0.0005f;    // V0.2-6：深度偏置（世界单位）
    float shadowRange = 60.0f;     // V0.2-6：正交投影半宽（米）
    int shadowMapSize = 2048;      // V0.2-6：深度贴图边长（Inspector 限幅 512..4096）

    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

}  // namespace NovaEngine
