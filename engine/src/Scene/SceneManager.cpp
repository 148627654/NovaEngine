// SceneManager.cpp —— 场景管理（V0.3-12，波 3）
//
// Build Settings 列表持久化于 assets/build.novabuild（{version:1, scenes:[{name,path}]}）；
// init 读盘（缺失/损坏 → 空列表 + warn，不崩溃）；编辑器经 saveBuildSettings 写盘。
//
// loadScene 帧末执行（§15：Scene::update 之后调 flushPendingLoad；延迟防广播中迭代器失效）：
//   1. 查构建列表（查无 → error + 中止，不崩溃）
//   2. 读新场景 JSON（AssetStream：pak 兼容、中文路径安全）
//   3. 【铁律①】收集 DDOL 子树（必须先于 deserialize——旧场景仍在）：
//      遍历旧场景，dontDestroyOnLoad 且父级无该标志的对象为子树根，逐个
//      serializeSubtree 保留原 ID；DDOL 打在非根对象上时以其为子树根迁移、
//      挂回新场景根（父级不迁移，行为文档化）
//   4. SceneSerializer::deserialize 原地重建（同一 Scene 对象，外部 Scene* 不失效）
//   5. 【铁律②】挂回 DDOL：逐个 instantiateSubtreeKeepingIds(scene, json, nullptr)
//      （后于 deserialize、先于 start；原 DDOL 根为根层级 → nullptr 挂场景根）
//   6. AudioEngine::stopAll → start() → Events::post(SceneLoaded{name})（§14.9 顺序）

#include <NovaEngine/Scene/SceneManager.h>
#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Audio/AudioEngine.h>
#include <NovaEngine/Core/Events.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Serialization/PrefabSerializer.h>
#include <NovaEngine/Serialization/SceneSerializer.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace NovaEngine {

namespace {

// Build Settings 文件（相对项目根；与 BuildSettingsPanel 约定一致）
constexpr const char* kBuildSettingsRel = "assets/build.novabuild";

std::vector<SceneBuildInfo> s_buildList;
Scene* s_activeScene = nullptr;
std::string s_pendingLoad;   // 空 = 无待加载；单槽「最新覆盖」（同帧多次 loadScene 以最后一次为准）

}  // namespace

void SceneManager::init() {
    std::string text;
    if (!AssetStream::readAllText(kBuildSettingsRel, text)) {
        Logger::warn("SceneManager: 未找到 {}，Build Settings 列表为空（首次使用请到编辑器 Build Settings 保存）",
                     kBuildSettingsRel);
        return;
    }
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(text);
    } catch (const std::exception& ex) {
        Logger::warn("SceneManager: {} 解析失败（{}），使用空列表", kBuildSettingsRel, ex.what());
        return;
    }
    if (!doc.is_object() || !doc.contains("scenes") || !doc["scenes"].is_array()) {
        Logger::warn("SceneManager: {} 结构不符（缺 scenes 数组），使用空列表", kBuildSettingsRel);
        return;
    }
    s_buildList.clear();
    for (const auto& e : doc["scenes"]) {
        if (!e.is_object()) continue;
        SceneBuildInfo info;
        info.name = e.value("name", std::string());
        info.path = std::filesystem::path(e.value("path", std::string()));
        if (!info.name.empty() && !info.path.empty())
            s_buildList.push_back(std::move(info));
    }
    Logger::info("SceneManager: Build Settings 已加载（{} 个场景）", s_buildList.size());
}

void SceneManager::shutdown() {
    s_activeScene = nullptr;
    s_buildList.clear();
    s_pendingLoad.clear();
}

void SceneManager::registerScene(const std::string& name, const std::filesystem::path& path) {
    if (name.empty() || path.empty()) return;
    // 同名覆盖（幂等语义，与 ComponentRegistry 注册表一致）
    for (auto& b : s_buildList) {
        if (b.name == name) {
            b.path = path;
            return;
        }
    }
    s_buildList.push_back(SceneBuildInfo{name, path});
}

const std::vector<SceneBuildInfo>& SceneManager::buildList() {
    return s_buildList;
}

bool SceneManager::saveBuildSettings(const std::vector<SceneBuildInfo>& scenes) {
    nlohmann::json doc;
    doc["version"] = 1;
    doc["scenes"] = nlohmann::json::array();
    for (const SceneBuildInfo& s : scenes) {
        if (s.name.empty() || s.path.empty()) {
            Logger::warn("SceneManager: 跳过空条目（name/path 为空）");
            continue;
        }
        nlohmann::json e;
        e["name"] = s.name;
        e["path"] = s.path.string();
        doc["scenes"].push_back(std::move(e));
    }
    const std::filesystem::path abs = AssetDatabase::projectRoot() / kBuildSettingsRel;
    std::ofstream out(abs);   // path 重载（中文路径安全）
    if (!out) {
        Logger::error("SceneManager: 写入 Build Settings 失败: {}", abs.string());
        return false;
    }
    out << doc.dump(2);
    s_buildList = scenes;   // 同步内存列表（后续 loadScene 立即生效）
    Logger::info("SceneManager: Build Settings 已保存（{} 个场景）→ {}", s_buildList.size(),
                 abs.string());
    return true;
}

bool SceneManager::loadScene(const std::string& name) {
    if (name.empty()) return false;
    const auto found = std::find_if(s_buildList.begin(), s_buildList.end(),
                                    [&](const SceneBuildInfo& b) { return b.name == name; });
    if (found == s_buildList.end()) {
        Logger::error("SceneManager: 场景 \"{}\" 不在 Build Settings 列表中（先到 Build Settings 勾选保存）", name);
        return false;
    }
    s_pendingLoad = name;   // 单槽最新覆盖：同帧多次 loadScene 以最后一次为准
    Logger::info("SceneManager: 场景切换已入队: {}（帧末执行）", name);
    return true;
}

void SceneManager::flushPendingLoad() {
    if (!s_activeScene || s_pendingLoad.empty())
        return;   // 无活动场景 / 无待加载 → 安全 no-op

    const std::string name = s_pendingLoad;
    s_pendingLoad.clear();

    // 1. 查构建列表（loadScene 已查过，此处防列表中途被改）
    const auto found = std::find_if(s_buildList.begin(), s_buildList.end(),
                                    [&](const SceneBuildInfo& b) { return b.name == name; });
    if (found == s_buildList.end()) {
        Logger::error("SceneManager: 场景 \"{}\" 不在 Build Settings 列表中，加载中止", name);
        return;
    }

    // 2. 读新场景 JSON（AssetStream：pak 挂载走内存，磁盘中文路径安全）
    std::string text;
    if (!AssetStream::readAllText(found->path, text)) {
        Logger::error("SceneManager: 读取场景文件失败 {}", found->path.string());
        return;
    }
    nlohmann::json newDoc;
    try {
        newDoc = nlohmann::json::parse(text);
    } catch (const std::exception& ex) {
        Logger::error("SceneManager: 场景 JSON 解析失败 {}: {}", found->path.string(), ex.what());
        return;
    }
    if (!newDoc.is_object()) {
        Logger::error("SceneManager: 场景 JSON 根节点不是对象 {}", found->path.string());
        return;
    }

    // 3. 【铁律①】收集 DDOL 子树（必须先于 deserialize——旧场景仍在）：
    //    dontDestroyOnLoad 且父级无该标志的对象 = 迁移子树根（含根层级 DDOL 根）；
    //    DDOL 打在非根对象上时：以其为子树根迁移、挂回新场景根（父级不迁移，文档化）
    std::vector<std::string> ddolSubtrees;
    s_activeScene->forEachObject([&](GameObject* go) {
        if (!go->dontDestroyOnLoad())
            return;
        const GameObject* parent = go->parent();
        if (parent != nullptr && parent->dontDestroyOnLoad())
            return;   // 父级已是迁移根：本对象随父级子树一并序列化，不重复收集
        ddolSubtrees.push_back(PrefabSerializer::serializeSubtree(*go));
    });

    // 4. 原地重建（同一 Scene 对象：外部持有的 Scene* 不失效；旧场景全部对象销毁）
    SceneSerializer::deserialize(*s_activeScene, newDoc.dump());

    // 5. 【铁律②】挂回 DDOL（后于 deserialize、先于 start）：
    //    原 DDOL 根为根层级 → parent=nullptr 挂场景根，语义一致；ID 冲突自动重生成 + warn（B 线实现）
    for (const std::string& tree : ddolSubtrees) {
        if (!PrefabSerializer::instantiateSubtreeKeepingIds(*s_activeScene, tree, nullptr))
            Logger::warn("SceneManager: DDOL 子树挂回失败（详见上方错误）");
    }

    // 6. 收尾顺序（§14.9）：停旧声 → 广播 onStart → 延迟事件通知脚本
    AudioEngine::stopAll();
    s_activeScene->start();
    Events::post(SceneLoaded{name});
    Logger::info("SceneManager: 场景切换完成: {}（DDOL 迁移 {} 棵子树）", name, ddolSubtrees.size());
}

Scene* SceneManager::activeScene() {
    return s_activeScene;
}

void SceneManager::setActiveScene(Scene* scene) {
    s_activeScene = scene;
    if (scene == nullptr)
        s_pendingLoad.clear();   // 无活动场景：清待加载队列（退出 Play 兜底）
}

void SceneManager::dontDestroyOnLoad(GameObject* go) {
    if (go != nullptr)
        go->setDontDestroyOnLoad(true);
}

}  // namespace NovaEngine
