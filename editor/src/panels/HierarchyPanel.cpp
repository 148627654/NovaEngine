#include "HierarchyPanel.h"

#include "../DragDrop.h"
#include "../EditorContext.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>   // InputText(std::string*) 包装（ImGui 1.90+ 移除内建重载）

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Input.h>
#include <NovaEngine/Core/UUID.h>
#include <NovaEngine/Scene/Camera.h>
#include <NovaEngine/Scene/DirectionalLight.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/MeshRenderer.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/Transform.h>

namespace NovaEditor {

namespace {

// target 是否是 node 的祖先（防止把父级拖入自己的子树形成环）
bool isAncestorOf(NovaEngine::GameObject* target, NovaEngine::GameObject* node) {
    for (NovaEngine::GameObject* p = node; p != nullptr; p = p->parent())
        if (p == target) return true;
    return false;
}

// 创建带图元 MeshRenderer 的对象：meshPath 用 "builtin:" 前缀（AssetDatabase 解析），
// 保证图元网格可以随场景序列化往返。
NovaEngine::GameObject* createPrimitive(NovaEngine::Scene* scene, const char* name,
                                        const char* builtinMesh, NovaEngine::GameObject* parent) {
    NovaEngine::GameObject* go = scene->createGameObject(name, parent);
    auto* mr = go->addComponent<NovaEngine::MeshRenderer>();
    mr->meshPath = builtinMesh;
    mr->mesh = NovaEngine::AssetDatabase::instance().getMesh(mr->meshPath);
    return go;
}

}  // namespace

HierarchyPanel::HierarchyPanel() : Panel("Hierarchy") {}

void HierarchyPanel::onGui(EditorContext& ctx, EditorCamera&) {
    using namespace NovaEngine;
    ImGui::Begin(m_name.c_str(), &open);
    Scene* scene = ctx.scene;
    if (!scene) {
        ImGui::End();
        return;
    }

    // F2 重命名（单选对象；输入框激活期间 WantCaptureKeyboard 会挡住 Del 等快捷键）
    if (Input::isKeyPressed(Key::F2) && !ImGui::GetIO().WantCaptureKeyboard)
        if (GameObject* go = ctx.activeObject()) {
            m_renamingId = go->id();
            m_renameBuffer = go->name();
        }

    m_rowRects.clear();
    ImGui::BeginChild("##hier_tree", ImVec2(0.0f, 0.0f), false);
    for (GameObject* root : scene->rootObjects())
        drawNode(ctx, root);

    // 点击空白取消选择
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsAnyItemHovered())
        ctx.clearSelection();

    // 空白右键菜单（挂根创建）
    if (ImGui::BeginPopupContextWindow("##blankctx",
                                       ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        showContextMenu(ctx, nullptr, false);
        ImGui::EndPopup();
    }
    ImGui::EndChild();

    // 拖放目标（kHierarchyPayload）：按行区域判定落点，空白 = 挂根
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DragDrop::kHierarchyPayload)) {
            const uint64_t srcId = *static_cast<const uint64_t*>(payload->Data);
            GameObject* src = scene->findGameObject(srcId);
            if (src && ctx.state == EditorContext::EditorState::Edit) {
                uint64_t dstId = UUID::kInvalid;
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                for (const auto& row : m_rowRects)
                    if (mouse.x >= row.min.x && mouse.x <= row.max.x &&
                        mouse.y >= row.min.y && mouse.y <= row.max.y) {
                        dstId = row.id;
                        break;
                    }
                GameObject* dst = (dstId != UUID::kInvalid) ? scene->findGameObject(dstId) : nullptr;
                if (!dst) {
                    if (src->parent()) {
                        src->setParent(nullptr);  // 空白 = 挂根
                        ctx.markSceneDirty();
                    }
                } else if (dst != src && !isAncestorOf(dst, src)) {
                    src->setParent(dst);  // 目标行 = setParent（保持世界变换）
                    ctx.markSceneDirty();
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    processPending(ctx);
    handleShortcuts(ctx);
    ImGui::End();
}

void HierarchyPanel::drawNode(EditorContext& ctx, NovaEngine::GameObject* go) {
    ImGui::PushID(reinterpret_cast<const void*>(static_cast<uintptr_t>(go->id())));

    // ---- 重命名行 ----
    if (m_renamingId == go->id()) {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        const bool enter = ImGui::InputText("##rename", &m_renameBuffer,
                                            ImGuiInputTextFlags_EnterReturnsTrue |
                                                ImGuiInputTextFlags_AutoSelectAll);
        if (enter || ImGui::IsItemDeactivatedAfterEdit()) {
            if (!m_renameBuffer.empty()) {
                go->setName(m_renameBuffer);
                ctx.markSceneDirty();
            }
            m_renamingId = 0;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_renamingId = 0;  // Esc 取消
        }
        m_rowRects.push_back({glm::vec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y),
                              glm::vec2(ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y),
                              go->id()});
        ImGui::PopID();
        return;
    }

    // ---- 树节点 ----
    const bool hasChildren = !go->children().empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (go->parent() == nullptr) flags |= ImGuiTreeNodeFlags_DefaultOpen;  // 根默认展开
    if (ctx.isSelected(go->id())) flags |= ImGuiTreeNodeFlags_Selected;

    const bool nodeOpen = ImGui::TreeNodeEx("##node", flags, "%s", go->name().c_str());
    // 单击选中（点箭头只展开、不改选择）
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        if (ImGui::GetIO().KeyCtrl) ctx.toggleSelected(go->id());
        else ctx.setSelected(go->id());
    }
    m_rowRects.push_back({glm::vec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y),
                          glm::vec2(ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y),
                          go->id()});

    // 拖拽源（重父子）
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        const uint64_t id = go->id();
        ImGui::SetDragDropPayload(DragDrop::kHierarchyPayload, &id, sizeof(id));
        ImGui::TextUnformatted(go->name().c_str());
        ImGui::EndDragDropSource();
    }

    // 节点右键菜单（先选中右键对象，Unity 习惯）
    if (ImGui::BeginPopupContextItem("##nodectx")) {
        if (!ctx.isSelected(go->id())) ctx.setSelected(go->id());
        showContextMenu(ctx, go, true);
        ImGui::EndPopup();
    }

    if (nodeOpen) {
        for (const auto& child : go->children())
            drawNode(ctx, child.get());
        if (hasChildren) ImGui::TreePop();
    }
    ImGui::PopID();
}

void HierarchyPanel::showContextMenu(EditorContext& ctx, NovaEngine::GameObject* parent, bool onNode) {
    using namespace NovaEngine;
    const bool edit = ctx.state == EditorContext::EditorState::Edit;

    if (ImGui::MenuItem("Create Empty", nullptr, false, edit)) {
        GameObject* go = ctx.scene->createGameObject("GameObject", parent);
        ctx.setSelected(go->id());
        ctx.markSceneDirty();
    }
    if (ImGui::MenuItem("Create Cube", nullptr, false, edit)) {
        ctx.setSelected(createPrimitive(ctx.scene, "Cube", "builtin:cube", parent)->id());
        ctx.markSceneDirty();
    }
    if (ImGui::MenuItem("Create Plane", nullptr, false, edit)) {
        ctx.setSelected(createPrimitive(ctx.scene, "Plane", "builtin:plane", parent)->id());
        ctx.markSceneDirty();
    }
    if (ImGui::MenuItem("Create Directional Light", nullptr, false, edit)) {
        GameObject* go = ctx.scene->createGameObject("Directional Light", parent);
        go->addComponent<DirectionalLight>();
        go->transform()->setLocalEulerAngles(glm::vec3(50.0f, -30.0f, 0.0f));  // 经典斜下方向
        ctx.setSelected(go->id());
        ctx.markSceneDirty();
    }
    if (ImGui::MenuItem("Create Camera", nullptr, false, edit)) {
        GameObject* go = ctx.scene->createGameObject("Camera", parent);
        auto* cam = go->addComponent<Camera>();
        cam->isMain = ctx.scene->findComponents<Camera>().size() == 1;  // 首个相机默认主相机
        ctx.setSelected(go->id());
        ctx.markSceneDirty();
    }

    if (onNode) {
        ImGui::Separator();
        // 延迟到树遍历结束后执行（重建场景会让节点指针作废）
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, edit))
            m_pendingOps.emplace_back(PendingOp::Duplicate, parent->id());
        if (ImGui::MenuItem("Delete", "Del", false, edit))
            m_pendingOps.emplace_back(PendingOp::Delete, parent->id());
    }
}

void HierarchyPanel::processPending(EditorContext& ctx) {
    if (m_pendingOps.empty()) return;
    std::vector<uint64_t> newSelection;
    bool dirty = false;
    for (auto& [op, id] : m_pendingOps) {
        NovaEngine::GameObject* go = ctx.scene->findGameObject(id);
        if (!go) continue;
        if (op == PendingOp::Delete) {
            ctx.scene->destroyGameObject(id);  // 入销毁队列，帧末真正删除
            dirty = true;
        } else if (op == PendingOp::Duplicate) {
            NovaEngine::GameObject* dup = DragDrop::duplicateGameObject(*ctx.scene, go);
            if (dup) {
                newSelection.push_back(dup->id());
                dirty = true;
            }
        }
    }
    m_pendingOps.clear();
    // 先清销毁队列再推快照：让 Undo/Redo 快照不含待销毁对象（redo 删除才能保持删除态）
    if (dirty) ctx.scene->flushDestroyQueue();
    if (newSelection.size() == 1) {
        ctx.setSelected(newSelection.front());
    } else if (newSelection.size() > 1) {
        ctx.clearSelection();
        for (uint64_t id : newSelection) ctx.toggleSelected(id);
    }
    if (dirty) ctx.markSceneDirty();
}

void HierarchyPanel::handleShortcuts(EditorContext& ctx) {
    if (ImGui::GetIO().WantCaptureKeyboard) return;
    if (ctx.state != EditorContext::EditorState::Edit) return;  // Play 态禁止结构修改

    // Del 删除选中（含子树，走销毁队列）
    if (NovaEngine::Input::isKeyPressed(NovaEngine::Key::Delete) && !ctx.selectedIds.empty()) {
        for (uint64_t id : ctx.selectedIds)
            ctx.scene->destroyGameObject(id);
        ctx.clearSelection();
        ctx.scene->flushDestroyQueue();  // 先清队列再推快照（Undo/Redo 快照不含待销毁对象）
        ctx.markSceneDirty();
    }

    // Ctrl+D 复制选中
    const bool ctrl = NovaEngine::Input::isKeyDown(NovaEngine::Key::LeftControl) ||
                      NovaEngine::Input::isKeyDown(NovaEngine::Key::RightControl);
    if (ctrl && NovaEngine::Input::isKeyPressed(NovaEngine::Key::D) && !ctx.selectedIds.empty()) {
        const std::vector<uint64_t> ids(ctx.selectedIds.begin(), ctx.selectedIds.end());
        ctx.clearSelection();
        for (uint64_t id : ids) {
            if (NovaEngine::GameObject* go = ctx.scene->findGameObject(id)) {
                NovaEngine::GameObject* dup = DragDrop::duplicateGameObject(*ctx.scene, go);
                if (dup) ctx.toggleSelected(dup->id());  // 选中全部副本
            }
        }
        ctx.markSceneDirty();
    }
}

}  // namespace NovaEditor
