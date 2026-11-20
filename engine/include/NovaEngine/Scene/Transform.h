#pragma once
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include "Component.h"

namespace NovaEngine {

class Transform : public Component {
public:
    explicit Transform(GameObject* owner);

    // 局部 TRS（相对父级）
    const glm::vec3& localPosition() const;
    void setLocalPosition(const glm::vec3& v);
    const glm::quat& localRotation() const;
    void setLocalRotation(const glm::quat& q);
    const glm::vec3& localScale() const;
    void setLocalScale(const glm::vec3& v);
    glm::vec3 localEulerAngles() const;         // 度
    void setLocalEulerAngles(const glm::vec3& eulerDeg);
    glm::mat4 localMatrix() const;   // 按值返回（P1 修复 2026-11-20：thread_local 静态引用被父级递归覆写，见 Transform.cpp）

    // 世界（惰性计算 + 脏标记）
    const glm::mat4& worldMatrix() const;
    glm::vec3 worldPosition() const;
    glm::quat worldRotation() const;

    // 世界方向（forward = +Z，right = +X，up = +Y）
    glm::vec3 right() const;
    glm::vec3 up() const;
    glm::vec3 forward() const;

    // 便捷操作（脚本常用）
    void translate(const glm::vec3& worldDelta);
    void rotate(const glm::vec3& eulerDeg);     // 绕自身轴累计旋转
    void lookAt(const glm::vec3& worldTarget);

private:
    void markDirty();                           // 置自身与全部后代世界矩阵脏
    glm::vec3 m_position{0.0f};
    glm::quat m_rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 m_scale{1.0f};
    mutable glm::mat4 m_world{1.0f};
    mutable bool m_worldDirty = true;
    friend class GameObject;    // setParent 时由 GameObject 调 markDirty 递归置脏
};

}  // namespace NovaEngine
