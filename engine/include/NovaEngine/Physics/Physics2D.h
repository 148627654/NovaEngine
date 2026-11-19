#pragma once
#include <glm/vec2.hpp>

namespace NovaEngine {

class Scene;
class GameObject;

// 2D 碰撞求解（V0.5-10 v1）：每帧（Play）对全部 BoxCollider2D 做 AABB 重叠对检测
//（O(n²) 上限 500 组件——mini 引擎规模可接受，验收 < 0.5ms）→ 事件回调：
// OnCollision2DEnter/Stay/Exit、OnTrigger2DEnter/Stay/Exit（复用 Events 类型化事件；
// Enter/Exit 边沿由内部状态表维护）。
struct Collision2DEvent {
    GameObject* other = nullptr;   // 对方 GameObject（对方已销毁/已删除时投 Exit 为 nullptr）
    glm::vec2 overlap{0.0f};       // X/Y 两轴穿透深度（重叠时 > 0；Exit 恒为 0）
};

// 六种类型化事件（V0.5-10 c8）：脚本用 Behaviour::subscribeEvent<Collision2DEnterEvent>(...) 订阅，
// 每个重叠对两侧各投递一次（other 指向对方）。触发规则（Unity 语义）：
//   双方均非 Trigger → Collision2D 系列；至少一方 isTrigger → Trigger2D 系列（两类事件独立）。
// 运行时切换 isTrigger：先按旧类别投 Exit、再按新类别投 Enter。
struct Collision2DEnterEvent : Collision2DEvent {};
struct Collision2DStayEvent : Collision2DEvent {};
struct Collision2DExitEvent : Collision2DEvent {};
struct Trigger2DEnterEvent : Collision2DEvent {};
struct Trigger2DStayEvent : Collision2DEvent {};
struct Trigger2DExitEvent : Collision2DEvent {};

class Physics2D {
public:
    static void step(Scene& scene);   // Scene::update 内、onUpdate 广播后调用（固定点，契约）
};

}  // namespace NovaEngine
