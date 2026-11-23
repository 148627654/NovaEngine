// ===== V1.0-2 AsyncLoader（§27.1）：后台资产加载线程真实实现（cR 波 2）=====
//
// 架构裁定（ROADMAP §10-D）：唯一新线程 = 资产加载线程。铁律：
//   线程只做 CPU 侧（JSON 解析、MeshData/Assimp、贴图解码）；GPU 上传（Mesh::create/
//   Texture 创建/材质资产解析挂 GPU）一律回主线程 flush 执行；物理/渲染/脚本/UI 保持主线程。
//
// 管线：
//   入队（主线程，经 SceneManager 解析路径）→ 工作线程：读场景 JSON + 解析校验 +
//   资产 CPU 预热（BinaryCache 落盘：网格 MeshData/贴图 RGBA8 解码——主线程挂载时
//   命中缓存，仅剩 GPU 上传）→ 结果（JSON 文本 + 参数快照 + 代际）入结果队列 →
//   flushPendingResults（主线程帧末，SceneManager::flushPendingLoad 之前）：
//   代际校验 → SceneManager::mountAsyncScene（挂载 + start，deserialize 内完成 GPU 上传）。
//
// 取消语义：reset() → generation++，清队列；旧代际结果在 flush 时作废不挂载
//（在飞任务无法中止，其结果被代际校验丢弃——§27.1 取消语义）。
// 异常隔离：工作线程 try/catch(...)——坏 JSON/坏模型 → error 日志 + 该场景加载失败标记，
// 不传播不崩溃（挂载端 mountAsyncScene 同样防御）。
// 与热重载共存：本线程不触碰 ScriptRegistry/DLL（场景挂载在主线程，脚本 DLL 交互不变）。
// 已知限制（文档化）：
//   - 网格 BinaryCache 暖盘后，本会话首次挂载仍会因 AssetDatabase 材质旁表门控
//     （P1-2：m_materialCache 空 → 回退 Assimp 重解析）在主线程重跑一次 Assimp——
//     属既有同步路径同款行为；波 3 cR 收口 AssetDatabase 时加 CPU-only prefetch API 可闭环。
//   - 贴图仅预热 RGBA8 缓存；压缩缓存键需 GPU 能力查询（GL），工作线程无上下文不做。
//   - 音频/天空盒 cubemap/IBL/地形高度图不预热（v1 范围）。
//   - BinaryCache 写盘与主线程读盘的并发：键不同文件互不干扰；同键竞态最坏 = 缓存损坏
//     被既有 corrupt 自愈路径删除重导（无崩溃风险）。

#include <NovaEngine/Assets/AsyncLoader.h>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Assets/BinaryCache.h>
#include <NovaEngine/Assets/ModelLoader.h>
#include <NovaEngine/Assets/TextureMeta.h>
#include <NovaEngine/Assets/TextureLoader.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/SceneManager.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace NovaEngine {

namespace {

// ===== 任务与结果 =====
struct SceneLoadTask {
    std::string sceneName;
    std::filesystem::path scenePath;   // 相对项目根（AssetStream 直接可用）
    bool additive = false;
    uint32_t gen = 0;
    std::unordered_map<std::string, std::string> paramsSnapshot;   // 入队瞬间冻结（§27.1）
};

struct SceneLoadResult {
    std::string sceneName;
    bool additive = false;
    uint32_t gen = 0;
    std::string jsonText;             // 已解析校验过的场景 JSON（挂载直接消费）
    std::unordered_map<std::string, std::string> paramsSnapshot;
    bool failed = false;
    std::string error;
};

// ===== 线程与队列（唯一工作线程）=====
std::mutex s_mutex;
std::condition_variable s_cv;
std::thread s_worker;
bool s_initialized = false;   // init 后为 true（shutdown 复位）
bool s_stop = false;
std::vector<SceneLoadTask> s_tasks;
std::vector<SceneLoadResult> s_results;
uint32_t s_generation = 0;
size_t s_totalEnqueued = 0;   // 本代际入队总数（progress 分母）
size_t s_completedCount = 0;  // 本代际完成数（progress 分子）

// ===== 路径解析（与 AssetDatabase::resolveAssetAbs 同口径；pak 挂载时直接返回项目根路径）=====
std::filesystem::path resolveAbs(const std::filesystem::path& relativePath) {
    const std::filesystem::path projAbs = AssetDatabase::projectRoot() / relativePath;
    if (AssetStream::pakMounted())
        return projAbs;
    std::error_code ec;
    if (std::filesystem::exists(projAbs, ec))
        return projAbs;
    const std::filesystem::path cwdAbs = std::filesystem::current_path(ec) / relativePath;
    if (!ec && std::filesystem::exists(cwdAbs, ec))
        return cwdAbs;
    return projAbs;
}

// 稳健读：先原路径（pak 键优先 + CWD 相对，与同步路径同语义），失败再项目根绝对路径兜底
//（测试进程 CWD ≠ 项目根时保证可读；编辑器正常路径两次尝试结果一致）
bool readAllBytesRobust(const std::filesystem::path& relativePath, std::vector<uint8_t>& out) {
    if (AssetStream::readAllBytes(relativePath, out))
        return true;
    return AssetStream::readAllBytes(resolveAbs(relativePath), out);
}

bool readAllTextRobust(const std::filesystem::path& relativePath, std::string& out) {
    if (AssetStream::readAllText(relativePath, out))
        return true;
    return AssetStream::readAllText(resolveAbs(relativePath), out);
}

// ===== 资产 CPU 预热（BinaryCache 落盘；键约定与 AssetDatabase 消费端一致）=====

// 网格：binKey = makeKey(源字节)；子网格键 = binKey + "#" + i（与 loadModel 逐字节同款）
void warmMeshCache(const std::string& relPath) {
    if (relPath.empty())
        return;
    std::vector<uint8_t> srcBytes;
    if (!readAllBytesRobust(relPath, srcBytes)) {
        Logger::warn("[AsyncLoader] 预热网格失败（读取）: {}", relPath);
        return;
    }
    const std::string binKey = BinaryCache::makeKey(srcBytes);
    {
        MeshData probe;
        if (BinaryCache::loadMeshData(binKey + "#0", probe))
            return;   // 已预热（任一子网格命中即视为完整——写入是原子的逐文件操作）
    }
    const std::string ext = std::filesystem::path(relPath).extension().string();
    const ModelImportResult parsed =
        ModelLoader::loadFromMemory(srcBytes.data(), srcBytes.size(), ext);
    if (parsed.meshes.empty())
        return;   // ModelLoader 已记录错误日志
    for (size_t i = 0; i < parsed.meshes.size(); ++i)
        BinaryCache::saveMeshData(binKey + "#" + std::to_string(i), parsed.meshes[i]);
}

// 贴图：RGBA8 解码缓存（键 = makeKey(源字节, meta 文本)，与 getTexture 同款）
void warmTextureCache(const std::string& relPath) {
    if (relPath.empty())
        return;
    const std::filesystem::path abs = resolveAbs(relPath);
    std::vector<uint8_t> srcBytes;
    if (!readAllBytesRobust(relPath, srcBytes))
        return;
    bool metaExisted = false;
    const TextureImportSettings settings = TextureMeta::loadOrCreate(abs, metaExisted);
    const std::string binKey =
        BinaryCache::makeKey(srcBytes, TextureMeta::serialize(settings));
    int w = 0, h = 0;
    std::vector<uint8_t> pixels;
    if (BinaryCache::loadTextureRgba8(binKey, w, h, pixels))
        return;   // 已预热
    if (!TextureLoader::loadRgba8(abs, w, h, pixels))
        return;   // 解码失败：保持 null 语义（挂载时 getTexture 同样失败，warn 在彼处）
    BinaryCache::saveTextureRgba8(binKey, w, h, pixels);
}

// 场景 JSON 递归收集资产路径：已知键集合（§27.1 v1 范围）；
// materialAsset 指向 .novamat（material 段同键集）→ 读文件再走一遍
const std::unordered_set<std::string> kMeshKeys = {"mesh", "meshPath"};
const std::unordered_set<std::string> kTextureKeys = {"texturePath", "albedoMap", "normalMap",
                                                      "metallicMap", "roughnessMap",
                                                      "emissionMap"};

void collectAssetRefs(const nlohmann::json& node, std::vector<std::string>& outMeshes,
                      std::vector<std::string>& outTextures, int depth) {
    if (depth > 4)
        return;   // 防御：材质资产引用深度上限（资产自身不引用资产，两层足够）
    if (node.is_object()) {
        for (const auto& [key, value] : node.items()) {
            if (value.is_string()) {
                const std::string s = value.get<std::string>();
                if (s.empty())
                    continue;
                if (kMeshKeys.contains(key))
                    outMeshes.push_back(s);
                else if (kTextureKeys.contains(key))
                    outTextures.push_back(s);
                else if (key == "materialAsset") {
                    // .novamat = material 段 JSON（同键集）→ 读 + 递归收集其贴图
                    std::string matText;
                    if (readAllTextRobust(s, matText)) {
                        try {
                            const nlohmann::json matDoc = nlohmann::json::parse(matText);
                            collectAssetRefs(matDoc, outMeshes, outTextures, depth + 1);
                        } catch (const std::exception&) {
                            Logger::warn("[AsyncLoader] 材质资产 JSON 无效: {}", s);
                        }
                    }
                }
                // clipPath/skyCubemapPath/lutPath/heightmapPath：v1 不预热（见文件头限制）
            } else {
                collectAssetRefs(value, outMeshes, outTextures, depth);
            }
        }
    } else if (node.is_array()) {
        for (const auto& e : node)
            collectAssetRefs(e, outMeshes, outTextures, depth);
    }
}

// 工作线程主循环
void workerMain() {
    for (;;) {
        SceneLoadTask task;
        {
            std::unique_lock lock(s_mutex);
            s_cv.wait(lock, [] { return s_stop || !s_tasks.empty(); });
            if (s_stop && s_tasks.empty())
                return;
            if (s_tasks.empty())
                continue;
            task = std::move(s_tasks.front());
            s_tasks.erase(s_tasks.begin());
        }

        SceneLoadResult result;
        result.sceneName = task.sceneName;
        result.additive = task.additive;
        result.gen = task.gen;
        result.paramsSnapshot = std::move(task.paramsSnapshot);
        // ===== 线程异常隔离（§27.1）：任何异常 → 该场景加载失败，不传播不崩溃 =====
        try {
            if (!readAllTextRobust(task.scenePath, result.jsonText))
                throw std::runtime_error("读取场景文件失败");
            const nlohmann::json doc = nlohmann::json::parse(result.jsonText);   // 解析校验
            if (!doc.is_object())
                throw std::runtime_error("场景 JSON 根节点不是对象");
            // 资产 CPU 预热（Assimp/贴图解码在后台完成；GPU 上传留待主线程挂载）
            std::vector<std::string> meshes, textures;
            collectAssetRefs(doc, meshes, textures, 0);
            for (const std::string& m : meshes)
                warmMeshCache(m);
            for (const std::string& t : textures)
                warmTextureCache(t);
        } catch (const std::exception& e) {
            result.failed = true;
            result.error = e.what();
        } catch (...) {
            result.failed = true;
            result.error = "未知异常";
        }
        if (result.failed)
            Logger::error("[AsyncLoader] 场景 \"{}\" 后台加载失败: {}", result.sceneName,
                          result.error);

        {
            std::lock_guard lock(s_mutex);
            if (task.gen == s_generation)
                ++s_completedCount;
            s_results.push_back(std::move(result));
        }
    }
}

}  // namespace

// ==================== 公开接口 ====================

void AsyncLoader::init() {
    // 引擎启动（Renderer::init 之后）；幂等。线程启动即阻塞等待任务
    if (s_initialized)
        return;
    s_stop = false;
    s_worker = std::thread(workerMain);
    s_initialized = true;
    Logger::info("[AsyncLoader] 资产加载线程已启动");
}

void AsyncLoader::shutdown() {
    // 停止 + join（丢弃未完成任务；须先于 Renderer::shutdown——挂载的 GPU 上传在主线程）
    if (!s_initialized)
        return;
    {
        std::lock_guard lock(s_mutex);
        s_stop = true;
    }
    s_cv.notify_all();
    if (s_worker.joinable())
        s_worker.join();
    {
        std::lock_guard lock(s_mutex);
        s_tasks.clear();
        s_results.clear();
        s_totalEnqueued = 0;
        s_completedCount = 0;
    }
    s_initialized = false;
    Logger::info("[AsyncLoader] 资产加载线程已停止");
}

bool AsyncLoader::loadSceneAsync(const std::string& name, bool additive) {
    // 经 SceneManager 解析路径（主线程）；查无 → false。调用方（SceneManager）已 reset()
    if (!s_initialized)
        return false;
    const auto& buildList = SceneManager::buildList();
    const auto found = std::find_if(buildList.begin(), buildList.end(),
                                    [&](const SceneBuildInfo& b) { return b.name == name; });
    if (found == buildList.end()) {
        Logger::error("[AsyncLoader] 场景 \"{}\" 不在 Build Settings 列表中", name);
        return false;
    }
    SceneLoadTask task;
    task.sceneName = name;
    task.scenePath = found->path;
    task.additive = additive;
    task.paramsSnapshot = SceneManager::snapshotSceneParams();   // 入队瞬间冻结（§27.1）
    {
        std::lock_guard lock(s_mutex);
        task.gen = s_generation;
        ++s_totalEnqueued;
        s_tasks.push_back(std::move(task));
    }
    s_cv.notify_one();
    return true;
}

void AsyncLoader::flushPendingResults() {
    // 主线程帧末（SceneManager::flushPendingLoad 之前）：依次执行结果体（挂载 + start）
    std::vector<SceneLoadResult> results;
    {
        std::lock_guard lock(s_mutex);
        results.swap(s_results);
    }
    for (const SceneLoadResult& r : results) {
        if (r.gen != s_generation) {
            Logger::info("[AsyncLoader] 丢弃过期代际结果: {}（gen {} != {}，取消语义）",
                         r.sceneName, r.gen, s_generation);
            continue;   // 旧结果作废不挂载（§27.1 代际取消）
        }
        if (r.failed) {
            Logger::error("[AsyncLoader] 场景 \"{}\" 加载失败，不挂载（原因见上方）", r.sceneName);
            continue;
        }
        SceneManager::mountAsyncScene(r.sceneName, r.additive, r.jsonText, r.paramsSnapshot);
    }
}

float AsyncLoader::progress() {
    // 0..1（完成/总数；无任务 = 1）
    std::lock_guard lock(s_mutex);
    if (s_totalEnqueued == 0)
        return 1.0f;
    const float done = std::min(s_completedCount, s_totalEnqueued);
    return done / static_cast<float>(s_totalEnqueued);
}

uint32_t AsyncLoader::generation() {
    std::lock_guard lock(s_mutex);
    return s_generation;
}

void AsyncLoader::reset() {
    // 清队列 + generation++（退出 Play/LoadScene 时调用；旧代际结果作废）
    uint32_t gen = 0;
    {
        std::lock_guard lock(s_mutex);
        ++s_generation;
        s_tasks.clear();
        s_results.clear();
        s_totalEnqueued = 0;
        s_completedCount = 0;
        gen = s_generation;   // P2-4 修复：日志值取锁内副本（锁外读共享变量 = 数据竞争）
    }
    Logger::info("[AsyncLoader] 已重置（代际 {}）——在飞任务结果将作废", gen);
}

}  // namespace NovaEngine
