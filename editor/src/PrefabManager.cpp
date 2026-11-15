// PrefabManager.cpp —— 预制体编辑器侧管理（V0.3-9 波 4）
//
// Apply 语义（§14.7）：
//   1. serializeSubtree(实例根) 取当前完整状态
//   2. 与源逐字段合并：override 差异键写回源（点分路径精确写回）
//   3. 嵌套实例展开烘焙：源树中嵌套 PrefabInstance 以其当前完整状态整体替换 +
//      ID 经嵌套映射重映射回源 ID 空间 + 删除该嵌套 PrefabInstance 组件（递归更深层）
//   4. saveAsset → 刷新场景内全部同路径实例（非 override 字段重读新源值）
//   5. Undo 文件通道：FileRestore = 资产改写前内容（B 线约定：undo 原样写回，
//      redo 由消费方按 sceneJson 经 rewriteAssetFromScene 重算）
// 已知取舍（v1）：未映射的新对象以实例 ID 追加进源；源中多出（实例已删除）的对象保留；
//   override 检测 = 动态比对（不在 PrefabInstance 中持久化）。

#include "PrefabManager.h"

#include "EditorContext.h"
#include "UndoRedoManager.h"

#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/Component.h>
#include <NovaEngine/Scene/ComponentRegistry.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/PrefabInstance.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scripting/Behaviour.h>
#include <NovaEngine/Serialization/PrefabSerializer.h>
#include <NovaEngine/Serialization/SceneSerializer.h>

#include <algorithm>
#include <cstring>
#include <typeinfo>
#include <utility>

// 说明（P1-1/P2-1 修复）：引擎的 SerializationInternals.h 是 engine/src 内部头，不在编辑器
// include 路径；此处内联镜像其「公共字段」写/读语义（键名/默认值逐字一致）——
// Transform(position/rotation/scale)、Behaviour(enabled)。若语义升级须两边同步。

namespace NovaEditor {

namespace {

using namespace NovaEngine;

// ===== 源文档缓存 =====
std::unordered_map<std::string, nlohmann::json> s_cache;

// 文档 {"root": ...} → 根节点指针；文档本身即节点（防御兼容）也可
const nlohmann::json* rootNodeOf(const nlohmann::json& doc) {
    if (!doc.is_object()) return nullptr;
    if (doc.contains("root") && doc["root"].is_object())
        return &doc["root"];
    if (doc.contains("components"))
        return &doc;
    return nullptr;
}

// ===== 公共字段写/读（镜像 SerializationInternals 语义，P1-1/P2-1 修复）=====
// Transform/Behaviour 无 onDeserialize 重写——其公共字段由序列化公共层读写。
// 此处内联同构实现，保证 override 比对与 Revert 对公共字段真实生效。
std::string componentTypeNameLocal(const Component* comp);   // 前向声明（定义在本文件稍后）
nlohmann::json vec3ToJsonLocal(const glm::vec3& v) {
    return nlohmann::json::array({v.x, v.y, v.z});
}
nlohmann::json quatToJsonLocal(const glm::quat& q) {
    return nlohmann::json::array({q.x, q.y, q.z, q.w});
}
glm::vec3 jsonToVec3Local(const nlohmann::json& j, const glm::vec3& fallback) {
    if (!j.is_array() || j.size() < 3)
        return fallback;
    try {
        return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
    } catch (const std::exception&) {
        return fallback;
    }
}
glm::quat jsonToQuatLocal(const nlohmann::json& j, const glm::quat& fallback) {
    if (!j.is_array() || j.size() < 4)
        return fallback;
    try {
        return glm::normalize(glm::quat(j[3].get<float>(), j[0].get<float>(), j[1].get<float>(), j[2].get<float>()));
    } catch (const std::exception&) {
        return fallback;
    }
}

// 单组件完整 JSON（type + 公共字段 + onSerialize）——与 SerializationInternals::writeComponentsJson 同构
nlohmann::json componentFullJson(const Component* comp) {
    nlohmann::json cj;
    cj["type"] = componentTypeNameLocal(comp);
    if (const auto* t = dynamic_cast<const Transform*>(comp)) {
        cj["position"] = vec3ToJsonLocal(t->localPosition());
        cj["rotation"] = quatToJsonLocal(t->localRotation());
        cj["scale"] = vec3ToJsonLocal(t->localScale());
    }
    if (const auto* b = dynamic_cast<const Behaviour*>(comp))
        cj["enabled"] = b->enabled;
    comp->onSerialize(cj);
    return cj;
}

// 把单组件 JSON（含公共字段）应用到实例上已存在的同类型组件（P1-1 修复：
// Transform/Behaviour 的公共字段必须显式写回；缺省键不覆盖 = 单字段 revert 语义）
void applyComponentJson(GameObject* go, const nlohmann::json& cj) {
    const std::string type = cj.value("type", "");
    Component* target = nullptr;
    for (Component* c : go->getComponents<Component>())
        if (componentTypeNameLocal(c) == type) { target = c; break; }
    if (!target) return;
    if (auto* t = dynamic_cast<Transform*>(target)) {
        t->setLocalPosition(jsonToVec3Local(cj.value("position", nlohmann::json()), t->localPosition()));
        t->setLocalRotation(jsonToQuatLocal(cj.value("rotation", nlohmann::json()), t->localRotation()));
        t->setLocalScale(jsonToVec3Local(cj.value("scale", nlohmann::json()), t->localScale()));
    }
    if (auto* b = dynamic_cast<Behaviour*>(target))
        b->enabled = cj.contains("enabled") && cj["enabled"].is_boolean() ? cj["enabled"].get<bool>()
                                                                          : b->enabled;
    target->onDeserialize(cj);   // 扩展字段（仅含 JSON 中出现的键）
}

const nlohmann::json* findComponent(const nlohmann::json& node, const std::string& type);   // DevOps 修复：前向声明（定义在下方 153 行，此处先于定义使用 → C3861）

// Apply 成功后把「以实例 ID 进入源」的新对象补进映射（其源 ID = 实例 ID，P1-2 修复配套）；
// 嵌套实例子树由其自身映射治理，不递归（v1 语义，文档化）
void syncNewObjectIds(const nlohmann::json& node, std::unordered_map<uint64_t, uint64_t>& map,
                      bool isRoot) {
    if (!node.is_object()) return;
    if (!isRoot && findComponent(node, "PrefabInstance"))
        return;   // 嵌套实例根：子树归嵌套映射
    const uint64_t id = node.value("id", uint64_t(0));
    if (id != 0 && !map.contains(id))
        map[id] = id;
    if (node.contains("children") && node["children"].is_array())
        for (const auto& ch : node["children"])
            syncNewObjectIds(ch, map, false);
}

// 组件类型名（与 SceneSerializer 同约定）：内置查 ComponentRegistry 反查，脚本走 typeid 尾段
std::string componentTypeNameLocal(const Component* comp) {
    for (const std::string& n : ComponentRegistry::builtinNames())
        if (ComponentRegistry::typeIdForName(n) == comp->typeId())
            return n;
    std::string raw = typeid(*comp).name();
    for (const char* prefix : {"class ", "struct ", "enum "}) {
        if (raw.starts_with(prefix)) {
            raw.erase(0, std::strlen(prefix));
            break;
        }
    }
    const auto pos = raw.rfind("::");
    return (pos == std::string::npos) ? raw : raw.substr(pos + 2);
}

// 节点内按类型找组件（const / 可变）
const nlohmann::json* findComponent(const nlohmann::json& node, const std::string& type) {
    if (!node.contains("components") || !node["components"].is_array()) return nullptr;
    for (const auto& c : node["components"])
        if (c.is_object() && c.value("type", "") == type)
            return &c;
    return nullptr;
}
nlohmann::json* findComponentMutable(nlohmann::json& node, const std::string& type) {
    if (!node.contains("components") || !node["components"].is_array()) return nullptr;
    for (auto& c : node["components"])
        if (c.is_object() && c.value("type", "") == type)
            return &c;
    return nullptr;
}

// 按 ID 递归查找节点 / 可变子节点
const nlohmann::json* findNodeById(const nlohmann::json& node, uint64_t id) {
    if (!node.is_object()) return nullptr;
    if (node.value("id", uint64_t(0)) == id) return &node;
    if (node.contains("children") && node["children"].is_array())
        for (const auto& ch : node["children"])
            if (const auto* hit = findNodeById(ch, id))
                return hit;
    return nullptr;
}
nlohmann::json* findChildMutable(nlohmann::json& node, uint64_t id) {
    if (!node.contains("children") || !node["children"].is_array()) return nullptr;
    for (auto& ch : node["children"])
        if (ch.is_object() && ch.value("id", uint64_t(0)) == id)
            return &ch;
    return nullptr;
}

// PrefabInstance 组件序列化字段 "sourceIds" = [[instId, srcId], ...] → 映射
std::unordered_map<uint64_t, uint64_t> parseSourceIdMap(const nlohmann::json& compJson) {
    std::unordered_map<uint64_t, uint64_t> map;
    if (!compJson.contains("sourceIds") || !compJson["sourceIds"].is_array()) return map;
    for (const auto& pair : compJson["sourceIds"]) {
        if (!pair.is_array() || pair.size() < 2) continue;
        map[pair[0].get<uint64_t>()] = pair[1].get<uint64_t>();
    }
    return map;
}

// 删除节点内指定类型组件（全部命中）
void eraseComponentByType(nlohmann::json& node, const std::string& type) {
    if (!node.contains("components") || !node["components"].is_array()) return;
    // DevOps 修复：std::erase_if 不匹配 nlohmann::json（无匹配重载）→ 等价 erase-remove 惯用法
    auto& comps = node["components"];
    comps.erase(std::remove_if(comps.begin(), comps.end(),
                               [&](const nlohmann::json& c) {
                                   return c.is_object() && c.value("type", "") == type;
                               }),
                comps.end());
}

// 递归 ID 重映射（node 自身 + 全部子孙；map 未覆盖的 ID 保留原值）
void remapIds(nlohmann::json& node, const std::unordered_map<uint64_t, uint64_t>& map) {
    if (!node.is_object()) return;
    if (node.contains("id")) {
        auto it = map.find(node.value("id", uint64_t(0)));
        if (it != map.end())
            node["id"] = it->second;
    }
    if (node.contains("children") && node["children"].is_array())
        for (auto& ch : node["children"])
            remapIds(ch, map);
}

// 就地展开嵌套实例：node 自身或子孙带 PrefabInstance 组件 →
//   以其当前完整状态为内容、组件删除、ID 经其映射重映射回源 ID 空间；递归更深层嵌套
void bakeSubtree(nlohmann::json& node) {
    if (!node.is_object()) return;
    if (const nlohmann::json* instComp = findComponent(node, "PrefabInstance")) {
        const auto nestedMap = parseSourceIdMap(*instComp);
        eraseComponentByType(node, "PrefabInstance");
        remapIds(node, nestedMap);
    }
    if (node.contains("children") && node["children"].is_array())
        for (auto& ch : node["children"])
            bakeSubtree(ch);
}

// 点分路径读写（"material.albedo"）
const nlohmann::json* getJsonPath(const nlohmann::json& root, const std::string& dotted) {
    const nlohmann::json* cur = &root;
    size_t pos = 0;
    while (cur->is_object() && pos < dotted.size()) {
        const size_t dot = dotted.find('.', pos);
        const std::string key = dotted.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos);
        const auto it = cur->find(key);
        if (it == cur->end()) return nullptr;
        cur = &(*it);
        if (dot == std::string::npos) break;
        pos = dot + 1;
    }
    return cur;
}
void setJsonPath(nlohmann::json& root, const std::string& dotted, const nlohmann::json& value) {
    nlohmann::json* cur = &root;
    size_t pos = 0;
    while (true) {
        const size_t dot = dotted.find('.', pos);
        const std::string key = dotted.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos);
        if (dot == std::string::npos) {
            (*cur)[key] = value;
            return;
        }
        if (!cur->contains(key) || !(*cur)[key].is_object())
            (*cur)[key] = nlohmann::json::object();
        cur = &(*cur)[key];
        pos = dot + 1;
    }
}

// ===== 合并（Apply 第 2/3 步）=====
// 根层合并：override 差异键写回源；根 PrefabInstance 组件是实例记录本身，不写回源；
// children 中带 PrefabInstance 的子树走烘焙分支（整体替换 + ID 重映射 + 删组件）
bool mergeRootIntoSource(nlohmann::json& srcNode, const nlohmann::json& instNode,
                         const std::unordered_map<uint64_t, uint64_t>& map) {
    bool changed = false;
    // 1. 组件合并
    if (instNode.contains("components") && instNode["components"].is_array()) {
        for (const auto& ic : instNode["components"]) {
            if (!ic.is_object()) continue;
            const std::string type = ic.value("type", "");
            if (type.empty() || type == "PrefabInstance") continue;
            nlohmann::json* srcComp = findComponentMutable(srcNode, type);
            if (!srcComp) {
                if (!srcNode.contains("components") || !srcNode["components"].is_array())
                    srcNode["components"] = nlohmann::json::array();
                srcNode["components"].push_back(ic);   // 实例新增组件写回源
                changed = true;
            } else {
                for (const auto& key : PrefabManager::fieldCompare(ic, *srcComp)) {
                    if (const auto* v = getJsonPath(ic, key))
                        setJsonPath(*srcComp, key, *v);
                    changed = true;
                }
            }
        }
    }
    // 2. children 递归
    if (instNode.contains("children") && instNode["children"].is_array()) {
        if (!srcNode.contains("children") || !srcNode["children"].is_array())
            srcNode["children"] = nlohmann::json::array();
        for (const auto& ich : instNode["children"]) {
            if (!ich.is_object()) continue;
            if (findComponent(ich, "PrefabInstance")) {
                // 嵌套实例：整体烘焙替换（ID 已重映射为源 ID）
                nlohmann::json baked = ich;
                bakeSubtree(baked);
                const uint64_t srcId = baked.value("id", uint64_t(0));
                if (nlohmann::json* dst = findChildMutable(srcNode, srcId))
                    *dst = baked;
                else
                    srcNode["children"].push_back(baked);
                changed = true;
            } else {
                const uint64_t instId = ich.value("id", uint64_t(0));
                const auto mit = map.find(instId);
                if (mit == map.end()) {
                    // P1-2 修复：首次 Apply 后新对象已以实例 ID 进入源——先按 instId 在源树
                    // 查重，命中则并入合并流程（防重复 Apply 重复追加）；未命中才追加完整副本
                    //（保留实例 ID，v1 取舍见文件头；Apply 成功后 syncNewObjectIds 会把其补进映射）
                    if (nlohmann::json* existing = findChildMutable(srcNode, instId))
                        changed |= mergeRootIntoSource(*existing, ich, map);
                    else {
                        srcNode["children"].push_back(ich);
                        changed = true;
                    }
                } else if (nlohmann::json* srcChild = findChildMutable(srcNode, mit->second)) {
                    changed |= mergeRootIntoSource(*srcChild, ich, map);
                } else {
                    nlohmann::json copy = ich;
                    copy["id"] = mit->second;
                    srcNode["children"].push_back(std::move(copy));
                    changed = true;
                }
            }
        }
    }
    return changed;
}

// ===== 实例刷新（Apply 第 5 步）=====
void refreshComponent(GameObject* go, Component* comp,
                      const std::unordered_map<uint64_t, uint64_t>& map,
                      const nlohmann::json& oldRoot, const nlohmann::json& newRoot) {
    const std::string type = componentTypeNameLocal(comp);
    const auto mit = map.find(go->id());
    if (mit == map.end()) return;   // 新对象：无源可循
    const nlohmann::json oldComp =
        PrefabManager::sourceComponentJson(oldRoot, map, go->id(), type);
    const nlohmann::json newComp =
        PrefabManager::sourceComponentJson(newRoot, map, go->id(), type);
    if (oldComp.is_null() || newComp.is_null()) return;   // 源中无此组件（实例新增）→ 保留实例值

    // P2-1 修复：比对与写回均用「完整组件 JSON」（含 Transform/Behaviour 公共字段），
    // 避免公共字段被 onSerialize 空输出漏比对/漏写回
    const nlohmann::json instJson = componentFullJson(comp);
    const auto overrides = PrefabManager::fieldCompare(instJson, oldComp);   // 相对旧源的差异 = override
    nlohmann::json applied = newComp;
    for (const auto& key : overrides)
        if (const auto* v = getJsonPath(instJson, key))
            setJsonPath(applied, key, *v);
    applyComponentJson(go, applied);   // 非 override 字段重读新源值（含公共字段），override 字段保留实例值
}

void refreshNode(GameObject* go, const std::unordered_map<uint64_t, uint64_t>& map,
                 const nlohmann::json& oldRoot, const nlohmann::json& newRoot) {
    // 1. 本对象组件刷新（PrefabInstance 组件本身跳过——嵌套实例记录不在源中）
    for (Component* comp : go->getComponents<Component>()) {
        if (comp->typeId() == Component::componentTypeId<PrefabInstance>())
            continue;
        refreshComponent(go, comp, map, oldRoot, newRoot);
    }
    // 2. 子节点：若 go 是嵌套实例根 → 其子树用其自身映射
    const auto* nested = go->getComponent<PrefabInstance>();
    const auto& childMap = nested ? nested->instanceToSource : map;
    for (const auto& child : go->children())
        refreshNode(child.get(), childMap, oldRoot, newRoot);
}

}  // namespace

const nlohmann::json* PrefabManager::sourceJson(const std::filesystem::path& prefabPath) {
    if (prefabPath.empty()) return nullptr;
    const std::string key = prefabPath.generic_string();
    if (auto it = s_cache.find(key); it != s_cache.end())
        return &it->second;
    nlohmann::json doc;
    if (!PrefabSerializer::loadAsset(prefabPath, doc)) {
        Logger::warn("PrefabManager: 源资产加载失败 {}（override 对比降级禁用）", key);
        return nullptr;
    }
    if (!rootNodeOf(doc)) {
        Logger::warn("PrefabManager: 源资产结构不符（缺 root 节点）{}", key);
        return nullptr;
    }
    return &(s_cache[key] = std::move(doc));
}

void PrefabManager::clearCache() {
    s_cache.clear();
}

std::filesystem::path PrefabManager::createPrefabAsset(GameObject* root,
                                                       const std::filesystem::path& relPath) {
    if (!root || relPath.empty()) return {};
    // serializeSubtree 输出即 {"version","name","root"} 文档，与 instantiateSubtree 同构
    const nlohmann::json doc = nlohmann::json::parse(PrefabSerializer::serializeSubtree(*root));
    if (!PrefabSerializer::saveAsset(relPath, doc)) {
        Logger::error("PrefabManager: 创建预制体失败 {}", relPath.string());
        return {};
    }
    s_cache.erase(relPath.generic_string());
    Logger::info("PrefabManager: 已创建预制体 {}", relPath.string());
    return relPath;
}

std::vector<std::string> PrefabManager::fieldCompare(const nlohmann::json& a,
                                                     const nlohmann::json& b) {
    std::vector<std::string> out;
    // 递归比较对象键（点分路径）；忽略 "type"（组件类型标识，非字段）
    struct Impl {
        static void go(const std::string& prefix, const nlohmann::json& x, const nlohmann::json& y,
                       std::vector<std::string>& out) {
            if (!x.is_object() || !y.is_object()) {
                if (x != y && !prefix.empty()) out.push_back(prefix);
                return;
            }
            for (auto it = x.begin(); it != x.end(); ++it) {
                if (it.key() == "type") continue;
                const std::string key = prefix.empty() ? it.key() : prefix + "." + it.key();
                const auto bit = y.find(it.key());
                if (bit == y.end()) { out.push_back(key); continue; }
                Impl::go(key, it.value(), bit.value(), out);
            }
            for (auto it = y.begin(); it != y.end(); ++it) {
                if (it.key() == "type") continue;
                if (!x.contains(it.key()))
                    out.push_back(prefix.empty() ? it.key() : prefix + "." + it.key());
            }
        }
    };
    Impl::go("", a, b, out);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

nlohmann::json PrefabManager::sourceComponentJson(const nlohmann::json& srcRoot,
                                                  const std::unordered_map<uint64_t, uint64_t>& map,
                                                  uint64_t instObjId, const std::string& typeName) {
    const auto mit = map.find(instObjId);
    if (mit == map.end()) return nlohmann::json();
    const nlohmann::json* node = findNodeById(srcRoot, mit->second);
    if (!node) return nlohmann::json();
    const nlohmann::json* comp = findComponent(*node, typeName);
    return comp ? *comp : nlohmann::json();
}

void PrefabManager::collectObjectOverrides(
    const nlohmann::json& srcDoc, uint64_t srcObjId, GameObject* go,
    std::unordered_map<std::string, std::vector<std::string>>& out) {
    const nlohmann::json* srcRoot = rootNodeOf(srcDoc);
    if (!srcRoot) return;
    const nlohmann::json* srcNode = findNodeById(*srcRoot, srcObjId);
    if (!srcNode) return;
    for (const Component* comp : go->getComponents<Component>()) {
        const std::string type = componentTypeNameLocal(comp);
        if (type.empty() || type == "PrefabInstance") continue;
        const nlohmann::json* srcComp = findComponent(*srcNode, type);
        if (!srcComp) continue;   // 实例新增组件：无 override 概念
        // P2-1 修复：完整组件 JSON（含 Transform/Behaviour 公共字段）参与比对——
        // 未改过不亮、改过才亮（此前 onSerialize 空输出对公共字段恒假阳性）
        auto keys = fieldCompare(componentFullJson(comp), *srcComp);
        if (!keys.empty())
            out[type] = std::move(keys);
    }
}

bool PrefabManager::applyToAsset(EditorContext& ctx, GameObject* instanceRoot) {
    using namespace NovaEngine;
    if (!instanceRoot) return false;
    auto* inst = instanceRoot->getComponent<PrefabInstance>();
    if (!inst || inst->prefabPath.empty()) {
        Logger::warn("Prefab Apply: 对象无 PrefabInstance 组件，无法应用");
        return false;
    }

    // P2-3 修复：空映射 = 实例与源完全失联（异常态）——整树按实例 ID 追加会污染源，中止
    if (inst->instanceToSource.empty()) {
        Logger::warn("Prefab Apply: instanceToSource 为空（实例与源失联），已中止");
        return false;
    }

    // 1. 旧源（refresh 差异基准 + Undo 文件通道旧内容）
    const nlohmann::json* srcPtr = sourceJson(inst->prefabPath);
    if (!srcPtr) return false;
    const nlohmann::json oldSrc = *srcPtr;
    nlohmann::json src = oldSrc;
    nlohmann::json* srcRoot = const_cast<nlohmann::json*>(rootNodeOf(src));
    if (!srcRoot) {
        Logger::warn("Prefab Apply: 源文档结构不符（缺 root 节点）{}", inst->prefabPath);   // DevOps 修复：prefabPath 已是 std::string，无 .string() 成员
        return false;
    }

    // 2. 实例树（当前完整状态）
    nlohmann::json instRoot;
    try {
        instRoot = nlohmann::json::parse(PrefabSerializer::serializeSubtree(*instanceRoot));
    } catch (const std::exception& ex) {
        Logger::error("Prefab Apply: 实例序列化失败: {}", ex.what());
        return false;
    }

    // 3. 合并（override 写回 + 嵌套烘焙；根实例记录组件不写回）
    mergeRootIntoSource(*srcRoot, rootNodeOf(instRoot) ? *rootNodeOf(instRoot) : instRoot,
                        inst->instanceToSource);

    // 4. saveAsset + 缓存失效（下次 sourceJson 重读新内容）
    if (!PrefabSerializer::saveAsset(inst->prefabPath, src)) {
        Logger::error("Prefab Apply: 保存资产失败 {}", inst->prefabPath);   // DevOps 修复：同上，std::string 无 .string()
        return false;
    }
    clearCache();
    // P1-2 修复配套：保存成功后把「以实例 ID 进入源」的新对象补进映射（源 ID = 实例 ID）——
    // 后续 Apply 的 override 检测/合并对这些对象立即生效，且不再重复追加
    syncNewObjectIds(instRoot, inst->instanceToSource, /*isRoot=*/true);

    // 5. 刷新场景内全部同路径实例（含自身：override 字段保留，其余重读新源值）
    const nlohmann::json* newSrcPtr = sourceJson(inst->prefabPath);
    if (newSrcPtr) {
        const nlohmann::json* oldRoot = rootNodeOf(oldSrc);
        const nlohmann::json* newRoot = rootNodeOf(*newSrcPtr);
        if (oldRoot && newRoot)
            for (const PrefabInstance* p : ctx.scene->findComponents<PrefabInstance>())
                if (p->prefabPath == inst->prefabPath && p->gameObject())
                    refreshNode(p->gameObject(), p->instanceToSource, *oldRoot, *newRoot);
    }

    // 6. Undo 文件通道：场景快照 + 资产改写前内容（B 线约定：undo 原样写回）
    ctx.undoRedo.pushSnapshotWithFiles(
        SceneSerializer::serialize(*ctx.scene),
        {FileRestore{inst->prefabPath, oldSrc.dump(4)}});
    ctx.sceneDirty = true;   // 手动置脏（pushSnapshotWithFiles 已入栈，避免 markSceneDirty 双推）
    Logger::info("Prefab Apply 完成: {}（override 写回 + 嵌套烘焙 + 实例刷新）",
                 inst->prefabPath);   // DevOps 修复：同上
    return true;
}

bool PrefabManager::revertComponent(EditorContext& ctx, GameObject* instObj,
                                    const std::string& typeName,
                                    const std::vector<std::string>& onlyKeys) {
    using namespace NovaEngine;
    if (!instObj || typeName.empty()) return false;
    GameObject* root = findInstanceRoot(instObj);
    auto* inst = root ? root->getComponent<PrefabInstance>() : nullptr;
    if (!inst) return false;
    const nlohmann::json* srcPtr = sourceJson(inst->prefabPath);
    if (!srcPtr) return false;
    const nlohmann::json* srcRoot = rootNodeOf(*srcPtr);
    if (!srcRoot) return false;

    // 找实例组件（按类型名匹配）与其源组件 JSON
    Component* target = nullptr;
    for (Component* comp : instObj->getComponents<Component>())
        if (componentTypeNameLocal(comp) == typeName) { target = comp; break; }
    if (!target) return false;
    const nlohmann::json srcComp =
        sourceComponentJson(*srcRoot, inst->instanceToSource, instObj->id(), typeName);
    if (srcComp.is_null()) return false;

    // 单字段 revert：源组件 JSON 取单键子集（缺省键不覆盖实例当前值）；
    // 子集必须携带 "type" 供 applyComponentJson 定位实例组件
    nlohmann::json revertJson = srcComp;
    if (!onlyKeys.empty()) {
        revertJson = nlohmann::json::object();
        revertJson["type"] = srcComp.value("type", typeName);
        for (const auto& key : onlyKeys)
            if (const auto* v = getJsonPath(srcComp, key))
                setJsonPath(revertJson, key, *v);
    }
    // P1-1 修复：经 applyComponentJson 走公共字段写回通道——Transform/Behaviour 无
    // onDeserialize 重写，直接 onDeserialize 会静默 no-op（「改位置后 Revert」必须真实生效）
    applyComponentJson(instObj, revertJson);
    ctx.markSceneDirty();   // Revert 不改文件：普通场景快照即可（B 线约定空文件清单）
    Logger::info("Prefab Revert: {}.{} 已还原为源值", instObj->name(),
                 onlyKeys.empty() ? typeName : onlyKeys.front());
    return true;
}

GameObject* PrefabManager::findInstanceRoot(GameObject* go) {
    for (GameObject* cur = go; cur != nullptr; cur = cur->parent())
        if (cur->getComponent<NovaEngine::PrefabInstance>())
            return cur;
    return nullptr;
}

void PrefabManager::revertAll(EditorContext& ctx, GameObject* instObj) {
    // 递归整棵实例子树：全部组件（PrefabInstance 记录组件除外）onDeserialize 回源值；
    // 嵌套实例根的子树按其自身映射溯源（revertComponent 内部经 findInstanceRoot 定位）
    if (!instObj) return;
    for (Component* comp : instObj->getComponents<Component>()) {
        if (comp->typeId() == Component::componentTypeId<PrefabInstance>())
            continue;
        revertComponent(ctx, instObj, componentTypeNameLocal(comp));
    }
    for (const auto& child : instObj->children())
        revertAll(ctx, child.get());
}

bool PrefabManager::rewriteAssetFromScene(EditorContext& ctx, const std::filesystem::path& prefabPath) {
    // redo 文件重算：场景内首个同路径实例的当前子树重新序列化写回
    for (const NovaEngine::PrefabInstance* p : ctx.scene->findComponents<NovaEngine::PrefabInstance>()) {
        if (p->prefabPath != prefabPath || !p->gameObject()) continue;
        const nlohmann::json doc =
            nlohmann::json::parse(PrefabSerializer::serializeSubtree(*p->gameObject()));
        if (!PrefabSerializer::saveAsset(prefabPath, doc)) return false;
        s_cache.erase(prefabPath.generic_string());
        return true;
    }
    return false;
}

}  // namespace NovaEditor
