#include "DragDrop.h"

#include <nlohmann/json.hpp>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/ModelLoader.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Core/UUID.h>
#include <NovaEngine/Render/Mesh.h>
#include <NovaEngine/Render/Texture.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/MeshRenderer.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/Transform.h>
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
            return {NovaEngine::AssetDatabase::instance().getTexture(rel), rel};
        // 项目外：拷贝进 assets/textures/ 后按相对路径走缓存
        const std::filesystem::path copiedRel = copyIntoAssets(abs);
        if (!copiedRel.empty())
            return {NovaEngine::AssetDatabase::instance().getTexture(copiedRel), copiedRel};
    }
    NovaEngine::Logger::warn("贴图未找到，使用纯色: {}", texPath.string());
    return {nullptr, {}};
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

    const fs::path subDir = isTextureFile(srcAbs) ? "assets/textures" : "assets/models";
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

    // 网格走 AssetDatabase（缓存生效：同一模型重复拖入不重复导入）
    std::vector<std::shared_ptr<Mesh>> meshes = AssetDatabase::instance().loadModel(rel);
    if (meshes.empty()) return nullptr;  // loadModel 内部已 Logger::error

    // 材质信息（loadModel 只返回网格，材质需另解析；与 loadModel 的子网格顺序一致）
    const ModelImportResult imported = ModelLoader::load(srcAbs);

    // 根 GO（文件名）+ 每个子网格一个子 GO
    GameObject* root = scene.createGameObject(srcAbs.stem().string());
    root->transform()->setLocalPosition(spawnPosition);

    for (size_t i = 0; i < meshes.size(); ++i) {
        const std::string subName =
            (i < imported.meshes.size() && !imported.meshes[i].name.empty())
                ? imported.meshes[i].name
                : std::format("SubMesh{}", i);
        GameObject* sub = scene.createGameObject(subName, root);
        auto* mr = sub->addComponent<MeshRenderer>();
        mr->mesh = meshes[i];
        mr->meshPath = toPortable(rel);  // 所有子网格指向源文件
        if (i < imported.materials.size())
            mr->material = imported.materials[i];
        if (i < imported.albedoMapPaths.size() && !imported.albedoMapPaths[i].empty()) {
            // 同时写入 albedoMapPath（序列化用），保证 Ctrl+S 后贴图不丢失
            const auto [tex, texRel] =
                resolveTextureAndPath(srcAbs.parent_path(), imported.albedoMapPaths[i]);
            mr->material.albedoMap = tex;
            mr->material.albedoMapPath = toPortable(texRel);  // 空路径 = 纯色
        }
    }

    Logger::info("导入模型 {}（{} 个子网格）", toPortable(rel), meshes.size());
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

}  // namespace NovaEditor::DragDrop
