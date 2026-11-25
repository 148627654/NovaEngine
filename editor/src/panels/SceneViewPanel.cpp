#include "SceneViewPanel.h"

#include "TerrainPanel.h"   // TerrainBrushSettings（地形笔刷设置单例，V0.5-8）

#include "../DragDrop.h"
#include "../EditorCamera.h"
#include "../EditorContext.h"
#include "../GizmoController.h"

#include <imgui.h>

#include <NovaEngine/Assets/AssetDatabase.h>  // importModel（三角形拾取子网格定位，V1.0-12）
#include <NovaEngine/Assets/AssetStream.h>    // 模型字节读取（pak/磁盘统一入口，V1.0-12）
#include <NovaEngine/Assets/BinaryCache.h>    // 局部空间 MeshData 二进制缓存（V1.0-12）
#include <NovaEngine/Core/Input.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Core/Stats.h>
#include <NovaEngine/Core/Time.h>
#include <NovaEngine/Core/UUID.h>
#include <NovaEngine/Physics/PhysicsWorld.h>   // debugDraw（Show Colliders 线段收集，V0.3-1）
#include <NovaEngine/Render/PostProcess.h>     // PostProcessSettings 完整类型 + hdrSupported（V0.3-14 波 3）
#include <NovaEngine/Render/Renderer.h>
#include <NovaEngine/Scene/Camera.h>
#include <NovaEngine/Scene/Canvas.h>
#include <NovaEngine/Scene/MeshRenderer.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/SceneManager.h>   // loadedScenes（V1.0-2 波 2 合波接线）
#include <NovaEngine/Scene/SceneRenderer.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Terrain/Terrain.h>       // 地形笔刷 raycast/applyBrush（V0.5-8，c6）
#include <NovaEngine/UI/UIRenderer.h>         // 运行时 UI 输入（V0.3-3；渲染已迁入 renderSceneToOutput 管线）

// glfwGetTime（Play toast 时间戳）用；顺序要求与 EditorApplication 一致：
// glad 先行（__gl_h_ 守卫令 GLFW 内嵌 <GL/gl.h> 变 no-op），GLFW 头在 glad 之后。
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace NovaEditor {

namespace {

// 世界 AABB：局部 AABB 的 8 角点经世界矩阵变换后的包围盒（拾取与聚焦用）
NovaEngine::AABB transformAabb(const NovaEngine::AABB& local, const glm::mat4& world) {
    const glm::vec3 mn = local.min, mx = local.max;
    const glm::vec3 corners[8] = {
        {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z},
        {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mn.x, mx.y, mx.z}, {mx.x, mx.y, mx.z},
    };
    NovaEngine::AABB out;
    for (const glm::vec3& c : corners) {
        const glm::vec3 p = glm::vec3(world * glm::vec4(c, 1.0f));
        out.min = glm::min(out.min, p);
        out.max = glm::max(out.max, p);
    }
    return out;
}

// 射线-AABB slab 测试（§6.1）
bool rayAabb(const glm::vec3& origin, const glm::vec3& dir, const NovaEngine::AABB& box, float& outT) {
    float tmin = 0.0f, tmax = std::numeric_limits<float>::max();
    for (int i = 0; i < 3; ++i) {
        const float o = origin[i], d = dir[i];
        if (std::abs(d) < 1e-8f) {
            if (o < box.min[i] || o > box.max[i]) return false;
            continue;
        }
        const float inv = 1.0f / d;
        float t1 = (box.min[i] - o) * inv, t2 = (box.max[i] - o) * inv;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }
    outT = tmin;
    return true;
}

// V1.0-12 多视口：ImGui 多视口模式下全部坐标为 OS 绝对坐标（io.MousePos 同源——
// GLFW 后端把窗口位置并入鼠标事件），而 GetWindowPos/GetCursorScreenPos 返回「所在
// 视口内」局部坐标——跨视口运算（射线拾取/gizmo/相机/外部拖放区域）必须加视口原点。
// 单视口模式 io.MousePos = 客户端坐标，视口原点不参与（主视口 Pos 是 OS 窗口位置，
// 加了反而错）。
ImVec2 windowGlobalPos(const ImVec2& local) {
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        const ImVec2 vp = ImGui::GetWindowViewport()->Pos;
        return ImVec2(local.x + vp.x, local.y + vp.y);
    }
    return local;
}

// ===== 三角形级精确拾取（V1.0-12，#11 闭环，§28.2）=====
// 数据链：MeshRenderer.meshPath →（builtin: 图元本地生成 / 资产经 AssetStream + BinaryCache
// 取局部空间 MeshData，免 Assimp 重解析）→ 每次拾取按 worldMatrix 变换到世界空间 →
// Möller–Trumbore 逐三角形求最近交点。护栏：单网格 >200k 三角形跳过精确测试
//（AABB 结果降级 + warn 一次）；数据缺失同样 AABB 降级。gizmo/选择行为不变
//（hit 语义变化仅「大平面不再遮挡后方物体」）。
constexpr size_t kMaxPickTriangles = 200000;

// 拾取数据缓存条目（P1-2 终审裁定，§28.2）：Mesh* 键 + weak_ptr 属主——
// 命中判定 = owner.lock() == mesh。Mesh 析构后 weak_ptr 过期（lock()=nullptr ≠ 新 mesh），
// 堆地址复用也不会错配几何；解析失败缓存 nullptr 防每击重试。
struct PickDataEntry {
    std::weak_ptr<const NovaEngine::Mesh> owner;
    std::shared_ptr<const NovaEngine::MeshData> data;
};

std::unordered_map<const NovaEngine::Mesh*, PickDataEntry>& pickLocalDataCache() {
    static std::unordered_map<const NovaEngine::Mesh*, PickDataEntry> s;
    return s;
}

// warn 一次集合（>200k 三角形 / 数据缺失）
std::unordered_set<const NovaEngine::Mesh*>& pickWarnedSet() {
    static std::unordered_set<const NovaEngine::Mesh*> s;
    return s;
}

// P1-2：清扫过期条目（属主已析构）——插入时全表扫一次（插入频率 = 点击级、每 Mesh 一次），
// 死条目不再持有 MeshData → CPU 几何驻留闭环；缓存内存 = O(会话内同时存活网格数）。
// 顺手清 warn 一次集合同键（非必须，裁定允许）。
void sweepPickCache() {
    auto& cache = pickLocalDataCache();
    auto& warned = pickWarnedSet();
    size_t removed = 0;
    for (auto it = cache.begin(); it != cache.end();) {
        if (it->second.owner.expired()) {
            warned.erase(it->first);
            it = cache.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    if (removed > 0)
        NovaEngine::Logger::debug("[PickCache] 清扫过期条目 {} 条（存活 {} 条）", removed,
                                  cache.size());
}

// builtin: 图元局部几何（与 Primitives 逐顶点一致：cube ±0.5 / plane 10x10 于 y=0 /
// sphere 32 段 16 环——Mesh 不保留 CPU 数据，拾取侧本地生成同形 MeshData）
std::shared_ptr<NovaEngine::MeshData> builtinPickData(const std::string& name) {
    using namespace NovaEngine;
    auto d = std::make_shared<MeshData>();
    if (name == "cube") {
        const auto addFace = [&d](const glm::vec3& n, const glm::vec3& t) {
            const glm::vec3 b = glm::cross(n, t);
            const glm::vec3 c = n * 0.5f;
            const glm::vec3 corners[4] = {c - t * 0.5f - b * 0.5f, c + t * 0.5f - b * 0.5f,
                                          c + t * 0.5f + b * 0.5f, c - t * 0.5f + b * 0.5f};
            const uint32_t base = static_cast<uint32_t>(d->vertices.size());
            for (const glm::vec3& p : corners)
                d->vertices.push_back(Vertex{p, n, glm::vec2(0.0f), t});
            const uint32_t idx[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
            d->indices.insert(d->indices.end(), idx, idx + 6);
        };
        addFace(glm::vec3( 1.0f, 0.0f, 0.0f), glm::vec3( 0.0f, 0.0f, -1.0f));
        addFace(glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3( 0.0f, 0.0f,  1.0f));
        addFace(glm::vec3( 0.0f, 1.0f, 0.0f), glm::vec3( 1.0f, 0.0f,  0.0f));
        addFace(glm::vec3( 0.0f,-1.0f, 0.0f), glm::vec3( 1.0f, 0.0f,  0.0f));
        addFace(glm::vec3( 0.0f, 0.0f, 1.0f), glm::vec3( 1.0f, 0.0f,  0.0f));
        addFace(glm::vec3( 0.0f, 0.0f,-1.0f), glm::vec3(-1.0f, 0.0f,  0.0f));
        return d;
    }
    if (name == "plane") {
        const float h = 5.0f;  // 与 Primitives::plane 默认 size=10 一致
        const glm::vec3 n(0.0f, 1.0f, 0.0f), t(1.0f, 0.0f, 0.0f);
        d->vertices = {Vertex{glm::vec3(-h, 0.0f, -h), n, glm::vec2(0.0f, 0.0f), t},
                       Vertex{glm::vec3( h, 0.0f, -h), n, glm::vec2(1.0f, 0.0f), t},
                       Vertex{glm::vec3( h, 0.0f,  h), n, glm::vec2(1.0f, 1.0f), t},
                       Vertex{glm::vec3(-h, 0.0f,  h), n, glm::vec2(0.0f, 1.0f), t}};
        d->indices = {0, 2, 1, 0, 3, 2};
        return d;
    }
    if (name == "sphere") {
        const int segments = 32, rings = 16;  // Primitives::sphere 默认
        const int cols = segments + 1;
        for (int ring = 0; ring <= rings; ++ring) {
            const float phi = glm::pi<float>() * static_cast<float>(ring) / rings;
            const float y = std::cos(phi), r = std::sin(phi);
            for (int seg = 0; seg <= segments; ++seg) {
                const float theta = glm::two_pi<float>() * seg / segments;
                const glm::vec3 pos(r * std::cos(theta), y, r * std::sin(theta));
                d->vertices.push_back(Vertex{pos, glm::normalize(pos), glm::vec2(0.0f),
                                             glm::vec3(-std::sin(theta), 0.0f, std::cos(theta))});
            }
        }
        for (int ring = 0; ring < rings; ++ring)
            for (int seg = 0; seg < segments; ++seg) {
                const uint32_t a = static_cast<uint32_t>(ring * cols + seg);
                const uint32_t b = static_cast<uint32_t>((ring + 1) * cols + seg);
                d->indices.insert(d->indices.end(), {a, a + 1, b, a + 1, b + 1, b});
            }
        return d;
    }
    return nullptr;
}

// 资产网格拾取数据：importModel（内存缓存命中——场景已持有同一批 shared_ptr）按指针
// 定位子网格序号，再从二进制缓存取局部空间 MeshData（免 Assimp 重解析）。
// 失败 → nullptr（调用方按 AABB 降级 + warn 一次）。
std::shared_ptr<const NovaEngine::MeshData> resolvePickData(std::shared_ptr<const NovaEngine::Mesh> mesh,
                                                            const std::string& meshPath) {
    using namespace NovaEngine;
    if (!mesh) return nullptr;   // 调用方已守卫非空，防御兜底
    auto& cache = pickLocalDataCache();
    if (auto it = cache.find(mesh.get()); it != cache.end()) {
        // P1-2：属主命中判定——weak_ptr 锁回同一 Mesh 才命中（含 nullptr 失败记录）；
        // 过期条目（Mesh 已析构/地址复用）擦除重算
        if (it->second.owner.lock() == mesh) return it->second.data;
        cache.erase(it);
        pickWarnedSet().erase(mesh.get());
    }

    std::shared_ptr<const MeshData> out;
    if (meshPath.starts_with("builtin:")) {
        out = builtinPickData(meshPath.substr(8));
    } else if (!meshPath.empty()) {
        const AssetImportResult imported = AssetDatabase::instance().importModel(meshPath);
        size_t idx = std::numeric_limits<size_t>::max();
        for (size_t i = 0; i < imported.meshes.size(); ++i)
            if (imported.meshes[i].get() == mesh.get()) { idx = i; break; }
        if (idx != std::numeric_limits<size_t>::max()) {
            std::vector<uint8_t> bytes;
            const std::filesystem::path abs = AssetDatabase::projectRoot() / meshPath;
            if (AssetStream::readAllBytes(abs, bytes)) {
                const std::string key =
                    BinaryCache::makeKey(bytes) + "#" + std::to_string(idx);
                auto data = std::make_shared<MeshData>();
                if (BinaryCache::loadMeshData(key, *data)) out = data;
            }
        }
    }
    if (!out && pickWarnedSet().insert(mesh.get()).second)
        Logger::warn("三角形拾取数据不可用，AABB 降级: {}（mesh={:p}）",
                     meshPath.empty() ? std::string("(空路径)") : meshPath,
                     static_cast<const void*>(mesh.get()));
    sweepPickCache();   // P1-2：插入前全表清扫过期条目
    cache[mesh.get()] = PickDataEntry{mesh, out};  // 失败同样缓存 nullptr，防每击重试
    return out;
}

// Möller–Trumbore（无背面剔除——平面背面/内表面同样可点选；退化三角形剔除）
bool rayTriangle(const glm::vec3& o, const glm::vec3& d, const glm::vec3& a,
                 const glm::vec3& b, const glm::vec3& c, float& outT) {
    const glm::vec3 e1 = b - a, e2 = c - a;
    const glm::vec3 p = glm::cross(d, e2);
    const float det = glm::dot(e1, p);
    if (std::abs(det) < 1e-9f) return false;
    const float inv = 1.0f / det;
    const glm::vec3 s = o - a;
    const float u = glm::dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    const glm::vec3 q = glm::cross(s, e1);
    const float v = glm::dot(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = glm::dot(e2, q) * inv;
    if (t <= 1e-6f) return false;
    outT = t;
    return true;
}

// 拾取：AABB 粗筛（保留，先剔除）→ 逐三角形精确测试 → 最近交点。
// 候选按 AABB t 升序处理；AABB t 是精确命中 t 的下界，候选 AABB t ≥ 当前最优
// 精确 t 时其后候选不可能更近 → 剪枝（大场景拾取关键优化）。
uint64_t pickObject(NovaEngine::Scene* scene, const glm::vec3& origin, const glm::vec3& dir) {
    using namespace NovaEngine;
    const auto pickT0 = std::chrono::steady_clock::now();

    // 1. AABB 粗筛
    struct Candidate {
        float t;  // AABB 命中 t
        MeshRenderer* mr;
        GameObject* go;
    };
    std::vector<Candidate> cands;
    scene->forEachObject([&](GameObject* go) {
        if (!go->activeInHierarchy()) return;
        auto* mr = go->getComponent<MeshRenderer>();
        if (!mr || !mr->mesh) return;
        const AABB world = transformAabb(mr->mesh->aabb(), go->transform()->worldMatrix());
        float t = 0.0f;
        if (rayAabb(origin, dir, world, t)) cands.push_back({t, mr, go});
    });
    std::sort(cands.begin(), cands.end(),
              [](const Candidate& a, const Candidate& b) { return a.t < b.t; });

    // 2. 逐候选精确测试（bestT = 精确 t 与降级 AABB t 的并集最优）
    float bestT = std::numeric_limits<float>::max();
    uint64_t bestId = UUID::kInvalid;
    size_t exactTested = 0, degraded = 0;
    std::vector<glm::vec3> worldPos;  // 世界空间顶点（拾取内逐候选复用；每候选变换一次）
    for (const Candidate& c : cands) {
        if (c.t >= bestT) break;  // 下界剪枝
        const auto data = resolvePickData(c.mr->mesh, c.mr->meshPath);
        const bool hasTris =
            data && !data->indices.empty() && data->indices.size() % 3 == 0 &&
            (!data->vertices.empty() || !data->skinnedVertices.empty());
        const size_t triCount = hasTris ? data->indices.size() / 3 : 0;
        if (!hasTris || triCount > kMaxPickTriangles) {
            // AABB 结果降级：>200k 三角形（防大模型卡顿，warn 一次）或数据缺失
            if (hasTris && pickWarnedSet().insert(c.mr->mesh.get()).second)
                Logger::warn("网格三角形数 {} 超过护栏 {}，拾取按 AABB 降级: {}", triCount,
                             kMaxPickTriangles, c.go->name());
            if (c.t < bestT) { bestT = c.t; bestId = c.go->id(); }
            ++degraded;
            continue;
        }
        // 世界空间变换缓存语义：每候选（GameObject）变换一次、其全部三角形复用
        //（矩阵每帧可变——gizmo 拖拽中帧帧不同，持久缓存无收益且会无限增长）
        const bool skinned = !data->skinnedVertices.empty();
        const size_t vCount = skinned ? data->skinnedVertices.size() : data->vertices.size();
        worldPos.resize(vCount);
        const glm::mat4 world = c.go->transform()->worldMatrix();
        for (size_t i = 0; i < vCount; ++i) {
            const glm::vec3& lp =
                skinned ? data->skinnedVertices[i].position : data->vertices[i].position;
            worldPos[i] = glm::vec3(world * glm::vec4(lp, 1.0f));
        }
        float t = 0.0f;
        for (size_t i = 0; i + 2 < data->indices.size(); i += 3) {
            if (rayTriangle(origin, dir, worldPos[data->indices[i]],
                            worldPos[data->indices[i + 1]], worldPos[data->indices[i + 2]], t) &&
                t < bestT) {
                bestT = t;
                bestId = c.go->id();
            }
        }
        ++exactTested;
    }

    const auto pickMs = std::chrono::duration<float, std::milli>(
                            std::chrono::steady_clock::now() - pickT0).count();
    // P2-6：debug 级（每次点击一条 info 会刷屏 Console）
    Logger::debug("拾取完成: 候选 {}（精确 {} / 降级 {}）/ 命中 {} / 耗时 {:.2f} ms",
                  cands.size(), exactTested, degraded,
                  bestId == UUID::kInvalid ? std::string("无") : std::to_string(bestId), pickMs);
    return bestId;
}

// 碰撞体线框颜色（V0.3-1 波 2 编辑器接线：绿色，Unity Physics Debug 视觉一致）
constexpr glm::vec3 kColliderLineColor(0.25f, 0.95f, 0.35f);

// 子树（含自身）全部 MeshRenderer 的世界包围盒并集
void accumulateAabb(NovaEngine::GameObject* go, NovaEngine::AABB& out, bool& any) {
    auto* mr = go->getComponent<NovaEngine::MeshRenderer>();
    if (mr && mr->mesh) {
        const NovaEngine::AABB world = transformAabb(mr->mesh->aabb(), go->transform()->worldMatrix());
        out.min = glm::min(out.min, world.min);
        out.max = glm::max(out.max, world.max);
        any = true;
    }
    for (const auto& child : go->children()) accumulateAabb(child.get(), out, any);
}

// Play 模式渲染相机（§7.3）：isMain 优先，无则第一个 Camera，再无返回 nullptr（回退编辑器相机）。
// Reviewer 追加：跳过非激活层级（!activeInHierarchy）的对象，与引擎侧 collectCamera 逻辑对齐——
// followSelected 的相机选择与 Play 渲染保持一致。
NovaEngine::Camera* pickPlayCamera(NovaEngine::Scene* scene) {
    auto cameras = scene->findComponents<NovaEngine::Camera>();
    if (cameras.empty()) return nullptr;
    for (auto* c : cameras)
        if (c->isMain && c->gameObject()->activeInHierarchy()) return c;
    for (auto* c : cameras)
        if (c->gameObject()->activeInHierarchy()) return c;
    return nullptr;   // 无激活相机：回退编辑器相机
}

}  // namespace

SceneViewPanel::SceneViewPanel() : Panel("Scene") {}

void SceneViewPanel::onGui(EditorContext& ctx, EditorCamera& camera) {
    using namespace NovaEngine;
    Scene* scene = ctx.scene;
    const bool playMode = ctx.state == EditorContext::EditorState::Play;
    const bool editMode = !playMode;

    ImGui::Begin(m_name.c_str(), &open);

    // 上报屏幕区域（外部拖放目标判定用；V1.0-12 多视口：全局坐标，与 io.MousePos 同源）
    const ImVec2 winPosGlobal = windowGlobalPos(ImGui::GetWindowPos());
    ctx.viewportScreenMin = winPosGlobal;
    ctx.viewportScreenMax = winPosGlobal + ImGui::GetWindowSize();

    // Play 态顶部工具栏（V0.2-13/V0.3-10 子集）：分辨率预设 / Render Scale / Follow Selected。
    // Edit 态工具栏（V0.5-8，c6）：「Terrain Brush」开关（场景含 Terrain 时可用）。
    if (playMode) drawPlayToolbar(ctx);
    else drawEditToolbar(ctx);

    const ImVec2 vpPos = ImGui::GetCursorScreenPos();
    // V1.0-12 多视口：全局坐标（面板被拖出主窗口时 io.MousePos 与视口局部坐标不同源）
    const ImVec2 vpPosG = windowGlobalPos(vpPos);
    const ImVec2 vpSize = ImGui::GetContentRegionAvail();
    if (vpSize.x <= 1.0f || vpSize.y <= 1.0f || !scene) {
        // 折叠/最小化时尺寸为 0：跳过整个 FBO 渲染路径，避免 GL 报错（§11.8）
        ImGui::End();
        return;
    }

    // 游戏分辨率（V0.2-13，仅 Play 生效）：free = 面板尺寸；否则固定 (width,height)。
    // 相机投影 aspect 用「目标分辨率」宽高比；FBO 实际尺寸 = 目标 × renderScale ×
    // ImGui 帧缓冲缩放比（FBO → 物理像素；SCALE_TO_MONITOR 下该值恒 1，无额外放大）
    const ImGuiIO& io = ImGui::GetIO();
    const float fbScale = std::max(io.DisplayFramebufferScale.x, 1.0f);
    // HiDPI（2026-11-16）：UI 逻辑尺寸倍率 = 显示器内容缩放（150% = 1.5），
    // fontSize/offset 等保持逻辑尺寸、按 1.5 倍物理像素渲染（文字不再发虚）
    UIRenderer::setDpiScale(std::max(ctx.displayContentScale, 1.0f));
    int targetW = static_cast<int>(vpSize.x), targetH = static_cast<int>(vpSize.y);
    if (playMode && !ctx.gameViewSettings.free) {
        targetW = std::max(1, ctx.gameViewSettings.width);
        targetH = std::max(1, ctx.gameViewSettings.height);
    }
    const float renderScale =
        playMode ? std::clamp(ctx.gameViewSettings.renderScale, 0.5f, 2.0f) : 1.0f;
    int w = std::max(1, static_cast<int>(std::round(targetW * renderScale * fbScale)));
    int h = std::max(1, static_cast<int>(std::round(targetH * renderScale * fbScale)));
    if (playMode) {
        // Reviewer P2-4：Custom 分辨率 × renderScale 2.0 可达 16384² MSAA（数 GB 显存必挂），
        // 每维 clamp [16, 4096]（Edit 态尺寸即面板尺寸，不受影响）
        w = std::clamp(w, 16, 4096);
        h = std::clamp(h, 16, 4096);
    }
    const float targetAspect = static_cast<float>(targetW) / static_cast<float>(targetH);

    // FBO 同步（V0.3-14 波 3 双 FBO）：hdrFbo 承载场景渲染（pp.enabled && hdrSupported
    // → RGBA16F，否则 RGBA8 直通）；outFbo 为 LDR 输出（RGBA8、无 MSAA，
    // renderSceneToOutput 内部绘制后处理/UI 的目标）。尺寸或 hdr 模式变化 → 全删重建
    const bool hdrMode = scene->postProcess.enabled && PostProcess::hdrSupported();
    // 波 2 合波（§25.3 + 骨架日落地裁定 4）：SSAO 开且 hdr 时挂法线/线性深度附件（MRT）；
    // 开关变化与 hdr 模式变化同权重（都需全删重建，附件集合不同）
    // V1.0-4（§27.3，cG 一行补）：DOF 同样消费线性深度附件（与 SSAO 同源 MRT）
    const bool normalDepth = hdrMode && (scene->postProcess.ssaoEnabled ||
                                         scene->postProcess.dofEnabled);
    bool fboReady = true;
    if (!m_fboCreated || hdrMode != m_fboHdrMode || normalDepth != m_fboNormalDepth) {
        m_hdrFbo.destroy();
        m_outFbo.destroy();
        if (!m_hdrFbo.create(w, h, 4, hdrMode, normalDepth) || !m_outFbo.create(w, h, 1, false)) {
            Logger::error("SceneView: Framebuffer 创建失败 ({}x{})", w, h);
            fboReady = false;   // m_fboCreated 保持 false，下帧重试
        } else {
            m_fboCreated = true;
            m_fboHdrMode = hdrMode;
            m_fboNormalDepth = normalDepth;
        }
    } else if (w != m_hdrFbo.width() || h != m_hdrFbo.height()) {
        m_hdrFbo.resize(w, h);
        m_outFbo.resize(w, h);
        if (m_hdrFbo.width() != w || m_hdrFbo.height() != h ||
            m_outFbo.width() != w || m_outFbo.height() != h) {
            // resize 无返回值：尺寸未生效视为重建失败，销毁并下帧重试（Reviewer P2-4）
            Logger::error("SceneView: Framebuffer 重建失败 ({}x{})", w, h);
            m_hdrFbo.destroy();
            m_outFbo.destroy();
            m_fboCreated = false;
            fboReady = false;
        }
    }
    if (!fboReady) {
        // FBO 不可用：跳过本帧 3D 渲染（不画图、不跑渲染管线），下帧自动重试
        ImGui::End();
        return;
    }

    // Follow Selected（V0.3-10 子集）：每帧渲染前把主相机 Transform 设到 选中世界位置+偏移
    if (playMode && ctx.followSelected) {
        applyFollowSelected(ctx);
    } else {
        m_followOffsetValid = false;   // 未跟随时重置：退出 Play / 关闭开关后偏移失效
    }

    // 离屏渲染（Stats "render" 段：FBO bind 前取 t0）
    const auto renderT0 = std::chrono::steady_clock::now();
    RenderCamera renderCam;   // 编辑器相机（Play 无场景相机时作 fallback；gizmo 用）
    renderCam.view = camera.viewMatrix();
    renderCam.projection = camera.projectionMatrix(targetAspect);
    renderCam.position = camera.position();

    // V0.3-14 波 3 重线：Edit/Play 统一走 renderSceneToOutput（相机选择/天空/阴影/grid/submit/
    // 描边/后处理/UI 全在引擎管线内，A 线实现）。原先两处直接 UIRenderer::render 调用
    //（Play 约 222 行 / Edit 约 308 行）已删除——UI pass 已由管线内部包含（§14.3 迁移目标达成）。
    // 描边（仅 Edit）：选中对象橙色描边（Play 下 outlines 传 nullptr）
    std::vector<SceneRenderer::OutlineItem> outlines;
    if (editMode) {
        const glm::vec3 kOutlineColor(1.0f, 0.55f, 0.1f);
        for (uint64_t id : ctx.selectedIds) {
            GameObject* go = scene->findGameObject(id);
            if (!go) continue;
            auto* mr = go->getComponent<MeshRenderer>();
            if (mr && mr->mesh)
                outlines.push_back(SceneRenderer::OutlineItem{mr->mesh,
                                                              go->transform()->worldMatrix(),
                                                              kOutlineColor});
        }
    }
    // 注入点（仅 Play）：Show Colliders 线框在场景 FBO 内 3D pass 之后、resolve 之前绘制
    //（afterSceneDraw 回调由管线在 hdrFbo 绑定状态下调用）
    std::function<void()> afterSceneDraw;
    if (playMode && m_showColliders && scene->physics()) {
        afterSceneDraw = [scene] {
            std::vector<glm::vec3> lines;
            scene->physics()->debugDraw([&lines](const glm::vec3& a, const glm::vec3& b) {
                lines.push_back(a);
                lines.push_back(b);
            });
            if (!lines.empty())
                Renderer::drawLines(lines, kColliderLineColor);
        };
    }
    m_hdrFbo.bind();
    // 参数形态按架构师最终冻结签名（afterSceneDraw 为 const std::function<void()>&，
    // 默认 nullptr；空 std::function = 无注入点回调）
    // V1.0-2 波 2 合波接线（§27.1）：Play 中经 SceneManager::loadedScenes() 合并渲染
    //（additive 场景一并渲染；Edit/无登记场景回退单场景，单场景 = 旧入口逐像素等价）
    const std::vector<Scene*>& loaded = SceneManager::loadedScenes();
    std::vector<const Scene*> scenes;
    if (playMode && !loaded.empty())
        scenes.assign(loaded.begin(), loaded.end());
    else
        scenes = {scene};
    const uint32_t outTex = SceneRenderer::renderScenesToOutput(
        scenes, renderCam, m_hdrFbo, m_outFbo, scene->postProcess,
        editMode ? &outlines : nullptr,
        afterSceneDraw,
        /*useSceneCamera=*/!editMode);   // Edit 用编辑器相机（场景相机不得劫持 Scene View）
    m_hdrFbo.unbind();
    const auto renderT1 = std::chrono::steady_clock::now();
    Stats::setSectionMs("render",
                        std::chrono::duration<float, std::milli>(renderT1 - renderT0).count());

    // 显示：outTex = renderSceneToOutput 返回的 LDR 输出纹理（后处理/UI 之后）。
    // Edit 直接铺满（FBO = 面板尺寸）；Play 在面板内容区内等比 fit-rect 居中（letterbox）。
    // FBO 纹理相对 ImGui 上下颠倒：UV 必须 (0,1)-(1,0)（§11.8）
    bool hovered = false;
    if (editMode) {
        ImGui::Image((ImTextureID)(intptr_t)outTex, vpSize,
                     ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        hovered = ImGui::IsItemHovered();
        // Edit 态滚轮（V0.5 验收补强）：悬停视口内时投递 UI 滚轮（仅 ScrollView 滚动；
        // 消费则抑制相机缩放）。Edit 态 FBO 与面板 1:1，鼠标换算无需 letterbox。
        m_uiConsumedWheel = false;
        if (hovered) {
            const ImGuiIO& mio = ImGui::GetIO();
            // V1.0-12 §28.2.1：窗口逻辑（多视口下 io.MousePos 为 OS 绝对坐标，vpPosG 为
            // 视口全局起点）→ 布局空间统一走 UIRenderer::layoutFromWindowPoint（内容缩放
            // 参与）；processWheelInput 期望物理 FBO 坐标 → 布局 ×contentScale 还原物理
            //（与本模块内部 ÷dpiScale 逐点自洽）。分母收敛：原用 ImGui fbScale（帧缓冲
            // 缩放比），现用 displayContentScale（GLFW 内容缩放）——SCALE_TO_MONITOR 下
            // 两者同源相等，且与每帧 setDpiScale 同值（helper 恒等）。150% HiDPI 命中
            // 修正（原实现直接传逻辑坐标偏 1/1.5）；100% 下 cs=1 行为不变。
            const float cs = std::max(ctx.displayContentScale, 1.0f);
            const glm::vec2 rel(mio.MousePos.x - vpPosG.x, mio.MousePos.y - vpPosG.y);
            const glm::vec2 mouseFbo = UIRenderer::layoutFromWindowPoint(rel, cs) * cs;
            m_uiConsumedWheel = UIRenderer::processWheelInput(*scene, mouseFbo);
        }
        // Edit 态 UI 预览提示（V0.5-13 验收体验）：场景含 Canvas 时提示点击/存档需进 Play
        //（滚轮滚动 Edit 态已可用；拾取选择本就仅 Edit 态可用）
        if (!scene->findComponents<Canvas>().empty()) {
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(vpPos.x + 8.0f, vpPos.y + vpSize.y - 30.0f),
                IM_COL32(255, 210, 90, 255),
                "Edit 态：滚轮可滚 ScrollView；点击/Save Game 请 Ctrl+P 进 Play");
        }
    } else {
        const float fitScale = std::min(1.0f, std::min(vpSize.x / targetW, vpSize.y / targetH));
        const ImVec2 dstSize(targetW * fitScale, targetH * fitScale);
        const ImVec2 offset((vpSize.x - dstSize.x) * 0.5f, (vpSize.y - dstSize.y) * 0.5f);
        ImGui::SetCursorPos(ImGui::GetCursorPos() + offset);  // 居中偏移
        ImGui::Image((ImTextureID)(intptr_t)outTex, dstSize,
                     ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        hovered = ImGui::IsItemHovered();
        // UI 输入（V0.3-3）：Play 态把鼠标从面板坐标经 letterbox fit-rect 换算到
        // FBO 渲染分辨率坐标（w/h 即 UIRenderer::render 的布局空间；HiDPI 下
        // 逻辑鼠标 × dpi 与物理 FBO 的换算在 w/dstSize 中自然抵消，公式不变）；
        // Edit 态不处理。仅悬停在游戏画面内时投递（工具栏/letterbox 黑边不算命中）。
        // V1.0-12 §28.2.1 分母口径说明：w 内嵌 fbScale（ImGui 帧缓冲缩放比），
        // SCALE_TO_MONITOR 下 == contentScale == setDpiScale（GLFW 内容缩放），三种
        // 口径同源；统一换算入口 UIRenderer::layoutFromWindowPoint 见 Edit 滚轮路径。
        if (hovered) {
            const ImGuiIO& mio = ImGui::GetIO();
            const glm::vec2 mouseFbo(
                (mio.MousePos.x - (vpPosG.x + offset.x)) * (static_cast<float>(w) / dstSize.x),
                (mio.MousePos.y - (vpPosG.y + offset.y)) * (static_cast<float>(h) / dstSize.y));
            UIRenderer::processInput(*scene, mouseFbo, Input::isMouseDown(MouseButton::Left),
                                     Input::isMousePressed(MouseButton::Left));
        }
    }
    const bool windowHovered = ImGui::IsWindowHovered();
    // V1.0-12 多视口：跨视口运算（射线/gizmo/相机）统一用全局坐标
    const glm::vec2 vpPosF(vpPosG.x, vpPosG.y), vpSizeF(vpSize.x, vpSize.y);

    // Play 中 UI 优先于场景拾取（§24.5 接线守护）：UIRenderer::lastPointerConsumed()
    // 为真（本帧 UI 交互组件消费了指针——onClick/拖拽/滚动）时，跳过其余任何指针处理。
    // 当前 Play 无场景拾取，此接线为 V0.5-10 2D 与未来拾取预留 + 验收守护；
    // Edit 分支不调 hitTest（UI 预览不挡拾取）。processInput 已在上方 Play 分支调用。
    const bool uiConsumedPointer = playMode && UIRenderer::lastPointerConsumed();

    // gizmo（ImGui::Image 之后绘制进前景 drawlist，覆盖在视口之上）
    if (editMode && m_gizmo) {
        m_gizmo->setCamera(renderCam.view, renderCam.projection);
        m_gizmo->update(ctx, vpPosF, vpSizeF);
    }

    // 编辑器相机输入（gizmo 拖拽期间抑制；鼠标离开视口后若仍在拖拽则继续；
    // Edit 态 UI 滚轮已消费时跳过相机缩放——列表与相机不争抢同一 tick）
    const bool gizmoActive = m_gizmo && m_gizmo->isUsing();
    if (editMode && !gizmoActive && (windowHovered || camera.isInteracting()))
        camera.update(Time::deltaTime(), vpPosF, vpSizeF, m_uiConsumedWheel);

    // ===== 地形笔刷（V0.5-8，c6）：Edit 态工具栏开关 + 左键拖拽在地形上刷 =====
    // 射线 raycast 命中 Terrain（多地形取最近）；设置来自 TerrainBrushSettings 单例
    // （TerrainPanel 同源；面板 dock 接线前用默认值）。相机旋转/平移用右键/中键，
    // 左键无冲突；gizmo 拖拽/悬停抑制（优先 gizmo）。
    if (editMode && m_terrainBrushEnabled && hovered && !gizmoActive &&
        !camera.isInteracting()) {
        const ImGuiIO& mio = ImGui::GetIO();
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const glm::vec2 mouse(mio.MousePos.x, mio.MousePos.y);
            const auto [origin, dir] = camera.screenPointToRay(mouse, vpPosF, vpSizeF);
            NovaEngine::Terrain* best = nullptr;
            glm::vec3 hit(0.0f);
            float bestDist = std::numeric_limits<float>::max();
            for (NovaEngine::Terrain* t : scene->findComponents<NovaEngine::Terrain>()) {
                glm::vec3 hitLocal(0.0f);
                if (t->raycast(origin, dir, hitLocal)) {
                    const float d = glm::length(hitLocal - origin);
                    if (d < bestDist) { bestDist = d; best = t; hit = hitLocal; }
                }
            }
            if (best) {
                TerrainBrushSettings& brush = TerrainBrushSettings::instance();
                best->applyBrush(hit, brush.radius, brush.strength, brush.mode);
                ctx.markDirtyOnly();        // 拖拽中间帧只置脏（防刷撤销栈）
                m_brushStrokeActive = true;
            }
        } else if (m_brushStrokeActive) {
            m_brushStrokeActive = false;
            ctx.markSceneDirty();           // 收笔：推一次撤销快照
        }
    }
    // 帧末：全部地形脏区域重建（法线 + 碰撞；Terrain 内部按帧节流每帧最多一次，
    // GPU 上传由 TerrainRenderer 在渲染钩子内检测版本变化完成）
    if (editMode && scene)
        for (NovaEngine::Terrain* t : scene->findComponents<NovaEngine::Terrain>())
            t->rebuildDirtyRegion();

    // 射线-AABB 拾取（位移阈值 3px、Ctrl 加选、gizmo 悬停抑制；仅 Edit 态；
    // 笔刷开启时跳过拾取——左键属于刷子）
    if (!m_terrainBrushEnabled)
        handlePicking(ctx, camera, vpPosF, vpSizeF, hovered);

    // F 聚焦（§6.2）
    if (editMode && hovered && Input::isKeyPressed(Key::F) && !ImGui::GetIO().WantCaptureKeyboard)
        focusSelected(ctx, camera);

    // 接受资产拖放（AssetBrowser → SceneView；落点 = 射线与 y=0 平面交点）
    // 注意：目标注册不依赖 hovered（与 HierarchyPanel 一致）——拖拽过程中 IsItemHovered
    // 可能因 gizmo 手柄/重叠窗口误判为 false，导致目标未注册、payload 被 ImGui 丢弃。
    // Play 中 UI 消费指针的帧跳过（uiConsumedPointer，见上方守护注释）。
    if (!uiConsumedPointer && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DragDrop::kAssetPayload)) {
            const std::filesystem::path srcPath(std::string(static_cast<const char*>(payload->Data)));
            Logger::info("视口收到拖放资产: {}", srcPath.string());
            if (ctx.state != EditorContext::EditorState::Edit) {
                Logger::warn("Play 模式下禁止导入模型");
            } else if (DragDrop::isModelFile(srcPath)) {
                const ImVec2 m = ImGui::GetIO().MousePos;
                const glm::vec3 spawn =
                    camera.intersectGround(glm::vec2(m.x, m.y), vpPosF, vpSizeF);
                GameObject* root = DragDrop::importModelToScene(*scene, srcPath, spawn);
                if (root) {
                    ctx.setSelected(root->id());
                    focusGameObject(camera, root);  // 自动聚焦新物体（§6.4）
                    ctx.markSceneDirty();
                }
            } else if (DragDrop::isPrefabFile(srcPath)) {
                // 预制体拖入（V0.3-9 波 4）：实例化 + PrefabInstance 溯源组件 + 选中聚焦
                const ImVec2 m = ImGui::GetIO().MousePos;
                const glm::vec3 spawn =
                    camera.intersectGround(glm::vec2(m.x, m.y), vpPosF, vpSizeF);
                GameObject* root = DragDrop::importPrefabToScene(*scene, srcPath, spawn);
                if (root) {
                    ctx.setSelected(root->id());
                    focusGameObject(camera, root);
                    ctx.markSceneDirty();
                }
            } else {
                Logger::warn("仅支持将模型/预制体文件拖入场景视口: {}", srcPath.filename().string());
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Play toast（V0.3-10 子集）：进入 Play 未保存提示，3 秒非阻塞。
    // 纯文本绘制用前景 drawlist 无妨（非 gizmo 交互，无悬停判定依赖）。
    if (playMode && ctx.playToastUntil > glfwGetTime()) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const char* text = "Scene has unsaved changes (Play snapshot will restore)";
        const ImVec2 ts = ImGui::CalcTextSize(text);
        const float pad = 10.0f;
        const ImVec2 center(vpPos.x + vpSize.x * 0.5f, vpPos.y + ts.y + pad);
        const ImVec2 mn(center.x - ts.x * 0.5f - pad, center.y - ts.y * 0.5f - pad * 0.5f);
        const ImVec2 mx(center.x + ts.x * 0.5f + pad, center.y + ts.y * 0.5f + pad * 0.5f);
        dl->AddRectFilled(mn, mx, IM_COL32(30, 30, 38, 235), 4.0f);
        dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f),
                    IM_COL32(240, 220, 140, 255), text);
    }

    ImGui::End();
}

void SceneViewPanel::drawPlayToolbar(EditorContext& ctx) {
    // V0.2-13：Free / 1920x1080 / 1280x720 / Custom 预设；Custom 时 W/H 可编辑；Render Scale 0.5–2.0
    auto& gv = ctx.gameViewSettings;
    int preset = 0;
    if (!gv.free) {
        if (gv.width == 1920 && gv.height == 1080) preset = 1;
        else if (gv.width == 1280 && gv.height == 720) preset = 2;
        else preset = 3;   // Custom
    }
    const char* presets[] = {"Free", "1920x1080", "1280x720", "Custom"};
    if (ImGui::Combo("Resolution", &preset, presets,
                     static_cast<int>(sizeof(presets) / sizeof(presets[0])))) {
        if (preset == 0) gv.free = true;
        else {
            gv.free = false;
            if (preset == 1) { gv.width = 1920; gv.height = 1080; }
            else if (preset == 2) { gv.width = 1280; gv.height = 720; }
            // Custom：保留当前 W/H
        }
    }
    const bool custom = !gv.free && preset == 3;
    if (custom) {
        // Custom：W/H 可编辑（DragInt 直接改运行期设置）
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::DragInt("W", &gv.width, 1.0f, 64, 8192);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::DragInt("H", &gv.height, 1.0f, 64, 8192);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::SliderFloat("Render Scale", &gv.renderScale, 0.5f, 2.0f, "%.1f"))
        gv.renderScale = std::round(gv.renderScale * 10.0f) / 10.0f;   // 步长 0.1
    // Follow Selected（V0.3-10 子集）：开关切换时重置偏移
    ImGui::SameLine();
    if (ImGui::Checkbox("Follow Selected", &ctx.followSelected)) m_followOffsetValid = false;
    // Show Colliders（V0.3-1 波 2 编辑器接线）：Play 中开关碰撞体线框（面板局部状态，
    // 不序列化不置脏；渲染路径见 onGui 的 Play 分支）
    ImGui::SameLine();
    ImGui::Checkbox("Show Colliders", &m_showColliders);
    ImGui::Separator();
}

void SceneViewPanel::drawEditToolbar(EditorContext& ctx) {
    // V0.5-8（c6）：「Terrain Brush」开关（Edit 态，场景含 Terrain 时可用——
    // 无 Terrain 灰置）。开启后左键拖拽在地形上刷（raycast 命中），设置见 TerrainPanel。
    using namespace NovaEngine;
    const bool hasTerrain = ctx.scene != nullptr &&
                            !ctx.scene->findComponents<Terrain>().empty();
    if (!hasTerrain && m_terrainBrushEnabled)
        m_terrainBrushEnabled = false;   // 地形被删除：自动关刷
    if (!hasTerrain)
        ImGui::BeginDisabled();
    const bool clicked = ImGui::Button(m_terrainBrushEnabled ? "Terrain Brush [ON]"
                                                             : "Terrain Brush");
    if (!hasTerrain)
        ImGui::EndDisabled();
    if (clicked)
        m_terrainBrushEnabled = !m_terrainBrushEnabled;
    ImGui::Separator();
}

void SceneViewPanel::applyFollowSelected(EditorContext& ctx) {
    // V0.3-10 子集：主相机跟随选中对象。启用时记录 相机世界位置 − 选中对象世界位置 偏移，
    // 每帧渲染前把相机 Transform 设到 选中世界位置+偏移（朝向不变；退出 Play 快照自动还原）。
    using namespace NovaEngine;
    Scene* scene = ctx.scene;
    if (!scene) return;
    Camera* cam = pickPlayCamera(scene);   // isMain 优先，无则第一个
    if (!cam) return;
    const auto selected = ctx.selectedObjects();
    if (selected.empty()) return;
    Transform* ct = cam->transform();
    Transform* st = selected.front()->transform();
    if (!ct || !st) return;

    if (!m_followOffsetValid) {
        m_followOffset = ct->worldPosition() - st->worldPosition();
        m_followOffsetValid = true;
    }
    const glm::vec3 targetWorld = st->worldPosition() + m_followOffset;
    // 有父级：反解局部（朝向不变，只改位置）
    GameObject* camGo = cam->gameObject();
    if (camGo->parent() && camGo->parent()->transform())
        ct->setLocalPosition(
            glm::vec3(glm::inverse(camGo->parent()->transform()->worldMatrix()) *
                      glm::vec4(targetWorld, 1.0f)));
    else
        ct->setLocalPosition(targetWorld);
}

void SceneViewPanel::handlePicking(EditorContext& ctx, EditorCamera& camera,
                                   const glm::vec2& vpPos, const glm::vec2& vpSize, bool hovered) {
    if (ctx.state != EditorContext::EditorState::Edit) return;
    const ImGuiIO& io = ImGui::GetIO();

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_pickPressPos = glm::vec2(io.MousePos.x, io.MousePos.y);
        m_pickArmed = true;
    }
    if (m_pickArmed && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_pickArmed = false;
        if (!hovered || (m_gizmo && m_gizmo->isOver())) return;  // gizmo 悬停抑制点击选择
        const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
        if (glm::length(mouse - m_pickPressPos) > 3.0f) return;  // 拖动超过 3px：视为相机操作

        const auto [origin, dir] = camera.screenPointToRay(mouse, vpPos, vpSize);
        const uint64_t hitId = pickObject(ctx.scene, origin, dir);
        if (hitId != NovaEngine::UUID::kInvalid) {
            if (io.KeyCtrl) ctx.toggleSelected(hitId);  // Ctrl 加选/减选
            else ctx.setSelected(hitId);
            // 拾取成功日志（配合 GizmoController 的激活日志排查「选中但 gizmo 不显示」）
            if (NovaEngine::GameObject* hit = ctx.scene->findGameObject(hitId))
                NovaEngine::Logger::info("拾取命中: {}（id={}）", hit->name(), hitId);
        } else if (!io.KeyCtrl) {
            ctx.clearSelection();  // 点击空白取消选择
        }
    }
}

void SceneViewPanel::focusSelected(EditorContext& ctx, EditorCamera& camera) {
    NovaEngine::GameObject* go = ctx.activeObject();
    if (!go && !ctx.selectedIds.empty())
        go = ctx.scene->findGameObject(*ctx.selectedIds.begin());
    if (!go) {
        camera.focus(glm::vec3(0.0f, 1.0f, 0.0f), 8.0f);  // 无选中：聚焦原点
        return;
    }
    focusGameObject(camera, go);
}

void SceneViewPanel::focusGameObject(EditorCamera& camera, NovaEngine::GameObject* go) {
    // 世界 AABB 中心 c 与半径 r → focus(c, max(r*3, 0.5))（§6.2）
    NovaEngine::AABB box;
    bool any = false;
    accumulateAabb(go, box, any);
    if (!any) {
        camera.focus(go->transform()->worldPosition(), 3.0f);
        return;
    }
    const glm::vec3 center = (box.min + box.max) * 0.5f;
    const float radius = glm::length(box.max - box.min) * 0.5f;
    camera.focus(center, std::max(radius * 3.0f, 0.5f));
}

}  // namespace NovaEditor
