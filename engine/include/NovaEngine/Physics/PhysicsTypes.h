#pragma once
#include <glm/vec3.hpp>

namespace NovaEngine {

class GameObject;

// 射线命中结果（Physics → scene 依赖合法；公共头无 Jolt 类型）
struct RaycastHit {
    bool hit = false;
    GameObject* gameObject = nullptr;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f};
    float distance = 0.0f;
};

// 触发器事件（V0.3-1）：PhysicsWorld 帧末 post；脚本 subscribeEvent<TriggerEvent> 接收
struct TriggerEvent {
    bool entered = false;                    // true=Enter，false=Exit
    GameObject* gameObjectA = nullptr;       // 自身
    GameObject* gameObjectB = nullptr;       // 对方
};

}  // namespace NovaEngine
