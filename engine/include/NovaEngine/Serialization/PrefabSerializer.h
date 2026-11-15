#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

#include <nlohmann/json_fwd.hpp>

namespace NovaEngine {

class Scene;
class GameObject;

// 预制体序列化（V0.3-9 引擎侧，波 3 前置；冻结签名——C 线 SceneManager 按此引用）：
// .novaprefab 树形 JSON：{version:1, name, root:{id,name,active,components:[...],children:[...]}}
// 树节点无 parent 字段（层级由 children 嵌套表达）；组件节点结构与场景文件一致
// （复用 engine/src/Serialization/SerializationInternals.h 共享写/读）。
// PrefabInstance 组件与 override 编辑（Apply/Revert/嵌套烘焙）属波 4，头不变补实现。
class PrefabSerializer {
public:
    // 以 root 为根的子树 → 树形 JSON 字符串（Transform 局部值照写，不含 root 的父级）
    static std::string serializeSubtree(GameObject& root);

    // 树形 JSON → 递归实例化（全部 ID 重生成；组件挂载走 ComponentRegistry；
    // 挂到 parent 之下（nullptr = 场景根）；outInstanceToSource 追加 新实例ID→源ID
    // 映射（不清理，供同场景多实例累积；波 4 PrefabInstance 消费）；失败返回 nullptr
    static GameObject* instantiateSubtree(Scene& scene, const std::string& treeJson,
                                          std::unordered_map<uint64_t, uint64_t>* outInstanceToSource,
                                          GameObject* parent = nullptr);

    // 保留源 ID 实例化（SceneManager DDOL 迁移用）；ID 冲突时 Scene::instantiate
    // 内部已重生成并 warn；失败返回 nullptr
    static GameObject* instantiateSubtreeKeepingIds(Scene& scene, const std::string& treeJson,
                                                    GameObject* parent = nullptr);

    // 资产读写：loadAsset 走 AssetStream（pak 挂载后自动读内存，未挂载磁盘中文路径安全）；
    // saveAsset 与 SceneSerializer::saveToFile 同策（AssetStream 无写通道，ofstream 直写）
    static bool loadAsset(const std::filesystem::path& relPath, nlohmann::json& out);
    static bool saveAsset(const std::filesystem::path& relPath, const nlohmann::json& root);
};

}  // namespace NovaEngine
