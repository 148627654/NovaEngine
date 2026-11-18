#pragma once
#include <glm/vec2.hpp>

namespace NovaEngine {

class Scene;
class GameObject;

// 2D 碰撞求解（V0.5-10 v1）：每帧（Play）对全部 BoxCollider2D 做 AABB 重叠对检测
//（O(n²) 上限 500 组件——mini 引擎规模可接受，验收 < 0.5ms）→ 事件回调：
// OnCollision2DEnter/Stay/Exit、OnTrigger2DEnter/Stay/Exit（复用 Events 类型化事件；
// Enter/Exit 边沿由内部状态表维护）。
struct Collision2DEvent { GameObject* other = nullptr; glm::vec2 overlap{0.0f}; };
class Physics2D {
public:
    static void step(Scene& scene);   // Scene::update 内、onUpdate 广播后调用（固定点，契约）
};

}  // namespace NovaEngine
