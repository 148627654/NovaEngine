#include <NovaEngine/Serialization/SceneSerializer.h>
#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Scene/ComponentRegistry.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scene/MeshRenderer.h>
#include <NovaEngine/Scene/Camera.h>
#include <NovaEngine/Scene/DirectionalLight.h>
#include <NovaEngine/Scripting/Behaviour.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Core/UUID.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <string_view>
#include <typeinfo>
#include <unordered_map>

namespace NovaEngine {

namespace {

// ---- glm <-> JSON 辅助（JSON schema 见 docs/ARCHITECTURE.md §5）----

nlohmann::json vec3ToJson(const glm::vec3& v) {
    return nlohmann::json::array({v.x, v.y, v.z});
}

glm::vec3 jsonToVec3(const nlohmann::json& j, const glm::vec3& fallback) {
    if (!j.is_array() || j.size() < 3)
        return fallback;
    try {
        return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
    } catch (const std::exception&) {
        return fallback;
    }
}

nlohmann::json quatToJson(const glm::quat& q) {
    return nlohmann::json::array({q.x, q.y, q.z, q.w});  // 四元数 [x, y, z, w]
}

glm::quat jsonToQuat(const nlohmann::json& j, const glm::quat& fallback) {
    if (!j.is_array() || j.size() < 4)
        return fallback;
    try {
        // glm::quat 构造参数顺序为 (w, x, y, z)
        return glm::normalize(glm::quat(j[3].get<float>(), j[0].get<float>(), j[1].get<float>(), j[2].get<float>()));
    } catch (const std::exception&) {
        return fallback;
    }
}

// ---- 组件类型名 ----

std::string scriptTypeName(const Component* comp) {
    // 脚本组件类型名：注册名 = 类名最后一段（ScriptRegistry 名字裁剪裁定），
    // 与 MSVC typeid(*comp).name() 的 "class NovaGame::SpinningCube" 尾段一致
    std::string raw = typeid(*comp).name();
    for (const char* prefix : {"class ", "struct ", "enum "}) {
        if (raw.starts_with(prefix)) {
            raw.erase(0, std::string_view(prefix).size());
            break;
        }
    }
    const auto pos = raw.rfind("::");
    return (pos == std::string::npos) ? raw : raw.substr(pos + 2);
}

std::string componentTypeName(const Component* comp) {
    if (dynamic_cast<const Transform*>(comp))
        return "Transform";
    if (dynamic_cast<const MeshRenderer*>(comp))
        return "MeshRenderer";
    if (dynamic_cast<const Camera*>(comp))
        return "Camera";
    if (dynamic_cast<const DirectionalLight*>(comp))
        return "DirectionalLight";
    const std::string name = scriptTypeName(comp);
    if (!ScriptRegistry::exists(name))
        Logger::warn("序列化：脚本组件 \"{}\" 未在 ScriptRegistry 注册，反序列化时将被跳过", name);
    return name;
}

// 字段类型不符（如 "id" 是字符串）时回退默认值，保证畸形文件不崩溃
template<typename T>
T safeGet(const nlohmann::json& j, const char* key, const T& fallback) {
    try {
        return j.value(key, fallback);
    } catch (const std::exception&) {
        return fallback;
    }
}

// 写出单个对象节点（id/name/active/parent/components），供 serialize 与 serializeGameObject 共用
void writeGameObjectNode(nlohmann::json& out, GameObject* go) {
    out["id"] = go->id();
    out["name"] = go->name();
    out["active"] = go->activeSelf();
    out["parent"] = (go->parent() != nullptr) ? go->parent()->id() : UUID::kInvalid;

    std::vector<Component*> comps = go->getComponents<Component>();
    // schema 约定：组件数组第一项必须是 Transform
    const auto it = std::find_if(comps.begin(), comps.end(),
                                 [](Component* c) { return dynamic_cast<Transform*>(c) != nullptr; });
    if (it != comps.end() && it != comps.begin())
        std::rotate(comps.begin(), it, it + 1);

    nlohmann::json arr = nlohmann::json::array();
    for (Component* c : comps) {
        nlohmann::json cj;
        // 先写 "type" 名字，再写公共字段，最后调 onSerialize(out) 合并扩展字段
        cj["type"] = componentTypeName(c);
        if (auto* t = dynamic_cast<Transform*>(c)) {
            cj["position"] = vec3ToJson(t->localPosition());
            cj["rotation"] = quatToJson(t->localRotation());
            cj["scale"] = vec3ToJson(t->localScale());
        }
        if (auto* b = dynamic_cast<Behaviour*>(c))
            cj["enabled"] = b->enabled;
        c->onSerialize(cj);
        arr.push_back(std::move(cj));
    }
    out["components"] = std::move(arr);
}

}  // namespace

std::string SceneSerializer::serialize(const Scene& scene, int indent) {
    nlohmann::json root;
    root["version"] = 4;   // V0.3-4/6：+PBR 材质字段 / materialAsset 材质库双轨（读时兼容 1/2/3/4）
    root["name"] = scene.name();

    {
        nlohmann::json env;
        env["skyColor"] = vec3ToJson(scene.skyColor);
        env["ambientColor"] = vec3ToJson(scene.ambientColor);
        env["showGrid"] = scene.showGrid;
        env["fogColor"] = vec3ToJson(scene.fogColor);   // V0.2-5：网格雾色（缺省随 skyColor 联动）
        // 天空（V0.2-7）：skyMode 存字符串
        env["skyMode"] = scene.skyMode == SkyMode::Cubemap ? "Cubemap" : "Gradient";
        env["skyHorizonColor"] = vec3ToJson(scene.skyHorizonColor);
        env["skyZenithColor"] = vec3ToJson(scene.skyZenithColor);
        env["skyCubemapPath"] = scene.skyCubemapPath;
        env["skyRotation"] = scene.skyRotation;
        env["skyExposure"] = scene.skyExposure;
        root["environment"] = std::move(env);
    }
    {
        nlohmann::json cam;
        cam["target"] = vec3ToJson(scene.editorCameraTarget);
        cam["distance"] = scene.editorCameraDistance;
        cam["yaw"] = scene.editorCameraYaw;
        cam["pitch"] = scene.editorCameraPitch;
        root["editorCamera"] = std::move(cam);
    }

    // 对象平铺列表：forEachObject 深度优先
    nlohmann::json objects = nlohmann::json::array();
    scene.forEachObject([&objects](GameObject* go) {
        nlohmann::json node;
        writeGameObjectNode(node, go);
        objects.push_back(std::move(node));
    });
    root["objects"] = std::move(objects);

    // indent = -1 时 nlohmann dump 输出紧凑单行（Undo 快照用，省内存）
    return root.dump(indent);
}

std::string SceneSerializer::serializeGameObject(GameObject& object) {
    // 单对象 JSON（含 parent 引用），编辑器复制/剪贴板用
    nlohmann::json node;
    writeGameObjectNode(node, &object);
    return node.dump(4);
}

void SceneSerializer::deserialize(Scene& target, const std::string& json) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json);
    } catch (const std::exception& e) {
        Logger::error("反序列化场景失败：JSON 解析错误（{}），场景保持不变", e.what());
        return;
    }
    if (!root.is_object()) {
        Logger::error("反序列化场景失败：JSON 根节点不是对象，场景保持不变");
        return;
    }
    const int version = safeGet(root, "version", 0);
    // V0.3：写 4，读 1/2/3/4（旧文件默认值兜底）；其它版本 warn 但继续尝试加载
    if (version != 1 && version != 2 && version != 3 && version != 4)
        Logger::warn("场景文件版本为 {}（期望 1/2/3/4），仍尝试加载", version);

    // 1. 清空 target（保留 Scene 对象身份）：全部对象入销毁队列并立即执行
    {
        std::vector<uint64_t> doomed;
        target.forEachObject([&doomed](GameObject* go) { doomed.push_back(go->id()); });
        for (uint64_t id : doomed)
            target.destroyGameObject(id);
        target.flushDestroyQueue();
    }

    const nlohmann::json kEmptyObject = nlohmann::json::object();
    const nlohmann::json kEmptyArray = nlohmann::json::array();
    const nlohmann::json& objectsNode =
        (root.contains("objects") && root["objects"].is_array()) ? root["objects"] : kEmptyArray;

    // 2. 先建全部对象（instantiate 不自动加 Transform）
    struct ObjRecord {
        GameObject* go;
        const nlohmann::json* node;
    };
    std::vector<ObjRecord> records;
    records.reserve(objectsNode.size());
    for (const auto& obj : objectsNode) {
        if (!obj.is_object())
            continue;
        const uint64_t id = safeGet(obj, "id", uint64_t(UUID::kInvalid));
        const std::string name = safeGet(obj, "name", std::string("GameObject"));
        GameObject* go = target.instantiate(id, name);
        go->setActiveSelf(safeGet(obj, "active", true));
        records.push_back({go, &obj});
    }

    // 3. 逐组件 createComponentFromJson + attachComponent(callAwake=false) + onDeserialize
    for (const auto& rec : records) {
        const nlohmann::json& compsNode =
            (rec.node->contains("components") && (*rec.node)["components"].is_array())
                ? (*rec.node)["components"]
                : kEmptyArray;
        for (const auto& cj : compsNode) {
            if (!cj.is_object())
                continue;
            std::unique_ptr<Component> comp = createComponentFromJson(rec.go, cj);
            if (!comp)
                continue;  // 未知类型：createComponentFromJson 内部已 Logger::warn 并跳过

            const std::string typeName = safeGet(cj, "type", std::string());
            Component* raw = comp.get();
            // typeId 查 ComponentRegistry；脚本组件不在注册表 → 0（getComponents 走 dynamic_cast，不受影响）
            rec.go->attachComponent(std::move(comp), ComponentRegistry::typeIdForName(typeName),
                                    /*callAwake=*/false);

            // Transform 公共字段（schema 中由序列化器写，这里也由序列化器读）
            if (auto* t = dynamic_cast<Transform*>(raw)) {
                t->setLocalPosition(jsonToVec3(cj.value("position", nlohmann::json()), glm::vec3(0.0f)));
                t->setLocalRotation(jsonToQuat(cj.value("rotation", nlohmann::json()),
                                               glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));
                t->setLocalScale(jsonToVec3(cj.value("scale", nlohmann::json()), glm::vec3(1.0f)));
            }
            // Behaviour 公共字段 enabled
            if (auto* b = dynamic_cast<Behaviour*>(raw))
                b->enabled = safeGet(cj, "enabled", true);

            // 组件扩展字段：畸形字段类型不崩溃，仅告警
            try {
                raw->onDeserialize(cj);
            } catch (const std::exception& e) {
                Logger::warn("反序列化组件 \"{}\" 字段失败（{}），已保留默认值", typeName, e.what());
            }
        }
    }

    // 3.5 无 Transform 防御（V0.2-14）：畸形文件/旧文件缺 Transform 时补默认 Transform，
    //     保证 setParent/transform() 等路径不空指针崩溃（callAwake=false，第 5 步统一补调）
    for (const auto& rec : records) {
        if (rec.go->transform() != nullptr) continue;
        Logger::warn("反序列化：对象 \"{}\" 缺少 Transform 组件，已补充默认 Transform", rec.go->name());
        rec.go->attachComponent(std::make_unique<Transform>(rec.go),
                                Component::componentTypeId<Transform>(),
                                /*callAwake=*/false);
    }

    // 4. 重建父子层级：全部对象已挂根，按 parent 字段 setParent(keepWorldTransform=false)
    //    （直接用文件里的局部值，不改动世界矩阵语义）
    for (const auto& rec : records) {
        const uint64_t parentId = safeGet(*rec.node, "parent", uint64_t(UUID::kInvalid));
        if (parentId == UUID::kInvalid)
            continue;
        if (GameObject* p = target.findGameObject(parentId))
            rec.go->setParent(p, false);
        else
            Logger::warn("反序列化：对象 \"{}\" 的父级 {} 不存在，保持根层级", rec.go->name(), parentId);
    }

    // 5. 全部组件统一补调 onAwake（此时字段已就绪、父级已存在）
    target.forEachObject([](GameObject* go) {
        for (Component* c : go->getComponents<Component>())
            c->onAwake();
    });

    // 6. 恢复环境 / 编辑器相机 / 场景名
    {
        const nlohmann::json& env =
            (root.contains("environment") && root["environment"].is_object()) ? root["environment"] : kEmptyObject;
        target.skyColor = jsonToVec3(env.value("skyColor", nlohmann::json()), glm::vec3(0.18f, 0.22f, 0.35f));
        target.ambientColor = jsonToVec3(env.value("ambientColor", nlohmann::json()), glm::vec3(0.15f, 0.15f, 0.18f));
        target.showGrid = safeGet(env, "showGrid", true);
        // V0.2-5：fogColor 缺省随 skyColor 联动（旧文件无此字段 → 取刚读到的 skyColor）
        target.fogColor = jsonToVec3(env.value("fogColor", nlohmann::json()), target.skyColor);

        // 天空（V0.2-7）：全部字段默认值兜底，旧 JSON 开箱即用
        const std::string skyModeStr = safeGet(env, "skyMode", std::string("Gradient"));
        if (skyModeStr == "Cubemap") {
            target.skyMode = SkyMode::Cubemap;
        } else {
            if (skyModeStr != "Gradient")
                Logger::warn("反序列化：未知天空模式 \"{}\"，回退渐变", skyModeStr);
            target.skyMode = SkyMode::Gradient;
        }
        target.skyHorizonColor =
            jsonToVec3(env.value("skyHorizonColor", nlohmann::json()), glm::vec3(0.55f, 0.62f, 0.72f));
        target.skyZenithColor =
            jsonToVec3(env.value("skyZenithColor", nlohmann::json()), glm::vec3(0.22f, 0.30f, 0.48f));
        target.skyCubemapPath = safeGet(env, "skyCubemapPath", std::string());
        target.skyRotation = safeGet(env, "skyRotation", 0.0f);
        target.skyExposure = safeGet(env, "skyExposure", 1.0f);

        const nlohmann::json& cam =
            (root.contains("editorCamera") && root["editorCamera"].is_object()) ? root["editorCamera"] : kEmptyObject;
        target.editorCameraTarget = jsonToVec3(cam.value("target", nlohmann::json()), glm::vec3(0.0f, 1.0f, 0.0f));
        target.editorCameraDistance = safeGet(cam, "distance", 8.0f);
        target.editorCameraYaw = safeGet(cam, "yaw", 0.0f);
        target.editorCameraPitch = safeGet(cam, "pitch", -0.35f);

        target.setName(safeGet(root, "name", std::string("Untitled")));
    }
}

bool SceneSerializer::saveToFile(const Scene& scene, const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        Logger::error("保存场景失败：无法打开文件 {}", path.string());
        return false;
    }
    file << serialize(scene);
    if (!file.good()) {
        Logger::error("保存场景失败：写入文件 {} 出错", path.string());
        return false;
    }
    return true;
}

bool SceneSerializer::loadFromFile(Scene& target, const std::filesystem::path& path) {
    // 读取收敛到 AssetStream（中文路径安全；Wave 2 pak 挂载后场景也可从内存 entry 读）
    std::string content;
    if (!AssetStream::readAllText(path, content)) {
        Logger::error("加载场景失败：无法打开文件 {}", path.string());
        return false;
    }
    // 先做一次解析校验，失败时返回 false 且不改动场景
    try {
        static_cast<void>(nlohmann::json::parse(content));
    } catch (const std::exception& e) {
        Logger::error("加载场景失败：{} 不是合法的 JSON（{}）", path.string(), e.what());
        return false;
    }
    deserialize(target, content);
    return true;
}

void SceneSerializer::remapGameObjectIds(nlohmann::json& objectJson) {
    // 复制对象：递归换新 ID，含 parent 字段同步重映射。
    // 支持单对象 JSON（serializeGameObject 产物）与 {"objects":[...]} 数组两种形态；
    // 不在本 JSON 内的 parent 引用（原父级未随副本一起复制）保持不变 → 副本成为原父级的兄弟节点。
    std::unordered_map<uint64_t, uint64_t> remap;

    std::function<void(nlohmann::json&)> collect = [&](nlohmann::json& node) {
        if (node.is_object()) {
            if (node.contains("id") && node["id"].is_number_unsigned())
                remap.emplace(node["id"].get<uint64_t>(), UUID::kInvalid);
            for (const char* key : {"objects", "children"}) {
                if (node.contains(key) && node[key].is_array())
                    for (auto& child : node[key])
                        collect(child);
            }
        } else if (node.is_array()) {
            for (auto& child : node)
                collect(child);
        }
    };
    collect(objectJson);

    // 为每个旧 ID 分配新 ID（保证互不相同且非 0）
    for (auto& [oldId, newId] : remap) {
        (void)oldId;
        do {
            newId = UUID::generate();
        } while (newId == UUID::kInvalid || remap.contains(newId));
    }

    std::function<void(nlohmann::json&)> rewrite = [&](nlohmann::json& node) {
        if (node.is_object()) {
            if (node.contains("id") && node["id"].is_number_unsigned()) {
                auto it = remap.find(node["id"].get<uint64_t>());
                if (it != remap.end())
                    node["id"] = it->second;
            }
            if (node.contains("parent") && node["parent"].is_number_unsigned()) {
                auto it = remap.find(node["parent"].get<uint64_t>());
                if (it != remap.end())
                    node["parent"] = it->second;
            }
            for (auto& [key, value] : node.items()) {
                (void)key;
                rewrite(value);
            }
        } else if (node.is_array()) {
            for (auto& child : node)
                rewrite(child);
        }
    };
    rewrite(objectJson);
}

std::unique_ptr<Component> SceneSerializer::createComponentFromJson(GameObject* owner, const nlohmann::json& j) {
    // 组件分发唯一入口：内置组件查 ComponentRegistry（各组件 .cpp 用 NE_REGISTER_COMPONENT 自注册），
    // 未知再走 ScriptRegistry；两者皆未知 warn 并跳过（不崩溃）
    if (!j.is_object()) {
        Logger::warn("反序列化：组件节点不是 JSON 对象，已跳过");
        return nullptr;
    }
    const std::string type = safeGet(j, "type", std::string());
    if (auto builtin = ComponentRegistry::create(type, owner))
        return builtin;
    if (auto script = ScriptRegistry::create(type, owner))
        return script;
    Logger::warn("反序列化：未知组件类型 \"{}\"，已跳过该组件（不崩溃）", type);
    return nullptr;
}

}  // namespace NovaEngine
