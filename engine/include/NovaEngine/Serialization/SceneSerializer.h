#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <nlohmann/json_fwd.hpp>

namespace NovaEngine {

class Scene;
class GameObject;
class Component;

class SceneSerializer {
public:
    static std::string serialize(const Scene& scene);                  // → JSON 字符串（Play 快照也用它）
    static std::string serializeGameObject(GameObject& object);        // 复制/剪贴板用
    static void deserialize(Scene& target, const std::string& json);   // 清空并按 JSON 重建 target（对象身份不变）
    static bool saveToFile(const Scene& scene, const std::filesystem::path& path);
    static bool loadFromFile(Scene& target, const std::filesystem::path& path);
    // 复制对象：递归换新 ID（编辑器 Duplicate 用）
    static void remapGameObjectIds(nlohmann::json& objectJson);

private:
    static std::unique_ptr<Component> createComponentFromJson(GameObject* owner, const nlohmann::json& j);
    // 内置组件按名字分发：Transform/MeshRenderer/Camera/DirectionalLight；其余走 ScriptRegistry
};

}  // namespace NovaEngine
