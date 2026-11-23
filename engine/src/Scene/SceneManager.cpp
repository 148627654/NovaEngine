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
#include <NovaEngine/Assets/AsyncLoader.h>   // V1.0-2：取消语义（loadScene/loadSceneAsync 先 reset）
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

// ===== V1.0-2 多场景状态（§27.1）=====
// additive 场景所有权在 SceneManager（唯一持有方）；loadedScenes 视图 = active 在前。
std::vector<std::unique_ptr<Scene>> s_additiveScenes;
std::vector<Scene*> s_loadedScenesView;   // activeScene 在前（update/渲染顺序）
// ===== V1.0-2 延迟卸载队列（Reviewer P1-1 修复）=====
// unloadScene 只做校验 + 入队（不改视图/不析构——脚本可能在 updateAll 的 onUpdate 中调用，
// 立即 erase+rebuildLoadedView 会让 updateAll 的 range-for 迭代器失效、后续项悬垂）。
// 真正销毁（erase + Scene 析构 + releaseUnused + SceneUnloaded 事件）在 flushPendingLoad
// 内执行——帧序保证它晚于 updateAll（§15 帧内顺序：update → flushPendingLoad）。
std::vector<std::string> s_pendingUnloads;   // 待卸载场景名（去重）

// 场景间参数：活表（setSceneParam 写）+ 挂载期快照（loadScene/loadSceneAsync 调用瞬间冻结；
// 挂载 start() 期间 getSceneParam 读到快照，挂载结束恢复活表）
std::unordered_map<std::string, std::string> s_sceneParams;
std::unordered_map<std::string, std::string> s_pendingLoadParams;   // 同步 loadScene 路径的快照

void rebuildLoadedView() {
    s_loadedScenesView.clear();
    if (s_activeScene != nullptr)
        s_loadedScenesView.push_back(s_activeScene);
    for (const auto& s : s_additiveScenes)
        s_loadedScenesView.push_back(s.get());
}

// 从全部已加载场景（含 additive）收集 DDOL 子树根（铁律①：先于 deserialize）
std::vector<std::string> collectDdolSubtrees() {
    std::vector<std::string> subtrees;
    for (Scene* scene : s_loadedScenesView) {
        if (scene == nullptr)
            continue;
        scene->forEachObject([&](GameObject* go) {
            if (!go->dontDestroyOnLoad())
                return;
            const GameObject* parent = go->parent();
            if (parent != nullptr && parent->dontDestroyOnLoad())
                return;   // 父级已是迁移根：本对象随父级子树一并序列化，不重复收集
            subtrees.push_back(PrefabSerializer::serializeSubtree(*go));
        });
    }
    return subtrees;
}

// 稳健读场景文本：先原路径（pak 键优先 + CWD 相对），失败再项目根绝对路径兜底
//（V1.0-2：异步工作线程同样口径；测试进程 CWD ≠ 项目根时保证可读）
bool readSceneTextRobust(const std::filesystem::path& path, std::string& out) {
    if (AssetStream::readAllText(path, out))
        return true;
    return AssetStream::readAllText(AssetDatabase::projectRoot() / path, out);
}

// 卸载全部 additive 场景（主场景切换/退出 Play 用；DDOL 子树已先收集）
void unloadAllAdditive() {
    s_additiveScenes.clear();   // Scene 析构 → 全部对象销毁 → 物理 body 拆除
    rebuildLoadedView();
    AssetDatabase::instance().releaseUnused();   // §27.1 内存回落：卸载后显式释放无引用资产
}

// 执行延迟卸载队列（flushPendingLoad 内调用——帧序晚于 updateAll，P1-1）
void flushPendingUnloads() {
    if (s_pendingUnloads.empty())
        return;
    std::vector<std::string> names;
    names.swap(s_pendingUnloads);
    for (const std::string& name : names) {
        const auto it = std::find_if(s_additiveScenes.begin(), s_additiveScenes.end(),
                                     [&](const std::unique_ptr<Scene>& s) {
                                         return s->name() == name;
                                     });
        if (it == s_additiveScenes.end())
            continue;   // 已被主场景切换/退出 Play 整体卸载：不重复发事件
        s_additiveScenes.erase(it);   // Scene 析构 → 对象/物理/音频收尾
        rebuildLoadedView();
        AssetDatabase::instance().releaseUnused();
        Events::post(SceneUnloaded{name});   // 延迟投递（真正卸载时发，语义不变）
        Logger::info("SceneManager: additive 场景已卸载: {}（releaseUnused 已调用）", name);
    }
}

// 挂载期参数守卫：start() 期间 getSceneParam 读到加载瞬间快照；析构恢复活表
struct MountParamsGuard {
    explicit MountParamsGuard(const std::unordered_map<std::string, std::string>& snapshot)
        : m_backup(std::move(s_sceneParams)) {
        s_sceneParams = snapshot;
    }
    ~MountParamsGuard() { s_sceneParams = std::move(m_backup); }
    std::unordered_map<std::string, std::string> m_backup;
};

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
    s_pendingUnloads.clear();   // V1.0-2：清延迟卸载队列
    s_additiveScenes.clear();   // V1.0-2：释放全部 additive 场景
    s_loadedScenesView.clear();
    s_sceneParams.clear();
    s_pendingLoadParams.clear();
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
    // V1.0-2 取消语义（§27.1）：同步路径与 loadSceneAsync 共用代际——任一新的加载请求
    // 先 reset()（旧异步结果作废不挂载）；参数表在调用瞬间冻结快照
    AsyncLoader::reset();
    s_pendingLoadParams = s_sceneParams;
    s_pendingLoad = name;   // 单槽最新覆盖：同帧多次 loadScene 以最后一次为准
    Logger::info("SceneManager: 场景切换已入队: {}（帧末执行）", name);
    return true;
}

void SceneManager::flushPendingLoad() {
    // V1.0-2（P1-1）：先执行延迟卸载队列——帧序晚于 updateAll，脚本 onUpdate 中调用
    // unloadScene 的销毁在此安全落地（迭代已结束，无悬垂）；无待卸载时零开销
    flushPendingUnloads();
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

    // 2. 读新场景 JSON（AssetStream：pak 挂载走内存，磁盘中文路径安全；
    //    V1.0-2 兜底：CWD 相对读失败时再试项目根绝对路径——异步路径同口径）
    std::string text;
    if (!readSceneTextRobust(found->path, text)) {
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
    //    V1.0-2（§27.1）：DDOL 迁移对 additive 场景同样生效——从全部已加载场景收集
    //    （DDOL 子树随主场景迁移；additive 场景随主场景切换整体卸载）
    std::vector<std::string> ddolSubtrees = collectDdolSubtrees();
    unloadAllAdditive();   // 主场景切换 = 卸载全部 additive（Unity LoadScene(single) 语义）

    // 4. 原地重建（同一 Scene 对象：外部持有的 Scene* 不失效；旧场景全部对象销毁）
    SceneSerializer::deserialize(*s_activeScene, newDoc.dump());

    // 5. 【铁律②】挂回 DDOL（后于 deserialize、先于 start）：
    //    原 DDOL 根为根层级 → parent=nullptr 挂场景根，语义一致；ID 冲突自动重生成 + warn（B 线实现）
    for (const std::string& tree : ddolSubtrees) {
        if (!PrefabSerializer::instantiateSubtreeKeepingIds(*s_activeScene, tree, nullptr))
            Logger::warn("SceneManager: DDOL 子树挂回失败（详见上方错误）");
    }

    // 6. 收尾顺序（§14.9）：停旧声 → 广播 onStart（参数快照守卫：onStart 期间 getSceneParam
    //    读到加载瞬间快照）→ 延迟事件通知脚本
    AudioEngine::stopAll();
    {
        MountParamsGuard guard(s_pendingLoadParams);
        s_activeScene->start();
    }
    Events::post(SceneLoaded{name});
    Logger::info("SceneManager: 场景切换完成: {}（DDOL 迁移 {} 棵子树）", name, ddolSubtrees.size());
}

Scene* SceneManager::activeScene() {
    return s_activeScene;
}

void SceneManager::setActiveScene(Scene* scene) {
    s_activeScene = scene;
    if (scene == nullptr) {
        // 无活动场景：清待加载/待卸载队列（退出 Play 兜底）+ V1.0-2 卸载全部 additive +
        // 取消在飞异步加载（旧结果作废不挂载）——退出 Play 快照还原路径经此收口
        s_pendingLoad.clear();
        s_pendingUnloads.clear();   // 场景已随 unloadAllAdditive 整体销毁，队列名作废
        unloadAllAdditive();
        AsyncLoader::reset();
    }
    rebuildLoadedView();
}

void SceneManager::dontDestroyOnLoad(GameObject* go) {
    if (go != nullptr)
        go->setDontDestroyOnLoad(true);
}

// ==================== V1.0-2 多场景工作流（§27.1）====================

bool SceneManager::loadSceneAsync(const std::string& name, bool additive) {
    if (name.empty())
        return false;
    const auto found = std::find_if(s_buildList.begin(), s_buildList.end(),
                                    [&](const SceneBuildInfo& b) { return b.name == name; });
    if (found == s_buildList.end()) {
        Logger::error("SceneManager: 场景 \"{}\" 不在 Build Settings 列表中（先到 Build Settings 勾选保存）", name);
        return false;
    }
    // 取消语义（§27.1）：任一新的加载请求先 reset()——旧代际结果作废不挂载
    AsyncLoader::reset();
    // P2-9 轻量互斥：非 additive 异步加载是新的加载目标，作废同帧更早入队的同步加载请求
    //（帧序：flushPendingResults 先于 flushPendingLoad——若不在此清，同帧 sync loadScene("A")
    // + loadSceneAsync("B", false) 会先后执行两次主场景切换）。反向时序（先异步后同步）由
    // loadScene 内的 reset() 代际作废兜底，无需处理。additive 不清（与主场景切换并存合法）。
    if (!additive)
        s_pendingLoad.clear();
    if (!AsyncLoader::loadSceneAsync(name, additive)) {
        Logger::error("SceneManager: 异步加载入队失败: {}（AsyncLoader 未初始化？）", name);
        return false;
    }
    Logger::info("SceneManager: 场景异步加载已入队: {}（additive={}，后台解码，帧末挂载）",
                 name, additive);
    return true;
}

bool SceneManager::unloadScene(const std::string& name) {
    // ===== V1.0-2 延迟卸载（Reviewer P1-1 修复）：本函数只做校验 + 入队 =====
    // 不在此处 erase/rebuildLoadedView/析构——脚本常在 updateAll 的 onUpdate 中调用，
    // 立即销毁会让 updateAll 的 range-for 迭代器失效、后续迭代项指向已析构 Scene。
    // 真正的卸载（Scene 析构 + releaseUnused + SceneUnloaded 事件）在帧末
    // flushPendingLoad → flushPendingUnloads 执行（帧序晚于 updateAll，§15）。
    if (s_activeScene != nullptr && s_activeScene->name() == name) {
        Logger::error("SceneManager: 不可卸载活动场景 \"{}\"（仅 additive 场景可卸载）", name);
        return false;
    }
    const bool loaded = std::any_of(s_additiveScenes.begin(), s_additiveScenes.end(),
                                    [&](const std::unique_ptr<Scene>& s) {
                                        return s->name() == name;
                                    });
    if (!loaded) {
        Logger::error("SceneManager: 卸载失败——additive 场景 \"{}\" 未加载", name);
        return false;
    }
    if (std::find(s_pendingUnloads.begin(), s_pendingUnloads.end(), name) !=
        s_pendingUnloads.end()) {
        return true;   // 已在队列：幂等
    }
    s_pendingUnloads.push_back(name);
    Logger::info("SceneManager: additive 场景卸载已入队: {}（帧末 flushPendingLoad 执行）", name);
    return true;
}

const std::vector<Scene*>& SceneManager::loadedScenes() {
    return s_loadedScenesView;   // activeScene 在前（渲染/update 顺序，§27.1）
}

void SceneManager::updateAll(float dt) {
    // 全部已加载场景 update（activeScene 先）；每 Scene 独立 PhysicsWorld 各自 step
    //（帧内顺序：主场景先——§27.1）。替代单 activeScene::update 调用点（接线归合波日）。
    // P1-1 兜底：迭代局部副本——unloadScene 已改延迟销毁，但脚本 onUpdate 仍可能经
    // setActiveScene(nullptr)/loadScene 等路径改视图，副本迭代对一切中途变更免疫
    const std::vector<Scene*> snapshot = s_loadedScenesView;
    for (Scene* scene : snapshot) {
        if (scene != nullptr)
            scene->update(dt);
    }
}

void SceneManager::setSceneParam(const std::string& key, const std::string& jsonValue) {
    s_sceneParams[key] = jsonValue;
}

std::string SceneManager::getSceneParam(const std::string& key) {
    const auto it = s_sceneParams.find(key);
    return (it != s_sceneParams.end()) ? it->second : std::string();   // 无 → 空串
}

void SceneManager::clearSceneParams() {
    s_sceneParams.clear();
}

std::unordered_map<std::string, std::string> SceneManager::snapshotSceneParams() {
    return s_sceneParams;   // 拷贝 = 冻结快照（AsyncLoader 入队瞬间消费）
}

bool SceneManager::mountAsyncScene(
    const std::string& name, bool additive, const std::string& jsonText,
    const std::unordered_map<std::string, std::string>& paramsSnapshot) {
    // AsyncLoader::flushPendingResults 的主线程执行体（GPU 上传发生在 deserialize 的
    // 资产解析阶段——铁律：加载线程不触碰 GL）。异常防护：挂载失败不崩溃（error + false）。
    if (s_activeScene == nullptr && !additive) {
        Logger::error("SceneManager: 异步场景 \"{}\" 挂载中止（无活动场景，非 additive 不可挂载）", name);
        return false;
    }
    if (!jsonText.empty()) {
        try {
            nlohmann::json doc = nlohmann::json::parse(jsonText);
            if (!doc.is_object())
                throw std::runtime_error("场景 JSON 根节点不是对象");
        } catch (const std::exception& e) {
            Logger::error("SceneManager: 异步场景 \"{}\" 挂载中止（JSON 无效）: {}", name, e.what());
            return false;
        }
    }
    try {
        if (additive) {
            // 挂新场景对象共存（不替换 activeScene；DDOL 不参与 additive 挂载）
            auto scene = std::make_unique<Scene>(name);
            SceneSerializer::deserialize(*scene, jsonText);
            {
                MountParamsGuard guard(paramsSnapshot);   // onStart 期间可读参数快照
                scene->start();
            }
            s_additiveScenes.push_back(std::move(scene));
            rebuildLoadedView();
            Events::post(SceneLoaded{name});   // 延迟投递（脚本感知 additive 场景就绪）
        } else {
            // 替换主场景（等价同步 loadScene 的 DDOL 迁移语义）。
            // P2-9 不变量：走到此处时 s_pendingLoad 必为空——loadSceneAsync(false) 入队时
            // 已清；若此后又有 loadScene（更新请求）则其 reset() 已使本结果代际作废，
            // 挂载不会发生。故此处不再清（flushPendingLoad 随后执行的是更晚入队的请求）。
            std::vector<std::string> ddolSubtrees = collectDdolSubtrees();
            unloadAllAdditive();
            SceneSerializer::deserialize(*s_activeScene, jsonText);
            for (const std::string& tree : ddolSubtrees) {
                if (!PrefabSerializer::instantiateSubtreeKeepingIds(*s_activeScene, tree, nullptr))
                    Logger::warn("SceneManager: DDOL 子树挂回失败（详见上方错误）");
            }
            AudioEngine::stopAll();
            {
                MountParamsGuard guard(paramsSnapshot);
                s_activeScene->start();
            }
            Events::post(SceneLoaded{name});
        }
    } catch (const std::exception& e) {
        Logger::error("SceneManager: 异步场景 \"{}\" 挂载异常（{}）——该场景加载失败", name,
                      e.what());
        return false;
    }
    Logger::info("SceneManager: 异步场景挂载完成: {}（additive={}）", name, additive);
    return true;
}

}  // namespace NovaEngine
