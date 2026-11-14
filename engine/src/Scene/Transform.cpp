#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scene/GameObject.h>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace NovaEngine {

Transform::Transform(GameObject* owner) : Component(owner) {}

const glm::vec3& Transform::localPosition() const {
    return m_position;
}

void Transform::setLocalPosition(const glm::vec3& v) {
    m_position = v;
    markDirty();
}

const glm::quat& Transform::localRotation() const {
    return m_rotation;
}

void Transform::setLocalRotation(const glm::quat& q) {
    m_rotation = glm::normalize(q);
    markDirty();
}

const glm::vec3& Transform::localScale() const {
    return m_scale;
}

void Transform::setLocalScale(const glm::vec3& v) {
    m_scale = v;
    markDirty();
}

glm::vec3 Transform::localEulerAngles() const {
    // glm::eulerAngles 返回弧度，对外接口约定为度
    return glm::degrees(glm::eulerAngles(m_rotation));
}

void Transform::setLocalEulerAngles(const glm::vec3& eulerDeg) {
    m_rotation = glm::quat(glm::radians(eulerDeg));
    markDirty();
}

const glm::mat4& Transform::localMatrix() const {
    // TRS 合成：T * R * S
    static thread_local glm::mat4 s_local{1.0f};
    const glm::mat4 t = glm::translate(glm::mat4(1.0f), m_position);
    const glm::mat4 r = glm::mat4_cast(m_rotation);
    const glm::mat4 s = glm::scale(glm::mat4(1.0f), m_scale);
    s_local = t * r * s;
    return s_local;
}

const glm::mat4& Transform::worldMatrix() const {
    // 惰性计算：parent ? parent->worldMatrix() * localMatrix() : localMatrix()
    if (m_worldDirty) {
        const GameObject* go = gameObject();
        if (go != nullptr && go->parent() != nullptr && go->parent()->transform() != nullptr)
            m_world = go->parent()->transform()->worldMatrix() * localMatrix();
        else
            m_world = localMatrix();
        m_worldDirty = false;
    }
    return m_world;
}

glm::vec3 Transform::worldPosition() const {
    const glm::mat4& w = worldMatrix();
    return glm::vec3(w[3]);
}

glm::quat Transform::worldRotation() const {
    const GameObject* go = gameObject();
    if (go != nullptr && go->parent() != nullptr && go->parent()->transform() != nullptr)
        return go->parent()->transform()->worldRotation() * m_rotation;
    return m_rotation;
}

glm::vec3 Transform::right() const {
    return worldRotation() * glm::vec3(1.0f, 0.0f, 0.0f);
}

glm::vec3 Transform::up() const {
    return worldRotation() * glm::vec3(0.0f, 1.0f, 0.0f);
}

glm::vec3 Transform::forward() const {
    return worldRotation() * glm::vec3(0.0f, 0.0f, 1.0f);
}

void Transform::translate(const glm::vec3& worldDelta) {
    // 世界空间平移：有父级时把新世界坐标反变换回局部
    const glm::vec3 wp = worldPosition() + worldDelta;
    const GameObject* go = gameObject();
    if (go != nullptr && go->parent() != nullptr && go->parent()->transform() != nullptr) {
        const glm::mat4 invParent = glm::inverse(go->parent()->transform()->worldMatrix());
        m_position = glm::vec3(invParent * glm::vec4(wp, 1.0f));
    } else {
        m_position = wp;
    }
    markDirty();
}

void Transform::rotate(const glm::vec3& eulerDeg) {
    // 绕自身轴累计旋转：四元数右乘（局部空间旋转）
    m_rotation = glm::normalize(m_rotation * glm::quat(glm::radians(eulerDeg)));
    markDirty();
}

void Transform::lookAt(const glm::vec3& worldTarget) {
    // 让自身 forward（+Z）指向目标；用正交基显式构建（右手系），避免左手系混淆
    const glm::vec3 wp = worldPosition();
    const glm::vec3 dir = worldTarget - wp;
    if (glm::dot(dir, dir) < 1e-10f)
        return;  // 目标与自身重合，忽略
    const glm::vec3 f = glm::normalize(dir);
    // 世界 up 与 forward 近平行时回退到 +X 作参考轴
    glm::vec3 ref = glm::vec3(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(ref, f)) > 0.999f)
        ref = glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 r = glm::normalize(glm::cross(ref, f));
    const glm::vec3 u = glm::cross(f, r);
    const glm::mat3 basis(r, u, f);  // 列主序：列 = r/u/f
    const glm::quat worldRot = glm::normalize(glm::quat_cast(basis));

    // 换算回局部旋转（有父级时除掉父级世界旋转）
    const GameObject* go = gameObject();
    if (go != nullptr && go->parent() != nullptr && go->parent()->transform() != nullptr)
        m_rotation = glm::inverse(go->parent()->transform()->worldRotation()) * worldRot;
    else
        m_rotation = worldRot;
    markDirty();
}

void Transform::markDirty() {
    // 置自身与全部后代世界矩阵脏（setParent 时由 GameObject 递归调用）
    m_worldDirty = true;
    const GameObject* go = gameObject();
    if (go == nullptr)
        return;
    for (const auto& child : go->children()) {
        if (Transform* t = child->transform())
            t->markDirty();
    }
}

}  // namespace NovaEngine
