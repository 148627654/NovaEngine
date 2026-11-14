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

    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

}  // namespace NovaEngine
