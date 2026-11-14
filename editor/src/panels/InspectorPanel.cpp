#include "InspectorPanel.h"

#include "../EditorContext.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>   // InputText(std::string*) 包装（ImGui 1.90+ 移除内建重载）
#include <nlohmann/json.hpp>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Render/SkyboxRenderer.h>
#include <NovaEngine/Scene/Camera.h>
#include <NovaEngine/Scene/DirectionalLight.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/MeshRenderer.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scripting/Behaviour.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>
#include <NovaEngine/Serialization/SceneSerializer.h>

#include <glm/gtc/type_ptr.hpp>

#include <string>
#include <vector>

namespace NovaEditor {

namespace {

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
void editScriptFields(EditorContext& ctx, NovaEngine::Behaviour* script) {
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
            if (ImGui::Checkbox(it.key().c_str(), &b)) { v = b; changed = true; instantChange = true; }
        } else if (v.is_number_float()) {
            float f = v.get<float>();
            if (ImGui::DragFloat(it.key().c_str(), &f, 0.1f)) { v = f; changed = true; }
        } else if (v.is_number_integer()) {
            int n = static_cast<int>(v.get<int64_t>());
            if (ImGui::DragInt(it.key().c_str(), &n, 1.0f)) { v = static_cast<int64_t>(n); changed = true; }
        } else if (v.is_string()) {
            std::string s = v.get<std::string>();
            if (ImGui::InputText(it.key().c_str(), &s)) { v = s; changed = true; }
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

    GameObject* go = ctx.activeObject();
    if (!go) {
        if (ctx.selectedIds.size() > 1)
            ImGui::Text("%zu objects selected", ctx.selectedIds.size());
        else
            ImGui::TextUnformatted("No object selected");
        // 无选中对象时显示 Environment（天空）折叠区（V0.2-7）
        if (ctx.selectedIds.empty()) drawEnvironment(ctx, scene);
        ImGui::End();
        return;
    }

    // 名称与激活状态（即时变更：编辑结束即推快照）
    std::string name = go->name();
    if (ImGui::InputText("Name", &name)) {
        go->setName(name);
        ctx.markDirtyOnly();  // 输入中间帧只置脏
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    bool active = go->activeSelf();
    if (ImGui::Checkbox("Active", &active)) {
        go->setActiveSelf(active);
        ctx.markSceneDirty();  // Checkbox 无拖拽中间帧，变更即时入栈
    }
    ImGui::Separator();

    drawTransform(ctx, go);
    ImGui::Separator();
    drawMeshRenderer(ctx, go);
    drawCamera(ctx, go);
    drawDirectionalLight(ctx, go);
    // 移除脚本会重建整个场景（旧指针作废），立即结束本帧其余绘制
    if (drawScripts(ctx, go)) {
        ImGui::End();
        return;
    }
    drawAddComponent(ctx, go);
    ImGui::End();
}

void InspectorPanel::drawTransform(EditorContext& ctx, NovaEngine::GameObject* go) {
    NovaEngine::Transform* t = go->transform();
    if (!t) return;
    ImGui::TextUnformatted("Transform");

    glm::vec3 pos = t->localPosition();
    if (ImGui::DragFloat3("Position", glm::value_ptr(pos), 0.05f)) {
        t->setLocalPosition(pos);
        ctx.markDirtyOnly();
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();

    glm::vec3 euler = t->localEulerAngles();  // 度（UI 角度用度，存储用四元数）
    if (ImGui::DragFloat3("Rotation", glm::value_ptr(euler), 0.5f)) {
        t->setLocalEulerAngles(euler);
        ctx.markDirtyOnly();
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();

    glm::vec3 scale = t->localScale();
    if (ImGui::DragFloat3("Scale", glm::value_ptr(scale), 0.05f, 0.001f, 1000.0f)) {
        t->setLocalScale(scale);
        ctx.markDirtyOnly();
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
}

void InspectorPanel::drawMeshRenderer(EditorContext& ctx, NovaEngine::GameObject* go) {
    NovaEngine::MeshRenderer* mr = go->getComponent<NovaEngine::MeshRenderer>();
    if (!mr) return;
    if (!ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID("mr");
    bool remove = false;
    const bool edit = ctx.state == EditorContext::EditorState::Edit;

    // mesh 资产路径（Enter 生效；builtin:cube/sphere/plane 或项目内相对路径）。
    // EnterReturnsTrue 字段只在 Enter 分支应用值 → 只在 Enter 分支推快照
    //（点击别处不应用也不置脏，避免「无变更却脏」）。
    std::string meshPath = mr->meshPath;
    if (ImGui::InputText("Mesh", &meshPath, ImGuiInputTextFlags_EnterReturnsTrue)) {
        mr->meshPath = meshPath;
        mr->mesh = NovaEngine::AssetDatabase::instance().getMesh(meshPath);
        ctx.markSceneDirty();
    }
    ImGui::TextDisabled("(builtin:cube / builtin:sphere / builtin:plane or relative path)");

    // 材质编辑
    if (ImGui::ColorEdit3("Albedo", glm::value_ptr(mr->material.albedo))) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::SliderFloat("Roughness", &mr->material.roughness, 0.0f, 1.0f)) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::SliderFloat("Metallic", &mr->material.metallic, 0.0f, 1.0f)) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();

    // 贴图路径回显 + 编辑（Enter 生效）；同时写入 albedoMapPath（序列化用，避免 Ctrl+S 丢失）
    const std::string boundLabel =
        mr->material.albedoMapPath.empty() ? "Albedo Map: none"
                                           : "Albedo Map: " + mr->material.albedoMapPath;
    ImGui::TextUnformatted(boundLabel.c_str());
    std::string texPath = mr->material.albedoMapPath;
    if (ImGui::InputText("Albedo Map Path", &texPath, ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (texPath.empty()) {
            mr->material.albedoMap = nullptr;
            mr->material.albedoMapPath.clear();
            ctx.markSceneDirty();
        } else {
            auto tex = NovaEngine::AssetDatabase::instance().getTexture(texPath);
            if (tex) {
                mr->material.albedoMap = tex;
                mr->material.albedoMapPath = texPath;  // 同步写路径（项目内相对路径）
                ctx.markSceneDirty();
            } else {
                NovaEngine::Logger::warn("贴图加载失败: {}", texPath);
            }
        }
    }

    if (!edit) ImGui::BeginDisabled();
    if (ImGui::Button("Remove Component")) remove = true;
    if (!edit) ImGui::EndDisabled();
    ImGui::PopID();
    if (remove) {
        go->removeComponent<NovaEngine::MeshRenderer>();
        ctx.markSceneDirty();
    }
}

void InspectorPanel::drawCamera(EditorContext& ctx, NovaEngine::GameObject* go) {
    NovaEngine::Camera* cam = go->getComponent<NovaEngine::Camera>();
    if (!cam) return;
    if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID("cam");
    bool remove = false;
    const bool edit = ctx.state == EditorContext::EditorState::Edit;

    if (ImGui::DragFloat("FOV", &cam->fov, 0.5f, 10.0f, 170.0f)) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::DragFloat("Near Plane", &cam->nearPlane, 0.01f, 0.001f, 10.0f)) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::DragFloat("Far Plane", &cam->farPlane, 1.0f, 10.0f, 10000.0f)) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::ColorEdit4("Clear Color", glm::value_ptr(cam->clearColor))) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::Checkbox("Main Camera", &cam->isMain)) ctx.markSceneDirty();

    if (!edit) ImGui::BeginDisabled();
    if (ImGui::Button("Remove Component")) remove = true;
    if (!edit) ImGui::EndDisabled();
    ImGui::PopID();
    if (remove) {
        go->removeComponent<NovaEngine::Camera>();
        ctx.markSceneDirty();
    }
}

void InspectorPanel::drawDirectionalLight(EditorContext& ctx, NovaEngine::GameObject* go) {
    NovaEngine::DirectionalLight* light = go->getComponent<NovaEngine::DirectionalLight>();
    if (!light) return;
    if (!ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID("dir");
    bool remove = false;
    const bool edit = ctx.state == EditorContext::EditorState::Edit;

    if (ImGui::ColorEdit3("Color", glm::value_ptr(light->color))) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::DragFloat("Intensity", &light->intensity, 0.01f, 0.0f, 100.0f)) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    ImGui::TextDisabled("(direction follows Transform.forward)");

    // 阴影参数（V0.2-6）
    if (ImGui::Checkbox("Cast Shadows", &light->castShadows)) ctx.markSceneDirty();
    if (ImGui::DragFloat("Shadow Bias", &light->shadowBias, 0.0001f, 0.0f, 0.01f, "%.5f"))
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::DragFloat("Shadow Range", &light->shadowRange, 1.0f, 5.0f, 200.0f))
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::DragInt("Shadow Map Size", &light->shadowMapSize, 512, 512, 4096))
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

bool InspectorPanel::drawScripts(EditorContext& ctx, NovaEngine::GameObject* go) {
    const bool edit = ctx.state == EditorContext::EditorState::Edit;
    auto scripts = go->getComponents<NovaEngine::Behaviour>();
    if (scripts.empty()) return false;
    const std::vector<std::string> names = scriptTypeNamesInOrder(go);

    bool sceneRebuilt = false;
    for (size_t i = 0; i < scripts.size(); ++i) {
        NovaEngine::Behaviour* script = scripts[i];
        const std::string title =
            (i < names.size() && !names[i].empty()) ? ("Script (" + names[i] + ")") : "Script";
        ImGui::PushID(script);
        if (ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            // enable 勾选（Behaviour 独有；内置组件无 enabled 字段）
            if (ImGui::Checkbox("Enabled", &script->enabled)) ctx.markSceneDirty();
            editScriptFields(ctx, script);
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

    // 内置组件 + ScriptRegistry 注册脚本
    std::vector<std::string> items = {"Camera", "DirectionalLight", "MeshRenderer"};
    for (auto& n : NovaEngine::ScriptRegistry::allNames()) items.push_back(n);
    std::vector<const char*> cstrs;
    cstrs.reserve(items.size());
    for (auto& s : items) cstrs.push_back(s.c_str());

    int selected = -1;
    if (ImGui::Combo("Add Component", &selected, cstrs.data(), static_cast<int>(cstrs.size()))) {
        if (selected == 0) go->addComponent<NovaEngine::Camera>();
        else if (selected == 1) go->addComponent<NovaEngine::DirectionalLight>();
        else if (selected == 2) go->addComponent<NovaEngine::MeshRenderer>();
        else {
            // 脚本组件共用 Behaviour 的 typeId（裁定 #4），运行期按 dynamic_cast 区分
            auto comp = NovaEngine::ScriptRegistry::create(items[selected], go);
            if (comp)
                go->attachComponent(std::move(comp),
                                    NovaEngine::Component::componentTypeId<NovaEngine::Behaviour>());
        }
        ctx.markSceneDirty();
    }
    if (!edit) ImGui::EndDisabled();
}

void InspectorPanel::drawEnvironment(EditorContext& ctx, NovaEngine::Scene* scene) {
    using namespace NovaEngine;
    if (!ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID("env");

    // 天空模式（V0.2-7）
    int mode = static_cast<int>(scene->skyMode);
    const char* modes[] = {"Gradient", "Cubemap"};
    const int modeCount = static_cast<int>(sizeof(modes) / sizeof(modes[0]));
    if (ImGui::Combo("Sky Mode", &mode, modes, modeCount)) {
        scene->skyMode = static_cast<SkyMode>(mode);
        ctx.markSceneDirty();
    }

    if (ImGui::ColorEdit3("Horizon Color", glm::value_ptr(scene->skyHorizonColor)))
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::ColorEdit3("Zenith Color", glm::value_ptr(scene->skyZenithColor)))
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();

    // cubemap 路径（Enter 生效；空 = 渐变回退）
    std::string path = scene->skyCubemapPath;
    if (ImGui::InputText("Cubemap Path", &path, ImGuiInputTextFlags_EnterReturnsTrue)) {
        scene->skyCubemapPath = path;
        ctx.markSceneDirty();
    }
    ImGui::TextDisabled("(6-face dir or equirect file; empty = gradient fallback)");

    if (ImGui::DragFloat("Sky Rotation", &scene->skyRotation, 1.0f, -360.0f, 360.0f))
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::DragFloat("Sky Exposure", &scene->skyExposure, 0.05f, 0.0f, 10.0f))
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();

    ImGui::PopID();
}

}  // namespace NovaEditor
