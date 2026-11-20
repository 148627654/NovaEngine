#include "Packager.h"

#include <nlohmann/json.hpp>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/AssetReferenceScanner.h>   // V1.0-7：打包前引用预检（§26.4）
#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Assets/PakArchive.h>
#include <NovaEngine/Assets/TextureMeta.h>
#include <NovaEngine/Core/Logger.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <functional>
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

// 收集单个场景 JSON 的引用资产（V0.3-12 波 3 抽取：入口场景与 build.novabuild 清单内
// 全部场景共用同一逻辑）：MeshRenderer 的 mesh（跳过 builtin:）+ material.albedoMap +
// PrefabInstance 的 .novaprefab（V0.3-9 波 4，递归收集嵌套）+ 天空盒
void collectPrefabRecursive(const std::string& relPath, const fs::path& projectRoot,
                            std::map<std::string, fs::path>& files,
                            std::vector<std::string>& missing, int depth);

void collectSceneReferences(const nlohmann::json& root, const fs::path& projectRoot,
                            std::map<std::string, fs::path>& files,
                            std::vector<std::string>& missing) {
    std::error_code ec;
    if (root.contains("objects") && root["objects"].is_array()) {
        for (const auto& obj : root["objects"]) {
            if (!obj.is_object()) continue;
            if (!obj.contains("components") || !obj["components"].is_array()) continue;
            for (const auto& comp : obj["components"]) {
                if (!comp.is_object()) continue;
                const std::string type = comp.value("type", std::string());
                if (type == "MeshRenderer") {
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
                } else if (type == "PrefabInstance") {
                    // .novaprefab 引用（V0.3-9 波 4）：资产文件 + 其内部引用递归收集
                    const std::string prefabPath = comp.value("prefabPath", std::string());
                    if (!prefabPath.empty())
                        collectPrefabRecursive(prefabPath, projectRoot, files, missing, 0);
                }
            }
        }
    }

    // 天空盒（environment.skyCubemapPath）：目录 → px/nx/py/ny/pz/nz 六面
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
}

// 收集 .novaprefab 资产 + 其内部引用（嵌套预制体 / 网格 / 贴图；树形文档递归遍历）。
// 深度上限 8 防嵌套环；files.contains 去重（同一资产只收一次）
void collectPrefabRecursive(const std::string& relPath, const fs::path& projectRoot,
                            std::map<std::string, fs::path>& files,
                            std::vector<std::string>& missing, int depth) {
    if (depth > 8) {
        NovaEngine::Logger::warn("预制体嵌套过深（>8 层），停止递归收集: {}", relPath);
        return;
    }
    const fs::path abs = resolveInProject(relPath, projectRoot);
    if (!fileExists(abs)) {
        missing.push_back(relPath);
        return;
    }
    const std::string rel = toPakRel(abs, projectRoot);
    if (rel.empty()) {
        missing.push_back(relPath);
        return;
    }
    if (files.contains(rel)) return;   // 已收集（兼防环）
    files.emplace(rel, abs);

    nlohmann::json doc;
    {
        std::ifstream ifs(abs, std::ios::binary);
        if (!ifs) return;
        try { ifs >> doc; } catch (const std::exception&) { return; }
    }
    std::function<void(const nlohmann::json&)> walk = [&](const nlohmann::json& node) {
        if (!node.is_object()) return;
        if (node.contains("components") && node["components"].is_array()) {
            for (const auto& c : node["components"]) {
                if (!c.is_object()) continue;
                const std::string type = c.value("type", "");
                if (type == "PrefabInstance") {
                    const std::string nested = c.value("prefabPath", std::string());
                    if (!nested.empty())
                        collectPrefabRecursive(nested, projectRoot, files, missing, depth + 1);
                } else if (type == "MeshRenderer") {
                    const std::string mesh = c.value("mesh", std::string());
                    if (!mesh.empty() && !mesh.starts_with("builtin:")) {
                        const fs::path mAbs = resolveInProject(mesh, projectRoot);
                        if (fileExists(mAbs)) {
                            const std::string mRel = toPakRel(mAbs, projectRoot);
                            if (!mRel.empty()) files.emplace(mRel, mAbs);
                        } else {
                            missing.push_back(mesh);
                        }
                    }
                    if (c.contains("material") && c["material"].is_object()) {
                        const std::string albedo = c["material"].value("albedoMap", std::string());
                        if (!albedo.empty())
                            addTextureWithMeta(files, missing, resolveInProject(albedo, projectRoot),
                                               projectRoot);
                    }
                }
            }
        }
        if (node.contains("children") && node["children"].is_array())
            for (const auto& ch : node["children"])
                walk(ch);
    };
    walk(doc.contains("root") ? doc["root"] : doc);
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

    // 2.5 引用预检（V1.0-7，§26.4）：入口场景 + build.novabuild 清单内全部场景逐一
    //     scanScene，逐引用存在性检查（经 AssetStream：pak 命中优先、磁盘回退）。
    //     任一缺失：逐条 Logger::error（含引用方文件）→ 返回 false 不产出 pak
    //     （「打包遇缺失引用直接失败并列出清单」，不静默）。
    //     路径口径与扫描器/Packager 收集清单一致：相对项目根、正斜杠、无 ".."（§26.7-6）。
    {
        std::vector<std::string> precheckScenes;
        precheckScenes.push_back(sceneRel);
        // build.novabuild 清单场景并入预检（与 3.4 段同口径；解析失败仅 warn——3.4 段旧行为）
        std::string buildText;
        if (AssetStream::readAllText("assets/build.novabuild", buildText)) {
            try {
                const nlohmann::json build = nlohmann::json::parse(buildText);
                if (build.is_object() && build.contains("scenes") && build["scenes"].is_array())
                    for (const auto& s : build["scenes"]) {
                        if (!s.is_object()) continue;
                        const std::string pathStr = s.value("path", std::string());
                        if (!pathStr.empty() &&
                            std::find(precheckScenes.begin(), precheckScenes.end(), pathStr) ==
                                precheckScenes.end())
                            precheckScenes.push_back(pathStr);
                    }
            } catch (const std::exception& e) {
                Logger::warn("打包预检：build.novabuild 解析失败，仅预检入口场景: {}", e.what());
            }
        }

        int missingRefs = 0;
        for (const std::string& sceneRelPath : precheckScenes) {
            const std::vector<NovaEngine::AssetReference> refs =
                NovaEngine::AssetReferenceScanner::scanScene(fs::path(sceneRelPath));
            for (const auto& ref : refs) {
                const fs::path abs = projectRoot / fs::path(ref.assetPath);
                if (NovaEngine::AssetStream::exists(abs))
                    continue;
                Logger::error("打包预检：缺失引用 [{}] {}（引用方 {}）", ref.kind, ref.assetPath,
                              ref.fromFile);
                ++missingRefs;
            }
        }
        if (missingRefs > 0) {
            Logger::error("打包失败：引用预检发现 {} 个缺失引用（清单见上），不产出 pak",
                          missingRefs);
            return false;
        }
        Logger::info("打包预检通过：{} 个场景引用完整", precheckScenes.size());
    }

    // 3. 资源收集。map 按 pak 相对路径排序 → 确定性布局（同输入两次打包产物可复现）
    std::map<std::string, fs::path> files;  // pak 内相对路径 → 源文件绝对路径
    std::vector<std::string> missing;       // 缺失资源清单（逐条报错，不带到运行时）

    // 3.1 场景文件本身
    files.emplace(sceneRel, sceneAbs);

    // 3.2/3.3 入口场景引用资产（mesh/albedoMap/天空盒；抽取为共用助手，V0.3-12 波 3）
    collectSceneReferences(root, projectRoot, files, missing);

    // 3.4 Build Settings 场景集合（V0.3-12 波 3）：读 assets/build.novabuild，把清单内全部
    // 场景文件、其引用资产与 novabuild 本身一并进 pak（Player 内 SceneManager::loadScene
    // 依赖）。清单缺失/损坏 → 仅打包入口场景（旧行为，warn 提示）
    {
        std::string buildText;
        if (AssetStream::readAllText("assets/build.novabuild", buildText)) {
            try {
                const nlohmann::json build = nlohmann::json::parse(buildText);
                if (build.is_object() && build.contains("scenes") && build["scenes"].is_array()) {
                    for (const auto& s : build["scenes"]) {
                        if (!s.is_object()) continue;
                        const std::string pathStr = s.value("path", std::string());
                        if (pathStr.empty()) continue;
                        const fs::path abs = resolveInProject(pathStr, projectRoot);
                        if (!fileExists(abs)) { missing.push_back(pathStr); continue; }
                        const std::string rel = toPakRel(abs, projectRoot);
                        if (rel.empty()) { missing.push_back(pathStr); continue; }
                        if (files.contains(rel)) continue;   // 与入口场景重复 → 跳过
                        files.emplace(rel, abs);
                        nlohmann::json sceneRoot;
                        std::ifstream ifs(abs, std::ios::binary);
                        if (ifs) {
                            try { ifs >> sceneRoot; }
                            catch (const std::exception&) { sceneRoot = nlohmann::json(); }
                        }
                        if (sceneRoot.is_object())
                            collectSceneReferences(sceneRoot, projectRoot, files, missing);
                    }
                    // novabuild 本身进 pak（Player 启动时 SceneManager::init 读盘）
                    files.emplace("assets/build.novabuild", projectRoot / "assets" / "build.novabuild");
                    Logger::info("Build Settings 场景集合已并入打包清单（{} 个场景）",
                                 build["scenes"].size());
                }
            } catch (const std::exception& e) {
                Logger::warn("build.novabuild 解析失败，仅打包入口场景: {}", e.what());
            }
        }
    }

    // 3.5 assets/shaders/ 全目录（*.vert / *.frag；运行时 Renderer/Skybox/Cubemap 初始化必需）
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

    // 3.6 assets/input/ 全目录 *.novainput（V0.5-11）：Player 启动后
    // InputMap::load 经 AssetStream 读 pak 内配置。目录不存在 = 尚无输入配置 →
    // 仅提示（Player 内置默认绑定兜底），不视为失败
    const fs::path inputDir = projectRoot / "assets" / "input";
    if (fs::is_directory(inputDir, ec)) {
        for (fs::recursive_directory_iterator it(
                 inputDir, fs::directory_options::skip_permission_denied, ec),
             end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) { ec.clear(); continue; }
            std::string name = it->path().filename().string();
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (!name.empty() && name.front() == '.') continue;  // 隐藏文件
            if (!name.ends_with(".novainput")) continue;
            const std::string rel = toPakRel(it->path(), projectRoot);
            if (!rel.empty()) files.emplace(rel, it->path());
        }
        if (ec) Logger::warn("遍历输入配置目录中断: {}", ec.message());
    } else {
        Logger::info("assets/input/ 不存在，打包不含输入绑定配置（Player 使用内置默认绑定）");
    }

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

    // 7.5 拷贝脚本 DLL（V0.5-7）：Player 形态 NovaGame.dll（Player 启动按 exe 目录加载，
    //    其引擎符号引用绑定 NovaPlayer.exe 导出）。缺失视为打包失败（场景脚本会丢）
#ifdef NOVA_SCRIPTS_DLL
    const fs::path gameDll(NOVA_GAME_PLAYER_DLL_PATH);
    const fs::path gameDllOut = outputDir / "NovaGame.dll";
    if (!fileExists(gameDll)) {
        Logger::error("打包失败：NovaGame.dll 不存在: {}（请先构建 NovaGamePlayer target）",
                      gameDll.string());
        return false;
    }
    copyEc.clear();
    fs::copy_file(gameDll, gameDllOut, fs::copy_options::overwrite_existing, copyEc);
    if (copyEc) {
        Logger::error("打包失败：拷贝 NovaGame.dll 失败: {}（{}）", gameDllOut.string(),
                      copyEc.message());
        return false;
    }
#endif

    // 8. Console 报告产物清单
    Logger::info("Build Player 成功 —— 产物清单:");
    Logger::info("  [pak] {}（{} 个文件，场景 {}）", pakPath.string(), files.size(), sceneRel);
    Logger::info("  [exe] {}", exePath.string());
#ifdef NOVA_SCRIPTS_DLL
    Logger::info("  [dll] {}", gameDllOut.string());
#endif
    Logger::info("  运行方式: {} --scene {}（自动挂载同目录 {}，也可 --pak 显式指定）",
                 exePath.string(), sceneRel, pakPath.filename().string());
    return true;
}

}  // namespace NovaEditor
