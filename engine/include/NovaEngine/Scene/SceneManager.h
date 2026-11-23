#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace NovaEngine {

class Scene;
class GameObject;
class AsyncLoader;

// 场景构建信息（assets/build.novabuild 条目；name 唯一键）
struct SceneBuildInfo {
    std::string name;
    std::filesystem::path path;   // 相对项目根的 .nova.json 路径（如 "assets/scenes/demo.nova.json"）
};

// 场景加载完成事件（§14.9）：flushPendingLoad 末尾 Events::post 延迟投递，
// 脚本 subscribeEvent<SceneLoaded> 感知切场景（投递发生在下一帧 flushDeferred）
struct SceneLoaded {
    std::string name;
};

// 场景卸载完成事件（V1.0-2，§27.1）：unloadScene 成功后 Events::post 延迟投递
struct SceneUnloaded {
    std::string name;
};

// 场景管理（V0.3-12，波 3）：引擎级单例。
// Build Settings 列表持久化于 assets/build.novabuild JSON：
//   { "version": 1, "scenes": [ { "name": "Demo", "path": "assets/scenes/demo.nova.json" } ] }
// init 读盘（缺失/损坏 → 空列表 + warn，不崩溃）；编辑器经 saveBuildSettings 写盘。
// loadScene 只入队（查无/失败 → error + false），帧末 flushPendingLoad 实际执行
//（§15 帧内顺序：Scene::update 之后调用；延迟执行防广播中迭代器失效，§19.4）。
class SceneManager {
public:
    static void init();     // 读盘 assets/build.novabuild
    static void shutdown(); // 清 activeScene / 构建列表 / 待加载队列

    // 运行期注册（Play 内动态场景用；同名覆盖）；静态持久化由编辑器经 saveBuildSettings
    static void registerScene(const std::string& name, const std::filesystem::path& path);
    static const std::vector<SceneBuildInfo>& buildList();

    // 写 assets/build.novabuild 并同步内存列表（§14.9 未列的最小补全：BuildSettingsPanel 持久化需要）
    static bool saveBuildSettings(const std::vector<SceneBuildInfo>& scenes);

    // 入队加载：查无该名字 → Logger::error + false；成功入队 → true（帧末执行）
    static bool loadScene(const std::string& name);
    static void flushPendingLoad();   // 无 activeScene / 无待加载 → 安全 no-op

    // ===== V1.0-2 多场景工作流（§27.1 冻结签名追加）=====
    // 异步加载：入队到资产加载线程（后台 CPU 解码，GPU 上传回主线程，帧末 flushPendingResults
    // 生效）；查无 → error + false。取消语义：任一新的加载请求（含同步 loadScene）先 reset()
    // 旧代际结果作废不挂载。additive=true 挂新场景对象共存，false 替换主场景（等价 loadScene）。
    static bool loadSceneAsync(const std::string& name, bool additive = false);
    // 卸载：仅 additive 场景可卸；activeScene 卸载 = error + false；查无 = error + false。
    // 帧末延迟销毁（Reviewer P1-1）：调用时只校验 + 入队（脚本可在 updateAll 的 onUpdate 中
    // 调用，立即析构会让 updateAll 迭代器失效/悬垂）；Scene 析构 + releaseUnused（内存回落）
    // + SceneUnloaded 延迟事件在 flushPendingLoad 内执行（帧序晚于 updateAll）。
    static bool unloadScene(const std::string& name);
    // 已加载场景（activeScene 在前；渲染/update 顺序）。无活动场景 = 空
    static const std::vector<Scene*>& loadedScenes();
    // 全部已加载场景 update（activeScene 先）；替代单 activeScene::update 调用点（接线归合波日）
    static void updateAll(float dt);
    // 场景间参数：loadScene/loadSceneAsync 调用瞬间快照参数表；新场景脚本 onStart 期间 getSceneParam
    // 读到该快照；挂载结束后恢复活表（setSceneParam 继续写活表）
    static void setSceneParam(const std::string& key, const std::string& jsonValue);
    static std::string getSceneParam(const std::string& key);    // 无 → 空串
    static void clearSceneParams();

    static Scene* activeScene();
    static void setActiveScene(Scene* scene);   // nullptr = 无活动场景（清待加载队列 + 卸载全部 additive）

    // 便捷：等价 go->setDontDestroyOnLoad(true)（§14.9 签名保留）
    static void dontDestroyOnLoad(GameObject* go);

private:
    // AsyncLoader 挂载/参数入口（friend）：
    // mountAsyncScene = flushPendingResults 的主线程执行体（GPU 上传发生在挂载的
    // deserialize 阶段，铁律：不在加载线程触碰 GL）；
    // snapshotSceneParams = 异步任务入队瞬间冻结参数表（loadSceneAsync 调用点快照，§27.1）
    friend class AsyncLoader;
    static bool mountAsyncScene(const std::string& name, bool additive, const std::string& jsonText,
                                const std::unordered_map<std::string, std::string>& paramsSnapshot);
    static std::unordered_map<std::string, std::string> snapshotSceneParams();
};

}  // namespace NovaEngine
