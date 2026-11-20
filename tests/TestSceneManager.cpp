// TestSceneManager.cpp —— 场景管理族（§26.1 覆盖清单 5：SceneManager 入队/切换/DDOL 迁移，无 GL 可跑）
//
// 对 KNOWN-ISSUES：
//  - V0.3-12 契约：loadScene 只入队，帧末 flushPendingLoad 执行（防广播中迭代器失效）
//  - DDOL 铁律：先于 deserialize 收集 DDOL 子树、后于 deserialize 挂回（原 ID 保持）
//  - SceneLoaded 延迟事件（下一帧 flushDeferred 送达）
//  - 无活动场景 → flushPendingLoad 安全 no-op

#include <catch2/catch_test_macros.hpp>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Events.h>
#include <NovaEngine/Scene/SceneManager.h>
#include <NovaEngine/Serialization/SceneSerializer.h>

#include <nlohmann/json.hpp>

#include "TestCommon.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace NovaEngine;
using namespace NovaTest;

namespace {

// 造一个含标志对象的场景文件（写进项目根相对目录）
std::filesystem::path writeSceneFile(const std::filesystem::path& projectRoot,
                                     const std::string& fileName,
                                     const std::string& markerName) {
    const std::filesystem::path scenesDir = projectRoot / "assets" / "scenes";
    std::filesystem::create_directories(scenesDir);
    Scene s("SceneFile");
    s.createGameObject(markerName);
    const std::filesystem::path file = scenesDir / fileName;
    REQUIRE(SceneSerializer::saveToFile(s, file));
    return file;
}

}  // namespace

TEST_CASE("SceneManager: registerScene overwrite and buildList", "[scenemanager]") {
    SceneManager::shutdown();   // 隔离（逐用例进程时无害）
    SceneManager::registerScene("A", "assets/scenes/a.nova.json");
    SceneManager::registerScene("B", "assets/scenes/b.nova.json");
    REQUIRE(SceneManager::buildList().size() == 2);

    // 同名覆盖（幂等语义，与 ComponentRegistry 注册表一致）
    SceneManager::registerScene("A", "assets/scenes/a2.nova.json");
    REQUIRE(SceneManager::buildList().size() == 2);
    const auto& list = SceneManager::buildList();
    bool found = false;
    for (const auto& b : list)
        if (b.name == "A")
            found = (b.path == "assets/scenes/a2.nova.json");
    REQUIRE(found);

    SceneManager::shutdown();
}

TEST_CASE("SceneManager: enqueue/flush switch and SceneLoaded event", "[scenemanager]") {
    SceneManager::shutdown();
    TempDir tmp("nova-sm");
    AssetDatabase::setProjectRoot(tmp.path());

    const std::filesystem::path fileA = writeSceneFile(tmp.path(), "A.nova.json", "MarkerA");
    const std::filesystem::path fileB = writeSceneFile(tmp.path(), "B.nova.json", "MarkerB");
    SceneManager::registerScene("A", fileA);
    SceneManager::registerScene("B", fileB);

    // SceneLoaded 订阅（延迟事件：flushPendingLoad 末尾 post，下帧 flushDeferred 送达）
    std::vector<std::string> loadedNames;
    const Events::Subscription sub = Events::subscribe<SceneLoaded>([&](const SceneLoaded& e) {
        loadedNames.push_back(e.name);
    });

    Scene active("Active");
    active.createGameObject("PreSwitch");
    SceneManager::setActiveScene(&active);

    // 查无 → error + false
    REQUIRE_FALSE(SceneManager::loadScene("NoSuchScene"));

    // 入队：此时尚未执行切换
    REQUIRE(SceneManager::loadScene("A"));
    REQUIRE(findFirst(active, "PreSwitch") != nullptr);

    SceneManager::flushPendingLoad();
    REQUIRE(findFirst(active, "MarkerA") != nullptr);      // 新场景载入
    REQUIRE(findFirst(active, "PreSwitch") == nullptr);    // 旧场景清空
    REQUIRE(countObjects(active) == 1);

    // 事件延迟送达：flushDeferred 前不投递
    REQUIRE(loadedNames.empty());
    Events::flushDeferred();
    REQUIRE(loadedNames.size() == 1);
    REQUIRE(loadedNames[0] == "A");

    // 无待加载时 flush 安全 no-op
    SceneManager::flushPendingLoad();
    REQUIRE(countObjects(active) == 1);

    Events::unsubscribe(sub);
    SceneManager::shutdown();
}

TEST_CASE("SceneManager: DDOL subtree migration keeps IDs", "[scenemanager][ddol]") {
    SceneManager::shutdown();
    TempDir tmp("nova-sm-ddol");
    AssetDatabase::setProjectRoot(tmp.path());

    const std::filesystem::path fileB = writeSceneFile(tmp.path(), "B.nova.json", "MarkerB");
    SceneManager::registerScene("B", fileB);

    Scene active("Active");
    GameObject* persist = active.createGameObject("Persist");
    persist->setDontDestroyOnLoad(true);
    GameObject* persistChild = active.createGameObject("PersistChild", persist);
    GameObject* ddolLeaf = active.createGameObject("Leaf", persistChild);
    ddolLeaf->setDontDestroyOnLoad(true);   // 非根 DDOL：随父级子树迁移（父级是迁移根）
    active.createGameObject("Gone");
    const uint64_t persistId = persist->id();
    const uint64_t childId = persistChild->id();
    const uint64_t leafId = ddolLeaf->id();

    SceneManager::setActiveScene(&active);
    REQUIRE(SceneManager::loadScene("B"));
    SceneManager::flushPendingLoad();

    // 新场景载入
    REQUIRE(findFirst(active, "MarkerB") != nullptr);
    // DDOL 子树以原 ID 迁移存活
    REQUIRE(active.findGameObject(persistId) != nullptr);
    REQUIRE(active.findGameObject(childId) != nullptr);
    REQUIRE(active.findGameObject(leafId) != nullptr);
    GameObject* p = active.findGameObject(persistId);
    REQUIRE(p->dontDestroyOnLoad());
    REQUIRE(active.findGameObject(childId)->parent() == p);
    // 非 DDOL 对象消失
    REQUIRE(findFirst(active, "Gone") == nullptr);

    Events::flushDeferred();   // 消费 SceneLoaded 延迟事件
    SceneManager::shutdown();
}

TEST_CASE("SceneManager: no active scene flush no-op", "[scenemanager]") {
    SceneManager::shutdown();
    SceneManager::registerScene("A", "assets/scenes/a.nova.json");
    SceneManager::setActiveScene(nullptr);
    REQUIRE_FALSE(SceneManager::loadScene("NoSuch"));   // 查无仍 false
    REQUIRE(SceneManager::loadScene("A"));              // 入队成功
    SceneManager::flushPendingLoad();                   // 无活动场景 → no-op 不崩溃
    REQUIRE(SceneManager::activeScene() == nullptr);

    Scene active("Active");
    SceneManager::setActiveScene(&active);   // 设置活动场景清待加载队列
    SceneManager::flushPendingLoad();
    REQUIRE(countObjects(active) == 0);
    SceneManager::shutdown();
}

TEST_CASE("SceneManager: saveBuildSettings writes file", "[scenemanager]") {
    SceneManager::shutdown();
    TempDir tmp("nova-sm-save");
    AssetDatabase::setProjectRoot(tmp.path());
    std::filesystem::create_directories(tmp.path() / "assets");   // saveBuildSettings 不建父目录

    const std::vector<SceneBuildInfo> scenes = {
        {"Demo", "assets/scenes/demo.nova.json"},
        {"Second", "assets/scenes/second.nova.json"},
    };
    REQUIRE(SceneManager::saveBuildSettings(scenes));

    const std::filesystem::path file = tmp.path() / "assets" / "build.novabuild";
    REQUIRE(std::filesystem::exists(file));

    // 文件内容与内存列表一致
    std::ifstream in(file);
    REQUIRE(in.is_open());
    nlohmann::json doc = nlohmann::json::parse(in);
    REQUIRE(doc["version"] == 1);
    REQUIRE(doc["scenes"].size() == 2);
    REQUIRE(doc["scenes"][0]["name"] == "Demo");
    REQUIRE(doc["scenes"][1]["path"] == "assets/scenes/second.nova.json");
    REQUIRE(SceneManager::buildList().size() == 2);

    SceneManager::shutdown();
}
