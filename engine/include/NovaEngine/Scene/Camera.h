#pragma once
#include <glm/vec4.hpp>
#include <nlohmann/json_fwd.hpp>
#include "Component.h"

namespace NovaEngine {

// 投影模式（V0.5-10 2D，§25.8 冻结段，骨架日 c0 布）
enum class ProjectionMode { Perspective, Orthographic };

class Camera : public Component {
public:
    explicit Camera(GameObject* owner);

    float fov = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    glm::vec4 clearColor = glm::vec4(0.18f, 0.22f, 0.35f, 1.0f);
    bool isMain = false;    // Play 时优先使用

    // ===== V0.5-10 2D 投影（追加；序列化键由 c8 在 Camera.cpp 补，缺省 = 旧行为零变化）=====
    ProjectionMode projectionMode = ProjectionMode::Perspective;
    float orthoSize = 5.0f;                                        // 正交半高（米；Unity 语义）

    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

}  // namespace NovaEngine
