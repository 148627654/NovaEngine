#include "EditorCamera.h"

#include <NovaEngine/Core/Input.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace NovaEditor {

namespace {

// 视口内每像素对应的世界距离（中键平移灵敏度，随 distance 缩放）
float worldPerPixel(float distance, float fovDeg, float viewportHeight) {
    if (viewportHeight <= 0.0f) return 0.0f;
    return 2.0f * distance * std::tan(glm::radians(fovDeg) * 0.5f) / viewportHeight;
}

glm::vec3 rightVector(const EditorCamera& cam) {
    return glm::normalize(glm::cross(cam.forward(), glm::vec3(0.0f, 1.0f, 0.0f)));
}

glm::vec3 upVector(const EditorCamera& cam) {
    return glm::normalize(glm::cross(rightVector(cam), cam.forward()));
}

}  // namespace

void EditorCamera::update(float dt, const glm::vec2&, const glm::vec2& viewportSize,
                          bool uiConsumedWheel) {
    using namespace NovaEngine;

    // 滚轮缩放：速度按 distance 缩放（远时快、近时慢）。
    // Edit 态 UI 滚轮优先（V0.5 验收补强）：ScrollView 已消费本帧滚轮 → 相机不动
    const float scroll = uiConsumedWheel ? 0.0f : Input::scrollDelta().y;
    if (scroll != 0.0f) {
        distance *= std::exp(-scroll * 0.12f);
        distance = std::clamp(distance, 0.1f, 1000.0f);
    }

    // 右键拖拽旋转（yaw/pitch，弧度）
    if (Input::isMousePressed(MouseButton::Right)) m_rotating = true;
    if (Input::isMouseReleased(MouseButton::Right)) m_rotating = false;
    if (m_rotating) {
        const glm::vec2 delta = Input::mouseDelta();
        yaw += delta.x * 0.01f;
        pitch += delta.y * 0.01f;
        pitch = std::clamp(pitch, -1.55f, 1.55f);  // 避开万向锁极点
    }

    // 中键拖拽平移（抓取式：场景跟随鼠标方向移动）
    if (Input::isMousePressed(MouseButton::Middle)) m_panning = true;
    if (Input::isMouseReleased(MouseButton::Middle)) m_panning = false;
    if (m_panning) {
        const glm::vec2 delta = Input::mouseDelta();
        const float wpp = worldPerPixel(distance, fov, viewportSize.y);
        target -= rightVector(*this) * delta.x * wpp;
        target += upVector(*this) * delta.y * wpp;
    }

    // WASD 平移（沿相机平面；Shift 加速）——须按住右键（Unity flythrough 同款）。
    // 2026-11-17 修复：W/E/R 已被 gizmo 模式切换占用，裸按（视口悬停即生效）
    // 会边切位移模式边把镜头推飞（用户报障「W 位移整个镜头会移动」）。
    // 右键拖拽旋转 + WASD 飞行组合保持不变。
    if (!Input::isMouseDown(MouseButton::Right)) return;
    float speed = distance * 0.3f;
    if (Input::isKeyDown(Key::LeftShift) || Input::isKeyDown(Key::RightShift)) speed *= 3.0f;
    const glm::vec3 fwd = forward();
    const glm::vec3 fwdFlat = glm::normalize(glm::vec3(fwd.x, 0.0f, fwd.z));
    const glm::vec3 right = glm::normalize(glm::cross(fwdFlat, glm::vec3(0.0f, 1.0f, 0.0f)));
    if (Input::isKeyDown(Key::W)) target += fwdFlat * speed * dt;
    if (Input::isKeyDown(Key::S)) target -= fwdFlat * speed * dt;
    if (Input::isKeyDown(Key::A)) target -= right * speed * dt;
    if (Input::isKeyDown(Key::D)) target += right * speed * dt;
}

void EditorCamera::focus(const glm::vec3& newTarget, float newDistance) {
    target = newTarget;
    distance = std::clamp(newDistance, 0.1f, 1000.0f);
}

glm::vec3 EditorCamera::position() const {
    // 球坐标：pitch 为负时相机高于 target（默认 -0.35 即经典俯视编辑视图）
    const glm::vec3 offset(
        distance * std::cos(pitch) * std::sin(yaw),
        -distance * std::sin(pitch),
        distance * std::cos(pitch) * std::cos(yaw));
    return target + offset;
}

glm::vec3 EditorCamera::forward() const {
    return glm::normalize(target - position());
}

glm::mat4 EditorCamera::viewMatrix() const {
    return glm::lookAt(position(), target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 EditorCamera::projectionMatrix(float aspect) const {
    return glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
}

std::pair<glm::vec3, glm::vec3> EditorCamera::screenPointToRay(
    const glm::vec2& mousePos, const glm::vec2& viewportPos, const glm::vec2& viewportSize) const {
    if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) {
        const glm::vec3 pos = position();
        return {pos, forward()};
    }
    // 屏幕坐标（左上原点）→ NDC
    glm::vec2 ndc;
    ndc.x = (mousePos.x - viewportPos.x) / viewportSize.x * 2.0f - 1.0f;
    ndc.y = 1.0f - (mousePos.y - viewportPos.y) / viewportSize.y * 2.0f;

    const glm::mat4 invViewProj =
        glm::inverse(projectionMatrix(viewportSize.x / viewportSize.y) * viewMatrix());
    glm::vec4 nearP = invViewProj * glm::vec4(ndc, -1.0f, 1.0f);
    glm::vec4 farP = invViewProj * glm::vec4(ndc, 1.0f, 1.0f);
    nearP /= nearP.w;
    farP /= farP.w;

    const glm::vec3 origin(nearP);
    const glm::vec3 dir = glm::normalize(glm::vec3(farP) - origin);
    return {origin, dir};
}

glm::vec3 EditorCamera::intersectGround(const glm::vec2& mousePos,
                                        const glm::vec2& viewportPos, const glm::vec2& viewportSize) const {
    const auto [origin, dir] = screenPointToRay(mousePos, viewportPos, viewportSize);
    // 与 y=0 平面求交（射线向下才可能命中）
    if (std::abs(dir.y) > 1e-5f && dir.y < 0.0f) {
        const float t = -origin.y / dir.y;
        if (t > 0.0f) return origin + dir * t;
    }
    // 无交点：相机 target 前方 3m
    return target + forward() * 3.0f;
}

}  // namespace NovaEditor
