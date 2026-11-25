// TestMemoryV13.cpp —— V1.0-13 内存与泄漏审计（§28.1，全部无 GL、headless 可跑）
//
// 用例（逐条对应 §28.1 契约）：
//  ① 100 次 LoadScene 同步切换 + additive 卸载循环 → memorySnapshot 回到基线
//     （容差：本地 5% / CI 10%（§28.1）+ 三轮平均防分配器抖动 + 失败重跑一次）
//  ② Play 100 次循环（Scene::start/update×3/退出快照还原，harness 直驱、无编辑器 GUI）
//     → 基线对比（Profiler::memorySnapshot + CRT 堆差量双证据）
//  ③ 热重载：ScriptRuntime reload×10 → ScriptRegistry 名称集不变 + memorySnapshot 稳定
//     （NovaTestScripts 测试脚本 DLL 提供 HotReloadProbe 脚本）
//  ④ 脚本卸载路径：reload 后旧 DLL 引用计数为零（FreeLibrary 语义）——
//     exe 导出钩子 nova_test_hook_on_init 跨模块实例计数为权威判据：
//     旧模块引用计数未归零 → LoadLibrary 返回旧模块、初始化器不重跑 → 计数不增 → 红；
//     GetModuleHandleExW 模块句柄变化 + GetProcAddress 初始化计数为交叉校验。
//  附加：_CrtMemCheckpoint/_CrtMemDifference 的 CRT 堆净差量（自动基线证据，
//     与「V1.0-13 人工审计步骤」的 VS CRT 泄漏报告互为双证据，§28.1）。
//
// 资产度量设计（无 GL 纪律）：贴图/网格上传需 GL → 测试避开（Texture/Mesh 无 GL 守卫，
// 存量缺口已备案）；可无 GL 走通的资产度量 = 材质资产计数（.novamat 纯 JSON）+
// 音频 PCM 字节（miniaudio CPU 解码）——两者在场景切换/卸载中真实 churn。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/AsyncLoader.h>
#include <NovaEngine/Audio/AudioClip.h>
#include <NovaEngine/Core/Events.h>
#include <NovaEngine/Profiling/Profiler.h>
#include <NovaEngine/Scene/Collider.h>
#include <NovaEngine/Scene/Rigidbody.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/SceneManager.h>
#include <NovaEngine/Scripting/Behaviour.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>
#include <NovaEngine/Scripting/ScriptRuntime.h>
#include <NovaEngine/Serialization/SceneSerializer.h>

#include <nlohmann/json.hpp>

#include <crtdbg.h>   // _CrtMemCheckpoint/Difference（CRT 堆差量）
#include <windows.h>  // GetModuleHandleExW/GetProcAddress

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "TestCommon.h"

using namespace NovaEngine;
using namespace NovaTest;

namespace {

// ===== exe 导出钩子（NovaTestScripts 静态初始化器经导入表调用；跨模块实例单调计数）=====
// 每次 NovaTestScripts.dll 的新实例加载 → 初始化器重跑 → 计数 +1；
// 旧模块引用计数未归零（FreeLibrary 语义被破坏）→ LoadLibrary 返回旧模块且
// 初始化器不重跑 → 计数不变 → 用例④红。
int s_scriptInitHookCount = 0;

// ===== CRT 堆差量（V1.0-13 自动基线证据）=====
struct CrtMemSpan {
    _CrtMemState begin{}, end{}, diff{};
    void checkpointBegin() { _CrtMemCheckpoint(&begin); }
    void checkpointEnd() {
        _CrtMemCheckpoint(&end);
        _CrtMemDifference(&diff, &begin, &end);
    }
    long netBytes() const { return static_cast<long>(diff.lSizes[1]); }  // 分配 - 释放
    long netCount() const { return static_cast<long>(diff.lCounts[1]); }
};

// 容差：本地 5%，CI 放宽 10%（§28.1「本地与 CI 差异 → 容差 5%（CI 放宽 10%）」）
double leakTolerancePct() {
    if (const char* ci = std::getenv("CI"); ci != nullptr && ci[0] != '\0')
        return 10.0;
    return 5.0;
}

// ===== MemorySnapshot 字段级漂移 =====
std::vector<std::pair<const char*, size_t>> snapshotFields(const MemorySnapshot& s) {
    return {
        {"meshVertexBytes", s.meshVertexBytes},
        {"textureBytes", s.textureBytes},
        {"audioBytes", s.audioBytes},
        {"cubemapBytes", s.cubemapBytes},
        {"materialAssetCount", s.materialAssetCount},
        {"objectCount", s.objectCount},
        {"componentCount", s.componentCount},
        {"loadedSceneCount", s.loadedSceneCount},
    };
}

// 基线/测量值比较：零-零跳过；返回最大漂移百分比，逐字段 fprintf 明细（无堆串——
// CRT 差量测量段内禁止 std::string 拼接/Catch2 INFO：实测两者自身分配会污染净差量）
double maxFieldDriftPct(const MemorySnapshot& base, const MemorySnapshot& cur) {
    const auto b = snapshotFields(base);
    const auto c = snapshotFields(cur);
    double worst = 0.0;
    for (size_t i = 0; i < b.size(); ++i) {
        if (b[i].second == 0 && c[i].second == 0)
            continue;
        const double pct =
            (b[i].second == 0)
                ? (c[i].second == 0 ? 0.0 : 100.0)   // 基线零、当前非零 → 全量新增
                : std::abs(static_cast<double>(c[i].second) -
                           static_cast<double>(b[i].second)) *
                      100.0 / static_cast<double>(b[i].second);
        worst = std::max(worst, pct);
        fprintf(stderr, "  %s: %llu -> %llu（%.2f%%）\n", b[i].first,
                static_cast<unsigned long long>(b[i].second),
                static_cast<unsigned long long>(c[i].second), pct);
    }
    return worst;
}

std::vector<std::string> sortedNames() {
    std::vector<std::string> names = ScriptRegistry::allNames();
    std::sort(names.begin(), names.end());
    return names;
}

// ===== 内存探针脚本（用例①：onStart 加载音频资产并持有强引用；场景销毁 → 引用释放）=====
// 经 ScriptRegistry 注册（脚本通道，不污染 ComponentRegistry 内置组件守卫计数 26）
class MemProbe : public Behaviour {
public:
    explicit MemProbe(GameObject* owner) : Behaviour(owner) {}
    void onStart() override {
        if (!clipPath.empty())
            // 绝对化后加载：AssetStream 的磁盘相对读按 CWD 锚定（编辑器/Player 启动
            // chdir 项目根故相对路径可用；测试进程 CWD 无关 → 显式相对项目根）
            m_clip = AssetDatabase::instance().getAudioClip(AssetDatabase::projectRoot() /
                                                            clipPath);
    }
    void onSerialize(nlohmann::json& out) const override { out["clip"] = clipPath; }
    void onDeserialize(const nlohmann::json& in) override {
        clipPath = in.value("clip", std::string());
    }
    std::string clipPath;
    std::shared_ptr<AudioClip> m_clip;
};

// RAII：用例内注册/注销（进程态零残留；测试探针不进内置组件表）
struct MemProbeScriptGuard {
    MemProbeScriptGuard() {
        ScriptRegistry::registerScript("MemProbe",
                                       [](GameObject* go) {
                                           return std::make_unique<MemProbe>(go);
                                       });
    }
    ~MemProbeScriptGuard() { ScriptRegistry::unregisterScript("MemProbe"); }
};

// ===== 场景文件/资产写入 =====
// v4 场景 JSON：marker 对象 + MeshRenderer（materialAsset 引用，材质计数 churn）+
// MemProbe（音频计数 churn）；无贴图（GL 缺口避开）
void writeMemScene(const std::filesystem::path& file, const std::string& name,
                   const std::vector<std::string>& markers,
                   const std::string& materialAssetPath, const std::string& audioPath) {
    std::filesystem::create_directories(file.parent_path());
    nlohmann::json root;
    root["version"] = 4;
    root["name"] = name;
    root["objects"] = nlohmann::json::array();
    uint64_t id = 1;
    for (const auto& m : markers) {
        nlohmann::json obj;
        obj["id"] = id++;
        obj["name"] = m;
        obj["components"] = nlohmann::json::array();
        root["objects"].push_back(std::move(obj));
    }
    {
        nlohmann::json obj;
        obj["id"] = id++;
        obj["name"] = "Render";
        nlohmann::json mr;
        mr["type"] = "MeshRenderer";
        mr["mesh"] = "assets/models/cube1.obj";   // 无 GL：Mesh 上传降级跳过（warn），字段语义照常
        nlohmann::json mat = nlohmann::json::object();
        if (!materialAssetPath.empty())
            mat["materialAsset"] = materialAssetPath;
        mr["material"] = std::move(mat);
        obj["components"] = nlohmann::json::array({std::move(mr)});
        root["objects"].push_back(std::move(obj));
    }
    {
        nlohmann::json obj;
        obj["id"] = id++;
        obj["name"] = "Probe";
        nlohmann::json comp;
        comp["type"] = "MemProbe";
        if (!audioPath.empty())
            comp["clip"] = audioPath;
        obj["components"] = nlohmann::json::array({std::move(comp)});
        root["objects"].push_back(std::move(obj));
    }
    std::ofstream out(file);
    out << root.dump(2);
}

void writeMaterialAsset(const std::filesystem::path& file) {
    std::filesystem::create_directories(file.parent_path());
    nlohmann::json root;
    root["version"] = 1;
    root["material"] = nlohmann::json{{"albedo", nlohmann::json::array({0.9, 0.2, 0.1})},
                                      {"roughness", 0.4},
                                      {"metallic", 0.1}};
    std::ofstream out(file);
    out << root.dump(2);
}

// 最小单声道 16bit PCM wav（miniaudio CPU 解码 → audioBytes 度量；无 GL）
void writeWav(const std::filesystem::path& file, int sampleRate = 8000,
              int sampleCount = 400) {
    std::filesystem::create_directories(file.parent_path());
    const uint32_t dataBytes = static_cast<uint32_t>(sampleCount) * 2u;
    std::vector<uint8_t> wav(44u + dataBytes, 0);
    auto put = [&](size_t off, const char* s, size_t n) { std::memcpy(&wav[off], s, n); };
    auto put32 = [&](size_t off, uint32_t v) { std::memcpy(&wav[off], &v, 4); };
    auto put16 = [&](size_t off, uint16_t v) { std::memcpy(&wav[off], &v, 2); };
    put(0, "RIFF", 4);
    put32(4, 36 + dataBytes);
    put(8, "WAVE", 4);
    put(12, "fmt ", 4);
    put32(16, 16);
    put16(20, 1);   // PCM
    put16(22, 1);   // 单声道
    put32(24, static_cast<uint32_t>(sampleRate));
    put32(28, static_cast<uint32_t>(sampleRate) * 2u);
    put16(32, 2);
    put16(34, 16);
    put(36, "data", 4);
    put32(40, dataBytes);
    for (int i = 0; i < sampleCount; ++i) {
        const int16_t s = static_cast<int16_t>(
            std::sin(2.0 * 3.14159265358979 * 440.0 * i / sampleRate) * 8000);
        std::memcpy(&wav[44u + i * 2u], &s, 2);
    }
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(wav.data()),
              static_cast<std::streamsize>(wav.size()));
}

// 测试项目资产根：novamat + wav + cube 模型拷贝（网格导入无 GL 降级，模型文件仍需真实存在）
void setupMemProjectAssets(const std::filesystem::path& root) {
    writeMaterialAsset(root / "assets" / "materials" / "smoke.novamat");
    writeWav(root / "assets" / "audio" / "tick.wav");
    std::filesystem::create_directories(root / "assets" / "models");
    const std::filesystem::path srcRoot(NOVA_TEST_SOURCE_DIR);
    std::filesystem::copy_file(srcRoot / "assets" / "models" / "cube.obj",
                               root / "assets" / "models" / "cube1.obj",
                               std::filesystem::copy_options::overwrite_existing);
}

// 轮询 flushPendingResults 直到条件满足或超时（与 TestMultiSceneV12 同口径）
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

// exe 导出钩子定义（NovaTestScripts 经导入表调用；符号经 ENABLE_EXPORTS 导出，§25.5）
extern "C" __declspec(dllexport) void nova_test_hook_on_init() {
    ++s_scriptInitHookCount;
}

TEST_CASE("V1.0-13 scene switch x100 + unload cycles return to baseline",
          "[memory-v13][scene-switch]") {
    MemProbeScriptGuard probeGuard;   // MemProbe 脚本注册（用例内，结束注销）
    SceneManager::shutdown();
    TempDir tmp("nova-v13-switch");
    AssetDatabase::setProjectRoot(tmp.path());
    setupMemProjectAssets(tmp.path());

    writeMemScene(tmp.path() / "assets" / "scenes" / "A.nova.json", "A", {"MarkerA"},
                  "assets/materials/smoke.novamat", "assets/audio/tick.wav");
    writeMemScene(tmp.path() / "assets" / "scenes" / "B.nova.json", "B", {"MarkerB"},
                  "assets/materials/smoke.novamat", "assets/audio/tick.wav");
    writeMemScene(tmp.path() / "assets" / "scenes" / "Add.nova.json", "Add", {"MarkerAdd"},
                  "assets/materials/smoke.novamat", "assets/audio/tick.wav");
    SceneManager::registerScene("A", "assets/scenes/A.nova.json");
    SceneManager::registerScene("B", "assets/scenes/B.nova.json");
    SceneManager::registerScene("Add", "assets/scenes/Add.nova.json");

    Scene active("Active");
    SceneManager::setActiveScene(&active);

    // 首次加载 A：吸收一次性分配（缓存节点/日志缓冲等），随后建立基线
    REQUIRE(SceneManager::loadScene("A"));
    SceneManager::flushPendingLoad();
    Events::flushDeferred();   // 排空基线加载的延迟事件（防其在测量段内才释放 → 负净差）
    AssetDatabase::instance().releaseUnused();
    const MemorySnapshot baseline = Profiler::memorySnapshot();
    REQUIRE(baseline.materialAssetCount == 1);         // 场景 A 的 .novamat 存活
    REQUIRE(baseline.audioBytes == 400u * 4u);         // MemProbe onStart 加载 wav（400 f32 样本）
    REQUIRE(baseline.objectCount == 3);                // MarkerA + Render + Probe

    const double tol = leakTolerancePct();
    // B 场景预热一轮（B→A 回到基线场景）：吸收 B 首载的一次性分配——防其落入
    // 测量段污染 CRT 零断言（Reviewer P2-1 抖动源；与 A 基线预热对称）
    REQUIRE(SceneManager::loadScene("B"));
    SceneManager::flushPendingLoad();
    REQUIRE(SceneManager::loadScene("A"));
    SceneManager::flushPendingLoad();
    Events::flushDeferred();
    AssetDatabase::instance().releaseUnused();

    // 三轮 × 100 次同步切换（三次平均防分配器抖动，§28.1）
    // 证据走 fprintf(stderr)——Catch2 的 UNSCOPED_INFO 按条分配常驻堆串（实测 764B
    // CRT 净差量即其自身），污染 CRT 差量测量，故测量段内禁用
    auto measureSyncSwitches = [&](int rounds) -> double {
        double sum = 0.0;
        for (int round = 0; round < rounds; ++round) {
            for (int i = 0; i < 100; ++i) {
                REQUIRE(SceneManager::loadScene("B"));
                SceneManager::flushPendingLoad();
                REQUIRE(SceneManager::loadScene("A"));
                SceneManager::flushPendingLoad();
                Events::flushDeferred();   // 排空延迟事件（防队列累积）
            }
            AssetDatabase::instance().releaseUnused();
            const MemorySnapshot cur = Profiler::memorySnapshot();
            const double pct = maxFieldDriftPct(baseline, cur);
            sum += pct;
            fprintf(stderr, "[v13] 第 %d 轮 100 次切换后（漂移 %.2f%%）\n", round + 1, pct);
        }
        return sum / static_cast<double>(rounds);
    };

    // 测量 = 三轮 × 100 次切换 + 一个 CRT 跨度；漂移或 CRT 净差任一超限 → 按 §28.1
    // 纪律整段失败重跑一次（CRT 零断言与快照漂移同纪律，Reviewer P2-1 收口）。
    // CRT 跨度为三轮合计净差——分配器抖动已在 300 次切换上摊销（等价三次平均）。
    auto runMeasurement = [&]() {
        struct Result {
            double avgPct = 0.0;
            CrtMemSpan crt;
        };
        Result r;
        r.crt.checkpointBegin();
        r.avgPct = measureSyncSwitches(3);
        r.crt.checkpointEnd();
        return r;
    };
    auto result = runMeasurement();
    if (result.avgPct > tol || result.crt.netBytes() != 0) {
        fprintf(stderr,
                "[v13] 首次测量（平均漂移 %.2f%%，CRT %ld 字节）超限，按纪律失败重跑一次\n",
                result.avgPct, result.crt.netBytes());
        result = runMeasurement();
    }
    fprintf(stderr, "[v13] 100 次同步切换 ×3 轮平均漂移: %.2f%%（容差 %.1f%%）\n",
            result.avgPct, tol);
    fprintf(stderr, "[v13] CRT 净差量: %ld 字节 / %ld 块\n", result.crt.netBytes(),
            result.crt.netCount());
    if (result.crt.netBytes() != 0) {   // 诊断：dump 残留块（仅数字，无 file/line）
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
        _CrtMemDumpAllObjectsSince(&result.crt.begin);
    }
    CHECK(result.avgPct <= tol);
    CHECK(result.crt.netBytes() == 0);   // CRT 堆净增零（自动基线证据）

    // ===== additive 加载/卸载循环（§27.1 卸载回落路径 + flushPendingUnloads 的 releaseUnused）=====
    AsyncLoaderGuard async;   // 工作线程（加载期 JSON 解析在后台）
    const MemorySnapshot addBaseline = Profiler::memorySnapshot();
    // additive 循环含 AsyncLoader 工作线程（任务队列/路径串末次残留），严格 0 不可达——
    // 断言有界（≤4KB，非按循环次数增长）；同步切换段的严格 0 是主证据。
    // 漂移或 CRT 超限 → 按 §28.1 纪律整段失败重跑一次（Reviewer P2-1 同纪律）
    auto runAdditive = [&]() {
        struct Result {
            double pct = 0.0;
            CrtMemSpan crt;
        };
        Result r;
        r.crt.checkpointBegin();
        for (int i = 0; i < 50; ++i) {
            REQUIRE(SceneManager::loadSceneAsync("Add", /*additive=*/true));
            REQUIRE(pollUntil([] { return SceneManager::loadedScenes().size() == 2; }));
            REQUIRE(SceneManager::unloadScene("Add"));
            SceneManager::flushPendingLoad();   // flushPendingUnloads → 卸载 + releaseUnused
            Events::flushDeferred();
        }
        r.crt.checkpointEnd();
        r.pct = maxFieldDriftPct(addBaseline, Profiler::memorySnapshot());
        return r;
    };
    auto addResult = runAdditive();
    if (addResult.pct > tol || addResult.crt.netBytes() > 4096) {
        fprintf(stderr,
                "[v13] additive 首次测量（漂移 %.2f%%，CRT %ld 字节）超限，按纪律失败重跑一次\n",
                addResult.pct, addResult.crt.netBytes());
        addResult = runAdditive();
    }
    fprintf(stderr, "[v13] additive 50 次加载/卸载后（漂移 %.2f%%）\n", addResult.pct);
    fprintf(stderr, "[v13] additive CRT 净差量: %ld 字节 / %ld 块\n", addResult.crt.netBytes(),
            addResult.crt.netCount());
    if (addResult.crt.netBytes() > 4096) {   // 诊断：超预算 dump 残留块
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
        _CrtMemDumpAllObjectsSince(&addResult.crt.begin);
    }
    CHECK(addResult.pct <= tol);
    CHECK(addResult.crt.netBytes() <= 4096);

    Events::flushDeferred();   // 排空残留延迟事件（防跨用例污染）
    SceneManager::shutdown();
}

TEST_CASE("V1.0-13 play exit x100 no growth", "[memory-v13][play-loop]") {
    MemProbeScriptGuard probeGuard;   // 脚本组件走注册脚本（普通 Behaviour 无工厂，
                                      // 快照还原反序列化会丢组件——见引擎 warn）
    SceneManager::shutdown();
    TempDir tmp("nova-v13-play");
    AssetDatabase::setProjectRoot(tmp.path());

    // 编辑态基线场景：物理体 + 静态碰撞 + 脚本组件（Jolt/组件全链 churn）
    Scene scene("PlayHarness");
    GameObject* body = scene.createGameObject("Body");
    body->addComponent<Rigidbody>();
    body->addComponent<BoxCollider>();
    scene.createGameObject("Ground")->addComponent<BoxCollider>();
    GameObject* probeGo = scene.createGameObject("Probe");
    probeGo->attachComponent(ScriptRegistry::create("MemProbe", probeGo),
                             Component::componentTypeId<Behaviour>());   // 脚本组件共用 Behaviour typeId（裁定 #4）
    const std::string editorSnap = SceneSerializer::serialize(scene);
    SceneManager::setActiveScene(&scene);

    // 一次 Play 进出 = 编辑器 Play 循环：start → update×3 → 退出快照还原（原地重建）
    auto onePlayCycle = [&]() {
        scene.start();                                  // 进入 Play：物理全量重建
        for (int f = 0; f < 3; ++f)
            scene.update(1.0f / 60.0f);
        SceneSerializer::deserialize(scene, editorSnap);   // 退出 Play：批量销毁 + 快照还原
        Events::flushDeferred();
    };

    onePlayCycle();   // 预热：吸收一次性分配（Jolt 池/物理线程等）
    const MemorySnapshot baseline = Profiler::memorySnapshot();
    REQUIRE(baseline.objectCount == 3);   // Body + Ground + Probe

    const double tol = leakTolerancePct();
    // 三轮 × 100 次 Play 循环（三次平均，§28.1；证据走 fprintf——见 ① 注）
    auto measurePlayCycles = [&](int rounds) -> double {
        double sum = 0.0;
        for (int round = 0; round < rounds; ++round) {
            for (int i = 0; i < 100; ++i)
                onePlayCycle();
            const MemorySnapshot cur = Profiler::memorySnapshot();
            const double pct = maxFieldDriftPct(baseline, cur);
            sum += pct;
            fprintf(stderr, "[v13] 第 %d 轮 Play 100 次后（漂移 %.2f%%）\n", round + 1, pct);
        }
        return sum / static_cast<double>(rounds);
    };

    // 漂移或 CRT 净差任一超限 → 按 §28.1 纪律整段失败重跑一次（Reviewer P2-1 同纪律；
    // CRT 跨度为三轮合计净差，抖动已在 300 次循环上摊销）
    auto runMeasurement = [&]() {
        struct Result {
            double avgPct = 0.0;
            CrtMemSpan crt;
        };
        Result r;
        r.crt.checkpointBegin();
        r.avgPct = measurePlayCycles(3);
        r.crt.checkpointEnd();
        return r;
    };
    auto result = runMeasurement();
    if (result.avgPct > tol || result.crt.netBytes() != 0) {
        fprintf(stderr,
                "[v13] 首次测量（平均漂移 %.2f%%，CRT %ld 字节）超限，按纪律失败重跑一次\n",
                result.avgPct, result.crt.netBytes());
        result = runMeasurement();
    }
    fprintf(stderr, "[v13] Play 100 次 ×3 轮平均漂移: %.2f%%（容差 %.1f%%）\n", result.avgPct,
            tol);
    fprintf(stderr, "[v13] CRT 净差量: %ld 字节 / %ld 块\n", result.crt.netBytes(),
            result.crt.netCount());
    if (result.crt.netBytes() != 0) {   // 诊断：dump 残留块（仅数字，无 file/line）
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
        _CrtMemDumpAllObjectsSince(&result.crt.begin);
    }
    CHECK(result.avgPct <= tol);
    CHECK(result.crt.netBytes() == 0);

    SceneManager::setActiveScene(nullptr);   // 退出 Play 收口（卸载钩子路径）
    SceneManager::shutdown();
}

TEST_CASE("V1.0-13 hot reload x10 registry stable + memory stable",
          "[memory-v13][hot-reload]") {
    TempDir tmp("nova-v13-reload");
    AssetDatabase::setProjectRoot(tmp.path());
    ScriptRuntime::setDllPath(NOVA_TEST_SCRIPT_DLL);
    const int hook0 = s_scriptInitHookCount;
    if (!ScriptRuntime::isLoaded())
        REQUIRE(ScriptRuntime::load());
    CHECK(s_scriptInitHookCount - hook0 == 1);   // 首次加载：模块初始化器跑一次（exe 钩子）

    // 场景带 HotReloadProbe 脚本组件（NovaTestScripts 注册；字段 hits/speed 供往返校验）
    Scene scene("Reload");
    {
        nlohmann::json root;
        root["version"] = 4;
        root["name"] = "Reload";
        root["objects"] = nlohmann::json::array();
        nlohmann::json obj;
        obj["id"] = 1;
        obj["name"] = "P";
        nlohmann::json comp;
        comp["type"] = "HotReloadProbe";
        comp["hits"] = 7;
        comp["speed"] = 2.5f;
        obj["components"] = nlohmann::json::array({std::move(comp)});
        root["objects"].push_back(std::move(obj));
        SceneSerializer::deserialize(scene, root.dump());
    }
    SceneManager::setActiveScene(&scene);
    const std::vector<std::string> namesBefore = sortedNames();
    REQUIRE(namesBefore.size() == 1);
    CHECK(namesBefore[0] == "HotReloadProbe");

    // 预热一次 reload（吸收 .old 备份/加载器一次性分配），随后建立基线
    REQUIRE(ScriptRuntime::reload(scene));
    const MemorySnapshot baseline = Profiler::memorySnapshot();
    REQUIRE(baseline.objectCount == 1);
    REQUIRE(baseline.componentCount == 2);   // Transform + HotReloadProbe

    const double tol = leakTolerancePct();
    // reload×10 + CRT 跨度；CRT 超限同样按 §28.1 纪律整段失败重跑一次（Reviewer P2-1）。
    // totalReloads 累计实际重载次数（含重跑）——钩子计数断言按实际次数对齐
    int totalReloads = 1;   // 已含预热
    auto runReloads = [&]() {
        CrtMemSpan span;
        span.checkpointBegin();
        for (int i = 0; i < 10; ++i) {
            REQUIRE(ScriptRuntime::reload(scene));
            CHECK(sortedNames() == namesBefore);   // 注册表名称集不变（逐轮）
        }
        totalReloads += 10;
        span.checkpointEnd();
        return span;
    };
    CrtMemSpan crt = runReloads();
    if (crt.netBytes() != 0) {
        fprintf(stderr, "[v13] 首次 reload×10 CRT %ld 字节非零，按纪律失败重跑一次\n",
                crt.netBytes());
        crt = runReloads();
    }
    const MemorySnapshot after = Profiler::memorySnapshot();
    const double pct = maxFieldDriftPct(baseline, after);
    fprintf(stderr, "[v13] reload ×10 后（漂移 %.2f%%）\n", pct);
    fprintf(stderr, "[v13] CRT 净差量: %ld 字节 / %ld 块\n", crt.netBytes(), crt.netCount());
    fprintf(stderr, "[v13] exe 初始化钩子计数增量: %d（预期 %d = 首次 1 + 预热 1 + 每段 10 次重载）\n",
            s_scriptInitHookCount - hook0, 1 + totalReloads);
    if (crt.netBytes() != 0) {
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
        _CrtMemDumpAllObjectsSince(&crt.begin);
    }
    CHECK(pct <= tol);
    CHECK(crt.netBytes() == 0);
    CHECK(s_scriptInitHookCount - hook0 == 1 + totalReloads);   // 每次 reload 均换新模块实例

    // 字段恢复验证：10 次重载后 hits/speed 原位保持（快照 JSON 内存态）
    const nlohmann::json snap = nlohmann::json::parse(ScriptRuntime::snapshotScriptFields(scene));
    REQUIRE(snap.is_array());
    REQUIRE(snap.size() == 1);
    CHECK(snap[0]["type"] == "HotReloadProbe");
    CHECK(snap[0]["fields"]["hits"] == 7);
    CHECK(snap[0]["fields"]["speed"] == Catch::Approx(2.5f));

    // unload 契约（ScriptRuntime.h）：FreeLibrary 前场景内不得残留脚本组件实例——
    // 先快照 + 清空注册表 + 重建（脚本组件被跳过、旧实例在 DLL 存活时安全虚析构），再 unload
    {
        const std::string snapJson = SceneSerializer::serialize(scene);
        ScriptRegistry::removeAllScripts();
        SceneSerializer::deserialize(scene, snapJson);
    }
    ScriptRuntime::unload();
    CHECK(ScriptRegistry::allNames().empty());   // unload 前清注册表（旧工厂悬垂防护）
    SceneManager::shutdown();
}

TEST_CASE("V1.0-13 loadedSceneCount reflects actual loaded scenes (Architect Q1)",
          "[memory-v13][loaded-count]") {
    // Architect 裁定 Q1 落地回归：loadedSceneCount 从「波 1 恒 1」占位收口为实际已加载
    // 场景数（activeScene 在前视图 = active + additive；无活动场景 = 0，§26.2）
    SceneManager::shutdown();
    TempDir tmp("nova-v13-lsc");
    AssetDatabase::setProjectRoot(tmp.path());
    {
        std::filesystem::create_directories(tmp.path() / "assets" / "scenes");
        nlohmann::json root;
        root["version"] = 4;
        root["name"] = "L";
        root["objects"] = nlohmann::json::array();
        nlohmann::json obj;
        obj["id"] = 1;
        obj["name"] = "LMarker";
        obj["components"] = nlohmann::json::array();
        root["objects"].push_back(std::move(obj));
        std::ofstream out(tmp.path() / "assets" / "scenes" / "L.nova.json");
        out << root.dump(2);
    }
    SceneManager::registerScene("L", "assets/scenes/L.nova.json");

    Scene lsc("Main");
    lsc.createGameObject("MainMarker");
    SceneManager::setActiveScene(&lsc);
    CHECK(Profiler::memorySnapshot().loadedSceneCount == 1);   // 仅主场景

    AsyncLoaderGuard async;
    REQUIRE(SceneManager::loadSceneAsync("L", /*additive=*/true));
    REQUIRE(pollUntil([] { return SceneManager::loadedScenes().size() == 2; }));
    CHECK(Profiler::memorySnapshot().loadedSceneCount == 2);   // active + additive 共存

    REQUIRE(SceneManager::unloadScene("L"));
    SceneManager::flushPendingLoad();   // 帧末卸载
    CHECK(Profiler::memorySnapshot().loadedSceneCount == 1);   // 卸载回落

    SceneManager::setActiveScene(nullptr);   // 退出 Play：无活动场景
    CHECK(Profiler::memorySnapshot().loadedSceneCount == 0);
    Events::flushDeferred();
    SceneManager::shutdown();
}

TEST_CASE("V1.0-13 reload frees old DLL (FreeLibrary semantics)",
          "[memory-v13][dll-unload]") {
    TempDir tmp("nova-v13-dll");
    AssetDatabase::setProjectRoot(tmp.path());
    ScriptRuntime::setDllPath(NOVA_TEST_SCRIPT_DLL);
    const int hook0 = s_scriptInitHookCount;
    if (!ScriptRuntime::isLoaded())
        REQUIRE(ScriptRuntime::load());
    CHECK(s_scriptInitHookCount - hook0 == 1);

    // 场景带脚本组件（走 reload 完整路径：快照 → 中间重建 → FreeLibrary → LoadLibrary → 重建）
    Scene scene("Dll");
    {
        nlohmann::json root;
        root["version"] = 4;
        root["name"] = "Dll";
        root["objects"] = nlohmann::json::array();
        nlohmann::json obj;
        obj["id"] = 1;
        obj["name"] = "P";
        obj["components"] = nlohmann::json::array({nlohmann::json{{"type", "HotReloadProbe"},
                                                                  {"hits", 3}}});
        root["objects"].push_back(std::move(obj));
        SceneSerializer::deserialize(scene, root.dump());
    }
    SceneManager::setActiveScene(&scene);

    const std::wstring dllPath = std::filesystem::path(NOVA_TEST_SCRIPT_DLL).wstring();
    HMODULE hBefore = nullptr;
    REQUIRE(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, dllPath.c_str(),
                               &hBefore) != 0);
    REQUIRE(hBefore != nullptr);

    REQUIRE(ScriptRuntime::reload(scene));
    // 权威判据（FreeLibrary 语义）：exe 钩子计数 +1 → 旧模块引用计数已归零、
    // LoadLibrary 创建全新实例（静态初始化器重跑）；旧模块泄漏 → 计数不变 → 红
    CHECK(s_scriptInitHookCount - hook0 == 2);

    // 交叉校验 1：新模块句柄存在（基址是否变化不作断言——ASLR 可能复用刚释放的
    // 同一基址槽，句柄相等不构成泄漏证据；权威判据是上方 exe 钩子计数）
    HMODULE hAfter = nullptr;
    REQUIRE(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, dllPath.c_str(),
                               &hAfter) != 0);
    REQUIRE(hAfter != nullptr);
    fprintf(stderr, "[v13] 模块基址: %p -> %p（%s）\n", hBefore, hAfter,
            hAfter != hBefore ? "新实例" : "基址复用，合法——权威判据为 exe 钩子计数");

    // 交叉校验 2：新模块实例初始化计数恒 1（每模块实例静态独立）
    using InitFn = int (*)();
    const auto initFn = reinterpret_cast<InitFn>(
        GetProcAddress(hAfter, "nova_test_script_init_count"));
    REQUIRE(initFn != nullptr);
    CHECK(initFn() == 1);

    // 脚本组件重建 + 字段恢复（旧实例在 FreeLibrary 前安全虚析构、新工厂原位恢复）
    const nlohmann::json snap = nlohmann::json::parse(ScriptRuntime::snapshotScriptFields(scene));
    REQUIRE(snap.is_array());
    REQUIRE(snap.size() == 1);
    CHECK(snap[0]["fields"]["hits"] == 3);

    // 最终 FreeLibrary 语义：unload 后模块完全卸载（引用计数归零 → 查询不到）。
    // 先按 unload 契约清场景脚本实例（快照 + 清注册表 + 重建 → 脚本跳过、安全虚析构）
    {
        const std::string snapJson = SceneSerializer::serialize(scene);
        ScriptRegistry::removeAllScripts();
        SceneSerializer::deserialize(scene, snapJson);
    }
    ScriptRuntime::unload();
    HMODULE hGone = nullptr;
    const BOOL found = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                          dllPath.c_str(), &hGone);
    CHECK(found == 0);   // 模块已不在进程（引用计数归零）
    CHECK(ScriptRegistry::allNames().empty());
    SceneManager::shutdown();
}
