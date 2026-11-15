#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace NovaEngine {
class GameObject;
}

namespace NovaEditor {

class EditorContext;

// 预制体编辑器侧管理（V0.3-9 波 4）：
// - 源资产 JSON 缓存（path → 文档 {"root": ...}）；Reload Assets 清空
// - createPrefabAsset：serializeSubtree + saveAsset（.novaprefab 与 instantiateSubtree 同构，
//   serializeSubtree 输出即 {"root": ...} 文档，直接落盘）
// - applyToAsset（§14.7 Apply 语义）：override 字段写回源 → 嵌套实例展开烘焙 →
//   saveAsset → 刷新场景内全部同路径实例（非 override 字段重读新源值）→
//   Undo 文件通道（FileRestore = 资产改写前内容，B 线约定）
// - revertComponent / fieldCompare：源值回写与 override 动态比对（Inspector 加粗）
// 合并/烘焙用 nlohmann 直接实现；文件读写经 PrefabSerializer（loadAsset/saveAsset）。
class PrefabManager {
public:
    // 源文档缓存；加载失败 → nullptr（Logger::warn 已记，调用方降级不崩溃）
    static const nlohmann::json* sourceJson(const std::filesystem::path& prefabPath);
    static void clearCache();

    // 创建资产：serializeSubtree(root) → saveAsset。成功返回相对路径；失败返回空 + Logger::error
    static std::filesystem::path createPrefabAsset(NovaEngine::GameObject* root,
                                                   const std::filesystem::path& relPath);

    // override 动态比对：两份组件 JSON 的差异键（点分路径，如 "material.albedo"；
    // 忽略 "type" 键）
    static std::vector<std::string> fieldCompare(const nlohmann::json& instJson,
                                                 const nlohmann::json& srcJson);

    // 实例对象某组件的源 JSON（按 map 溯源 + 源树查找）；未映射/未找到 → 空 json（is_null）
    static nlohmann::json sourceComponentJson(const nlohmann::json& srcRoot,
                                              const std::unordered_map<uint64_t, uint64_t>& map,
                                              uint64_t instObjId, const std::string& typeName);

    // 收集选中对象的 override 集合（typeName → 差异键列表，Inspector 字段强调 + Revert 用）
    static void collectObjectOverrides(
        const nlohmann::json& srcDoc, uint64_t srcObjId, NovaEngine::GameObject* go,
        std::unordered_map<std::string, std::vector<std::string>>& out);

    // Apply（§14.7 语义）；成功 → 已入 Undo 栈 + 置脏，返回 true
    static bool applyToAsset(EditorContext& ctx, NovaEngine::GameObject* instanceRoot);

    // Revert：源组件 JSON（onlyKeys 非空时取单键子集 = 单字段 revert）onDeserialize 回实例；
    // 不改文件（Undo 走普通场景快照，markSceneDirty 已入栈）
    static bool revertComponent(EditorContext& ctx, NovaEngine::GameObject* instObj,
                                const std::string& typeName,
                                const std::vector<std::string>& onlyKeys = {});

    // Revert All（Inspector 按钮）：递归整棵实例子树全部组件回源（嵌套实例按其自身映射溯源）
    static void revertAll(EditorContext& ctx, NovaEngine::GameObject* instObj);

    // 定位实例根：go 自身或祖先链挂 PrefabInstance 的对象（无 → nullptr）
    static NovaEngine::GameObject* findInstanceRoot(NovaEngine::GameObject* go);

    // redo 文件重算（UndoRedoManager 约定）：把场景内首个该路径实例的当前子树重新
    // 序列化写回资产（Apply = 场景 → 文件可重序列化）；无实例 → false（消费方回退原样写回）
    static bool rewriteAssetFromScene(EditorContext& ctx, const std::filesystem::path& prefabPath);
};

}  // namespace NovaEditor
