#pragma once
#include <filesystem>
#include <glm/vec3.hpp>

namespace NovaEngine {
class GameObject;
class Scene;
}  // namespace NovaEngine

// 拖拽/导入管线（§2.9 + §6.4）
namespace NovaEditor::DragDrop {

// ImGui 拖放 payload 类型常量
inline constexpr const char* kAssetPayload = "NOVA_ASSET_PATH";   // 字符串：绝对路径（含结尾 \0）
inline constexpr const char* kHierarchyPayload = "NOVA_GO_ID";    // uint64（对象 ID）

bool isModelFile(const std::filesystem::path& p);     // .obj .fbx .gltf .glb .dae
bool isTextureFile(const std::filesystem::path& p);   // .png .jpg .jpeg .bmp .tga
bool isSceneFile(const std::filesystem::path& p);     // .nova.json
bool isPrefabFile(const std::filesystem::path& p);    // .novaprefab（V0.3-9 波 4）

// 复制进 assets/models/ 或 assets/textures/（重名自动加 "_1" 后缀），返回项目内相对路径。
// 源文件已在项目内时不做复制，直接返回相对路径。
std::filesystem::path copyIntoAssets(const std::filesystem::path& sourcePath);

// 导入模型到场景：根 GO（文件名）+ 每个子网格一个子 GO（各挂 MeshRenderer），
// 返回根 GO（失败返回 nullptr）。spawnPosition 由调用方经射线与 y=0 平面求得。
NovaEngine::GameObject* importModelToScene(NovaEngine::Scene& scene,
    const std::filesystem::path& sourcePath, const glm::vec3& spawnPosition);

// 复制对象：serializeGameObject → remapGameObjectIds → 并入全场景 JSON 重建。
// 返回副本（失败返回 nullptr）。冻结接口无「插入单对象」API，故走整场景反序列化。
NovaEngine::GameObject* duplicateGameObject(NovaEngine::Scene& scene, NovaEngine::GameObject* source);

// 导入预制体（V0.3-9 波 4）：loadAsset + instantiateSubtree（生成 instanceToSource 映射），
// 实例根挂 PrefabInstance（prefabPath + 映射）。项目外源文件先经 copyIntoAssets 复制进
// assets/prefabs/。返回实例根（失败 nullptr）。
NovaEngine::GameObject* importPrefabToScene(NovaEngine::Scene& scene,
    const std::filesystem::path& sourcePath, const glm::vec3& spawnPosition);

}  // namespace NovaEditor::DragDrop
