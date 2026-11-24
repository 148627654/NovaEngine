#include "InspectorPanel.h"
#include "../DragDrop.h"           // kAssetPayload（V1.0-5 波 2 合波接线：Shader Graph 拖放赋值）
#include "AudioWidgets.h"         // 音频组件编辑（V0.3-2，本线 Coder C）
#include "MaterialEditorWidgets.h"   // 材质编辑控件集（Inspector 与 .novamat 模态共用，V0.3-6）
#include "ParticleWidgets.h"      // 粒子系统编辑（V0.5-6，波 2 合波接线；不匹配类型 no-op）
#include "PhysicsWidgets.h"       // 物理组件编辑（V0.3-1，Coder A 波 2 并行创建；不匹配类型 no-op）
#include <NovaEngine/Scene/CharacterController.h>   // V1.0-3 波 2 合波接线
#include "UIWidgets.h"            // UI 组件编辑（V0.3-3，Coder B 波 2 并行创建；不匹配类型 no-op）

#include "../EditorContext.h"
#include "../FileDialog.h"         // 浏览按钮（原生文件选择框，2026-11-24）
#include "../PrefabManager.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>   // InputText(std::string*) 包装（ImGui 1.90+ 移除内建重载）
#include <nlohmann/json.hpp>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Render/Material.h>
#include <NovaEngine/Scene/Camera.h>
#include <NovaEngine/Scene/ComponentRegistry.h>
#include <NovaEngine/Scene/DirectionalLight.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/LODGroup.h>
#include <NovaEngine/Scene/MeshRenderer.h>
#include <NovaEngine/Animation/SkeletalMeshRenderer.h>
#include <NovaEngine/Scene/BoxCollider2D.h>
#include <NovaEngine/Scene/SpriteRenderer.h>
#include <NovaEngine/Scene/PrefabInstance.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scripting/Behaviour.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>
#include <NovaEngine/Serialization/SceneSerializer.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace NovaEditor {

namespace {

// ===== Prefab override 字段标签强调（V0.3-9 波 4）=====
// 命中差异键（序列化键名）→ 金黄高亮 + "▶ " 前缀。ImGui 未加载粗体字体，
// 以颜色近似「加粗」（文案注释说明）；RAII 压栈/弹栈，作用于紧随其后的控件标签。
class FieldLabel {
public:
    FieldLabel(const char* uiLabel, const char* serializedKey, const std::vector<std::string>* overrides) {
        m_override = overrides != nullptr &&
                     std::find(overrides->begin(), overrides->end(), serializedKey) != overrides->end();
        if (m_override) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.82f, 0.25f, 1.0f));
            m_label = "▶ " + std::string(uiLabel);
        } else {
            m_label = uiLabel;
        }
    }
    ~FieldLabel() {
        if (m_override) ImGui::PopStyleColor();
    }
    const char* c_str() const { return m_label.c_str(); }

private:
    bool m_override = false;
    std::string m_label;
};

// 按类型取 override 键集合（无 → nullptr）
const std::vector<std::string>* overridesFor(
    const std::unordered_map<std::string, std::vector<std::string>>& byType, const char* type) {
    const auto it = byType.find(type);
    return it != byType.end() ? &it->second : nullptr;
}

// 对象 JSON 中的脚本组件类型名（顺序与 getComponents<Behaviour>() 一致，作面板标题用）
std::vector<std::string> scriptTypeNamesInOrder(NovaEngine::GameObject* go) {
    std::vector<std::string> names;
    const auto j = nlohmann::json::parse(NovaEngine::SceneSerializer::serializeGameObject(*go));
    if (!j.contains("components") || !j["components"].is_array()) return names;
    for (const auto& c : j["components"]) {
        const std::string type = c.value("type", "");
        if (NovaEngine::ScriptRegistry::exists(type)) names.push_back(type);
    }
    return names;
}

// 脚本字段的「伪反射」编辑：onSerialize(json) → 逐键 ImGui 编辑 → onDeserialize(json) 往返（§6.5）。
// 脏标记漏斗：拖拽/输入中 markDirtyOnly；编辑结束（IsItemDeactivatedAfterEdit）markSceneDirty。
// 注意：Checkbox 的激活/失活发生在同一点击帧内（ActiveIdPreviousFrame 不命中），
// IsItemDeactivatedAfterEdit 恒假——瞬时控件走 instantChange 即时推快照。
// overrides（V0.3-9 波 4）：override 差异键 → 该字段标签强调（▶ 前缀 + 金黄高亮）
void editScriptFields(EditorContext& ctx, NovaEngine::Behaviour* script,
                      const std::vector<std::string>* overrides) {
    nlohmann::json j;
    script->onSerialize(j);
    if (!j.is_object()) return;

    bool changed = false;
    bool deactivated = false;
    bool instantChange = false;   // checkbox 等瞬时变更（无拖拽中间帧）
    for (auto it = j.begin(); it != j.end(); ++it) {
        nlohmann::json& v = it.value();
        ImGui::PushID(it.key().c_str());
        if (v.is_boolean()) {
            bool b = v.get<bool>();
            if (ImGui::Checkbox(FieldLabel(it.key().c_str(), it.key().c_str(), overrides).c_str(), &b)) {
                v = b; changed = true; instantChange = true;
            }
        } else if (v.is_number_float()) {
            float f = v.get<float>();
            if (ImGui::DragFloat(FieldLabel(it.key().c_str(), it.key().c_str(), overrides).c_str(), &f, 0.1f)) {
                v = f; changed = true;
            }
        } else if (v.is_number_integer()) {
            int n = static_cast<int>(v.get<int64_t>());
            if (ImGui::DragInt(FieldLabel(it.key().c_str(), it.key().c_str(), overrides).c_str(), &n, 1.0f)) {
                v = static_cast<int64_t>(n); changed = true;
            }
        } else if (v.is_string()) {
            std::string s = v.get<std::string>();
            if (ImGui::InputText(FieldLabel(it.key().c_str(), it.key().c_str(), overrides).c_str(), &s)) {
                v = s; changed = true;
            }
        } else {
            ImGui::TextDisabled("%s (unsupported type)", it.key().c_str());
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) deactivated = true;
        ImGui::PopID();
    }
    if (changed) script->onDeserialize(j);  // 应用（拖拽中每帧往返 = 旧行为，值随拖拽实时生效）
    if (deactivated || instantChange) {
        ctx.markSceneDirty();               // 编辑结束 / 瞬时变更：推「变更后」快照
    } else if (changed) {
        ctx.markDirtyOnly();                // 拖拽/输入中间帧：只置脏防刷栈
    }
}

// 移除脚本组件：冻结接口无「按指针移除组件」API，变通为
//   定位对象 JSON 中的第 k 个脚本组件 → 删除 → 并入全场景 JSON → 整场景重建。
// 调用后 scene 内全部对象指针作废（调用方须立即停止使用旧指针）。
bool removeBehaviourComponent(NovaEngine::Scene& scene, NovaEngine::GameObject* go,
                              NovaEngine::Behaviour* target) {
    // target 在脚本组件中的序号
    int index = -1, i = 0;
    for (auto* b : go->getComponents<NovaEngine::Behaviour>()) {
        if (b == target) { index = i; break; }
        ++i;
    }
    if (index < 0) return false;

    nlohmann::json obj = nlohmann::json::parse(NovaEngine::SceneSerializer::serializeGameObject(*go));
    if (!obj.contains("components") || !obj["components"].is_array()) return false;
    int scriptIndex = 0, eraseAt = -1;
    for (size_t k = 0; k < obj["components"].size(); ++k) {
        const std::string type = obj["components"][k].value("type", "");
        if (NovaEngine::ScriptRegistry::exists(type)) {
            if (scriptIndex == index) { eraseAt = static_cast<int>(k); break; }
            ++scriptIndex;
        }
    }
    if (eraseAt < 0) return false;
    obj["components"].erase(obj["components"].begin() + eraseAt);

    nlohmann::json doc = nlohmann::json::parse(NovaEngine::SceneSerializer::serialize(scene));
    bool replaced = false;
    if (doc.contains("objects") && doc["objects"].is_array())
        for (auto& o : doc["objects"])
            if (o.value("id", uint64_t{0}) == go->id()) {
                o = obj;
                replaced = true;
                break;
            }
    if (!replaced) return false;
    NovaEngine::SceneSerializer::deserialize(scene, doc.dump());
    return true;
}

}  // namespace

InspectorPanel::InspectorPanel() : Panel("Inspector") {}

void InspectorPanel::onGui(EditorContext& ctx, EditorCamera&) {
    using namespace NovaEngine;
    ImGui::Begin(m_name.c_str(), &open);
    Scene* scene = ctx.scene;
    if (!scene) {
        ImGui::End();
        return;
    }
    const bool edit = ctx.state == EditorContext::EditorState::Edit;

    // 选中集（过滤失效 ID，保持选中顺序）
    const std::vector<GameObject*> selected = ctx.selectedObjects();
    if (selected.empty()) {
        ImGui::TextUnformatted("No object selected");
        // 无选中时显示 Snap Settings（V0.2-12；Environment 职责已迁移至 EnvironmentPanel）
        drawSnapSettings(ctx);
        ImGui::End();
        return;
    }
    if (selected.size() > 1) {
        // 多选批量编辑（V0.2-15）
        drawMultiEdit(ctx, selected);
        ImGui::End();
        return;
    }
    GameObject* go = selected.front();

    // 名称（Play 态禁用输入，修复 7.2-2；即时变更：编辑结束即推快照）
    std::string name = go->name();
    if (!edit) ImGui::BeginDisabled();
    if (ImGui::InputText("Name", &name)) {
        go->setName(name);
        ctx.markDirtyOnly();  // 输入中间帧只置脏
    }
    if (!edit) ImGui::EndDisabled();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    // Active（Play 态禁用输入，修复 7.2-2）
    bool active = go->activeSelf();
    if (!edit) ImGui::BeginDisabled();
    if (ImGui::Checkbox("Active", &active)) {
        go->setActiveSelf(active);
        ctx.markSceneDirty();  // Checkbox 无拖拽中间帧，变更即时入栈
    }
    if (!edit) ImGui::EndDisabled();
    // DontDestroyOnLoad（V0.3-12 波 3）：LoadScene 时该对象（作子树根）跨场景迁移存活
    //（§14.9；序列化字段，随场景保存/快照还原）
    bool ddol = go->dontDestroyOnLoad();
    if (!edit) ImGui::BeginDisabled();
    if (ImGui::Checkbox("Dont Destroy On Load", &ddol)) {
        go->setDontDestroyOnLoad(ddol);
        ctx.markSceneDirty();
    }
    if (!edit) ImGui::EndDisabled();
    ImGui::Separator();

    // Prefab override 集合（V0.3-9 波 4）：选中对象映射到源对象时，逐组件动态比对差异键
    //（序列化键名）；供各抽屉字段标签强调 + Prefab 区 override 列表/Revert
    std::unordered_map<std::string, std::vector<std::string>> overridesByType;
    if (NovaEngine::GameObject* prefabRoot = PrefabManager::findInstanceRoot(go)) {
        if (auto* inst = prefabRoot->getComponent<NovaEngine::PrefabInstance>()) {
            if (const nlohmann::json* src = PrefabManager::sourceJson(inst->prefabPath)) {
                const auto mit = inst->instanceToSource.find(go->id());
                if (mit != inst->instanceToSource.end())
                    PrefabManager::collectObjectOverrides(*src, mit->second, go, overridesByType);
            }
        }
    }

    drawTransform(ctx, go, overridesFor(overridesByType, "Transform"));
    ImGui::Separator();
    drawMeshRenderer(ctx, go, overridesFor(overridesByType, "MeshRenderer"));
    drawCamera(ctx, go, overridesFor(overridesByType, "Camera"));
    drawDirectionalLight(ctx, go, overridesFor(overridesByType, "DirectionalLight"));
    drawLODGroup(ctx, go);
    drawSpriteRenderer(ctx, go);
    drawBoxCollider2D(ctx, go);
    drawSkeletalMeshRenderer(ctx, go);
    // V0.3 波 2：物理/音频/UI 组件编辑——widget 头漏斗分发（三个 header-only 头签名一致：
    // drawXxxComponents(ctx, comp, editMode)；各头 dynamic_cast 识别自身组件类型，不匹配 no-op）。
    // 波 2 合波（V0.5-6）：ParticleWidgets 同签名接入。
    // 遍历 go 全部组件（getComponents 走 dynamic_cast，含派生类）。
    {
        const bool editMode = ctx.state == EditorContext::EditorState::Edit;
        for (auto* comp : go->getComponents<NovaEngine::Component>()) {
            drawPhysicsComponents(ctx, comp, editMode);
            drawAudioComponents(ctx, comp, editMode);
            drawUIComponents(ctx, comp, editMode);
            ParticleWidgets::drawParticleComponents(ctx, comp, editMode);
            // V1.0-3 波 2 合波接线：CharacterController 编辑器（PhysicsWidgets.h 提供；
            // 与物理组件同纪律：Edit 可编辑参数，Play 行为由引擎侧保证）
            if (auto* cc = dynamic_cast<NovaEngine::CharacterController*>(comp))
                drawCharacterControllerEditor(*cc, ctx);
            // 上面的 Remove 按钮可能已释放 comp（析构指针作废）：校验存活后继续，
            // 否则终止分发（后续快照指针不可再解引用）
            bool alive = false;
            for (auto* c : go->getComponents<NovaEngine::Component>())
                if (c == comp) { alive = true; break; }
            if (!alive) break;
        }
    }
    // Prefab 区（V0.3-9 波 4）：Open 路径显示 / Apply / Revert All / override 列表
    drawPrefabSection(ctx, go, overridesByType);
    // 移除脚本会重建整个场景（旧指针作废），立即结束本帧其余绘制
    if (drawScripts(ctx, go, overridesByType)) {
        ImGui::End();
        return;
    }
    drawAddComponent(ctx, go);
    ImGui::End();
}

void InspectorPanel::drawTransform(EditorContext& ctx, NovaEngine::GameObject* go,
                                   const std::vector<std::string>* overrides) {
    NovaEngine::Transform* t = go->transform();
    if (!t) return;
    ImGui::TextUnformatted("Transform");

    glm::vec3 pos = t->localPosition();
    if (ImGui::DragFloat3(FieldLabel("Position", "position", overrides).c_str(), glm::value_ptr(pos), 0.05f)) {
        t->setLocalPosition(pos);
        ctx.markDirtyOnly();
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();

    glm::vec3 euler = t->localEulerAngles();  // 度（UI 角度用度，存储用四元数）
    if (ImGui::DragFloat3(FieldLabel("Rotation", "rotation", overrides).c_str(), glm::value_ptr(euler), 0.5f)) {
        t->setLocalEulerAngles(euler);
        ctx.markDirtyOnly();
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();

    glm::vec3 scale = t->localScale();
    if (ImGui::DragFloat3(FieldLabel("Scale", "scale", overrides).c_str(), glm::value_ptr(scale), 0.05f, 0.001f, 1000.0f)) {
        t->setLocalScale(scale);
        ctx.markDirtyOnly();
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
}

void InspectorPanel::drawMeshRenderer(EditorContext& ctx, NovaEngine::GameObject* go,
                                      const std::vector<std::string>* overrides) {
    NovaEngine::MeshRenderer* mr = go->getComponent<NovaEngine::MeshRenderer>();
    if (!mr) return;
    if (!ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID("mr");
    bool remove = false;
    const bool edit = ctx.state == EditorContext::EditorState::Edit;

    // mesh 资产路径（Enter 生效；builtin:cube/sphere/plane 或项目内相对路径；Play 态禁用输入）。
    // EnterReturnsTrue 字段只在 Enter 分支应用值 → 只在 Enter 分支推快照
    //（点击别处不应用也不置脏，避免「无变更却脏」）。
    std::string meshPath = mr->meshPath;
    if (!edit) ImGui::BeginDisabled();
    const bool meshEntered = ImGui::InputText(FieldLabel("Mesh", "mesh", overrides).c_str(),
                                              &meshPath, ImGuiInputTextFlags_EnterReturnsTrue);
    // 浏览按钮（2026-11-24 用户需求）：原生文件框选模型（项目内取相对路径）
    bool meshBrowsed = false;
    if (edit) {
        ImGui::SameLine();
        if (ImGui::SmallButton("...##meshBrowse")) {
            std::string chosen;
            if (browseOpenFile(chosen, "选择模型文件",
                               std::string("模型 (*.obj;*.fbx;*.gltf;*.glb)") + '\0' +
                                   "*.obj;*.fbx;*.gltf;*.glb" + '\0' + "所有文件 (*.*)" + '\0' +
                                   "*.*",
                               "")) {
                meshPath = toProjectRelative(chosen);
                meshBrowsed = true;
            }
        }
    }
    if (meshEntered || meshBrowsed) {
        mr->meshPath = meshPath;
        mr->mesh = NovaEngine::AssetDatabase::instance().getMesh(meshPath);
        ctx.markSceneDirty();
    }
    if (!edit) ImGui::EndDisabled();
    ImGui::TextDisabled("(builtin:cube / builtin:sphere / builtin:plane or relative path)");

    // ---- 材质区（V0.3-6 扩展：材质资产双轨 + PBR 贴图/发射参数）----
    // 双轨：引用资产时显示路径 + Detach；未引用时 Assign 输入（Enter 生效经 getMaterialAsset 解析）。
    // 引用资产 → 内联字段区灰显（整块引用，无 per-instance override，Unity 语义）。
    const bool hasAsset = mr->material.materialAsset != nullptr;
    {
        const std::string assetLabel = mr->material.materialAssetPath.empty()
                                           ? "Material Asset: none"
                                           : "Material Asset: " + mr->material.materialAssetPath;
        ImGui::TextUnformatted(assetLabel.c_str());
        if (hasAsset) {
            if (!edit) ImGui::BeginDisabled();
            if (ImGui::Button("Detach Material Asset")) {
                mr->material.materialAsset.reset();
                mr->material.materialAssetPath.clear();
                ctx.markSceneDirty();
            }
            if (!edit) ImGui::EndDisabled();
        } else {
            // 局部缓冲：每次重输，Enter 才应用（与 Mesh 路径输入同模式）
            std::string assetPath;
            if (!edit) ImGui::BeginDisabled();
            const bool assetEntered = ImGui::InputText("Assign Material Asset", &assetPath,
                                                       ImGuiInputTextFlags_EnterReturnsTrue);
            // 浏览按钮（2026-11-24 用户需求）：原生文件框选 .novamat
            bool assetBrowsed = false;
            if (edit) {
                ImGui::SameLine();
                if (ImGui::SmallButton("...##matBrowse")) {
                    std::string chosen;
                    if (browseOpenFile(chosen, "选择材质资产",
                                       std::string("材质资产 (*.novamat)") + '\0' + "*.novamat" +
                                           '\0' + "所有文件 (*.*)" + '\0' + "*.*",
                                       "")) {
                        assetPath = toProjectRelative(chosen);
                        assetBrowsed = true;
                    }
                }
            }
            if (assetEntered || assetBrowsed) {
                if (assetPath.empty()) {
                    NovaEngine::Logger::warn("材质资产路径为空，未赋值");
                } else {
                    auto asset = NovaEngine::AssetDatabase::instance().getMaterialAsset(assetPath);
                    if (asset) {
                        mr->material.materialAsset = asset;
                        mr->material.materialAssetPath = assetPath;
                        ctx.markSceneDirty();
                    } else {
                        NovaEngine::Logger::warn("材质资产加载失败: {}（保持原状）", assetPath);
                    }
                }
            }
            if (!edit) ImGui::EndDisabled();
            ImGui::TextDisabled("(assets/... .novamat path, Enter)");
        }
        ImGui::Separator();
    }

    // ---- V1.0-5 波 2 合波接线：Shader Graph 引用（实例级，序列化键 shaderGraphPath；
    //      P1-3 修复后实例级优先于材质资产，渲染侧经缓存解析编译）----
    {
        const std::string sgLabel = mr->material.shaderGraphPath.empty()
                                        ? "Shader Graph: none"
                                        : "Shader Graph: " + mr->material.shaderGraphPath;
        ImGui::TextUnformatted(sgLabel.c_str());
        if (!mr->material.shaderGraphPath.empty()) {
            if (!edit) ImGui::BeginDisabled();
            if (ImGui::Button("Detach Shader Graph")) {
                mr->material.shaderGraphPath.clear();
                mr->material.shaderOverride.reset();   // 回退默认 blinn_phong
                ctx.markSceneDirty();
            }
            if (!edit) ImGui::EndDisabled();
        } else {
            // 拖放赋值（AssetBrowser .novashadergraph 为 kAssetPayload 拖拽源）
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload(NovaEditor::DragDrop::kAssetPayload)) {
                    const std::string abs(reinterpret_cast<const char*>(payload->Data),
                                          payload->DataSize);
                    const std::filesystem::path p(abs);
                    if (p.extension() == ".novashadergraph") {
                        std::error_code ec;
                        const std::filesystem::path rel =
                            std::filesystem::relative(p, NovaEngine::AssetDatabase::projectRoot(), ec);
                        if (!ec) {
                            mr->material.shaderGraphPath = rel.generic_string();
                            mr->material.shaderOverride.reset();
                            ctx.markSceneDirty();
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
        ImGui::Separator();
    }

    // 内联材质编辑（引用资产时灰显）
    if (hasAsset) ImGui::BeginDisabled();
    drawMaterialEditor(mr->material, ctx);
    if (hasAsset) ImGui::EndDisabled();

    if (!edit) ImGui::BeginDisabled();
    if (ImGui::Button("Remove Component")) remove = true;
    if (!edit) ImGui::EndDisabled();
    ImGui::PopID();
    if (remove) {
        go->removeComponent<NovaEngine::MeshRenderer>();
        ctx.markSceneDirty();
    }
}

void InspectorPanel::drawCamera(EditorContext& ctx, NovaEngine::GameObject* go,
                                const std::vector<std::string>* overrides) {
    NovaEngine::Camera* cam = go->getComponent<NovaEngine::Camera>();
    if (!cam) return;
    if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID("cam");
    bool remove = false;
    const bool edit = ctx.state == EditorContext::EditorState::Edit;

    if (ImGui::DragFloat(FieldLabel("FOV", "fov", overrides).c_str(), &cam->fov, 0.5f, 10.0f, 170.0f))
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::DragFloat(FieldLabel("Near Plane", "nearPlane", overrides).c_str(), &cam->nearPlane, 0.01f, 0.001f, 10.0f))
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::DragFloat(FieldLabel("Far Plane", "farPlane", overrides).c_str(), &cam->farPlane, 1.0f, 10.0f, 10000.0f))
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::ColorEdit4(FieldLabel("Clear Color", "clearColor", overrides).c_str(),
                          glm::value_ptr(cam->clearColor)))
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::Checkbox(FieldLabel("Main Camera", "isMain", overrides).c_str(), &cam->isMain))
        ctx.markSceneDirty();

    if (!edit) ImGui::BeginDisabled();
    if (ImGui::Button("Remove Component")) remove = true;
    if (!edit) ImGui::EndDisabled();
    ImGui::PopID();
    if (remove) {
        go->removeComponent<NovaEngine::Camera>();
        ctx.markSceneDirty();
    }
}

void InspectorPanel::drawDirectionalLight(EditorContext& ctx, NovaEngine::GameObject* go,
                                          const std::vector<std::string>* overrides) {
    NovaEngine::DirectionalLight* light = go->getComponent<NovaEngine::DirectionalLight>();
    if (!light) return;
    if (!ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID("dir");
    bool remove = false;
    const bool edit = ctx.state == EditorContext::EditorState::Edit;

    if (ImGui::ColorEdit3(FieldLabel("Color", "color", overrides).c_str(), glm::value_ptr(light->color)))
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::DragFloat(FieldLabel("Intensity", "intensity", overrides).c_str(), &light->intensity, 0.01f, 0.0f, 100.0f))
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    ImGui::TextDisabled("(direction follows Transform.forward)");

    // 阴影参数（V0.2-6）
    if (ImGui::Checkbox(FieldLabel("Cast Shadows", "castShadows", overrides).c_str(), &light->castShadows))
        ctx.markSceneDirty();
    if (ImGui::DragFloat(FieldLabel("Shadow Bias", "shadowBias", overrides).c_str(), &light->shadowBias, 0.0001f, 0.0f, 0.01f, "%.5f"))
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::DragFloat(FieldLabel("Shadow Range", "shadowRange", overrides).c_str(), &light->shadowRange, 1.0f, 5.0f, 200.0f))
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::DragInt(FieldLabel("Shadow Map Size", "shadowMapSize", overrides).c_str(), &light->shadowMapSize, 512, 512, 4096))
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();

    if (!edit) ImGui::BeginDisabled();
    if (ImGui::Button("Remove Component")) remove = true;
    if (!edit) ImGui::EndDisabled();
    ImGui::PopID();
    if (remove) {
        go->removeComponent<NovaEngine::DirectionalLight>();
        ctx.markSceneDirty();
    }
}

// ===== V0.5-3 LODGroup 编辑（波 2 合波接线，2026-11-19）：v1 上限 3 档，
// mesh 路径 + 距离阈值列表；路径变更清运行时缓存（下帧重新解析）=====
void InspectorPanel::drawLODGroup(EditorContext& ctx, NovaEngine::GameObject* go) {
    NovaEngine::LODGroup* lod = go->getComponent<NovaEngine::LODGroup>();
    if (!lod) return;
    if (!ImGui::CollapsingHeader("LOD Group", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID("lod");
    bool remove = false;
    const bool edit = ctx.state == EditorContext::EditorState::Edit;

    if (ImGui::Checkbox("Enabled", &lod->enabled)) ctx.markSceneDirty();
    if (ImGui::Checkbox("Cross Fade（v1 预留）", &lod->crossFade)) ctx.markSceneDirty();
    for (size_t i = 0; i < lod->lods.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::TextUnformatted(i == 0 ? "LOD 0" : (i == 1 ? "LOD 1" : "LOD 2"));
        std::string path = lod->lods[i].meshPath;
        if (ImGui::InputText("Mesh", &path, ImGuiInputTextFlags_EnterReturnsTrue)) {
            lod->lods[i].meshPath = path;
            lod->lods[i].mesh.reset();   // 路径变更清运行时缓存（resolveMeshFor 下帧重解析）
            ctx.markSceneDirty();
        } else if (ImGui::IsItemDeactivatedAfterEdit()) {
            lod->lods[i].meshPath = path;
            lod->lods[i].mesh.reset();
            ctx.markSceneDirty();
        }
        if (ImGui::DragFloat("Distance", &lod->lods[i].distance, 0.1f, 0.0f, 10000.0f))
            ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        ImGui::PopID();
    }
    if (edit) {
        if (lod->lods.size() < 3 && ImGui::Button("Add LOD")) {
            lod->lods.push_back({});
            ctx.markSceneDirty();
        }
        if (!lod->lods.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Remove Last LOD")) {
                lod->lods.pop_back();
                ctx.markSceneDirty();
            }
        }
    }

    if (!edit) ImGui::BeginDisabled();
    if (ImGui::Button("Remove Component")) remove = true;
    if (!edit) ImGui::EndDisabled();
    ImGui::PopID();
    if (remove) {
        go->removeComponent<NovaEngine::LODGroup>();
        ctx.markSceneDirty();
    }
}

// ===== V0.5-10 2D 组件编辑（波 2 2b 合波接线，2026-11-19）=====
void InspectorPanel::drawSpriteRenderer(EditorContext& ctx, NovaEngine::GameObject* go) {
    NovaEngine::SpriteRenderer* sr = go->getComponent<NovaEngine::SpriteRenderer>();
    if (!sr) return;
    if (!ImGui::CollapsingHeader("Sprite Renderer", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID("sprite");
    bool remove = false;
    const bool edit = ctx.state == EditorContext::EditorState::Edit;

    // 路径变更同步刷新运行时贴图（渲染收集只读 sr->texture 成员，§25.8 合波接缝）
    std::string path = sr->texturePath;
    const bool texEntered = ImGui::InputText("Texture", &path,
                                             ImGuiInputTextFlags_EnterReturnsTrue);
    // 浏览按钮（2026-11-24 用户需求）：原生文件框选贴图（项目内取相对路径）
    ImGui::SameLine();
    bool texBrowsed = false;
    if (ImGui::SmallButton("...##texBrowse")) {
        std::string chosen;
        if (browseOpenFile(chosen, "选择贴图文件",
                           std::string("贴图 (*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.hdr)") + '\0' +
                               "*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.hdr" + '\0' + "所有文件 (*.*)" +
                               '\0' + "*.*",
                           "")) {
            path = toProjectRelative(chosen);
            texBrowsed = true;
        }
    }
    if (texEntered || texBrowsed) {
        sr->texturePath = path;
        sr->texture = path.empty() ? nullptr
                                   : NovaEngine::AssetDatabase::instance().getTexture(path);
        ctx.markSceneDirty();
    } else if (ImGui::IsItemDeactivatedAfterEdit()) {
        sr->texturePath = path;
        sr->texture = path.empty() ? nullptr
                                   : NovaEngine::AssetDatabase::instance().getTexture(path);
        ctx.markSceneDirty();
    }
    if (ImGui::ColorEdit4("Color", glm::value_ptr(sr->color))) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::Checkbox("Flip X", &sr->flipX)) ctx.markSceneDirty();
    if (ImGui::Checkbox("Flip Y", &sr->flipY)) ctx.markSceneDirty();
    if (ImGui::DragInt("Sorting Layer", &sr->sortingLayer, 0.1f, 0, 100)) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::DragInt("Order In Layer", &sr->orderInLayer, 0.1f, -1000, 1000)) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::DragFloat("Pixels Per Unit", &sr->pixelsPerUnit, 1.0f, 0.01f, 10000.0f))
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();

    if (!edit) ImGui::BeginDisabled();
    if (ImGui::Button("Remove Component")) remove = true;
    if (!edit) ImGui::EndDisabled();
    ImGui::PopID();
    if (remove) {
        go->removeComponent<NovaEngine::SpriteRenderer>();
        ctx.markSceneDirty();
    }
}

void InspectorPanel::drawBoxCollider2D(EditorContext& ctx, NovaEngine::GameObject* go) {
    NovaEngine::BoxCollider2D* bc = go->getComponent<NovaEngine::BoxCollider2D>();
    if (!bc) return;
    if (!ImGui::CollapsingHeader("Box Collider 2D", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID("bc2d");
    bool remove = false;
    const bool edit = ctx.state == EditorContext::EditorState::Edit;

    if (ImGui::DragFloat2("Center", glm::value_ptr(bc->center), 0.05f)) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    glm::vec2 size = bc->size;
    if (ImGui::DragFloat2("Size", glm::value_ptr(size), 0.05f, 0.001f, 10000.0f))
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        bc->size = glm::max(size, glm::vec2(0.001f));   // 非法尺寸防御（反序列化同款钳制）
        ctx.markSceneDirty();
    }
    if (ImGui::Checkbox("Is Trigger", &bc->isTrigger)) ctx.markSceneDirty();

    if (!edit) ImGui::BeginDisabled();
    if (ImGui::Button("Remove Component")) remove = true;
    if (!edit) ImGui::EndDisabled();
    ImGui::PopID();
    if (remove) {
        go->removeComponent<NovaEngine::BoxCollider2D>();
        ctx.markSceneDirty();
    }
}

// ===== V0.5-9 蒙皮渲染器编辑（波 2 2b 合波接线；材质编辑复用材质库双轨，v1 只展示路径与骨架）=====
void InspectorPanel::drawSkeletalMeshRenderer(EditorContext& ctx, NovaEngine::GameObject* go) {
    NovaEngine::SkeletalMeshRenderer* smr =
        go->getComponent<NovaEngine::SkeletalMeshRenderer>();
    if (!smr) return;
    if (!ImGui::CollapsingHeader("Skinned Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::PushID("smr");
    bool remove = false;
    const bool edit = ctx.state == EditorContext::EditorState::Edit;

    std::string path = smr->meshPath;
    if (ImGui::InputText("Mesh", &path, ImGuiInputTextFlags_EnterReturnsTrue)) {
        smr->meshPath = path;
        ctx.markSceneDirty();
        // 网格重建归场景反序列化/资产重载（运行时 mesh 缓存由 SMR 反序列化维护）
    } else if (ImGui::IsItemDeactivatedAfterEdit()) {
        smr->meshPath = path;
        ctx.markSceneDirty();
    }
    ImGui::TextDisabled("(骨骼 %zu 根)", smr->skeleton.bones.size());

    if (!edit) ImGui::BeginDisabled();
    if (ImGui::Button("Remove Component")) remove = true;
    if (!edit) ImGui::EndDisabled();
    ImGui::PopID();
    if (remove) {
        go->removeComponent<NovaEngine::SkeletalMeshRenderer>();
        ctx.markSceneDirty();
    }
}

void InspectorPanel::drawPrefabSection(
    EditorContext& ctx, NovaEngine::GameObject* go,
    const std::unordered_map<std::string, std::vector<std::string>>& overridesByType) {
    using namespace NovaEngine;
    // 选中对象自身或祖先链有 PrefabInstance（§14.7：实例内任意对象均可编辑）
    GameObject* root = PrefabManager::findInstanceRoot(go);
    if (!root) return;
    auto* inst = root->getComponent<PrefabInstance>();
    if (!inst) return;
    if (!ImGui::CollapsingHeader("Prefab", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID("prefab");

    // Open：路径显示（v1 无资产级编辑场景，双轨工作流：场景内编辑 → Apply 写回）
    ImGui::TextUnformatted(inst->prefabPath.c_str());
    ImGui::TextDisabled("(instance root: %s)", root->name().c_str());

    const bool edit = ctx.state == EditorContext::EditorState::Edit;
    if (!edit) ImGui::BeginDisabled();
    if (ImGui::Button("Apply")) PrefabManager::applyToAsset(ctx, root);
    ImGui::SameLine();
    if (ImGui::Button("Revert All")) PrefabManager::revertAll(ctx, go);
    if (!edit) ImGui::EndDisabled();

    // override 列表（组件 + 字段粒度；▶ 键 = 与源不同，可单字段/整组件 Revert）
    if (overridesByType.empty()) {
        ImGui::TextDisabled("(no overrides on this object)");
    } else {
        ImGui::Separator();
        ImGui::TextDisabled("Overrides (▶ = differs from source):");
        for (const auto& [type, keys] : overridesByType) {
            ImGui::BulletText("%s", type.c_str());
            for (const auto& key : keys) {
                ImGui::Indent();
                ImGui::TextUnformatted(key.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(("Revert##" + type + key).c_str()))
                    PrefabManager::revertComponent(ctx, go, type, {key});
                ImGui::Unindent();
            }
            if (ImGui::SmallButton(("Revert Component##" + type).c_str()))
                PrefabManager::revertComponent(ctx, go, type);
        }
    }
    ImGui::PopID();
}

bool InspectorPanel::drawScripts(
    EditorContext& ctx, NovaEngine::GameObject* go,
    const std::unordered_map<std::string, std::vector<std::string>>& overridesByType) {
    const bool edit = ctx.state == EditorContext::EditorState::Edit;
    auto scripts = go->getComponents<NovaEngine::Behaviour>();
    if (scripts.empty()) return false;
    const std::vector<std::string> names = scriptTypeNamesInOrder(go);

    bool sceneRebuilt = false;
    for (size_t i = 0; i < scripts.size(); ++i) {
        NovaEngine::Behaviour* script = scripts[i];
        const std::string typeName = (i < names.size()) ? names[i] : std::string();
        const std::string title =
            (!typeName.empty()) ? ("Script (" + typeName + ")") : "Script";
        ImGui::PushID(script);
        if (ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            // enable 勾选（Behaviour 独有；内置组件无 enabled 字段）
            if (ImGui::Checkbox("Enabled", &script->enabled)) ctx.markSceneDirty();
            editScriptFields(ctx, script, overridesFor(overridesByType, typeName.c_str()));
            if (!edit) ImGui::BeginDisabled();
            if (ImGui::Button("Remove Component")) {
                if (removeBehaviourComponent(*ctx.scene, go, script)) {
                    ctx.markSceneDirty();
                    sceneRebuilt = true;
                }
            }
            if (!edit) ImGui::EndDisabled();
        }
        ImGui::PopID();
        if (sceneRebuilt) break;  // 场景已重建：后续脚本指针全部作废
    }
    return sceneRebuilt;
}

void InspectorPanel::drawAddComponent(EditorContext& ctx, NovaEngine::GameObject* go) {
    const bool edit = ctx.state == EditorContext::EditorState::Edit;
    ImGui::Separator();
    if (!edit) ImGui::BeginDisabled();  // Play 态组件增删禁用

    // 内置组件列表改由 ComponentRegistry 提供（V0.3 波 0）：物理/音频/UI 组件注册后自动出现，
    // 无需再改面板；脚本列表仍走 ScriptRegistry。两表合并、排序、去重后展示。
    // Transform 是强制组件（每个对象必有），过滤掉避免重复挂载。
    std::vector<std::string> items;
    for (const auto& n : NovaEngine::ComponentRegistry::builtinNames()) {
        if (n == "Transform") continue;
        items.push_back(n);
    }
    for (const auto& n : NovaEngine::ScriptRegistry::allNames()) items.push_back(n);
    std::sort(items.begin(), items.end());
    items.erase(std::unique(items.begin(), items.end()), items.end());  // 同名去重（两表合并兜底）
    std::vector<const char*> cstrs;
    cstrs.reserve(items.size());
    for (auto& s : items) cstrs.push_back(s.c_str());

    int selected = -1;
    if (ImGui::Combo("Add Component", &selected, cstrs.data(), static_cast<int>(cstrs.size()))) {
        const std::string& name = items[selected];
        // 按名字分发：先查内置组件注册表，命中则用注册表 typeId 挂载（onAwake 即时调用）；
        // 否则走脚本注册表（脚本组件共用 Behaviour 的 typeId，裁定 #4，运行期按 dynamic_cast 区分）。
        auto builtin = NovaEngine::ComponentRegistry::create(name, go);
        if (builtin) {
            go->attachComponent(std::move(builtin),
                                NovaEngine::ComponentRegistry::typeIdForName(name), true);
        } else {
            auto comp = NovaEngine::ScriptRegistry::create(name, go);
            if (comp)
                go->attachComponent(std::move(comp),
                                    NovaEngine::Component::componentTypeId<NovaEngine::Behaviour>());
        }
        ctx.markSceneDirty();
    }
    if (!edit) ImGui::EndDisabled();
}

void InspectorPanel::drawSnapSettings(EditorContext& ctx) {
    // V0.2-12：运行期参数（不序列化、不置脏）；有效吸附 = enabled 或 Ctrl 按住（GizmoController 判定）
    if (!ImGui::CollapsingHeader("Snap Settings", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID("snap");
    ImGui::Checkbox("Enable Snap", &ctx.snapSettings.enabled);
    ImGui::DragFloat3("Position", glm::value_ptr(ctx.snapSettings.position), 0.05f, 0.001f, 1000.0f);
    ImGui::DragFloat("Rotation (deg)", &ctx.snapSettings.rotationDeg, 1.0f, 0.0f, 360.0f);
    ImGui::DragFloat("Scale", &ctx.snapSettings.scale, 0.01f, 0.001f, 1000.0f);
    ImGui::PopID();
}

void InspectorPanel::drawMultiEdit(EditorContext& ctx,
                                   const std::vector<NovaEngine::GameObject*>& gos) {
    using namespace NovaEngine;
    const bool edit = ctx.state == EditorContext::EditorState::Edit;
    ImGui::Text("%zu objects selected", gos.size());

    // Active：全相等显示公共勾选；混合显示未勾选 + 灰字 "(mixed)"（点击勾选 = 全部 true）。
    // Play 态禁用输入（修复 7.2-2）。
    {
        bool allActive = true, noneActive = true;
        for (GameObject* go : gos) {
            allActive = allActive && go->activeSelf();
            noneActive = noneActive && !go->activeSelf();
        }
        bool buffer = allActive;   // 混合时为 false；勾选瞬间变 true → 全部激活
        if (!edit) ImGui::BeginDisabled();
        if (ImGui::Checkbox("Active", &buffer)) {
            for (GameObject* go : gos) go->setActiveSelf(buffer);
            ctx.markSceneDirty();  // Checkbox 无拖拽中间帧，变更即时入栈
        }
        if (!edit) ImGui::EndDisabled();
        if (!allActive && !noneActive) {
            ImGui::SameLine();
            ImGui::TextDisabled("(mixed)");
        }
    }
    ImGui::Separator();

    // Transform：Position / Rotation(欧拉) / Scale 逐轴批量编辑
    std::vector<Transform*> ts;
    ts.reserve(gos.size());
    for (GameObject* go : gos)
        if (Transform* t = go->transform()) ts.push_back(t);
    if (ts.empty()) return;

    ImGui::TextUnformatted("Transform");
    drawMultiVec3(ctx, "Position", ts,
                  [](Transform* t) { return t->localPosition(); },
                  [](Transform* t, const glm::vec3& v) { t->setLocalPosition(v); },
                  0.05f, 0.0f, 0.0f);
    drawMultiVec3(ctx, "Rotation", ts,
                  [](Transform* t) { return t->localEulerAngles(); },   // 度
                  [](Transform* t, const glm::vec3& v) { t->setLocalEulerAngles(v); },
                  0.5f, 0.0f, 0.0f);
    drawMultiVec3(ctx, "Scale", ts,
                  [](Transform* t) { return t->localScale(); },
                  [](Transform* t, const glm::vec3& v) { t->setLocalScale(v); },
                  0.05f, 0.001f, 1000.0f);
    ImGui::Separator();

    // Material：全部有 MeshRenderer → 公共材质区；部分有 → 灰字统计
    std::vector<MeshRenderer*> mrs;
    for (GameObject* go : gos)
        if (auto* mr = go->getComponent<MeshRenderer>()) mrs.push_back(mr);
    if (mrs.size() == gos.size()) {
        drawMultiMaterial(ctx, mrs);
    } else if (!mrs.empty()) {
        ImGui::TextDisabled("%zu/%zu have Mesh Renderer", mrs.size(), gos.size());
    }
}

void InspectorPanel::drawMultiVec3(EditorContext& ctx, const char* label,
                                   const std::vector<NovaEngine::Transform*>& ts,
                                   const std::function<glm::vec3(NovaEngine::Transform*)>& getter,
                                   const std::function<void(NovaEngine::Transform*, const glm::vec3&)>& setter,
                                   float speed, float vMin, float vMax) {
    // V0.2-15：全相等显示公共值；不等显示灰字 "(…)" 标记 + 缓冲 0，拖动把绝对值写全部对象。
    // 拖拽中 markDirtyOnly；结束（IsItemDeactivatedAfterEdit）统一单次 markSceneDirty（单步 Undo）。
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    static const char* kAxis[3] = {"X", "Y", "Z"};
    bool deactivated = false;
    for (int axis = 0; axis < 3; ++axis) {
        float common = 0.0f;
        bool equal = true;
        for (size_t i = 0; i < ts.size(); ++i) {
            const float v = getter(ts[i])[axis];
            if (i == 0) common = v;
            else if (std::abs(v - common) > 1e-5f) { equal = false; break; }
        }
        ImGui::PushID(axis);
        ImGui::SameLine();
        if (equal) {
            if (ImGui::DragFloat(kAxis[axis], &common, speed, vMin, vMax)) {
                for (NovaEngine::Transform* t : ts) {
                    glm::vec3 v = getter(t);
                    v[axis] = common;
                    setter(t, v);
                }
                ctx.markDirtyOnly();
            }
        } else {
            // 混合值：灰字 "(…)"（printf 格式无 % 占位符，仅作占位显示）+ 缓冲 0
            float buffer = 0.0f;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
            const bool dragged = ImGui::DragFloat(kAxis[axis], &buffer, speed, vMin, vMax, "(…)");
            ImGui::PopStyleColor();
            if (dragged) {
                for (NovaEngine::Transform* t : ts) {
                    glm::vec3 v = getter(t);
                    v[axis] = buffer;   // 拖动写绝对值到全部对象
                    setter(t, v);
                }
                ctx.markDirtyOnly();
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) deactivated = true;
        ImGui::PopID();
    }
    if (deactivated) ctx.markSceneDirty();
    ImGui::PopID();
}

void InspectorPanel::drawMultiMaterial(EditorContext& ctx,
                                       const std::vector<NovaEngine::MeshRenderer*>& mrs) {
    // 以第一个对象的材质为编辑缓冲，逐字段应用到全部 MeshRenderer（V0.2-15）；
    // V0.3-6 扩展：材质资产双轨 + PBR 贴图/发射参数（公共值语义）
    if (!ImGui::CollapsingHeader("Mesh Renderer (Material)", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID("multimat");
    const bool edit = ctx.state == EditorContext::EditorState::Edit;

    // 双轨（缓冲 = 首对象）：引用资产时 Detach 应用到全部；未引用时 Assign 输入应用到全部
    const bool frontHasAsset = mrs.front()->material.materialAsset != nullptr;
    {
        const std::string assetLabel =
            mrs.front()->material.materialAssetPath.empty()
                ? "Material Asset: none"
                : "Material Asset: " + mrs.front()->material.materialAssetPath;
        ImGui::TextUnformatted(assetLabel.c_str());
        if (frontHasAsset) {
            if (!edit) ImGui::BeginDisabled();
            if (ImGui::Button("Detach Material Asset")) {
                for (auto* mr : mrs) {
                    mr->material.materialAsset.reset();
                    mr->material.materialAssetPath.clear();
                }
                ctx.markSceneDirty();
            }
            if (!edit) ImGui::EndDisabled();
        } else {
            std::string assetPath;
            if (!edit) ImGui::BeginDisabled();
            const bool assetEntered = ImGui::InputText("Assign Material Asset", &assetPath,
                                                       ImGuiInputTextFlags_EnterReturnsTrue);
            // 浏览按钮（2026-11-24 用户需求）：原生文件框选 .novamat
            bool assetBrowsed = false;
            ImGui::SameLine();
            if (ImGui::SmallButton("...##matBrowse")) {
                std::string chosen;
                if (browseOpenFile(chosen, "选择材质资产",
                                   std::string("材质资产 (*.novamat)") + '\0' + "*.novamat" +
                                       '\0' + "所有文件 (*.*)" + '\0' + "*.*",
                                   "")) {
                    assetPath = toProjectRelative(chosen);
                    assetBrowsed = true;
                }
            }
            if (assetEntered || assetBrowsed) {
                if (assetPath.empty()) {
                    NovaEngine::Logger::warn("材质资产路径为空，未赋值");
                } else {
                    auto asset = NovaEngine::AssetDatabase::instance().getMaterialAsset(assetPath);
                    if (asset) {
                        for (auto* mr : mrs) {
                            mr->material.materialAsset = asset;
                            mr->material.materialAssetPath = assetPath;
                        }
                        ctx.markSceneDirty();
                    } else {
                        NovaEngine::Logger::warn("材质资产加载失败: {}（保持原状）", assetPath);
                    }
                }
            }
            if (!edit) ImGui::EndDisabled();
            ImGui::TextDisabled("(assets/... .novamat path, Enter)");
        }
        ImGui::Separator();
    }

    // 内联参数（首对象引用资产时灰显）
    if (frontHasAsset) ImGui::BeginDisabled();

    glm::vec3 albedo = mrs.front()->material.albedo;
    if (ImGui::ColorEdit3("Albedo", glm::value_ptr(albedo))) {
        for (auto* mr : mrs) mr->material.albedo = albedo;
        ctx.markDirtyOnly();
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();

    float roughness = mrs.front()->material.roughness;
    if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f)) {
        for (auto* mr : mrs) mr->material.roughness = roughness;
        ctx.markDirtyOnly();
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();

    float metallic = mrs.front()->material.metallic;
    if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f)) {
        for (auto* mr : mrs) mr->material.metallic = metallic;
        ctx.markDirtyOnly();
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();

    // 透明参数（V0.2-8）
    float alpha = mrs.front()->material.alpha;
    if (ImGui::SliderFloat("Alpha", &alpha, 0.0f, 1.0f)) {
        for (auto* mr : mrs) mr->material.alpha = alpha;
        ctx.markDirtyOnly();
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();

    int blendMode = static_cast<int>(mrs.front()->material.blendMode);
    const char* blendModes[] = {"Opaque", "AlphaBlend"};
    if (ImGui::Combo("Blend Mode", &blendMode, blendModes,
                     static_cast<int>(sizeof(blendModes) / sizeof(blendModes[0])))) {
        for (auto* mr : mrs) mr->material.blendMode = static_cast<NovaEngine::BlendMode>(blendMode);
        ctx.markSceneDirty();
    }

    // V0.3-4 PBR 发射参数（公共值语义）
    glm::vec3 emissionColor = mrs.front()->material.emissionColor;
    if (ImGui::ColorEdit3("Emission Color", glm::value_ptr(emissionColor))) {
        for (auto* mr : mrs) mr->material.emissionColor = emissionColor;
        ctx.markDirtyOnly();
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();

    float emissionIntensity = mrs.front()->material.emissionIntensity;
    if (ImGui::DragFloat("Emission Intensity", &emissionIntensity, 0.05f, 0.0f, 100.0f)) {
        for (auto* mr : mrs) mr->material.emissionIntensity = emissionIntensity;
        ctx.markDirtyOnly();
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();

    // PBR 贴图路径行（公共值缓冲自首对象；Enter 应用到全部；失败 warn 保持原状）
    for (const MaterialTextureSlot& slot : kMaterialTextureSlots) {
        const std::string boundLabel =
            (mrs.front()->material.*slot.pathField).empty()
                ? std::string(slot.label) + ": none"
                : std::string(slot.label) + ": " + (mrs.front()->material.*slot.pathField);
        ImGui::TextUnformatted(boundLabel.c_str());
        std::string pathBuf = mrs.front()->material.*slot.pathField;
        if (!edit) ImGui::BeginDisabled();
        const std::string inputLabel = std::string(slot.label) + " Path";
        if (ImGui::InputText(inputLabel.c_str(), &pathBuf, ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (pathBuf.empty()) {
                for (auto* mr : mrs) {
                    mr->material.*slot.mapField = nullptr;
                    mr->material.*slot.pathField = std::string();
                }
                ctx.markSceneDirty();
            } else {
                // 解析一次（AssetDatabase 缓存共享同一纹理实例），成功应用到全部
                auto tex = NovaEngine::AssetDatabase::instance().getTexture(pathBuf, slot.srgb);
                if (tex) {
                    for (auto* mr : mrs) {
                        mr->material.*slot.mapField = tex;
                        mr->material.*slot.pathField = pathBuf;
                    }
                    ctx.markSceneDirty();
                } else {
                    NovaEngine::Logger::warn("贴图加载失败: {}（{} 保持原状）", pathBuf, slot.label);
                }
            }
        }
        if (!edit) ImGui::EndDisabled();
    }

    if (frontHasAsset) ImGui::EndDisabled();
    ImGui::PopID();
}

}  // namespace NovaEditor
