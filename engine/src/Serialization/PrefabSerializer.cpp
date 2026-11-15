// PrefabSerializer.cpp —— 预制体序列化与实例化（V0.3-9 引擎侧，波 3 前置）
//
// 树形 JSON（与场景平铺格式的差异）：无 parent 字段、children[] 嵌套表达层级、
// 根外覆 version/name。节点其余键（id/name/active/components）与场景文件逐字一致，
// 组件写/读完全复用 SerializationInternals 共享辅助——组件键名/键序/默认值单一来源。
//
// 实例化顺序与场景反序列化同构：建节点 → 读组件（callAwake=false）→ 递归 children
// → setParent(keepWorldTransform=false，直接用树内局部值) → 子树统一补调 onAwake
// （DFS 前序，父先于子）。

#include <NovaEngine/Serialization/PrefabSerializer.h>

#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Core/UUID.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/Transform.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <memory>
#include <vector>

#include "SerializationInternals.h"

namespace NovaEngine {

using namespace SerializationInternals;

namespace {

const nlohmann::json kEmptyArray = nlohmann::json::array();

// 递归写树形节点：id/name/active/components + children（无 parent）
void writePrefabNode(nlohmann::json& out, GameObject* go) {
    writeGameObjectNode(out, go, /*withParent=*/false);
    nlohmann::json children = nlohmann::json::array();
    for (const auto& child : go->children()) {
        nlohmann::json childNode;
        writePrefabNode(childNode, child.get());
        children.push_back(std::move(childNode));
    }
    out["children"] = std::move(children);
    // DDOL（评审 P1-3）：树格式与场景平铺格式同键名同语义——仅 true 时写、追加于末。
    // 缺失此键时 SceneManager DDOL 迁移（serializeSubtree → instantiateSubtreeKeepingIds）
    // 挂回后标志复位 false，第二次 LoadScene 时 DDOL 对象会随旧场景销毁
    if (go->dontDestroyOnLoad())
        out["dontDestroyOnLoad"] = true;
}

// 解析 + 根校验：失败返回 false（实例化中止）；版本不符仅 warn 仍尝试
bool parseTree(const std::string& treeJson, nlohmann::json& outTree) {
    try {
        outTree = nlohmann::json::parse(treeJson);
    } catch (const std::exception& e) {
        Logger::error("[PrefabSerializer] 预制体 JSON 解析失败（{}），实例化中止", e.what());
        return false;
    }
    if (!outTree.is_object()) {
        Logger::error("[PrefabSerializer] 预制体 JSON 根节点不是对象，实例化中止");
        return false;
    }
    const int version = safeGet(outTree, "version", 0);
    if (version != 1)
        Logger::warn("[PrefabSerializer] 预制体版本为 {}（期望 1），仍尝试实例化", version);
    if (!outTree.contains("root") || !outTree["root"].is_object()) {
        Logger::error("[PrefabSerializer] 预制体缺少 root 节点，实例化中止");
        return false;
    }
    return true;
}

// 递归实例化单节点（不含 onAwake——由公共入口在整树父级就位后统一补调）：
// 本节点 → children → 子节点 setParent(本节点) → 本节点 setParent(parent)
GameObject* instantiateNode(Scene& scene, const nlohmann::json& node, bool keepIds,
                            std::unordered_map<uint64_t, uint64_t>* outMap, GameObject* parent) {
    if (!node.is_object()) {
        Logger::warn("[PrefabSerializer] 树节点不是 JSON 对象，已跳过");
        return nullptr;
    }
    const uint64_t srcId = safeGet(node, "id", uint64_t(UUID::kInvalid));
    uint64_t id = keepIds ? srcId : UUID::generate();
    if (id == UUID::kInvalid)
        id = UUID::generate();  // 防御：源 id 缺失/为 0（keepIds 时 Scene::instantiate 内部亦有冲突重生）
    GameObject* go = scene.instantiate(id, safeGet(node, "name", std::string("GameObject")));
    go->setActiveSelf(safeGet(node, "active", true));
    // DDOL（评审 P1-3）：读回树内 "dontDestroyOnLoad"（缺省 false，与场景平铺格式同默认），
    // 保证 DDOL 对象经预制体树格式迁移后标志不丢、后续 LoadScene 继续存活
    go->setDontDestroyOnLoad(safeGet(node, "dontDestroyOnLoad", false));
    if (outMap && !keepIds)
        outMap->emplace(go->id(), srcId);  // 新实例ID → 源ID（波 4 PrefabInstance override 消费）

    const nlohmann::json& compsNode =
        (node.contains("components") && node["components"].is_array()) ? node["components"]
                                                                       : kEmptyArray;
    readComponentsJson(go, compsNode);
    // 缺 Transform 防御（与场景反序列化 3.5 同策）：畸形预制体不空指针崩溃
    if (go->transform() == nullptr) {
        Logger::warn("[PrefabSerializer] 节点 \"{}\" 缺少 Transform 组件，已补充默认 Transform",
                     go->name());
        go->attachComponent(std::make_unique<Transform>(go), Component::componentTypeId<Transform>(),
                            /*callAwake=*/false);
    }

    if (node.contains("children") && node["children"].is_array()) {
        for (const auto& cj : node["children"]) {
            GameObject* childRoot = instantiateNode(scene, cj, keepIds, outMap, nullptr);
            if (!childRoot) continue;
            childRoot->setParent(go, /*keepWorldTransform=*/false);  // 直接用树内局部值
        }
    }

    if (parent)
        go->setParent(parent, /*keepWorldTransform=*/false);
    return go;
}

}  // namespace

std::string PrefabSerializer::serializeSubtree(GameObject& root) {
    nlohmann::json out;
    out["version"] = 1;
    out["name"] = root.name();
    writePrefabNode(out["root"], &root);
    return out.dump(4);
}

GameObject* PrefabSerializer::instantiateSubtree(
    Scene& scene, const std::string& treeJson,
    std::unordered_map<uint64_t, uint64_t>* outInstanceToSource, GameObject* parent) {
    nlohmann::json tree;
    if (!parseTree(treeJson, tree)) return nullptr;
    GameObject* root = instantiateNode(scene, tree["root"], /*keepIds=*/false,
                                       outInstanceToSource, parent);
    if (root) callAwakeRecursive(root);
    return root;
}

GameObject* PrefabSerializer::instantiateSubtreeKeepingIds(Scene& scene, const std::string& treeJson,
                                                           GameObject* parent) {
    nlohmann::json tree;
    if (!parseTree(treeJson, tree)) return nullptr;
    // 保留源 ID（DDOL 跨场景迁移）；ID 冲突由 Scene::instantiate 内部重生成并 warn
    GameObject* root = instantiateNode(scene, tree["root"], /*keepIds=*/true, nullptr, parent);
    if (root) callAwakeRecursive(root);
    return root;
}

bool PrefabSerializer::loadAsset(const std::filesystem::path& relPath, nlohmann::json& out) {
    std::string content;
    if (!AssetStream::readAllText(relPath, content)) {
        Logger::error("[PrefabSerializer] 加载预制体失败：无法读取 {}", relPath.string());
        return false;
    }
    try {
        out = nlohmann::json::parse(content);
    } catch (const std::exception& e) {
        Logger::error("[PrefabSerializer] 加载预制体失败：{} 不是合法的 JSON（{}）", relPath.string(),
                      e.what());
        return false;
    }
    return true;
}

bool PrefabSerializer::saveAsset(const std::filesystem::path& relPath, const nlohmann::json& root) {
    // 与 SceneSerializer::saveToFile 同策：ofstream 直写（AssetStream 无写通道）
    std::ofstream file(relPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        Logger::error("[PrefabSerializer] 保存预制体失败：无法打开文件 {}", relPath.string());
        return false;
    }
    file << root.dump(4);
    if (!file.good()) {
        Logger::error("[PrefabSerializer] 保存预制体失败：写入文件 {} 出错", relPath.string());
        return false;
    }
    return true;
}

}  // namespace NovaEngine
