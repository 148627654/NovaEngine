// Physics2D.cpp —— 2D 碰撞求解（V0.5-10 v1，§25.8）
//
// 每帧（Play）扫描场景全部 BoxCollider2D（activeInHierarchy）做 AABB 重叠对检测：
//   O(n²) 全对检测，组件数上限 500（超限截断并警告一次；§25.8 验收 < 0.5ms）；
//   世界 AABB = 局部盒 4 角点经世界矩阵变换取 X/Y 包围盒（位置/旋转/均匀缩放精确，旋转取保守盒）。
// Enter/Stay/Exit 边沿由进程级状态表维护：配对键 = 碰撞体指针对（指针复用经 GO id 校验防护）；
// 分离（或 isTrigger 运行时切换）→ 投 Exit；场景切换 → 静默剪枝（对象已随场景销毁）。
// 事件经 Events::publish 同步投递（publish 内部拷贝 handler 快照，防重入；脚本可即时响应）。
// 独立于 Jolt（不注册进 PhysicsWorld；2D 与 3D 物理共存不互扰）。
#include <NovaEngine/Physics/Physics2D.h>

#include <NovaEngine/Core/Events.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/BoxCollider2D.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/Transform.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace NovaEngine {

namespace {

constexpr size_t kMaxColliders = 500;   // §25.8 上限（mini 引擎规模；超限截断）

// 单帧参与检测的碰撞体快照（世界 AABB + 事件投递所需身份信息）
struct Collider2DSnapshot {
    const BoxCollider2D* collider = nullptr;
    GameObject* go = nullptr;
    uint64_t goId = 0;
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};
    bool trigger = false;
};

// 配对状态（状态表值）：Enter/Exit 边沿判定 + 场景切换剪枝 + 指针复用校验
struct PairState {
    uint64_t aId = 0, bId = 0;   // 两侧 GameObject id（进程内唯一，销毁后 findGameObject 得 nullptr）
    Scene* scene = nullptr;      // 归属场景：step 换场景 → 旧配对静默剪枝
    bool trigger = false;
};

// 配对键 = 碰撞体指针对（规范序）。碰撞体销毁后内存复用风险：
// 命中状态表时校验 GO id 与 trigger 一致，不一致按新配对处理（旧项由边沿循环补投 Exit）
struct PairKey {
    const BoxCollider2D* a = nullptr;
    const BoxCollider2D* b = nullptr;
    bool operator==(const PairKey& o) const { return a == o.a && b == o.b; }
};
struct PairKeyHash {
    size_t operator()(const PairKey& k) const {
        return std::hash<const BoxCollider2D*>{}(k.a) ^
               (std::hash<const BoxCollider2D*>{}(k.b) << 1);
    }
};

using PairTable = std::unordered_map<PairKey, PairState, PairKeyHash>;

// 进程级状态表（函数内静态防初始化顺序问题；生命周期 = 进程——Play 退出残留项
// 在下次 step 由「场景指针不一致」或「配对键不再重叠」路径清理）
PairTable& pairTable() {
    static PairTable table;
    return table;
}

bool overlaps(const Collider2DSnapshot& a, const Collider2DSnapshot& b) {
    return a.min.x < b.max.x && b.min.x < a.max.x && a.min.y < b.max.y && b.min.y < a.max.y;
}

glm::vec2 overlapOf(const Collider2DSnapshot& a, const Collider2DSnapshot& b) {
    return glm::vec2(std::min(a.max.x, b.max.x) - std::max(a.min.x, b.min.x),
                     std::min(a.max.y, b.max.y) - std::max(a.min.y, b.min.y));
}

// 向一侧投递事件：other 经 id 反查（对方已销毁 → nullptr，脚本判空即可；自身必存活）
template<typename E>
void publishToSide(Scene& scene, uint64_t /*selfId*/, uint64_t otherId, const glm::vec2& overlap) {
    E event;
    event.other = scene.findGameObject(otherId);
    event.overlap = overlap;
    Events::publish(event);
}

// 配对两侧各投递一次（Enter/Stay/Exit 共用；两侧 other 互为对方）
template<typename E>
void publishPair(Scene& scene, const PairState& st, const glm::vec2& overlap) {
    publishToSide<E>(scene, st.aId, st.bId, overlap);
    publishToSide<E>(scene, st.bId, st.aId, overlap);
}

// 旧状态表中本帧未再重叠（或场景已切换）的配对：补投 Exit 后移除。
// 场景切换（st.scene != 当前）静默剪枝：对象已随旧场景销毁，无存活订阅者
void fireExitsForVanishedPairs(Scene& scene, PairTable& table, const PairTable& cur) {
    for (auto it = table.begin(); it != table.end();) {
        const PairState& st = it->second;
        auto curIt = cur.find(it->first);
        const bool keyStillPresent = curIt != cur.end();
        // 键仍在但身份/类别变化（指针复用或 isTrigger 运行时切换）→ 同样补投旧类别 Exit
        const bool stateChanged = keyStillPresent &&
            (curIt->second.aId != st.aId || curIt->second.bId != st.bId ||
             curIt->second.trigger != st.trigger || curIt->second.scene != st.scene);
        if (keyStillPresent && !stateChanged) {
            ++it;
            continue;
        }
        if (st.scene == &scene) {
            GameObject* a = scene.findGameObject(st.aId);
            GameObject* b = scene.findGameObject(st.bId);
            if (st.trigger) {
                if (a) publishToSide<Trigger2DExitEvent>(scene, st.aId, st.bId, glm::vec2(0.0f));
                if (b) publishToSide<Trigger2DExitEvent>(scene, st.bId, st.aId, glm::vec2(0.0f));
            } else {
                if (a) publishToSide<Collision2DExitEvent>(scene, st.aId, st.bId, glm::vec2(0.0f));
                if (b) publishToSide<Collision2DExitEvent>(scene, st.bId, st.aId, glm::vec2(0.0f));
            }
        }
        it = table.erase(it);
    }
}

}  // namespace

void Physics2D::step(Scene& scene) {
    // 1. 收集快照：activeInHierarchy + BoxCollider2D + Transform（缺 Transform 跳过——
    //    与渲染收集同约定，畸形场景不崩溃）；上限 500，超限截断并警告一次
    std::vector<Collider2DSnapshot> colliders;
    scene.forEachObject([&](GameObject* go) {
        if (!go->activeInHierarchy()) return;
        const Transform* t = go->transform();
        if (!t) return;
        for (const auto* c : go->getComponents<BoxCollider2D>()) {
            if (colliders.size() >= kMaxColliders) {
                static bool warned = false;
                if (!warned) {
                    Logger::warn("[Physics2D] BoxCollider2D 超过 {} 上限，超出部分本帧不参与检测",
                                 kMaxColliders);
                    warned = true;
                }
                return;
            }
            Collider2DSnapshot snap;
            snap.collider = c;
            snap.go = go;
            snap.goId = go->id();
            snap.trigger = c->isTrigger;
            // 局部盒 4 角点（2D 平面 = 世界 XY，z=0）经世界矩阵变换取 X/Y 包围盒
            const glm::mat4& w = t->worldMatrix();
            const float hx = c->size.x * 0.5f, hy = c->size.y * 0.5f;
            const glm::vec2 corners[4] = {
                {c->center.x - hx, c->center.y - hy},
                {c->center.x + hx, c->center.y - hy},
                {c->center.x + hx, c->center.y + hy},
                {c->center.x - hx, c->center.y + hy},
            };
            snap.min = glm::vec2(std::numeric_limits<float>::max());
            snap.max = glm::vec2(std::numeric_limits<float>::lowest());
            for (const glm::vec2& p : corners) {
                const glm::vec4 wc = w * glm::vec4(p.x, p.y, 0.0f, 1.0f);
                snap.min.x = std::min(snap.min.x, wc.x);
                snap.min.y = std::min(snap.min.y, wc.y);
                snap.max.x = std::max(snap.max.x, wc.x);
                snap.max.y = std::max(snap.max.y, wc.y);
            }
            colliders.push_back(snap);
        }
    });

    // 2. O(n²) 重叠对（上限 500 → 最多 124750 对；纯 AABB 比较，远低于 0.5ms 验收线）
    std::vector<std::pair<size_t, size_t>> overlapPairs;
    if (colliders.size() >= 2) {
        overlapPairs.reserve(colliders.size() * 2);
        for (size_t i = 0; i < colliders.size(); ++i)
            for (size_t j = i + 1; j < colliders.size(); ++j)
                if (overlaps(colliders[i], colliders[j]))
                    overlapPairs.emplace_back(i, j);
    }

    // 3. 本帧配对表 + Enter/Stay 边沿判定
    PairTable cur;
    cur.reserve(overlapPairs.size() * 2);
    auto& table = pairTable();
    for (const auto& [i, j] : overlapPairs) {
        const Collider2DSnapshot& A = colliders[i];
        const Collider2DSnapshot& B = colliders[j];
        const PairKey key{A.collider, B.collider};
        const bool triggerPair = A.trigger || B.trigger;
        const PairState st{A.goId, B.goId, &scene, triggerPair};
        auto it = table.find(key);
        const bool existed = it != table.end() && it->second.aId == A.goId &&
                             it->second.bId == B.goId && it->second.scene == &scene &&
                             it->second.trigger == triggerPair;
        const glm::vec2 ov = overlapOf(A, B);
        if (triggerPair) {
            if (existed) publishPair<Trigger2DStayEvent>(scene, st, ov);
            else publishPair<Trigger2DEnterEvent>(scene, st, ov);
        } else {
            if (existed) publishPair<Collision2DStayEvent>(scene, st, ov);
            else publishPair<Collision2DEnterEvent>(scene, st, ov);
        }
        cur[key] = st;
    }

    // 4. 旧状态表中消失的配对补投 Exit（含 isTrigger 切换重分类），随后整表替换
    fireExitsForVanishedPairs(scene, table, cur);
    table = std::move(cur);
}

}  // namespace NovaEngine
