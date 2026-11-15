#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace NovaEngine {

class Scene;
class GameObject;

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

    static Scene* activeScene();
    static void setActiveScene(Scene* scene);   // nullptr = 无活动场景（并清待加载队列）

    // 便捷：等价 go->setDontDestroyOnLoad(true)（§14.9 签名保留）
    static void dontDestroyOnLoad(GameObject* go);
};

}  // namespace NovaEngine
