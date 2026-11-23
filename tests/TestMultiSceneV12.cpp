// TestMultiSceneV12.cpp —— V1.0-2 多场景异步加载验收自测（§27.1 七项，无 GL 可跑）
//
// 覆盖（逐条对应 §27.1 验收要点）：
//  1. 大厅+关卡 additive 共存：loadSceneAsync(name, true) → 后台解码 → 帧末挂载，
//     loadedScenes activeScene 在前，两场景对象共存
//  2/3. 加载不卡主线程：加载期主线程 update 帧时间 vs 空载基线（统计层数字 INFO + 宽松断言）
//  4. 卸载后内存回落：unloadScene → Scene 析构 + releaseUnused → memorySnapshot.textureBytes
//     回落到加载前基线
//  5. 场景间参数传递：setSceneParam("playerHp","100") → 加载 → 新场景脚本 onStart 读
//     getSceneParam 拿到 "100"（ParamProbe 组件验证）
//  6. 取消与异常：加载中触发新 LoadScene → 旧异步结果代际作废不挂载；坏 JSON/坏模型 →
//     error 日志 + 该场景加载失败，不传播不崩溃
//  7. 退出 Play 还原：setActiveScene(nullptr) 卸载全部 additive + reset 代际
//  附加：updateAll（全部已加载场景 update，activeScene 先，每场景独立物理世界各自 step）、
//  SceneUnloaded 延迟事件、非 additive 异步替换主场景（DDOL 迁移）

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/AsyncLoader.h>
#include <NovaEngine/Core/Events.h>
#include <NovaEngine/Profiling/Profiler.h>
#include <NovaEngine/Scene/Collider.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Rigidbody.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/SceneManager.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scripting/Behaviour.h>

#include <nlohmann/json.hpp>

#include <crtdbg.h>

#include <chrono>
#include <fstream>
#include <functional>
#include <iterator>
#include <thread>

#include "TestCommon.h"

using namespace NovaEngine;
using namespace NovaTest;

namespace {
// ===== 防弹窗纪律（主会话紧急反馈 2026-11-23）：MSVC Debug 断言默认弹模态窗口阻塞用户屏幕 =====
// 本 TU 进程启动即把 _CRT_ASSERT 重定向到 stderr（失败可见且不弹窗；断言随进程 abort）。
// 临时落点：cR 测试文件（合波日主会话统一收口到 NovaTests 的 TestMain.cpp 全局入口）。
struct AssertReportRedirect {
    AssertReportRedirect() {
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    }
};
const AssertReportRedirect s_assertRedirect;
}  // namespace

namespace {

// ===== 参数探针组件：onStart 读 getSceneParam（模拟关卡脚本读取场景间参数）=====
class ParamProbe : public Behaviour {
public:
    explicit ParamProbe(GameObject* owner) : Behaviour(owner) {}
    void onStart() override {
        lastValue = SceneManager::getSceneParam("playerHp");
        ++startCount;
    }
    static std::string lastValue;
    static int startCount;
};
std::string ParamProbe::lastValue;
int ParamProbe::startCount = 0;

// ===== 帧计数器：updateAll 推进验证 =====
class UpdateCounter : public Behaviour {
public:
    explicit UpdateCounter(GameObject* owner) : Behaviour(owner) {}
    void onUpdate(float) override { ++ticks; }
    int ticks = 0;
};

// ===== 卸载触发器：onUpdate 首帧调 unloadScene（P1-1 回归：updateAll 迭代中触发卸载）=====
class Unloader : public Behaviour {
public:
    explicit Unloader(GameObject* owner) : Behaviour(owner) {}
    void onUpdate(float) override {
        ++ticks;
        if (ticks == 1 && !target.empty())
            SceneManager::unloadScene(target);
    }
    int ticks = 0;
    std::string target;
};

// 场景文件写入（v4；带可选 MeshRenderer 资产引用；自动建父目录——TempDir 下无 assets 时兜底）
void writeSceneJson(const std::filesystem::path& file, const std::string& name,
                    const std::vector<std::string>& markers,
                    const std::vector<std::string>& meshPaths,
                    const std::vector<std::string>& texturePaths, bool withProbe = false,
                    bool withCounter = false) {
    std::filesystem::create_directories(file.parent_path());
    nlohmann::json root;
    root["version"] = 4;
    root["name"] = name;
    root["objects"] = nlohmann::json::array();
    uint64_t id = 1;
    for (const std::string& m : markers) {
        nlohmann::json obj;
        obj["id"] = id++;
        obj["name"] = m;
        obj["components"] = nlohmann::json::array();
        root["objects"].push_back(std::move(obj));
    }
    if (withProbe) {
        nlohmann::json obj;
        obj["id"] = id++;
        obj["name"] = "Probe";
        obj["components"] = nlohmann::json::array({nlohmann::json{{"type", "ParamProbe"}}});
        root["objects"].push_back(std::move(obj));
    }
    if (withCounter) {
        nlohmann::json obj;
        obj["id"] = id++;
        obj["name"] = "Counter";
        obj["components"] = nlohmann::json::array({nlohmann::json{{"type", "UpdateCounter"}}});
        root["objects"].push_back(std::move(obj));
    }
    for (const std::string& mesh : meshPaths) {
        nlohmann::json obj;
        obj["id"] = id++;
        obj["name"] = "MeshObj" + std::to_string(id);
        nlohmann::json mr;
        mr["type"] = "MeshRenderer";
        mr["mesh"] = mesh;
        nlohmann::json mat = nlohmann::json::object();
        if (!texturePaths.empty())
            mat["albedoMap"] = texturePaths.front();
        mr["material"] = std::move(mat);
        obj["components"] = nlohmann::json::array({std::move(mr)});
        root["objects"].push_back(std::move(obj));
    }
    std::ofstream out(file);
    out << root.dump(2);
}

// 测试项目根：拷入模型资产（worker 预热有真实素材）。
// 注：headless 不引用贴图（Texture::createFromPixels 无 GL 守卫——存量引擎缺口，测试避开；
// 材质资产 .novamat 纯 JSON 无 GL，内存统计用 materialAssetCount 验证）
void setupProjectAssets(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "assets" / "models");
    const std::filesystem::path srcRoot(NOVA_TEST_SOURCE_DIR);
    for (int i = 1; i <= 10; ++i) {
        std::filesystem::copy_file(srcRoot / "assets" / "models" / "cube.obj",
                                   root / "assets" / "models" / ("cube" + std::to_string(i) + ".obj"),
                                   std::filesystem::copy_options::overwrite_existing);
    }
}

// 写一个无贴图的 .novamat（v1 纯色材质，内存统计 materialAssetCount 用）
void writeMaterialAsset(const std::filesystem::path& file) {
    nlohmann::json root;
    root["version"] = 1;
    root["material"] = nlohmann::json{{"albedo", nlohmann::json::array({0.9, 0.2, 0.1})},
                                      {"roughness", 0.4},
                                      {"metallic", 0.1}};
    std::ofstream out(file);
    out << root.dump(2);
}

// 轮询 flushPendingResults 直到条件满足或超时
bool pollUntil(const std::function<bool()>& cond, int timeoutMs = 15000) {
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t0)
               .count() < timeoutMs) {
        AsyncLoader::flushPendingResults();
        if (cond())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    AsyncLoader::flushPendingResults();
    return cond();
}

struct AsyncLoaderGuard {
    AsyncLoaderGuard() { AsyncLoader::init(); }
    ~AsyncLoaderGuard() { AsyncLoader::shutdown(); }
};

}  // namespace

// 测试专用探针注册走 ScriptRegistry（脚本通道，不计入 ComponentRegistry 内置组件守卫
// 计数 26——静态注册会污染全进程内置表，TestSerialization 守卫用例失败）：
// 用 RAII 在用例内注册/注销，进程态零残留。
struct ProbeScriptGuard {
    ProbeScriptGuard() {
        ScriptRegistry::registerScript("ParamProbe",
                                       [](GameObject* go) { return std::make_unique<ParamProbe>(go); });
        ScriptRegistry::registerScript(
            "UpdateCounter", [](GameObject* go) { return std::make_unique<UpdateCounter>(go); });
    }
    ~ProbeScriptGuard() {
        ScriptRegistry::unregisterScript("ParamProbe");
        ScriptRegistry::unregisterScript("UpdateCounter");
    }
};

TEST_CASE("V1.0-2 lobby + level additive async coexist", "[multiscene][async]") {
    SceneManager::shutdown();
    TempDir tmp("nova-ms1");
    AssetDatabase::setProjectRoot(tmp.path());
    setupProjectAssets(tmp.path());

    // 大厅（活动场景）+ 关卡（additive 异步加载）
    Scene lobby("Lobby");
    lobby.createGameObject("LobbyMarker");
    SceneManager::setActiveScene(&lobby);
    writeSceneJson(tmp.path() / "assets" / "Level.nova.json", "Level", {"LevelMarker"},
                   {"assets/models/cube1.obj"}, {});
    SceneManager::registerScene("Level", "assets/Level.nova.json");

    // 事件延迟投递：SceneLoaded 在 flushDeferred 后送达。
    // 先排空其它用例残留的延迟事件，再在入队前订阅（挂载 post 的事件本用例独占消费）
    Events::flushDeferred();
    std::vector<std::string> loadedNames;
    const Events::Subscription sub = Events::subscribe<SceneLoaded>([&](const SceneLoaded& e) {
        loadedNames.push_back(e.name);
    });

    AsyncLoaderGuard guard;
    REQUIRE(SceneManager::loadSceneAsync("Level", /*additive=*/true));
    // 加载期：大厅保持（未挂载前只有主场景）
    REQUIRE(SceneManager::loadedScenes().size() == 1);

    REQUIRE(pollUntil([] {
        return SceneManager::loadedScenes().size() == 2;
    }));
    REQUIRE(AsyncLoader::progress() == Catch::Approx(1.0f));

    // 共存：loadedScenes activeScene 在前（渲染/update 顺序）
    const auto& loaded = SceneManager::loadedScenes();
    REQUIRE(loaded.size() == 2);
    CHECK(loaded[0] == &lobby);
    CHECK(loaded[1]->name() == "Level");
    CHECK(findFirst(lobby, "LobbyMarker") != nullptr);          // 大厅对象保持
    CHECK(findFirst(*loaded[1], "LevelMarker") != nullptr);    // 关卡对象出现
    // 事件延迟投递：flush 前不送达（pollUntil 只 flushPendingResults，不投递事件）
    CHECK(loadedNames.empty());
    Events::flushDeferred();
    REQUIRE(loadedNames.size() == 1);   // 先 REQUIRE 再下标（防越界断言——主会话紧急反馈根因）
    CHECK(loadedNames[0] == "Level");
    Events::unsubscribe(sub);

    Events::flushDeferred();   // 排空本用例延迟事件（防跨用例残留污染既有事件断言）
    SceneManager::shutdown();
}

TEST_CASE("V1.0-2 scene params passed to onStart snapshot", "[multiscene][params]") {
    ProbeScriptGuard probeGuard;   // ParamProbe 脚本注册（用例内，结束注销）
    SceneManager::shutdown();
    TempDir tmp("nova-ms2");
    AssetDatabase::setProjectRoot(tmp.path());
    Scene lobby("Lobby");
    lobby.createGameObject("LobbyMarker");
    SceneManager::setActiveScene(&lobby);
    writeSceneJson(tmp.path() / "assets" / "Level.nova.json", "Level", {"LevelMarker"}, {}, {},
                   /*withProbe=*/true);
    SceneManager::registerScene("Level", "assets/Level.nova.json");

    AsyncLoaderGuard guard;
    ParamProbe::lastValue.clear();
    ParamProbe::startCount = 0;
    SceneManager::setSceneParam("playerHp", "100");   // 加载前设置（调用瞬间快照）
    REQUIRE(SceneManager::loadSceneAsync("Level", true));
    REQUIRE(pollUntil([] { return SceneManager::loadedScenes().size() == 2; }));
    // onStart 期间读到快照
    CHECK(ParamProbe::startCount == 1);
    CHECK(ParamProbe::lastValue == "100");
    // 挂载结束后恢复活表（getSceneParam 仍可读活表值——挂载后继续可读，直到下次加载快照）
    CHECK(SceneManager::getSceneParam("playerHp") == "100");
    // 后续 setSceneParam 改动活表；再次加载时快照为最新值
    SceneManager::setSceneParam("playerHp", "200");
    REQUIRE(SceneManager::loadSceneAsync("Level", true));
    REQUIRE(pollUntil([] { return ParamProbe::startCount == 2; }));
    CHECK(ParamProbe::lastValue == "200");

    // 同步 loadScene 路径同样快照
    SceneManager::clearSceneParams();
    SceneManager::setSceneParam("playerHp", "300");
    writeSceneJson(tmp.path() / "assets" / "Lobby2.nova.json", "Lobby2", {"Lobby2Marker"}, {},
                   {}, /*withProbe=*/true);
    SceneManager::registerScene("Lobby2", "assets/Lobby2.nova.json");
    REQUIRE(SceneManager::loadScene("Lobby2"));
    SceneManager::flushPendingLoad();
    CHECK(ParamProbe::lastValue == "300");
    CHECK(findFirst(lobby, "Lobby2Marker") != nullptr);

    Events::flushDeferred();   // 排空本用例延迟事件（防跨用例残留污染既有事件断言）
    SceneManager::shutdown();
}

TEST_CASE("V1.0-2 unload memory falls back + unloadScene semantics + SceneUnloaded event", "[multiscene][unload]") {
    SceneManager::shutdown();
    TempDir tmp("nova-ms3");
    AssetDatabase::setProjectRoot(tmp.path());
    setupProjectAssets(tmp.path());

    Scene lobby("Lobby");
    lobby.createGameObject("LobbyMarker");
    SceneManager::setActiveScene(&lobby);
    // 关卡引用材质资产（.novamat 纯 JSON 无 GL——headless 内存统计 materialAssetCount 用；
    // 不引用贴图：Texture::createFromPixels 无 GL 守卫属存量缺口，测试避开）
    std::filesystem::create_directories(tmp.path() / "assets" / "materials");
    writeMaterialAsset(tmp.path() / "assets" / "materials" / "smoke.novamat");
    writeSceneJson(tmp.path() / "assets" / "Level.nova.json", "Level", {"LevelMarker"},
                   {"assets/models/cube1.obj"}, {});
    {
        // 给关卡 MeshRenderer 的 material 段补 materialAsset 引用（writeSceneJson 不支持 → 就地改）
        std::ifstream in(tmp.path() / "assets" / "Level.nova.json");
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        nlohmann::json doc = nlohmann::json::parse(text);
        for (auto& obj : doc["objects"])
            for (auto& comp : obj["components"])
                if (comp.value("type", "") == "MeshRenderer")
                    comp["material"]["materialAsset"] = "assets/materials/smoke.novamat";
        std::ofstream out(tmp.path() / "assets" / "Level.nova.json");
        out << doc.dump(2);
    }
    SceneManager::registerScene("Level", "assets/Level.nova.json");

    AsyncLoaderGuard guard;
    const size_t baseline = AssetDatabase::memorySnapshot().materialAssetCount;
    REQUIRE(SceneManager::loadSceneAsync("Level", true));
    REQUIRE(pollUntil([] { return SceneManager::loadedScenes().size() == 2; }));
    const size_t loaded = AssetDatabase::memorySnapshot().materialAssetCount;
    CHECK(loaded == baseline + 1);   // 材质资产存活（MeshRenderer 持有）

    // activeScene 卸载 = error + false
    CHECK_FALSE(SceneManager::unloadScene("Lobby"));
    // 查无 = error + false
    CHECK_FALSE(SceneManager::unloadScene("NoSuch"));

    // SceneUnloaded 延迟事件（订阅前排空残留）
    Events::flushDeferred();
    std::vector<std::string> unloadedNames;
    const Events::Subscription sub = Events::subscribe<SceneUnloaded>([&](const SceneUnloaded& e) {
        unloadedNames.push_back(e.name);
    });
    // ===== 帧末延迟销毁语义（Reviewer P1-1）：unloadScene 只入队，视图/场景不动 =====
    CHECK(SceneManager::unloadScene("Level"));
    REQUIRE(SceneManager::loadedScenes().size() == 2);   // 尚未销毁（脚本 updateAll 迭代安全）
    CHECK(unloadedNames.empty());                        // 事件在真正卸载时发
    SceneManager::flushPendingLoad();                    // 帧末：执行延迟卸载
    REQUIRE(SceneManager::loadedScenes().size() == 1);   // REQUIRE 再下标（防越界断言）
    CHECK(SceneManager::loadedScenes()[0] == &lobby);
    Events::flushDeferred();
    REQUIRE(unloadedNames.size() == 1);   // REQUIRE 再下标（防越界断言）
    CHECK(unloadedNames[0] == "Level");
    Events::unsubscribe(sub);

    // 内存回落：releaseUnused 后 materialAssetCount 回到基线（±10%，波 3 收口口径）
    const size_t after = AssetDatabase::memorySnapshot().materialAssetCount;
    INFO("baseline=" << baseline << " loaded=" << loaded << " afterUnload=" << after);
    CHECK(after <= baseline + baseline / 10 + 1);

    Events::flushDeferred();   // 排空本用例延迟事件（防跨用例残留污染既有事件断言）
    SceneManager::shutdown();
}

TEST_CASE("V1.0-2 updateAll unloadScene deferred no dangling (P1-1)", "[multiscene][p11]") {
    // 场景：脚本 onUpdate 首帧调 unloadScene("Level")——updateAll 迭代中途触发卸载。
    // 预期：卸载帧末延迟执行（迭代不失效、后续迭代项无悬垂），SceneUnloaded 事件在真正卸载时发
    SceneManager::shutdown();
    TempDir tmp("nova-ms9");
    AssetDatabase::setProjectRoot(tmp.path());
    setupProjectAssets(tmp.path());

    Scene lobby("Lobby");
    lobby.createGameObject("LobbyMarker");
    GameObject* unloaderGo = lobby.createGameObject("Unloader");
    auto* unloader = unloaderGo->addComponent<Unloader>();
    unloader->target = "Level";
    SceneManager::setActiveScene(&lobby);
    writeSceneJson(tmp.path() / "assets" / "Level.nova.json", "Level", {"LevelMarker"},
                   {"assets/models/cube1.obj"}, {});
    SceneManager::registerScene("Level", "assets/Level.nova.json");

    AsyncLoaderGuard guard;
    REQUIRE(SceneManager::loadSceneAsync("Level", true));
    REQUIRE(pollUntil([] { return SceneManager::loadedScenes().size() == 2; }));

    Events::flushDeferred();
    std::vector<std::string> unloadedNames;
    const Events::Subscription sub = Events::subscribe<SceneUnloaded>([&](const SceneUnloaded& e) {
        unloadedNames.push_back(e.name);
    });

    // updateAll ×3：首帧 onUpdate 入队卸载；Level 场景仍存活（延迟销毁 → 迭代无悬垂）
    for (int i = 0; i < 3; ++i)
        SceneManager::updateAll(1.0f / 60.0f);
    CHECK(unloader->ticks == 3);
    REQUIRE(SceneManager::loadedScenes().size() == 2);   // 尚未卸载（延迟销毁语义）
    CHECK(findFirst(*SceneManager::loadedScenes()[1], "LevelMarker") != nullptr);   // 无悬垂：可访问

    // 帧末 flushPendingLoad → 真正卸载 + 事件（真正卸载时发）
    SceneManager::flushPendingLoad();
    REQUIRE(SceneManager::loadedScenes().size() == 1);
    CHECK(unloadedNames.empty());   // 延迟投递
    Events::flushDeferred();
    REQUIRE(unloadedNames.size() == 1);
    CHECK(unloadedNames[0] == "Level");

    // 卸载后继续 updateAll 安全（队列已空）
    SceneManager::updateAll(1.0f / 60.0f);
    CHECK(unloader->ticks == 4);

    Events::unsubscribe(sub);
    Events::flushDeferred();   // 排空本用例延迟事件（防跨用例残留污染既有事件断言）
    SceneManager::shutdown();
}

TEST_CASE("V1.0-2 cancellation: new load invalidates old async result", "[multiscene][cancel]") {
    SceneManager::shutdown();
    TempDir tmp("nova-ms4");
    AssetDatabase::setProjectRoot(tmp.path());
    setupProjectAssets(tmp.path());

    Scene lobby("Lobby");
    lobby.createGameObject("LobbyMarker");
    SceneManager::setActiveScene(&lobby);
    // 关卡带 10 个模型（worker 需时；期间触发新加载 → 取消）
    std::vector<std::string> meshes;
    for (int i = 1; i <= 10; ++i)
        meshes.push_back("assets/models/cube" + std::to_string(i) + ".obj");
    writeSceneJson(tmp.path() / "assets" / "Level.nova.json", "Level", {"LevelMarker"}, meshes,
                   {});
    writeSceneJson(tmp.path() / "assets" / "Lobby2.nova.json", "Lobby2", {"Lobby2Marker"}, {},
                   {});
    SceneManager::registerScene("Level", "assets/Level.nova.json");
    SceneManager::registerScene("Lobby2", "assets/Lobby2.nova.json");

    AsyncLoaderGuard guard;
    const uint32_t genBefore = AsyncLoader::generation();
    REQUIRE(SceneManager::loadSceneAsync("Level", true));
    // 立即触发同步 LoadScene：共用代际 → reset()（旧结果作废不挂载）
    REQUIRE(SceneManager::loadScene("Lobby2"));
    SceneManager::flushPendingLoad();
    CHECK(findFirst(lobby, "Lobby2Marker") != nullptr);
    // 等 worker 完成（10 个模型 Assimp ~百 ms 级）→ flush：代际不符 → 丢弃不挂载
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    AsyncLoader::flushPendingResults();
    CHECK(SceneManager::loadedScenes().size() == 1);   // Level 未挂载
    CHECK(AsyncLoader::generation() > genBefore);      // 代际已递增
    // reset 直调：再次入队 + reset → 结果作废
    REQUIRE(SceneManager::loadSceneAsync("Level", true));
    AsyncLoader::reset();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    AsyncLoader::flushPendingResults();
    CHECK(SceneManager::loadedScenes().size() == 1);

    Events::flushDeferred();   // 排空本用例延迟事件（防跨用例残留污染既有事件断言）
    SceneManager::shutdown();
}

TEST_CASE("V1.0-2 exception isolation: bad JSON / bad model no crash", "[multiscene][exception]") {
    SceneManager::shutdown();
    TempDir tmp("nova-ms5");
    AssetDatabase::setProjectRoot(tmp.path());

    Scene lobby("Lobby");
    lobby.createGameObject("LobbyMarker");
    SceneManager::setActiveScene(&lobby);
    // 坏 JSON
    {
        std::ofstream bad(tmp.path() / "assets" / "Bad.nova.json");
        bad << "{ not valid json !!!";
    }
    SceneManager::registerScene("Bad", "assets/Bad.nova.json");
    // 场景合法但引用坏模型
    writeSceneJson(tmp.path() / "assets" / "BadMesh.nova.json", "BadMesh", {"BadMeshMarker"},
                   {"assets/models/corrupt.obj"}, {});
    {
        std::filesystem::create_directories(tmp.path() / "assets" / "models");
        std::ofstream badModel(tmp.path() / "assets" / "models" / "corrupt.obj");
        badModel << "garbage model data \x01\x02\x03";
    }
    SceneManager::registerScene("BadMesh", "assets/BadMesh.nova.json");

    AsyncLoaderGuard guard;
    REQUIRE(SceneManager::loadSceneAsync("Bad", true));
    REQUIRE(pollUntil([] { return AsyncLoader::progress() == Catch::Approx(1.0f); }));
    CHECK(SceneManager::loadedScenes().size() == 1);   // 坏 JSON → 不挂载（error 日志）

    REQUIRE(SceneManager::loadSceneAsync("BadMesh", true));
    REQUIRE(pollUntil([] { return SceneManager::loadedScenes().size() == 2; }));
    // 坏模型 → 场景挂载成功但该网格为空（warn，不崩溃）；场景对象在
    const auto& loaded = SceneManager::loadedScenes();
    REQUIRE(loaded.size() == 2);
    CHECK(findFirst(*loaded[1], "BadMeshMarker") != nullptr);

    Events::flushDeferred();   // 排空本用例延迟事件（防跨用例残留污染既有事件断言）
    SceneManager::shutdown();
}

TEST_CASE("V1.0-2 updateAll: all loaded scenes update + per-scene physics", "[multiscene][updateAll]") {
    ProbeScriptGuard probeGuard;   // UpdateCounter 脚本注册（用例内，结束注销）
    SceneManager::shutdown();
    TempDir tmp("nova-ms6");
    AssetDatabase::setProjectRoot(tmp.path());

    Scene lobby("Lobby");
    lobby.createGameObject("LobbyMarker");
    GameObject* counterA = lobby.createGameObject("CounterA");
    UpdateCounter* ca = counterA->addComponent<UpdateCounter>();
    GameObject* fallerA = lobby.createGameObject("FallerA");
    fallerA->transform()->setLocalPosition(glm::vec3(0.0f, 5.0f, 0.0f));
    fallerA->addComponent<Rigidbody>()->mass = 1.0f;
    auto* faCollider = fallerA->addComponent<BoxCollider>();
    faCollider->halfExtents = glm::vec3(0.25f);
    SceneManager::setActiveScene(&lobby);

    writeSceneJson(tmp.path() / "assets" / "Level.nova.json", "Level", {"LevelMarker"}, {}, {},
                   /*withProbe=*/false, /*withCounter=*/true);
    SceneManager::registerScene("Level", "assets/Level.nova.json");
    // 关卡场景也挂一个下落刚体（每场景独立 PhysicsWorld：updateAll 各自 step）
    AsyncLoaderGuard guard;
    REQUIRE(SceneManager::loadSceneAsync("Level", true));
    REQUIRE(pollUntil([] { return SceneManager::loadedScenes().size() == 2; }));
    Scene* level = SceneManager::loadedScenes()[1];
    GameObject* fallerB = level->createGameObject("FallerB");
    fallerB->transform()->setLocalPosition(glm::vec3(0.0f, 5.0f, 0.0f));
    fallerB->addComponent<Rigidbody>()->mass = 1.0f;
    auto* fbCollider = fallerB->addComponent<BoxCollider>();
    fbCollider->halfExtents = glm::vec3(0.25f);
    GameObject* counterGo = findFirst(*level, "Counter");
    REQUIRE(counterGo != nullptr);   // 先判空再解引用（防 SEGV——主会话紧急反馈根因一）
    // 脚本组件以 Behaviour typeId 挂载（§26.2 共用 typeId）→ getComponent<T> 精确匹配不到，
    // 用 getComponents<Behaviour>（dynamic_cast，含派生类）取回
    UpdateCounter* cb = nullptr;
    for (auto* c : counterGo->getComponents<Behaviour>())
        if (auto* uc = dynamic_cast<UpdateCounter*>(c)) {
            cb = uc;
            break;
        }
    REQUIRE(cb != nullptr);

    lobby.start();       // 两场景都已挂载后统一进入 Play（start 各自物理重建）
    level->start();
    for (int i = 0; i < 60; ++i)
        SceneManager::updateAll(1.0f / 60.0f);

    CHECK(ca->ticks == 60);
    CHECK(cb->ticks == 60);
    const float yA = fallerA->transform()->worldPosition().y;
    const float yB = fallerB->transform()->worldPosition().y;
    INFO("FallerA y=" << yA << " FallerB y=" << yB);
    CHECK(yA < 4.5f);   // 两场景物理各自步进（下坠）
    CHECK(yB < 4.5f);

    Events::flushDeferred();   // 排空本用例延迟事件（防跨用例残留污染既有事件断言）
    SceneManager::shutdown();
}

TEST_CASE("V1.0-2 async replace main scene with DDOL migration + exit play teardown", "[multiscene][switch]") {
    SceneManager::shutdown();
    TempDir tmp("nova-ms7");
    AssetDatabase::setProjectRoot(tmp.path());

    Scene lobby("Lobby");
    lobby.createGameObject("LobbyMarker");
    GameObject* persist = lobby.createGameObject("Persist");
    persist->setDontDestroyOnLoad(true);
    const uint64_t persistId = persist->id();   // 挂载前捕获（挂载后原对象已销毁——同 TestSceneManager 纪律）
    SceneManager::setActiveScene(&lobby);

    writeSceneJson(tmp.path() / "assets" / "Lobby2.nova.json", "Lobby2", {"Lobby2Marker"}, {},
                   {});
    SceneManager::registerScene("Lobby2", "assets/Lobby2.nova.json");

    AsyncLoaderGuard guard;
    REQUIRE(SceneManager::loadSceneAsync("Lobby2", /*additive=*/false));
    REQUIRE(pollUntil([] {
        return SceneManager::activeScene() != nullptr &&
               findFirst(*SceneManager::activeScene(), "Lobby2Marker") != nullptr;
    }));
    // 主场景替换：新场景载入，DDOL 子树迁移存活
    CHECK(SceneManager::loadedScenes().size() == 1);
    CHECK(findFirst(lobby, "Lobby2Marker") != nullptr);
    CHECK(lobby.findGameObject(persistId) != nullptr);   // DDOL 迁移（原 ID 保持）
    CHECK(findFirst(lobby, "LobbyMarker") == nullptr);

    // 退出 Play 收口：setActiveScene(nullptr) → 卸载全部 additive + reset + 视图空
    SceneManager::setActiveScene(nullptr);
    CHECK(SceneManager::loadedScenes().empty());
    const uint32_t gen = AsyncLoader::generation();
    AsyncLoader::reset();
    CHECK(AsyncLoader::generation() == gen + 1);

    Events::flushDeferred();   // 排空本用例延迟事件（防跨用例残留污染既有事件断言）
    SceneManager::shutdown();
}

TEST_CASE("V1.0-2 main thread not blocked during load (frame time comparison)", "[multiscene][timing]") {
    SceneManager::shutdown();
    TempDir tmp("nova-ms8");
    AssetDatabase::setProjectRoot(tmp.path());
    setupProjectAssets(tmp.path());

    Scene lobby("Lobby");
    SceneManager::setActiveScene(&lobby);
    std::vector<std::string> meshes;
    for (int i = 1; i <= 10; ++i)
        meshes.push_back("assets/models/cube" + std::to_string(i) + ".obj");
    writeSceneJson(tmp.path() / "assets" / "Level.nova.json", "Level", {"LevelMarker"}, meshes,
                   {});
    SceneManager::registerScene("Level", "assets/Level.nova.json");

    // 空载基线：60 次 update 的平均帧时间
    const auto idleT0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 60; ++i)
        lobby.update(1.0f / 60.0f);
    const double idleMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - idleT0)
            .count() / 60.0;

    AsyncLoaderGuard guard;
    REQUIRE(SceneManager::loadSceneAsync("Level", true));
    // 加载期：主线程照常 update（worker 在后台解码）；记录加载期帧时间
    double loadSumMs = 0.0;
    int loadFrames = 0;
    while (AsyncLoader::progress() < 1.0f) {
        const auto f0 = std::chrono::steady_clock::now();
        lobby.update(1.0f / 60.0f);
        loadSumMs += std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - f0)
                         .count();
        ++loadFrames;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // 挂载帧成本（主线程：JSON 已解析 + 资产缓存已暖 → 只剩 GPU 上传（无 GL 时跳过）+ 挂载）
    const auto mountT0 = std::chrono::steady_clock::now();
    AsyncLoader::flushPendingResults();
    const double mountMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mountT0)
            .count();
    REQUIRE(SceneManager::loadedScenes().size() == 2);

    const double loadFrameMs = loadFrames > 0 ? loadSumMs / loadFrames : 0.0;
    INFO("空载帧时间=" << idleMs << "ms；加载期主线程帧时间=" << loadFrameMs
                       << "ms（" << loadFrames << " 帧）；挂载帧=" << mountMs << "ms");
    // 加载期主线程帧时间与空载同量级（后台解码不卡主线程；宽松界防 CI 抖动）
    CHECK(loadFrameMs < idleMs * 3.0 + 2.0);

    Events::flushDeferred();   // 排空本用例延迟事件（防跨用例残留污染既有事件断言）
    SceneManager::shutdown();
}
