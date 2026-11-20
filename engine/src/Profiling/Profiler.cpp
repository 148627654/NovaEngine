// ===== V1.0-1 Profiler 完整版（§26.2，cR 波 1，2026-11-20）=====
// 替换骨架日桩实现；Profiler.h 签名冻结未动。
//
// 功能：
//  1. 600 帧环形缓冲 FrameRecord（帧号/帧时长/CPU 分段数组/GPU 分段/内存快照）；
//     beginFrame/endFrame 成对；pushSection 同名累加，名字表全局持久、帧末定型，
//     未上报段 = -1（§26.2）；frame(0)=最旧，越界 nullptr。
//  2. CPU 两档：模块档（update/physics/render/ui 由主循环 pushSection，合波日接线）
//     与脚本档（Scene::update 按脚本类名累计 onUpdate → pushSection("script:"+类名)）。
//     kMaxSections=64：0 号槽预留给合并桶 "script:其他"，脚本名溢出合并、非脚本名丢弃。
//  3. GPU timer：GL_TIME_ELAPSED 成对提交（GpuScope 构造 glBeginQuery / 析构 glEndQuery）；
//     24 个 query（4 帧在飞 × 6 段；结果存于 begin 命名的对象内，每段 1 个）环形复用，
//     帧末延迟 3 帧回读，未就绪跳过不阻塞（段保持 -1）；
//     无 GL 上下文 / GL 不支持计时查询 → 不提交，段恒 -1（面板显示 n/a）。
//  4. memorySnapshot：转发 AssetDatabase::memorySnapshot()（cG 波 1 已落地）+
//     活动场景统计（objectCount/componentCount 走 Scene::forEachObject，
//     loadedSceneCount 波 1 恒 1）。
//  5. lastOverheadMs：beginFrame/endFrame/pushSection/GpuScope 各入口 CPU 耗时
//     逐帧累计（验收 <1ms 证据）。
//
// 线程模型：全部主线程调用（§26.2 接线在编辑器/Player 主循环；无锁、确定性）。
#include <NovaEngine/Profiling/Profiler.h>
#include <NovaEngine/Profiling/ProfilerSections.h>
#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Render/Renderer.h>   // isInitialized（GL 就绪守卫，§26.0 冻结项 2）
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/SceneManager.h>

#include <glad/gl.h>   // 先 glad 后 GLFW（项目约定，本 TU 无需 GLFW）

#include <chrono>
#include <cstring>
#include <string>
#include <vector>

namespace NovaEngine {

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kMaxSections = Profiler::kMaxSections;
constexpr int kMaxGpuPasses = Profiler::kMaxGpuPasses;
constexpr uint32_t kCapacity = Profiler::kCapacity;

inline float msBetween(Clock::time_point t0, Clock::time_point t1) {
    return std::chrono::duration<float, std::milli>(t1 - t0).count();
}

// ==================== CPU 帧记录（环形缓冲） ====================
// 槽位 = frameNumber % kCapacity（frameNumber 从 1 起、全局递增）；frame(index) 按
// 最旧 = frameNumber - frameCount + 1 换算，wrap 前后语义连续。
FrameRecord s_records[kCapacity];
uint32_t s_frameCount = 0;      // 已填充帧数（≤ kCapacity）
uint64_t s_frameNumber = 0;     // 全局递增帧号（clear 清零）
bool s_frameOpen = false;
Clock::time_point s_beginTime{};
float s_sectionAcc[kMaxSections];          // 本帧 CPU 段累计（0 起算）
bool s_sectionReported[kMaxSections] = {}; // 本帧是否上报（帧末定型：未上报 → 记录置 -1）

// 名字表：全局持久、帧末定型（同名累加、跨帧 index 稳定；帧内未上报段以 -1 标记）
std::string s_nameTable[kMaxSections];
int s_nameCount = 0;

// 0 号槽预留给合并桶 "script:其他"（§26.2：64 段上限，脚本名超出合并）
struct NameTableInit {
    NameTableInit() {
        s_nameTable[0] = "script:其他";
        s_nameCount = 1;
    }
} s_nameTableInit;

int sectionIndex(const char* name) {
    for (int i = 0; i < s_nameCount; ++i)
        if (s_nameTable[i] == name) return i;
    if (s_nameCount >= kMaxSections) {
        // 表满：脚本段合并入 "script:其他"（0 号预留），其余丢弃（§26.2 口径）
        return (std::strncmp(name, "script:", 7) == 0) ? 0 : -1;
    }
    s_nameTable[s_nameCount] = name;
    return s_nameCount++;
}

// ==================== GPU timer（GL_TIME_ELAPSED） ====================
// 注：GL 计时查询结果存放在 glBeginQuery 命名的那个 query 对象内（glEndQuery 不接收
// 对象名）——每段只需 1 个 query 对象（begin/end 成对调用，非双对象；isolated 自测验证：
// 读未 begin 过的对象 = GL_INVALID_OPERATION）。
constexpr int kGpuFramesInFlight = 4;   // 延迟 3 帧回读 + 当前帧在飞
GLuint s_gpuQueries[kGpuFramesInFlight * kMaxGpuPasses] = {};
int s_gpuSectionCount[kGpuFramesInFlight] = {};          // 槽位已用段数（beginFrame 清零）
int s_gpuPassIndex[kGpuFramesInFlight][kMaxGpuPasses] = {};  // 槽位各段对应固定 pass 下标
bool s_gpuQueriesInitialized = false;
bool s_gpuQueryActive = false;   // GL 不允许嵌套计时查询；6 处包裹点顺序无嵌套（§26.2）

bool gpuTimerSupported() {
    // GL 上下文就绪（glad 已加载）且支持计时查询（GL 3.3 core 起；GLAD_GL_VERSION_3_3
    // 为 gladLoadGL 后填充的运行期标志）。不支持 → GpuScope 不提交，段恒 -1（面板 n/a）。
    return Renderer::isInitialized() && GLAD_GL_VERSION_3_3;
}

int gpuPassIndex(const char* name) {
    // §26.2 冻结 6 段（SceneRenderer/Renderer 骨架日包裹点）：
    // shadow/sky/opaque/transparent/post/ui
    static const char* kNames[kMaxGpuPasses] = {
        "shadow", "sky", "opaque", "transparent", "post", "ui"};
    for (int i = 0; i < kMaxGpuPasses; ++i)
        if (std::strcmp(name, kNames[i]) == 0) return i;
    return -1;   // 未知段名（不属 6 段）→ 不提交
}

void initGpuQueries() {
    // 首次 GpuScope 时惰性生成（此时 GL 上下文必为当前，§26.2 接线点均在渲染路径内）
    glGenQueries(kGpuFramesInFlight * kMaxGpuPasses, s_gpuQueries);
    s_gpuQueriesInitialized = true;
}

void readbackGpuQueries(uint64_t targetFrame) {
    // endFrame(f) 内回读帧 f-3 的查询结果：可用即取（ns→ms），未就绪跳过不阻塞
    //（段保持 -1）；被跳过的结果随 query 复用丢弃（§26.7-4 口径）。
    const int slot = static_cast<int>(targetFrame % kGpuFramesInFlight);
    FrameRecord& rec = s_records[targetFrame % kCapacity];
    for (int i = 0; i < s_gpuSectionCount[slot]; ++i) {
        const GLuint q = s_gpuQueries[slot * kMaxGpuPasses + i];
        GLuint avail = 0;
        glGetQueryObjectuiv(q, GL_QUERY_RESULT_AVAILABLE, &avail);
        if (!avail) continue;
        GLuint64 ns = 0;
        glGetQueryObjectui64v(q, GL_QUERY_RESULT, &ns);
        rec.gpuPassMs[s_gpuPassIndex[slot][i]] = static_cast<float>(ns) * 1e-6f;
    }
}

// ==================== CpuScope 起点栈 ====================
// 头文件无数据成员（冻结）→ 起点存文件级栈；支持嵌套（深度 64 截断，占位保平衡）。
struct CpuScopeEntry {
    std::string name;
    Clock::time_point start{};
    bool active = false;
};
std::vector<CpuScopeEntry> s_cpuScopeStack;

// GpuScope 提交栈（同 CpuScope 思路：冻结头无成员，提交状态存文件级栈）。
// GL 禁止嵌套计时查询——嵌套时内层占位跳过、析构平衡，外层查询不受干扰。
struct GpuScopeEntry {
    int slot = 0;    // 帧在飞槽位
    int idx = 0;     // 槽内段下标
    bool active = false;
};
std::vector<GpuScopeEntry> s_gpuScopeStack;

// ==================== 自身开销自测 ====================
float s_overheadAccMs = 0.0f;   // 本帧各入口耗时累计（beginFrame 重置、endFrame 结算）
float s_lastOverheadMs = 0.0f;

}  // namespace

// ==================== GpuScope ====================
// 语义（§26.2）：构造提交 glBeginQuery，析构提交 glEndQuery；关闭（无帧 / 无 GL /
// 不支持计时查询 / 表满 / 嵌套）时零提交。骨架日 6 处包裹点顺序无嵌套（§26.0）。
GpuScope::GpuScope(const char* name) {
    const auto t0 = Clock::now();
    const int pass = gpuPassIndex(name);
    const int slot = static_cast<int>(s_frameNumber % kGpuFramesInFlight);
    const bool canSubmit = s_frameOpen && gpuTimerSupported() && !s_gpuQueryActive &&
                           pass >= 0 && s_gpuSectionCount[slot] < kMaxGpuPasses;
    if (!canSubmit) {
        s_gpuScopeStack.push_back({0, 0, false});   // 占位保平衡（嵌套/关闭路径）
        s_overheadAccMs += msBetween(t0, Clock::now());
        return;
    }
    if (!s_gpuQueriesInitialized) initGpuQueries();
    const int idx = s_gpuSectionCount[slot]++;
    s_gpuPassIndex[slot][idx] = pass;
    glBeginQuery(GL_TIME_ELAPSED, s_gpuQueries[slot * kMaxGpuPasses + idx]);
    s_gpuQueryActive = true;
    s_gpuScopeStack.push_back({slot, idx, true});
    s_overheadAccMs += msBetween(t0, Clock::now());
}

GpuScope::~GpuScope() {
    const auto t0 = Clock::now();
    if (!s_gpuScopeStack.empty()) {   // 防御：clear() 后残余 scope 的析构
        const GpuScopeEntry e = s_gpuScopeStack.back();
        s_gpuScopeStack.pop_back();
        if (e.active) {
            glEndQuery(GL_TIME_ELAPSED);
            s_gpuQueryActive = false;
        }
    }
    s_overheadAccMs += msBetween(t0, Clock::now());
}

// ==================== CpuScope ====================
CpuScope::CpuScope(const char* name) {
    if (s_cpuScopeStack.size() < 64)
        s_cpuScopeStack.push_back({name ? name : "", Clock::now(), true});
    else
        s_cpuScopeStack.push_back({"", Clock::time_point{}, false});   // 深度截断：占位保平衡
}

CpuScope::~CpuScope() {
    if (!s_cpuScopeStack.empty()) {   // 防御：clear() 后残余 scope 的析构
        const CpuScopeEntry e = std::move(s_cpuScopeStack.back());
        s_cpuScopeStack.pop_back();
        if (e.active)
            Profiler::pushSection(e.name.c_str(), msBetween(e.start, Clock::now()));
    }
}

// ==================== Profiler ====================
void Profiler::beginFrame() {
    const auto t0 = Clock::now();
    if (s_frameOpen)
        endFrame();   // 防御：上帧缺 endFrame（异常路径）先补结算
    ++s_frameNumber;
    s_frameOpen = true;
    s_beginTime = t0;
    for (int i = 0; i < kMaxSections; ++i) {
        s_sectionAcc[i] = 0.0f;
        s_sectionReported[i] = false;
    }

    // 本帧记录槽预置（未上报段 = -1；GPU 段待延迟回读）
    FrameRecord& rec = s_records[s_frameNumber % kCapacity];
    rec.frameNumber = s_frameNumber;
    rec.frameMs = 0.0f;
    rec.memory = MemorySnapshot{};   // P2-11（Reviewer）：异常路径（endFrame 未执行）下不留旧帧内存快照
    for (int i = 0; i < kMaxSections; ++i) rec.sectionMs[i] = -1.0f;
    for (int i = 0; i < kMaxGpuPasses; ++i) rec.gpuPassMs[i] = -1.0f;

    // 本帧 GPU 槽清空（query 对按 槽位×段 环形复用）
    s_gpuSectionCount[s_frameNumber % kGpuFramesInFlight] = 0;

    s_overheadAccMs = msBetween(t0, Clock::now());
}

void Profiler::endFrame() {
    const auto t0 = Clock::now();
    if (!s_frameOpen) {
        s_overheadAccMs += msBetween(t0, Clock::now());
        return;   // 无 beginFrame（未接线/clear 后）→ 仅计开销
    }
    s_frameOpen = false;

    FrameRecord& rec = s_records[s_frameNumber % kCapacity];
    rec.frameMs = msBetween(s_beginTime, t0);
    // 帧末定型：已上报段写累计值，未上报段 = -1（§26.2）
    for (int i = 0; i < kMaxSections; ++i)
        rec.sectionMs[i] = s_sectionReported[i] ? s_sectionAcc[i] : -1.0f;
    rec.memory = memorySnapshot();   // 帧末内存快照

    // GPU 延迟 3 帧回读（前 3 帧无回读目标；无 GL 时查询从未提交，槽计数为 0）
    if (s_frameNumber >= kGpuFramesInFlight && Renderer::isInitialized())
        readbackGpuQueries(s_frameNumber - kGpuFramesInFlight + 1);

    if (s_frameCount < kCapacity) ++s_frameCount;

    s_overheadAccMs += msBetween(t0, Clock::now());
    s_lastOverheadMs = s_overheadAccMs;
}

void Profiler::pushSection(const char* name, float ms) {
    const auto t0 = Clock::now();
    if (name && name[0] != '\0' && ms >= 0.0f) {
        const int idx = sectionIndex(name);
        if (idx >= 0) {
            s_sectionAcc[idx] += ms;
            s_sectionReported[idx] = true;
            // 帧末迟到上报（endFrame 之后、下帧 beginFrame 之前）：归入刚闭合的帧
            if (!s_frameOpen && s_frameCount > 0) {
                float& v = s_records[s_frameNumber % kCapacity].sectionMs[idx];
                v = (v < 0.0f ? 0.0f : v) + ms;   // 该帧此前未上报 = -1，从 0 起算
            }
        }
    }
    s_overheadAccMs += msBetween(t0, Clock::now());
}

const FrameRecord* Profiler::frame(uint32_t index) {
    if (index >= s_frameCount || index >= kCapacity) return nullptr;
    // 最旧帧号 = s_frameNumber - s_frameCount + 1；槽位 = 帧号 % kCapacity（wrap 前后连续）
    const uint64_t oldest = s_frameNumber - s_frameCount + 1;
    return &s_records[(oldest + index) % kCapacity];
}

uint32_t Profiler::frameCount() { return s_frameCount; }
uint32_t Profiler::capacity() { return kCapacity; }

void Profiler::clear() {
    // P2-2（Reviewer）：面板 Clear 可能发生在 render/ui 段进行中（某 NE_GPU_SCOPE 已
    // begin 尚未析构）——直接清栈会让该查询永不出队，4 帧后槽位复用触发
    // GL_INVALID_OPERATION。先补一次 glEndQuery 收尾再清栈（查询只在 GL 就绪时
    // 才可能激活，守卫兜底；残余 GpuScope 的析构对空栈为 no-op）。
    if (s_gpuQueryActive && Renderer::isInitialized())
        glEndQuery(GL_TIME_ELAPSED);
    s_frameCount = 0;
    s_frameNumber = 0;
    s_frameOpen = false;
    s_lastOverheadMs = 0.0f;
    s_overheadAccMs = 0.0f;
    for (auto& rec : s_records) rec = FrameRecord{};
    // 名字表保留 "script:其他" 预留（其余清空；跨帧名字稳定原则不变）
    for (int i = 1; i < kMaxSections; ++i) s_nameTable[i].clear();
    s_nameCount = 1;
    for (int i = 0; i < kGpuFramesInFlight; ++i) s_gpuSectionCount[i] = 0;
    s_gpuQueryActive = false;
    s_cpuScopeStack.clear();
    s_gpuScopeStack.clear();
}

MemorySnapshot Profiler::memorySnapshot() {
    // 转发 AssetDatabase::memorySnapshot()（cG 波 1 已落地：贴图/立方体/材质资产计数）
    MemorySnapshot snap = AssetDatabase::memorySnapshot();
    // 活动场景统计（§26.2）：objectCount/componentCount 深度优先遍历累计；
    // P2-3（Reviewer）：componentCount() 直读 m_components 计数——避免 getComponents
    // 按值返回 vector 的每对象堆分配（万级对象场景的帧抖动源）。
    if (Scene* scene = SceneManager::activeScene()) {
        size_t objects = 0, components = 0;
        scene->forEachObject([&](GameObject* go) {
            ++objects;
            components += go->componentCount();
        });
        snap.objectCount = objects;
        snap.componentCount = components;
    }
    snap.loadedSceneCount = 1;   // 波 1 恒 1（V1.0-2 additive 落地后改为实际已加载场景数）
    return snap;
}

float Profiler::lastOverheadMs() { return s_lastOverheadMs; }

// ==================== ProfilerSections（面板段名读取） ====================
const char* ProfilerSections::sectionName(int index) {
    if (index < 0 || index >= s_nameCount) return nullptr;
    return s_nameTable[index].c_str();
}

int ProfilerSections::sectionCount() { return s_nameCount; }

const char* ProfilerSections::gpuPassName(int pass) {
    static const char* kNames[kMaxGpuPasses] = {
        "shadow", "sky", "opaque", "transparent", "post", "ui"};
    return (pass >= 0 && pass < kMaxGpuPasses) ? kNames[pass] : nullptr;
}

}  // namespace NovaEngine
