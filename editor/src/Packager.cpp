#include "Packager.h"

#include <nlohmann/json.hpp>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/PakArchive.h>
#include <NovaEngine/Assets/TextureMeta.h>
#include <NovaEngine/Core/Logger.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <map>
#include <string>
#include <vector>

// NovaPlayer.exe 编译产物路径（editor/CMakeLists.txt 编译期注入；$<CONFIG> 适配 VS 多配置）。
// 未定义时回退空串 = 预检必然失败并提示先构建 NovaPlayer target。
#ifndef NOVA_PLAYER_EXE_PATH
#define NOVA_PLAYER_EXE_PATH ""
#endif

namespace NovaEditor {

namespace {

namespace fs = std::filesystem;

// pak 内路径一律：项目内相对 + 正斜杠（PakArchive v1 约定）。跨盘/异常返回空（调用方报错）。
std::string toPakRel(const fs::path& abs, const fs::path& projectRoot) {
    std::error_code ec;
    const fs::path rel = fs::relative(abs, projectRoot, ec);
    if (ec) return {};
    return rel.generic_string();
}

// JSON 里的资产路径（项目内正斜杠相对路径）→ 绝对路径
fs::path resolveInProject(const std::string& ref, const fs::path& projectRoot) {
    const fs::path p(ref);
    return p.is_absolute() ? p : projectRoot / p;
}

// 文件存在性（中文路径安全：只走 std::filesystem，零编码转换）
bool fileExists(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

// 收集贴图 + 其 .novameta 伴随文件（meta 缺失时 loadOrCreate 自动生成默认落盘；
// 生成失败仅告警 —— 运行时 TextureLoader 会再尝试生成并使用默认设置）
// .hdr（HDR 六面/equirect）不消费 .novameta（引擎侧约定），跳过 meta 收集，避免误导性默认值。
void addTextureWithMeta(std::map<std::string, fs::path>& files, std::vector<std::string>& missing,
                        const fs::path& absTex, const fs::path& projectRoot) {
    if (!fileExists(absTex)) {
        missing.push_back(absTex.string());
        return;
    }
    const std::string rel = toPakRel(absTex, projectRoot);
    if (rel.empty()) {
        missing.push_back(absTex.string());
        return;
    }
    files.emplace(rel, absTex);

    std::string ext = absTex.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".hdr") return;

    bool metaExisted = false;
    NovaEngine::TextureMeta::loadOrCreate(absTex, metaExisted);  // 缺失 → 自动生成默认落盘
    const fs::path metaAbs = NovaEngine::TextureMeta::metaPathFor(absTex);
    const std::string metaRel = toPakRel(metaAbs, projectRoot);
    if (metaRel.empty() || !fileExists(metaAbs)) {
        NovaEngine::Logger::warn("贴图 meta 生成失败，打包不含 {}（运行时回退默认设置）",
                                 metaAbs.string());
        return;
    }
    files.emplace(metaRel, metaAbs);
}

}  // namespace

bool Packager::buildPlayer(const fs::path& sceneJsonPath, const fs::path& outputDir) {
    using namespace NovaEngine;

    const fs::path projectRoot = AssetDatabase::projectRoot();
    std::error_code ec;

    // 1. 场景路径绝对化（UI 传绝对路径；命令行传项目内相对路径）
    fs::path sceneAbs =
        (sceneJsonPath.is_absolute() ? sceneJsonPath : projectRoot / sceneJsonPath)
            .lexically_normal();
    if (!fileExists(sceneAbs)) {
        Logger::error("打包失败：场景文件不存在: {}", sceneAbs.string());
        return false;
    }
    const std::string sceneRel = toPakRel(sceneAbs, projectRoot);
    if (sceneRel.empty()) {
        Logger::error("打包失败：场景文件不在项目根内，无法生成 pak 相对路径: {}",
                      sceneAbs.string());
        return false;
    }

    // 2. 解析场景 JSON（ifstream path 重载，中文路径安全）
    nlohmann::json root;
    {
        std::ifstream ifs(sceneAbs, std::ios::binary);
        if (!ifs) {
            Logger::error("打包失败：无法读取场景文件: {}", sceneAbs.string());
            return false;
        }
        try {
            ifs >> root;
        } catch (const std::exception& e) {
            Logger::error("打包失败：场景 JSON 解析失败: {}（{}）", sceneAbs.string(), e.what());
            return false;
        }
        if (!root.is_object()) {
            Logger::error("打包失败：场景 JSON 根节点不是对象: {}", sceneAbs.string());
            return false;
        }
    }

    // 3. 资源收集。map 按 pak 相对路径排序 → 确定性布局（同输入两次打包产物可复现）
    std::map<std::string, fs::path> files;  // pak 内相对路径 → 源文件绝对路径
    std::vector<std::string> missing;       // 缺失资源清单（逐条报错，不带到运行时）

    // 3.1 场景文件本身
    files.emplace(sceneRel, sceneAbs);

    // 3.2 每个 MeshRenderer：mesh 路径（跳过 "builtin:" 内置图元）+ material.albedoMap
    if (root.contains("objects") && root["objects"].is_array()) {
        for (const auto& obj : root["objects"]) {
            if (!obj.is_object()) continue;
            if (!obj.contains("components") || !obj["components"].is_array()) continue;
            for (const auto& comp : obj["components"]) {
                if (!comp.is_object()) continue;
                if (comp.value("type", std::string()) != "MeshRenderer") continue;

                const std::string mesh = comp.value("mesh", std::string());
                if (!mesh.empty() && !mesh.starts_with("builtin:")) {
                    const fs::path meshAbs = resolveInProject(mesh, projectRoot);
                    if (!fileExists(meshAbs)) {
                        missing.push_back(mesh);
                    } else {
                        const std::string rel = toPakRel(meshAbs, projectRoot);
                        if (rel.empty()) missing.push_back(mesh);
                        else files.emplace(rel, meshAbs);
                    }
                }

                if (comp.contains("material") && comp["material"].is_object()) {
                    const std::string albedo = comp["material"].value("albedoMap", std::string());
                    if (!albedo.empty())
                        addTextureWithMeta(files, missing, resolveInProject(albedo, projectRoot),
                                           projectRoot);
                }
            }
        }
    }

    // 3.3 天空盒（environment.skyCubemapPath）：目录 → px/nx/py/ny/pz/nz 六面
    //（扩展名探测顺序与 AssetDatabase::getCubemap 一致）；文件 → equirect 单文件
    if (root.contains("environment") && root["environment"].is_object()) {
        const std::string sky = root["environment"].value("skyCubemapPath", std::string());
        if (!sky.empty()) {
            const fs::path skyAbs = resolveInProject(sky, projectRoot);
            if (fs::is_directory(skyAbs, ec)) {
                static constexpr std::array<const char*, 6> kFaces = {"px", "nx", "py",
                                                                      "ny", "pz", "nz"};
                static constexpr std::array<const char*, 6> kExts = {".png", ".jpg", ".jpeg",
                                                                     ".bmp", ".tga", ".hdr"};
                for (const char* face : kFaces) {
                    bool found = false;
                    for (const char* ext : kExts) {
                        const fs::path cand = skyAbs / (std::string(face) + ext);
                        if (fileExists(cand)) {
                            addTextureWithMeta(files, missing, cand, projectRoot);
                            found = true;
                            break;
                        }
                    }
                    if (!found) missing.push_back(sky + "/" + face + ".*");
                }
            } else {
                addTextureWithMeta(files, missing, skyAbs, projectRoot);
            }
        }
    }

    // 3.4 assets/shaders/ 全目录（*.vert / *.frag；运行时 Renderer/Skybox/Cubemap 初始化必需）
    const fs::path shaderDir = projectRoot / "assets" / "shaders";
    if (!fs::is_directory(shaderDir, ec)) {
        Logger::error("打包失败：着色器目录不存在: {}", shaderDir.string());
        return false;
    }
    for (fs::recursive_directory_iterator it(
             shaderDir, fs::directory_options::skip_permission_denied, ec),
         end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) { ec.clear(); continue; }
        std::string name = it->path().filename().string();
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!name.empty() && name.front() == '.') continue;  // 隐藏文件
        if (!(name.ends_with(".vert") || name.ends_with(".frag"))) continue;
        const std::string rel = toPakRel(it->path(), projectRoot);
        if (!rel.empty()) files.emplace(rel, it->path());
    }
    if (ec) Logger::warn("遍历着色器目录中断: {}", ec.message());

    // 4. 缺失资源：逐条报错并失败（不带到运行时）
    if (!missing.empty()) {
        for (const auto& m : missing)
            Logger::error("打包失败：缺失资源: {}", m);
        Logger::error("共 {} 个缺失资源，请修复后重新打包", missing.size());
        return false;
    }

    // 5. NovaPlayer.exe 预检（编译期注入的构建产物路径；缺失时打包不落地）
    const fs::path playerExe(NOVA_PLAYER_EXE_PATH);
    if (!fileExists(playerExe)) {
        Logger::error("打包失败：NovaPlayer.exe 不存在: {}（请先构建 NovaPlayer target）",
                      playerExe.string());
        return false;
    }

    // 6. 输出目录 + pak 打包（pak 名 = 场景文件名去掉 .nova.json 后缀，与 --scene 参数同 stem）
    ec.clear();
    if (!fs::create_directories(outputDir, ec) && ec) {
        Logger::error("打包失败：无法创建输出目录 {}: {}", outputDir.string(), ec.message());
        return false;
    }
    std::string sceneStem = sceneAbs.stem().string();  // "demo.nova"
    if (sceneStem.ends_with(".nova")) sceneStem.resize(sceneStem.size() - 5);
    const fs::path pakPath = outputDir / (sceneStem + ".novapak");

    std::vector<std::pair<fs::path, fs::path>> fileList;
    fileList.reserve(files.size());
    for (const auto& [rel, abs] : files)
        fileList.emplace_back(fs::path(rel), abs);  // rel 已为正斜杠字符串
    if (!PakArchive::pack(pakPath, fileList)) {
        Logger::error("打包失败：PakArchive::pack 写入失败: {}", pakPath.string());
        return false;
    }

    // 7. 拷贝 NovaPlayer.exe → 输出目录（发布目录 = exe + pak）
    const fs::path exePath = outputDir / "NovaPlayer.exe";
    std::error_code copyEc;
    fs::copy_file(playerExe, exePath, fs::copy_options::overwrite_existing, copyEc);
    if (copyEc) {
        Logger::error("打包失败：拷贝 NovaPlayer.exe 失败: {}（{}）", exePath.string(),
                      copyEc.message());
        return false;
    }

    // 8. Console 报告产物清单
    Logger::info("Build Player 成功 —— 产物清单:");
    Logger::info("  [pak] {}（{} 个文件，场景 {}）", pakPath.string(), files.size(), sceneRel);
    Logger::info("  [exe] {}", exePath.string());
    Logger::info("  运行方式: {} --scene {}（自动挂载同目录 {}，也可 --pak 显式指定）",
                 exePath.string(), sceneRel, pakPath.filename().string());
    return true;
}

}  // namespace NovaEditor
