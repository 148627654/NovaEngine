#include "DragDrop.h"

#include <nlohmann/json.hpp>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Core/UUID.h>
#include <NovaEngine/Render/Mesh.h>
#include <NovaEngine/Render/Texture.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/MeshRenderer.h>
#include <NovaEngine/Scene/PrefabInstance.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Serialization/PrefabSerializer.h>
#include <NovaEngine/Serialization/SceneSerializer.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <string_view>

namespace NovaEditor::DragDrop {

namespace {

// 不区分大小写的扩展名比较
bool hasExtension(const std::filesystem::path& p, std::string_view ext) {
    const std::string e = p.extension().string();
    if (e.size() != ext.size()) return false;
    for (size_t i = 0; i < e.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(e[i])) !=
            std::tolower(static_cast<unsigned char>(ext[i])))
            return false;
    return true;
}

// 可移植路径（正斜杠，用于持久化 meshPath/albedoMap 等）
std::string toPortable(const std::filesystem::path& p) {
    return p.generic_string();
}

// 重名自动加 "_1"、"_2" ... 后缀
std::filesystem::path uniquePath(const std::filesystem::path& p) {
    if (!std::filesystem::exists(p)) return p;
    for (int i = 1;; ++i) {
        const std::filesystem::path candidate =
            p.parent_path() / (p.stem().string() + "_" + std::to_string(i) + p.extension().string());
        if (!std::filesystem::exists(candidate)) return candidate;
    }
}

// 解析模型引用的贴图路径（相对模型目录）为 GPU 纹理 + 项目内相对路径（序列化用）：
//   项目内 → AssetDatabase::getTexture（走缓存），路径存相对项目根；
//   项目外 → 先 copyIntoAssets 拷进 assets/textures/（保证场景可移植），再走缓存。
// 返回 (GPU 纹理, 项目内相对路径)；解析失败返回 (nullptr, 空路径)（纯色）。
std::pair<std::shared_ptr<NovaEngine::Texture>, std::filesystem::path> resolveTextureAndPath(
    const std::filesystem::path& modelDir, const std::filesystem::path& texPath) {
    std::vector<std::filesystem::path> candidates;
    if (texPath.is_absolute()) {
        candidates.push_back(texPath);
    } else {
        candidates.push_back(modelDir / texPath);                      // 相对模型目录（§2.6）
        candidates.push_back(NovaEngine::AssetDatabase::projectRoot() / texPath);  // 相对项目根
    }
    for (const auto& c : candidates) {
        std::error_code ec;
        if (!std::filesystem::exists(c, ec)) continue;
        const std::filesystem::path abs = std::filesystem::absolute(c, ec).lexically_normal();
        const std::filesystem::path rel =
            std::filesystem::relative(abs, NovaEngine::AssetDatabase::projectRoot(), ec);
        const bool inside = !ec && !rel.empty() && *rel.begin() != "..";
        if (inside)
            return {NovaEngine::AssetDatabase::instance().getTexture(rel, /*srgb=*/true), rel};
        // 项目外：拷贝进 assets/textures/ 后按相对路径走缓存
        const std::filesystem::path copiedRel = copyIntoAssets(abs);
        if (!copiedRel.empty())
            return {NovaEngine::AssetDatabase::instance().getTexture(copiedRel, /*srgb=*/true),
                    copiedRel};
    }
    NovaEngine::Logger::warn("贴图未找到，使用纯色: {}", texPath.string());
    return {nullptr, {}};
}

// importModel 返回的贴图路径（契约：已解析为项目内相对路径，失败为空）→ GPU 纹理 + 项目内相对路径。
// 引擎未解析成功（意外为项目外/绝对路径）时回退旧逻辑：按模型目录/项目根候选搜索 + 拷贝进项目。
std::pair<std::shared_ptr<NovaEngine::Texture>, std::filesystem::path> resolveImportedTexture(
    const std::filesystem::path& texPath, const std::filesystem::path& modelDir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root = NovaEngine::AssetDatabase::projectRoot();
    const fs::path abs = fs::absolute(texPath, ec).lexically_normal();
    const fs::path rel = fs::relative(abs, root, ec);
    const bool inside = !ec && !rel.empty() && *rel.begin() != "..";
    if (inside) {
        // albedo 贴图按 sRGB 加载（Reviewer P1-1b）：与场景/.novamat 反序列化的 srgb=true 口径一致
        auto tex = NovaEngine::AssetDatabase::instance().getTexture(rel, /*srgb=*/true);
        if (tex) return {tex, rel};
    }
    // 回退：搜索 + 拷贝（拷贝/缓存逻辑保持）
    return resolveTextureAndPath(modelDir, texPath);
}

}  // namespace

bool isModelFile(const std::filesystem::path& p) {
    return hasExtension(p, ".obj") || hasExtension(p, ".fbx") || hasExtension(p, ".gltf") ||
           hasExtension(p, ".glb") || hasExtension(p, ".dae");
}

bool isTextureFile(const std::filesystem::path& p) {
    return hasExtension(p, ".png") || hasExtension(p, ".jpg") || hasExtension(p, ".jpeg") ||
           hasExtension(p, ".bmp") || hasExtension(p, ".tga");
}

bool isSceneFile(const std::filesystem::path& p) {
    std::string f = p.filename().string();
    if (f.size() < 10) return false;
    std::string tail = f.substr(f.size() - 10);  // ".nova.json"
    for (auto& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return tail == ".nova.json";
}

bool isPrefabFile(const std::filesystem::path& p) {
    // V0.3-9 波 4：.novaprefab（hasExtension 小写比较）
    return hasExtension(p, ".novaprefab");
}

std::filesystem::path copyIntoAssets(const std::filesystem::path& sourcePath) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root = NovaEngine::AssetDatabase::projectRoot();
    const fs::path srcAbs = fs::absolute(sourcePath, ec).lexically_normal();
    const fs::path rel = fs::relative(srcAbs, root, ec);

    // 已在项目内：无需复制，直接返回相对路径
    const bool inside = !ec && (rel.empty() || (rel.begin() != rel.end() && *rel.begin() != ".."));
    if (inside) {
        if (rel.empty()) return srcAbs.filename();
        return rel.lexically_normal();
    }

    // V0.3-9 波 4：.novaprefab → assets/prefabs/
    const fs::path subDir = isTextureFile(srcAbs) ? "assets/textures"
                          : isPrefabFile(srcAbs)  ? "assets/prefabs"
                                                  : "assets/models";
    const fs::path destDir = root / subDir;
    fs::create_directories(destDir, ec);
    if (ec) {
        NovaEngine::Logger::error("创建目录失败 {}: {}", destDir.string(), ec.message());
        return {};
    }
    const fs::path dest = uniquePath(destDir / srcAbs.filename());
    if (!fs::copy_file(srcAbs, dest, ec)) {
        NovaEngine::Logger::error("复制文件失败 {} → {}: {}", srcAbs.string(), dest.string(), ec.message());
        return {};
    }
    NovaEngine::Logger::info("已复制到 {}", toPortable(fs::relative(dest, root)));
    return fs::relative(dest, root);
}

NovaEngine::GameObject* importModelToScene(NovaEngine::Scene& scene,
                                           const std::filesystem::path& sourcePath,
                                           const glm::vec3& spawnPosition) {
    using namespace NovaEngine;
    namespace fs = std::filesystem;

    if (!isModelFile(sourcePath)) {
        Logger::warn("非模型文件，忽略导入: {}", sourcePath.string());
        return nullptr;
    }
    std::error_code ec;
    const fs::path srcAbs = fs::absolute(sourcePath, ec).lexically_normal();

    // 项目外 → 拷贝进 assets/models/（保证场景可移植）；项目内 → 直接用相对路径
    const fs::path rel = copyIntoAssets(srcAbs);
    if (rel.empty()) return nullptr;

    // 单次解析（V0.2-14 裁定 5）：importModel 返回网格 + 材质 + 贴图路径（Assimp 只解析一次；
    // 网格走既有缓存，材质/贴图路径入旁表缓存）。不再二次调 ModelLoader::load。
    const AssetImportResult imported = AssetDatabase::instance().importModel(rel);
    if (imported.meshes.empty()) return nullptr;  // importModel 内部已 Logger::error

    // 根 GO（文件名）+ 每个子网格一个子 GO
    GameObject* root = scene.createGameObject(srcAbs.stem().string());
    root->transform()->setLocalPosition(spawnPosition);

    for (size_t i = 0; i < imported.meshes.size(); ++i) {
        const std::string subName = std::format("SubMesh{}", i);
        GameObject* sub = scene.createGameObject(subName, root);
        auto* mr = sub->addComponent<MeshRenderer>();
        mr->mesh = imported.meshes[i];
        mr->meshPath = toPortable(rel);  // 所有子网格指向源文件
        if (i < imported.materials.size())
            mr->material = imported.materials[i];
        if (i < imported.albedoMapPaths.size() && !imported.albedoMapPaths[i].empty()) {
            // 贴图解析改用 importModel 返回的 albedoMapPaths（第 i 子网格对应）；
            // 同时写入 albedoMapPath（序列化用），保证 Ctrl+S 后贴图不丢失
            const auto [tex, texRel] =
                resolveImportedTexture(imported.albedoMapPaths[i], srcAbs.parent_path());
            mr->material.albedoMap = tex;
            mr->material.albedoMapPath = toPortable(texRel);  // 空路径 = 纯色
        }
    }

    Logger::info("导入模型 {}（{} 个子网格）", toPortable(rel), imported.meshes.size());
    return root;
}

NovaEngine::GameObject* duplicateGameObject(NovaEngine::Scene& scene, NovaEngine::GameObject* source) {
    using namespace NovaEngine;
    if (!source) return nullptr;

    // 单对象 JSON → 递归换新 ID → 顶层 parent 固定为原父级
    nlohmann::json obj = nlohmann::json::parse(SceneSerializer::serializeGameObject(*source));
    SceneSerializer::remapGameObjectIds(obj);
    const uint64_t parentId = source->parent() ? source->parent()->id() : UUID::kInvalid;
    obj["parent"] = parentId;
    const uint64_t newId = obj.value("id", UUID::kInvalid);

    // 冻结接口无「反序列化进现有场景」的入口：并入全场景 JSON 后整体重建
    nlohmann::json doc = nlohmann::json::parse(SceneSerializer::serialize(scene));
    if (!doc.contains("objects") || !doc["objects"].is_array()) return nullptr;
    doc["objects"].push_back(obj);
    SceneSerializer::deserialize(scene, doc.dump());

    return scene.findGameObject(newId);
}

NovaEngine::GameObject* importPrefabToScene(NovaEngine::Scene& scene,
                                            const std::filesystem::path& sourcePath,
                                            const glm::vec3& spawnPosition) {   // DevOps 修复：签名在 NovaEditor::DragDrop 命名空间内，须与头文件声明一致地全限定（体内 using 对签名无效）
    using namespace NovaEngine;
    namespace fs = std::filesystem;
    if (!isPrefabFile(sourcePath)) {
        Logger::warn("非预制体文件，忽略导入: {}", sourcePath.string());
        return nullptr;
    }
    std::error_code ec;
    const fs::path srcAbs = fs::absolute(sourcePath, ec).lexically_normal();

    // 项目外 → 复制进 assets/prefabs/（保证场景可移植）；项目内 → 直接用相对路径
    const fs::path rel = copyIntoAssets(srcAbs);
    if (rel.empty()) return nullptr;

    // loadAsset（AssetStream 读取）→ 文档即 {"root":...} 树，与 instantiateSubtree 同构
    nlohmann::json doc;
    if (!PrefabSerializer::loadAsset(rel, doc)) return nullptr;

    // 实例化（ID 重新生成）并记录 实例ID→源ID 映射
    std::unordered_map<uint64_t, uint64_t> map;
    GameObject* root = PrefabSerializer::instantiateSubtree(scene, doc.dump(), &map, nullptr);
    if (!root) return nullptr;
    root->transform()->setLocalPosition(spawnPosition);

    // 实例根挂 PrefabInstance 溯源组件（§14.7：prefabPath + instanceToSource）
    auto* inst = root->addComponent<PrefabInstance>();
    inst->prefabPath = rel.string();
    inst->instanceToSource = std::move(map);
    Logger::info("导入预制体 {}（{} 个对象）", rel.string(), inst->instanceToSource.size());
    return root;
}

}  // namespace NovaEditor::DragDrop
