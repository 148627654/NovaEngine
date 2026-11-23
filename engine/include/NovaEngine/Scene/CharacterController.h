#pragma once
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <NovaEngine/Scene/Component.h>

namespace NovaEngine {

// ===== V1.0-3 CharacterController（§27.2 冻结接口，2026-11-23 波 2 骨架日布）=====
// 选型裁定：封装 Jolt CharacterVirtual（楼梯步进/坡度限制/地面检测库内现成）——
// 库能力消费，非新依赖。头文件签名冻结：物理行为实现归 cR
//（engine/src/Scene/CharacterController.cpp）；骨架日桩已注册组件并实现序列化
//（§27.2 键名），cR 替换物理部分，签名变更报 Architect。
class CharacterController : public Component {
public:
    explicit CharacterController(GameObject* owner);
    float radius = 0.4f, height = 1.8f;          // 胶囊
    float stepHeight = 0.35f;                    // 楼梯步进高度
    float slopeLimitDeg = 45.0f;                 // 坡度上限（超过 = 阻挡）
    float mass = 70.0f, gravityScale = 1.0f;
    bool isGrounded() const;                     // Play 中有效
    void move(const glm::vec3& displacement);    // Play 中；Edit no-op（与物理组件同纪律）
    glm::vec3 velocity() const;
    void setVelocity(const glm::vec3& v);
    void onAwake() override; void onUpdate(float dt) override;
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
    // 注：与 Rigidbody 互斥（同 GO 挂两者 = 启动 warn + CharacterController 优先）

private:
    glm::vec3 m_velocity{0.0f};   // 骨架日补：velocity 存取（cR 实现 CharacterVirtual 后由物理驱动）
};

}  // namespace NovaEngine
