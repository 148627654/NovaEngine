#include "GizmoController.h"

#include "EditorContext.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <NovaEngine/Core/Input.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Render/Mesh.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/MeshRenderer.h>
#include <NovaEngine/Scene/Transform.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace NovaEditor {

namespace {

ImGuizmo::OPERATION toImGuizmoOperation(GizmoController::Operation op) {
    switch (op) {
    case GizmoController::Operation::Translate: return ImGuizmo::TRANSLATE;
    case GizmoController::Operation::Rotate:    return ImGuizmo::ROTATE;
    case GizmoController::Operation::Scale:     return ImGuizmo::SCALE;
    }
    return ImGuizmo::TRANSLATE;
}

ImGuizmo::MODE toImGuizmoMode(GizmoController::Space space) {
    return space == GizmoController::Space::Local ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
}

const char* operationName(GizmoController::Operation op) {
    switch (op) {
    case GizmoController::Operation::Translate: return "移动 Translate";
    case GizmoController::Operation::Rotate:    return "旋转 Rotate";
    case GizmoController::Operation::Scale:     return "缩放 Scale";
    }
    return "?";
}

const char* spaceName(GizmoController::Space space) {
    return space == GizmoController::Space::Local ? "Local" : "World";
}

// 多选写回暂存：读循环内预分解好的「新局部 TRS」（Reviewer P1-2）
struct LocalTRS {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

// 多选 pivot（V0.2-15）：有 MeshRenderer 的对象用 mesh->aabb() 8 角点世界变换，
// 取全部选中对象世界 AABB 并集中心；全无 MeshRenderer 则世界位置平均。
glm::vec3 computeMultiPivot(const std::vector<NovaEngine::GameObject*>& objects) {
    NovaEngine::AABB box;
    bool any = false;
    glm::vec3 posSum(0.0f);
    int posCount = 0;
    for (NovaEngine::GameObject* go : objects) {
        NovaEngine::Transform* t = go->transform();
        if (!t) continue;
        posSum += t->worldPosition();
        ++posCount;
        auto* mr = go->getComponent<NovaEngine::MeshRenderer>();
        if (!mr || !mr->mesh) continue;
        const NovaEngine::AABB& local = mr->mesh->aabb();
        const glm::mat4& world = t->worldMatrix();
        const glm::vec3 corners[8] = {
            {local.min.x, local.min.y, local.min.z}, {local.max.x, local.min.y, local.min.z},
            {local.min.x, local.max.y, local.min.z}, {local.max.x, local.max.y, local.min.z},
            {local.min.x, local.min.y, local.max.z}, {local.max.x, local.min.y, local.max.z},
            {local.min.x, local.max.y, local.max.z}, {local.max.x, local.max.y, local.max.z},
        };
        for (const glm::vec3& c : corners) {
            const glm::vec3 p = glm::vec3(world * glm::vec4(c, 1.0f));
            box.min = glm::min(box.min, p);
            box.max = glm::max(box.max, p);
        }
        any = true;
    }
    if (any) return (box.min + box.max) * 0.5f;
    if (posCount > 0) return posSum / static_cast<float>(posCount);
    return glm::vec3(0.0f);
}

}  // namespace

void GizmoController::setCamera(const glm::mat4& view, const glm::mat4& projection) {
    m_view = view;
    m_projection = projection;
}

void GizmoController::update(EditorContext& ctx, const glm::vec2& viewportPos, const glm::vec2& viewportSize) {
    const bool wasUsing = m_wasUsing;   // 本帧开始时的拖拽状态（用于检测 true→false 瞬间）
    m_using = false;
    m_over = false;

    // 仅 Edit 态显示；多选（>1）走批量路径，单选走原路径（V0.2-15）
    if (ctx.state != EditorContext::EditorState::Edit || viewportSize.x <= 0.0f ||
        viewportSize.y <= 0.0f || !ctx.scene) {
        // 早退路径也要推进拖拽状态（Reviewer P2-2）：拖拽中对象被删/取消选中时立即结算快照，
        // 防止 m_wasUsing 残留 true 导致下一帧推 phantom 快照
        if (wasUsing) ctx.markSceneDirty();
        m_wasUsing = false;
        return;
    }

    std::vector<NovaEngine::GameObject*> objects;
    if (ctx.selectedIds.size() > 1) {
        objects = ctx.selectedObjects();          // 多选：过滤失效 ID，保持选中顺序
    } else if (NovaEngine::GameObject* go = ctx.activeObject()) {
        // 反序列化路径可能存在无 Transform 的对象：不显示 gizmo（防御，Reviewer P2-3）
        if (go->transform()) objects.push_back(go);   // 恰好单选
    }
    if (objects.empty()) {
        if (wasUsing) ctx.markSceneDirty();
        m_wasUsing = false;
        return;
    }

    // 必须用当前 SceneView 窗口的 drawlist（而非前台 drawlist）：
    // 1) gizmo 图元追加在本帧 ImGui::Image 之后，自然覆盖在视口之上；
    // 2) ImGuizmo 的悬停判定 IsHoveringWindow 依赖 drawlist 的 _OwnerName 与
    //    g.HoveredWindow 匹配；前台 drawlist 的 owner 永远不是当前面板，
    //    mbMouseOver 恒假 → HandleTranslation/Scale/Rotation 直接返回，gizmo 无法交互。
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);

    // 初始矩阵：单选 = 对象世界矩阵；多选 = pivot 世界 AABB 并集中心
    //（World = translate(center)；Local 叠加第一个选中对象的旋转）
    glm::mat4 matrix(1.0f);
    if (objects.size() == 1) {
        matrix = objects.front()->transform()->worldMatrix();
    } else {
        const glm::vec3 center = computeMultiPivot(objects);
        matrix = glm::translate(glm::mat4(1.0f), center);
        if (m_space == Space::Local)
            matrix = matrix * glm::mat4_cast(objects.front()->transform()->worldRotation());
    }
    const glm::mat4 m0 = matrix;   // 多选：Manipulate 后 delta = M1 * inverse(M0)

    // 激活状态变化时打日志（选中对象/操作/空间三者任一变化输出一次，便于排查「gizmo 不显示」）
    const uint64_t goId = objects.front()->id();
    const size_t goCount = objects.size();
    if (goId != m_lastGoId || goCount != m_lastGoCount || m_operation != m_lastLoggedOp ||
        m_space != m_lastLoggedSpace) {
        if (goCount == 1) {
            NovaEngine::Logger::info("Gizmo 显示: {}（{}，{}）", objects.front()->name(),
                                     operationName(m_operation), spaceName(m_space));
        } else {
            NovaEngine::Logger::info("Gizmo 显示: 多选 {} 个对象（{}，{}）", goCount,
                                     operationName(m_operation), spaceName(m_space));
        }
        m_lastGoId = goId;
        m_lastGoCount = goCount;
        m_lastLoggedOp = m_operation;
        m_lastLoggedSpace = m_space;
    }

    // 吸附（V0.2-12）：有效 = Snap Settings 开启 或 Ctrl 按住。
    // ImGuizmo 原生 snap：Translate 逐轴 {s,s,s}；Rotate snap[0] 单位是「度」（内部 DEG2RAD）；
    // Scale 取 snap[0]。
    const bool snapOn = ctx.snapSettings.enabled ||
                        NovaEngine::Input::isKeyDown(NovaEngine::Key::LeftControl) ||
                        NovaEngine::Input::isKeyDown(NovaEngine::Key::RightControl);  // Reviewer P2-7：左右 Ctrl 均生效
    float snap[3] = {0.0f, 0.0f, 0.0f};
    if (snapOn) {
        switch (m_operation) {
        case Operation::Translate:
            snap[0] = ctx.snapSettings.position.x;
            snap[1] = ctx.snapSettings.position.y;
            snap[2] = ctx.snapSettings.position.z;
            break;
        case Operation::Rotate:
            snap[0] = ctx.snapSettings.rotationDeg;   // ImGuizmo 旋转 snap 期望度数
            break;
        case Operation::Scale:
            snap[0] = ctx.snapSettings.scale;
            break;
        }
    }
    // deltaMatrix 传 identity 矩阵（快照时的增量 delta，多选路径自行用 M1*inverse(M0) 计算）
    float deltaMatrix[16];
    std::copy_n(glm::value_ptr(glm::mat4(1.0f)), 16, deltaMatrix);

    // 矩阵直接传 glm::value_ptr，无需转置：ImGuizmo 的 float[16] 与 glm/OpenGL
    // 同为列主序内存布局（内部按行向量约定计算，与列主序输入恰好互相抵消）。
    // 显式转置会破坏平移并触发 "behind camera" 检测，令 gizmo 被静默裁掉不显示。
    ImGuizmo::Manipulate(glm::value_ptr(m_view), glm::value_ptr(m_projection),
                         toImGuizmoOperation(m_operation), toImGuizmoMode(m_space),
                         glm::value_ptr(matrix), deltaMatrix, snapOn ? snap : nullptr);

    if (ImGuizmo::IsUsing()) {
        if (objects.size() == 1) {
            // 单选：Manipulate 已把新矩阵写回 matrix（同一列主序内存布局），直接分解写回 Transform
            NovaEngine::Transform* transform = objects.front()->transform();
            glm::vec3 scale(1.0f), position(0.0f), skew(0.0f);
            glm::vec4 perspective;
            glm::quat rotation = glm::identity<glm::quat>();

            if (objects.front()->parent() && objects.front()->parent()->transform()) {
                // 有父级：反解局部（local = inverse(parentWorld) * world）
                const glm::mat4 local =
                    glm::inverse(objects.front()->parent()->transform()->worldMatrix()) * matrix;
                glm::decompose(local, scale, rotation, position, skew, perspective);
            } else {
                glm::decompose(matrix, scale, rotation, position, skew, perspective);
            }

            transform->setLocalPosition(position);
            transform->setLocalRotation(rotation);
            transform->setLocalScale(scale);
        } else {
            // 多选（V0.2-15）：delta D = M1 * inverse(M0)，每对象 W' = D * W（相对位置不变）。
            // Reviewer P1-2：读循环内预计算每对象 inverse(parentWorldAtStart) * D * worldAtStart
            // 并 decompose 出「新局部 TRS」暂存；写循环只把暂存值写回，彻底不重读父矩阵——
            // 否则父/子同选且子先写、父后写时，写循环重算的父矩阵已是新值，子最终世界 D²·W（双重叠加）。
            const glm::mat4 delta = matrix * glm::inverse(m0);
            std::vector<std::pair<NovaEngine::Transform*, LocalTRS>> entries;
            entries.reserve(objects.size());
            for (NovaEngine::GameObject* go : objects) {
                NovaEngine::Transform* t = go->transform();
                if (!t) continue;
                const glm::mat4 world = delta * t->worldMatrix();   // 全部读取发生在旧父级状态
                glm::vec3 scale(1.0f), position(0.0f), skew(0.0f);
                glm::vec4 perspective;
                glm::quat rotation = glm::identity<glm::quat>();
                if (go->parent() && go->parent()->transform()) {
                    // 有父级：反解局部（local = inverse(parentWorld) * world）
                    const glm::mat4 local =
                        glm::inverse(go->parent()->transform()->worldMatrix()) * world;
                    glm::decompose(local, scale, rotation, position, skew, perspective);
                } else {
                    glm::decompose(world, scale, rotation, position, skew, perspective);
                }
                entries.push_back({t, {position, rotation, scale}});
            }
            for (const auto& [t, trs] : entries) {
                t->setLocalPosition(trs.position);
                t->setLocalRotation(trs.rotation);
                t->setLocalScale(trs.scale);
            }
        }
        ctx.markDirtyOnly();  // 拖拽中每帧只置脏，不推快照（防刷栈，V0.2-1）
        m_using = true;
    }
    m_over = ImGuizmo::IsOver();

    // 拖拽结束瞬间（IsUsing 由 true→false）推「变更后」快照（单选/多选统一单次 markSceneDirty = 单步 Undo）
    if (wasUsing && !m_using) ctx.markSceneDirty();
    m_wasUsing = m_using;
}

void GizmoController::cycleOperation() {
    switch (m_operation) {
    case Operation::Translate: m_operation = Operation::Rotate; break;
    case Operation::Rotate:    m_operation = Operation::Scale;  break;
    case Operation::Scale:     m_operation = Operation::Translate; break;
    }
}

void GizmoController::setOperation(Operation op) {
    m_operation = op;
    NovaEngine::Logger::info("Gizmo 操作切换: {}", operationName(op));
}

void GizmoController::toggleSpace() {
    m_space = (m_space == Space::Local) ? Space::World : Space::Local;
    NovaEngine::Logger::info("Gizmo 空间切换: {}", spaceName(m_space));
}

}  // namespace NovaEditor
