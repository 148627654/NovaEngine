#pragma once
#include <nlohmann/json_fwd.hpp>
#include <glm/vec3.hpp>
#include "Component.h"

namespace NovaEngine {

// 刚体（V0.3-1）：动态刚体直改 Transform 会被物理覆盖（用 addForce/setVelocity 驱动）
class Rigidbody : public Component {
public:
    explicit Rigidbody(GameObject* owner);
    ~Rigidbody() override;    // 注销 body（定义在 Rigidbody.cpp）

    float mass = 1.0f;
    float linearDamping = 0.0f;
    float angularDamping = 0.05f;
    bool useGravity = true;
    bool isKinematic = false;

    void addForce(const glm::vec3& force);        // Play 中调用
    void addImpulse(const glm::vec3& impulse);
    glm::vec3 velocity() const;
    void setVelocity(const glm::vec3& v);

    void onAwake() override;   // 注册 body（无 Collider 时不建 body，运行期接口安全 no-op）
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

}  // namespace NovaEngine
