#pragma once
#include <glm/vec4.hpp>
#include <nlohmann/json_fwd.hpp>
#include "Component.h"

namespace NovaEngine {

class Camera : public Component {
public:
    explicit Camera(GameObject* owner);

    float fov = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    glm::vec4 clearColor = glm::vec4(0.18f, 0.22f, 0.35f, 1.0f);
    bool isMain = false;    // Play 时优先使用

    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

}  // namespace NovaEngine
