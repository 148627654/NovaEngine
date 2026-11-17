#pragma once
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <utility>

namespace NovaEditor {

// 轨道编辑器相机（§2.9）：
//   右键拖拽旋转 yaw/pitch；中键拖拽或 WASD 平移；滚轮按 distance 缩放；F 聚焦。
// 进入 Play 后 SceneView 不再调用 update()（输入被暂停）。
class EditorCamera {
public:
    // 每帧输入处理（仅在视口悬停或交互进行中由 SceneView 调用）
    // uiConsumedWheel：本帧 UI（ScrollView）已消费滚轮 → 跳过相机缩放（Edit 态 UI 滚轮优先）
    void update(float dt, const glm::vec2& viewportPos, const glm::vec2& viewportSize,
                bool uiConsumedWheel = false);

    // F 聚焦：只改 target/distance，不动 pitch/yaw（无插值，mini 简化）
    void focus(const glm::vec3& target, float distance);

    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float aspect) const;
    glm::vec3 position() const;
    glm::vec3 forward() const;   // 视线方向（target - position）

    // 屏幕坐标（左上原点，与 ImGui 一致）→ 世界射线 (origin, direction)
    std::pair<glm::vec3, glm::vec3> screenPointToRay(const glm::vec2& mousePos,
        const glm::vec2& viewportPos, const glm::vec2& viewportSize) const;

    // 射线与 y=0 平面交点；无交点时回退 target 前方 3m（模型导入落点用，§6.4）
    glm::vec3 intersectGround(const glm::vec2& mousePos,
        const glm::vec2& viewportPos, const glm::vec2& viewportSize) const;

    // 是否正在拖拽（旋转/平移中）——供 SceneView 在鼠标离开视口后继续处理输入
    bool isInteracting() const { return m_rotating || m_panning; }

    float fov = 60.0f, nearPlane = 0.1f, farPlane = 1000.0f;
    float distance = 8.0f, yaw = 0.0f, pitch = -0.35f;
    glm::vec3 target = glm::vec3(0.0f, 1.0f, 0.0f);

private:
    bool m_rotating = false;  // 右键拖拽中
    bool m_panning = false;   // 中键拖拽中
};

}  // namespace NovaEditor
