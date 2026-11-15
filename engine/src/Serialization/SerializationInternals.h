#pragma once
// 内部头（非公共 API，不放 include/NovaEngine）：material 段 JSON 序列化辅助。
// MeshRenderer / PrefabSerializer（波 4）/ MaterialAsset（波 1）共用；
// 基础键名与读默认值保持 V0.2-8 schema 一致：
// albedo / roughness / metallic / albedoMap / alpha / blendMode。
// V0.3-4（PBR）：+normalMap/metallicMap/roughnessMap/emissionMap/emissionColor/emissionIntensity。
// V0.3-6（材质库双轨）：+materialAsset；materialAssetPath 非空时只写资产路径、省略内联 material 段。
#include <filesystem>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Core/UUID.h>
#include <NovaEngine/Render/Material.h>
#include <NovaEngine/Scene/ComponentRegistry.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scripting/Behaviour.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

#include <algorithm>
#include <string_view>
#include <typeinfo>

namespace NovaEngine {
namespace SerializationInternals {

// 字段类型不符（如 albedo 是字符串）时回退默认值，畸形文件不崩溃
inline glm::vec3 jsonToVec3(const nlohmann::json& j, const glm::vec3& fallback) {
    if (!j.is_array() || j.size() < 3)
        return fallback;
    try {
        return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
    } catch (const std::exception&) {
        return fallback;
    }
}

// 字符串字段读取：键缺失或类型不符（如数字）回退默认值
inline std::string jsonToString(const nlohmann::json& j, const char* key, const std::string& fallback) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_string())
        return fallback;
    return j[key].get<std::string>();
}

// 浮点字段读取：键缺失或类型不符回退默认值
inline float jsonToFloat(const nlohmann::json& j, const char* key, float fallback) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_number())
        return fallback;
    try {
        return j[key].get<float>();
    } catch (const std::exception&) {
        return fallback;
    }
}

// material → JSON 节点（调用方负责把节点挂到 "material" 键，或用作 .novamat 根）
inline void writeMaterialJson(nlohmann::json& out, const Material& material) {
    // V0.3-6 材质库双轨：与渲染端指针门一致——materialAsset 已解析 或 materialAssetPath 非空
    // 均视为整块引用资产 → 只写 "materialAsset" 键并省略内联 material 段（无 per-instance override）
    if (material.materialAsset || !material.materialAssetPath.empty()) {
        out["materialAsset"] = material.materialAssetPath;
        return;
    }

    out["albedo"] = nlohmann::json::array({material.albedo.x, material.albedo.y, material.albedo.z});
    out["roughness"] = material.roughness;
    out["metallic"] = material.metallic;
    // JSON 键名保持 schema 的 "albedoMap" 不变，值取自 material.albedoMapPath（空串 = 纯色）
    out["albedoMap"] = material.albedoMapPath;
    // V0.2-8：alpha + blendMode（字符串 "Opaque"/"AlphaBlend"）
    out["alpha"] = material.alpha;
    out["blendMode"] = material.blendMode == BlendMode::AlphaBlend ? "AlphaBlend" : "Opaque";
    // V0.3-4 PBR 新键：贴图一律写路径字符串（空串 = 无贴图）
    out["normalMap"] = material.normalMapPath;
    out["metallicMap"] = material.metallicMapPath;
    out["roughnessMap"] = material.roughnessMapPath;
    out["emissionMap"] = material.emissionMapPath;
    out["emissionColor"] = nlohmann::json::array(
        {material.emissionColor.x, material.emissionColor.y, material.emissionColor.z});
    out["emissionIntensity"] = material.emissionIntensity;
}

// material 段 JSON 节点 → Material（旧键默认值与 V0.2-8 一致；新键 V0.3-4/6 默认值兜底）
inline void readMaterialJson(const nlohmann::json& in, Material& material) {
    // 入口防御（P2-5）：material 段不是 JSON 对象（字符串/数组等畸形形态）时，
    // 全部字段填默认值并返回——「畸形文件不崩溃」不依赖调用方守卫，
    // MaterialAsset::load 等直接调用方同样受保护
    if (!in.is_object()) {
        material = Material{};
        return;
    }

    // V0.3-6 双轨：materialAsset 非空 = 以资产为准（渲染用 materialAsset->material()）。
    // 资产指针由调用方（MeshRenderer::onDeserialize）经 AssetDatabase 解析；
    // 内联字段仍读入，作为资产加载失败时的回退值（"非空 + 内联同时存在 → 资产为准"）
    material.materialAssetPath = jsonToString(in, "materialAsset", std::string());

    material.albedo = jsonToVec3(in.value("albedo", nlohmann::json()), glm::vec3(0.8f));
    material.roughness = jsonToFloat(in, "roughness", 0.6f);
    material.metallic = jsonToFloat(in, "metallic", 0.0f);

    // 贴图路径（序列化往返用）→ 运行时纹理（经 AssetDatabase 解析）：
    // albedo 按 sRGB 加载（V0.3-4 起，与 .novameta 声明共同决定内部格式）；
    // normal/metallic/roughness/emission 一律线性——法线必须线性，否则切线空间光照错误
    auto resolveTexture = [](const std::string& path, bool srgb) -> std::shared_ptr<Texture> {
        if (path.empty())
            return nullptr;
        auto tex = AssetDatabase::instance().getTexture(std::filesystem::path(path), srgb);
        if (!tex)
            Logger::warn("材质加载贴图失败：{}（回退默认）", path);
        return tex;
    };
    material.albedoMapPath = jsonToString(in, "albedoMap", std::string());
    material.albedoMap = resolveTexture(material.albedoMapPath, /*srgb=*/true);
    material.normalMapPath = jsonToString(in, "normalMap", std::string());
    material.normalMap = resolveTexture(material.normalMapPath, /*srgb=*/false);
    material.metallicMapPath = jsonToString(in, "metallicMap", std::string());
    material.metallicMap = resolveTexture(material.metallicMapPath, /*srgb=*/false);
    material.roughnessMapPath = jsonToString(in, "roughnessMap", std::string());
    material.roughnessMap = resolveTexture(material.roughnessMapPath, /*srgb=*/false);
    material.emissionMapPath = jsonToString(in, "emissionMap", std::string());
    material.emissionMap = resolveTexture(material.emissionMapPath, /*srgb=*/false);

    // V0.3-4：自发光（emissionColor 缺省 [0,0,0]，emissionIntensity 缺省 1.0）
    material.emissionColor = jsonToVec3(in.value("emissionColor", nlohmann::json()), glm::vec3(0.0f));
    material.emissionIntensity = jsonToFloat(in, "emissionIntensity", 1.0f);

    // V0.2-8：旧文件（schema 1/2）无 alpha/blendMode 字段 → 默认值兜底（1.0 / Opaque）
    material.alpha = jsonToFloat(in, "alpha", 1.0f);
    const std::string blendStr = jsonToString(in, "blendMode", std::string("Opaque"));
    if (blendStr == "AlphaBlend") {
        material.blendMode = BlendMode::AlphaBlend;
    } else {
        if (blendStr != "Opaque")
            Logger::warn("材质反序列化：未知 blendMode \"{}\"，回退 Opaque", blendStr);
        material.blendMode = BlendMode::Opaque;
    }
}

// ============================================================
// GameObject 节点写/读辅助（V0.3 波 3 抽取）
// SceneSerializer 与 PrefabSerializer 共用。键名/键序/默认值与抽取前逐项一致
// （自 SceneSerializer.cpp 原匿名命名空间整段迁移，零行为变更）：
//   节点写：id → name → active → [parent] → components
//   组件写：type → Transform(position/rotation/scale) → Behaviour(enabled) → onSerialize
//   组件读：createComponentFromJson → attach(callAwake=false) → 公共字段 → onDeserialize
// ============================================================

inline nlohmann::json vec3ToJson(const glm::vec3& v) {
    return nlohmann::json::array({v.x, v.y, v.z});
}

inline nlohmann::json quatToJson(const glm::quat& q) {
    return nlohmann::json::array({q.x, q.y, q.z, q.w});  // 四元数 [x, y, z, w]
}

inline glm::quat jsonToQuat(const nlohmann::json& j, const glm::quat& fallback) {
    if (!j.is_array() || j.size() < 4)
        return fallback;
    try {
        // glm::quat 构造参数顺序为 (w, x, y, z)
        return glm::normalize(glm::quat(j[3].get<float>(), j[0].get<float>(), j[1].get<float>(), j[2].get<float>()));
    } catch (const std::exception&) {
        return fallback;
    }
}

// 字段类型不符（如 "id" 是字符串）时回退默认值，保证畸形文件不崩溃
template <typename T>
inline T safeGet(const nlohmann::json& j, const char* key, const T& fallback) {
    try {
        return j.value(key, fallback);
    } catch (const std::exception&) {
        return fallback;
    }
}

// 脚本组件类型名：注册名 = 类名最后一段（ScriptRegistry 名字裁剪裁定），
// 与 MSVC typeid(*comp).name() 的 "class NovaGame::SpinningCube" 尾段一致
inline std::string scriptTypeName(const Component* comp) {
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

// 内置组件名按 typeId 反查 ComponentRegistry 注册名；未命中走 scriptTypeName
// （脚本组件共用 Behaviour 的 typeId，与任何内置组件 typeId 都不相等，天然不会误判）
inline std::string componentTypeName(const Component* comp) {
    for (const std::string& name : ComponentRegistry::builtinNames())
        if (ComponentRegistry::typeIdForName(name) == comp->typeId())
            return name;
    const std::string name = scriptTypeName(comp);
    if (!ScriptRegistry::exists(name))
        Logger::warn("序列化：脚本组件 \"{}\" 未在 ScriptRegistry 注册，反序列化时将被跳过", name);
    return name;
}

// 组件分发唯一入口：内置组件查 ComponentRegistry（各组件 .cpp 用 NE_REGISTER_COMPONENT
// 自注册），未知再走 ScriptRegistry；两者皆未知 warn 并跳过（不崩溃）
inline std::unique_ptr<Component> createComponentFromJson(GameObject* owner, const nlohmann::json& j) {
    if (!j.is_object()) {
        Logger::warn("反序列化：组件节点不是 JSON 对象，已跳过");
        return nullptr;
    }
    const std::string type = safeGet(j, "type", std::string());
    // 用户脚本与内置组件同名时，反序列化按约定内置优先（与序列化时 componentTypeName
    // 的内置优先判定一致），但显式告警——防静默数据丢失级替换
    if (ComponentRegistry::exists(type) && ScriptRegistry::exists(type))
        Logger::warn("反序列化：组件名 \"{}\" 同时存在内置与脚本注册，内置优先（脚本同名组件被替换）", type);
    if (auto builtin = ComponentRegistry::create(type, owner))
        return builtin;
    if (auto script = ScriptRegistry::create(type, owner))
        return script;
    Logger::warn("反序列化：未知组件类型 \"{}\"，已跳过该组件（不崩溃）", type);
    return nullptr;
}

// 组件数组 → JSON（schema 约定：第一项必须是 Transform；先 "type" 再公共字段最后 onSerialize）
inline nlohmann::json writeComponentsJson(GameObject* go) {
    std::vector<Component*> comps = go->getComponents<Component>();
    const auto it = std::find_if(comps.begin(), comps.end(),
                                 [](Component* c) { return dynamic_cast<Transform*>(c) != nullptr; });
    if (it != comps.end() && it != comps.begin())
        std::rotate(comps.begin(), it, it + 1);

    nlohmann::json arr = nlohmann::json::array();
    for (Component* c : comps) {
        nlohmann::json cj;
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
    return arr;
}

// 写出单个对象节点（id/name/active/components；withParent=true 时加 parent 键）。
// SceneSerializer（平铺格式）走默认 withParent=true；PrefabSerializer（树形格式）传 false
inline void writeGameObjectNode(nlohmann::json& out, GameObject* go, bool withParent = true) {
    out["id"] = go->id();
    out["name"] = go->name();
    out["active"] = go->activeSelf();
    if (withParent)
        out["parent"] = (go->parent() != nullptr) ? go->parent()->id() : UUID::kInvalid;
    out["components"] = writeComponentsJson(go);
}

// 组件数组读：逐组件 createComponentFromJson → attachComponent(callAwake=false) →
// Transform/Behaviour 公共字段 → onDeserialize（畸形字段仅告警保留默认值）。
// 与 SceneSerializer::deserialize 原步骤 3 逐行等价（波 3 抽取，Prefab 共用）；
// onAwake 由调用方在字段/父级就绪后统一补调
inline void readComponentsJson(GameObject* go, const nlohmann::json& compsNode) {
    if (!compsNode.is_array())
        return;
    for (const auto& cj : compsNode) {
        if (!cj.is_object())
            continue;
        std::unique_ptr<Component> comp = createComponentFromJson(go, cj);
        if (!comp)
            continue;  // 未知类型：createComponentFromJson 内部已 Logger::warn 并跳过

        const std::string typeName = safeGet(cj, "type", std::string());
        Component* raw = comp.get();
        // typeId 查 ComponentRegistry；脚本组件不在注册表 → 0（getComponents 走 dynamic_cast，不受影响）
        go->attachComponent(std::move(comp), ComponentRegistry::typeIdForName(typeName),
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

// 子树统一补调 onAwake（字段已就绪、父级已存在后；DFS 前序——与场景反序列化第 5 步同序）
inline void callAwakeRecursive(GameObject* go) {
    for (Component* c : go->getComponents<Component>())
        c->onAwake();
    for (const auto& child : go->children())
        callAwakeRecursive(child.get());
}

}  // namespace SerializationInternals
}  // namespace NovaEngine
