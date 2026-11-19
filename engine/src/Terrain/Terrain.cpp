// Terrain.cpp —— 地形组件实现（V0.5-8，c6，§25.6）
//
// 数据流：16bit 灰度 PNG（stb 读 ushort）→ 内存高度图（行优先，z * res + x）
//   → CPU 法线（有限差分 + 边界钳制，与 terrain.vert 同方案）→ 渲染（TerrainRenderer
//   懒建 GPU 网格 + R16 高度纹理 + 阴影网格）→ 物理（PhysicsWorld 自由函数桥接
//   Jolt HeightFieldShape 静态体）。
// 笔刷：applyBrush 改内存高度并标脏 → 帧末 rebuildDirtyRegion（每帧最多一次，
//   Time::frameCount 节流）重算法线 + 版本号 +1（渲染器检测重传 GPU）+ 碰撞重建。
// 序列化：键名逐字按 §25.6 契约（高度数据不进 JSON——写回 16bit PNG）。
//
// 注意：STB_IMAGE_IMPLEMENTATION 全项目唯一实现点 = TextureLoader.cpp；
// 本文件只 include 头。STB_IMAGE_WRITE_IMPLEMENTATION 唯一实现点 = 本文件。

#include <NovaEngine/Terrain/Terrain.h>

#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Core/Time.h>
#include <NovaEngine/Physics/PhysicsWorld.h>   // register/unregisterTerrainHeightfield（追加接口）
#include <NovaEngine/Scene/ComponentRegistry.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Transform.h>

#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <limits>

namespace NovaEngine {

namespace {

constexpr uint32_t kDefaultRes = 512;

// stb 16bit 灰度 PNG（或任意格式取第一通道）→ ushort 数组。返回 false 时 Logger::warn。
bool loadHeightmapU16(const std::string& path, std::vector<uint16_t>& out, uint32_t& outRes) {
    std::vector<uint8_t> bytes;
    if (!AssetStream::readAllBytes(path, bytes)) {
        Logger::warn("[Terrain] 高度图读取失败: {}（保持平面地形）", path);
        return false;
    }
    int w = 0, h = 0, ch = 0;
    stbi_us* data = stbi_load_16_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                             &w, &h, &ch, 1);
    if (!data) {
        Logger::warn("[Terrain] 高度图解码失败: {}（{}；保持平面地形）", path,
                     stbi_failure_reason());
        return false;
    }
    if (w < 2 || w != h) {
        Logger::warn("[Terrain] 高度图须为正方形且边长 ≥ 2: {}（{}x{}；保持平面地形）",
                     path, w, h);
        stbi_image_free(data);
        return false;
    }
    out.resize(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = data[i];   // ushort per channel（8bit 源 stb 已放大到 0..65535）
    outRes = static_cast<uint32_t>(w);
    stbi_image_free(data);
    return true;
}

// 平滑衰减（brush 笔刷形状）：t = d/radius ∈ [0,1]，1 → 0 平滑落
float brushFalloff(float t) {
    const float c = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - c * c * (3.0f - 2.0f * c);
}

}  // namespace

Terrain::Terrain(GameObject* owner) : Component(owner) {
    resetHeightArrays(kDefaultRes);
}

Terrain::~Terrain() {
    // 注销 Jolt HeightField 静态体（Terrain* 键的注册表须在组件析构期清理，防悬垂）
    unregisterTerrainHeightfield(this);
}

void Terrain::onAwake() {
    // 挂载/反序列化字段就绪后注册高度场（Play 的 syncFromTransforms 会再全量重建一次）
    if (collisionEnabled)
        registerTerrainHeightfield(this);
}

uint32_t Terrain::resolution() const {
    return m_res;
}

void Terrain::resetHeightArrays(uint32_t res) {
    m_res = std::max(res, 2u);
    m_heights.assign(static_cast<size_t>(m_res) * m_res, 0);
    m_normals.assign(static_cast<size_t>(m_res) * m_res, glm::vec3(0.0f, 1.0f, 0.0f));
    m_heightVersion = 0;
    m_dirty = false;
}

bool Terrain::loadHeightmap() {
    if (heightmapPath.empty()) {
        resetHeightArrays(kDefaultRes);   // 空路径 = 平面 0 高度
        return true;
    }
    std::vector<uint16_t> data;
    uint32_t res = kDefaultRes;
    if (!loadHeightmapU16(heightmapPath, data, res)) {
        resetHeightArrays(kDefaultRes);
        return false;
    }
    m_res = res;
    m_heights = std::move(data);
    m_normals.assign(static_cast<size_t>(m_res) * m_res, glm::vec3(0.0f, 1.0f, 0.0f));
    computeNormalsInRegion(0, 0, static_cast<int>(m_res) - 1, static_cast<int>(m_res) - 1);
    ++m_heightVersion;
    m_dirty = false;
    Logger::info("[Terrain] 高度图已加载: {}（{}²）", heightmapPath, m_res);
    return true;
}

void Terrain::computeNormalsInRegion(int x0, int z0, int x1, int z1) {
    const int n = static_cast<int>(m_res);
    x0 = std::clamp(x0, 0, n - 1);
    z0 = std::clamp(z0, 0, n - 1);
    x1 = std::clamp(x1, 0, n - 1);
    z1 = std::clamp(z1, 0, n - 1);
    if (m_heights.empty() || m_normals.size() != m_heights.size())
        return;
    const float dx = worldSize.x / static_cast<float>(n - 1);
    const float dz = worldSize.y / static_cast<float>(n - 1);
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            // 边界钳制采样（与 terrain.vert 的 GL_NEAREST + CLAMP_TO_EDGE 单侧差一致）
            const int xl = std::max(x - 1, 0), xr = std::min(x + 1, n - 1);
            const int zd = std::max(z - 1, 0), zu = std::min(z + 1, n - 1);
            const float hL = static_cast<float>(m_heights[static_cast<size_t>(z) * n + xl]) *
                             heightScale / 65535.0f;
            const float hR = static_cast<float>(m_heights[static_cast<size_t>(z) * n + xr]) *
                             heightScale / 65535.0f;
            const float hD = static_cast<float>(m_heights[static_cast<size_t>(zd) * n + x]) *
                             heightScale / 65535.0f;
            const float hU = static_cast<float>(m_heights[static_cast<size_t>(zu) * n + x]) *
                             heightScale / 65535.0f;
            const glm::vec3 slopeN((hL - hR) * 0.5f / dx, 1.0f, (hD - hU) * 0.5f / dz);
            m_normals[static_cast<size_t>(z) * n + x] = glm::normalize(slopeN);
        }
    }
}

void Terrain::markDirtyRect(int x0, int z0, int x1, int z1) {
    const int n = static_cast<int>(m_res);
    x0 = std::clamp(x0 - 1, 0, n - 1);   // 法线重算需扩 1 格
    z0 = std::clamp(z0 - 1, 0, n - 1);
    x1 = std::clamp(x1 + 1, 0, n - 1);
    z1 = std::clamp(z1 + 1, 0, n - 1);
    if (!m_dirty) {
        m_dirtyMin = glm::ivec2(x0, z0);
        m_dirtyMax = glm::ivec2(x1, z1);
    } else {
        m_dirtyMin = glm::min(m_dirtyMin, glm::ivec2(x0, z0));
        m_dirtyMax = glm::max(m_dirtyMax, glm::ivec2(x1, z1));
    }
    m_dirty = true;
}

// 世界坐标 → 局部坐标（无 Transform 时恒等）
glm::vec3 toLocal(const Terrain& t, const glm::vec3& world) {
    const Transform* tr = t.transform();
    if (!tr)
        return world;
    return glm::vec3(glm::inverse(tr->worldMatrix()) * glm::vec4(world, 1.0f));
}

glm::vec3 toWorld(const Terrain& t, const glm::vec3& local) {
    const Transform* tr = t.transform();
    if (!tr)
        return local;
    return glm::vec3(tr->worldMatrix() * glm::vec4(local, 1.0f));
}

// 网格连续坐标（局部 XZ → 0..res-1 网格空间）→ 归一化高度（0..1，双线性）
float Terrain::sampleNormalized(float gx, float gz) const {
    const int n = static_cast<int>(m_res);
    const int x0 = std::min(std::max(static_cast<int>(gx), 0), n - 2);
    const int z0 = std::min(std::max(static_cast<int>(gz), 0), n - 2);
    const float fx = std::clamp(gx - x0, 0.0f, 1.0f), fz = std::clamp(gz - z0, 0.0f, 1.0f);
    const auto at = [&](int xi, int zi) {
        return static_cast<float>(m_heights[static_cast<size_t>(zi) * n + xi]) / 65535.0f;
    };
    const float h00 = at(x0, z0), h10 = at(x0 + 1, z0);
    const float h01 = at(x0, z0 + 1), h11 = at(x0 + 1, z0 + 1);
    return (h00 * (1.0f - fx) + h10 * fx) * (1.0f - fz) +
           (h01 * (1.0f - fx) + h11 * fx) * fz;
}

float Terrain::heightAt(float x, float z) const {
    // 世界 (x, z) → 局部网格 → 归一化高度 × heightScale → 局部表面点 → 世界 Y（米）
    const glm::vec3 local = toLocal(*this, glm::vec3(x, 0.0f, z));
    const int n = static_cast<int>(m_res);
    const float hx = worldSize.x * 0.5f, hz = worldSize.y * 0.5f;
    const float gx = std::clamp((local.x + hx) / worldSize.x, 0.0f, 1.0f) * (n - 1);
    const float gz = std::clamp((local.z + hz) / worldSize.y, 0.0f, 1.0f) * (n - 1);
    const glm::vec3 localSurface(local.x, sampleNormalized(gx, gz) * heightScale, local.z);
    return toWorld(*this, localSurface).y;
}

bool Terrain::raycast(const glm::vec3& origin, const glm::vec3& dir, glm::vec3& outHit) const {
    const glm::vec3 o = toLocal(*this, origin);
    const glm::vec3 d = toLocal(*this, origin + dir) - o;   // 方向按世界矩阵变换（未归一）
    const float dLen2 = glm::dot(d, d);
    if (dLen2 < 1e-12f)
        return false;

    const int n = static_cast<int>(m_res);
    const float hx = worldSize.x * 0.5f, hz = worldSize.y * 0.5f;
    // 网格 XZ 包围盒 slab 入口/出口
    float tNear = 0.0f, tFar = std::numeric_limits<float>::max();
    for (int i = 0; i < 2; ++i) {
        const float originC = i == 0 ? o.x : o.z;
        const float dirC = i == 0 ? d.x : d.z;
        const float half = i == 0 ? hx : hz;
        if (std::abs(dirC) < 1e-8f) {
            if (originC < -half || originC > half)
                return false;   // 平行且在盒外
            continue;
        }
        float t1 = (-half - originC) / dirC, t2 = (half - originC) / dirC;
        if (t1 > t2)
            std::swap(t1, t2);
        tNear = std::max(tNear, t1);
        tFar = std::min(tFar, t2);
        if (tNear > tFar)
            return false;
    }
    if (tFar <= tNear)
        return false;
    if (tNear < 0.0f)
        tNear = 0.0f;   // 射线起点在盒内（贴地刷笔常见）

    // 2D DDA 逐格遍历：每格两条三角形做 Möller-Trumbore，取首个命中
    const float sx = worldSize.x / static_cast<float>(n - 1);
    const float sz = worldSize.y / static_cast<float>(n - 1);
    auto gridOf = [&](const glm::vec3& p, int& cx, int& cz) {
        cx = std::clamp(static_cast<int>(std::floor((p.x + hx) / sx)), 0, n - 2);
        cz = std::clamp(static_cast<int>(std::floor((p.z + hz) / sz)), 0, n - 2);
    };
    auto localPos = [&](int gx, int gz) {
        return glm::vec3(-hx + gx * sx, 0.0f, -hz + gz * sz);
    };
    auto corner = [&](int gx, int gz, glm::vec3& p) {
        p = localPos(gx, gz);
        p.y = static_cast<float>(m_heights[static_cast<size_t>(gz) * n + gx]) / 65535.0f *
              heightScale;
    };

    glm::vec3 c00, c10, c01, c11;
    auto hitTriangle = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                           float tMin, float& tHit) -> bool {
        // Möller-Trumbore（o + t*d 参数化，d 未归一）
        const glm::vec3 e1 = b - a, e2 = c - a;
        const glm::vec3 pvec = glm::cross(d, e2);
        const float det = glm::dot(e1, pvec);
        if (std::abs(det) < 1e-9f)
            return false;
        const float inv = 1.0f / det;
        const glm::vec3 tvec = o - a;
        const float u = glm::dot(tvec, pvec) * inv;
        if (u < 0.0f || u > 1.0f)
            return false;
        const glm::vec3 qvec = glm::cross(tvec, e1);
        const float v = glm::dot(d, qvec) * inv;
        if (v < 0.0f || u + v > 1.0f)
            return false;
        const float t = glm::dot(e2, qvec) * inv;
        if (t < tMin || t >= tHit)
            return false;
        tHit = t;
        return true;
    };

    float t = tNear;
    float tHit = tFar;
    int cx = 0, cz = 0;
    gridOf(o + d * (tNear + 1e-4f), cx, cz);
    const int stepX = d.x >= 0.0f ? 1 : -1;
    const int stepZ = d.z >= 0.0f ? 1 : -1;
    float tMaxX = d.x != 0.0f
        ? ((d.x > 0.0f ? (cx + 1) * sx - hx : cx * sx - hx) - o.x) / d.x
        : std::numeric_limits<float>::max();
    float tMaxZ = d.z != 0.0f
        ? ((d.z > 0.0f ? (cz + 1) * sz - hz : cz * sz - hz) - o.z) / d.z
        : std::numeric_limits<float>::max();
    float tDeltaX = d.x != 0.0f ? sx / std::abs(d.x) : std::numeric_limits<float>::max();
    float tDeltaZ = d.z != 0.0f ? sz / std::abs(d.z) : std::numeric_limits<float>::max();

    while (cx >= 0 && cx < n - 1 && cz >= 0 && cz < n - 1 && t < tHit) {
        corner(cx, cz, c00);
        corner(cx + 1, cz, c10);
        corner(cx, cz + 1, c01);
        corner(cx + 1, cz + 1, c11);
        const float tCellEnd = std::min(std::min(tMaxX, tMaxZ), tHit);
        // 两三角形：对角 (c00,c10,c01) 与 (c11,c10,c01)
        if (tCellEnd > t) {
            hitTriangle(c00, c10, c01, t, tHit);
            hitTriangle(c11, c10, c01, t, tHit);
        }
        if (tHit <= tCellEnd)
            break;   // 本格内已命中
        // 步进到下一格
        if (tMaxX < tMaxZ) {
            cx += stepX;
            t = tMaxX;
            tMaxX += tDeltaX;
        } else {
            cz += stepZ;
            t = tMaxZ;
            tMaxZ += tDeltaZ;
        }
    }
    if (tHit >= tFar || tHit <= 0.0f)
        return false;
    outHit = toWorld(*this, o + d * tHit);
    return true;
}

void Terrain::applyBrush(const glm::vec3& worldCenter, float radius, float strength,
                         BrushMode mode) {
    if (radius <= 0.0f || m_heights.empty())
        return;
    const int n = static_cast<int>(m_res);
    const glm::vec3 localCenter = toLocal(*this, worldCenter);
    const float hx = worldSize.x * 0.5f, hz = worldSize.y * 0.5f;
    const float sx = worldSize.x / static_cast<float>(n - 1);
    const float sz = worldSize.y / static_cast<float>(n - 1);
    strength = std::clamp(strength, 0.0f, 1.0f);

    // 笔刷中心网格坐标（局部）
    const float gx = (localCenter.x + hx) / sx;
    const float gz = (localCenter.z + hz) / sz;
    const float cellsX = radius / sx + 1.0f, cellsZ = radius / sz + 1.0f;
    const int x0 = std::max(0, static_cast<int>(std::floor(gx - cellsX)));
    const int z0 = std::max(0, static_cast<int>(std::floor(gz - cellsZ)));
    const int x1 = std::min(n - 1, static_cast<int>(std::ceil(gx + cellsX)));
    const int z1 = std::min(n - 1, static_cast<int>(std::ceil(gz + cellsZ)));
    if (x0 > x1 || z0 > z1)
        return;

    // Flatten 目标 = 笔刷中心当前归一化高度（拖拽时随中心移动，形成滚动平台——v1 简化）
    const float flattenTarget = sampleNormalized(gx, gz);
    const float maxStep = 0.05f;   // 强度 1 时每次调用 ±5%（0..1 高度范围）

    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const glm::vec3 cellLocal = glm::vec3(-hx + x * sx, 0.0f, -hz + z * sz);
            const glm::vec3 cellWorld = toWorld(*this, cellLocal);
            // 世界 XZ 距离判定笔刷影响（半径 = 世界单位；旋转地形按 XZ 投影近似——v1）
            const float d = glm::length(glm::vec2(cellWorld.x - worldCenter.x,
                                                  cellWorld.z - worldCenter.z));
            if (d > radius)
                continue;
            const float w = brushFalloff(d / radius);
            const size_t idx = static_cast<size_t>(z) * n + x;
            const float h = static_cast<float>(m_heights[idx]) / 65535.0f;
            float target = h;
            switch (mode) {
            case BrushMode::Raise:
                target = h + maxStep * strength * w;
                break;
            case BrushMode::Lower:
                target = h - maxStep * strength * w;
                break;
            case BrushMode::Flatten:
                target = h + (flattenTarget - h) * w * strength;
                break;
            case BrushMode::Smooth: {
                // 3×3 邻域平均（边界钳制）——大半径加权平均代价 O(cells²)，v1 取邻域平均
                float sum = 0.0f;
                int cnt = 0;
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = std::clamp(x + dx, 0, n - 1);
                        const int nz = std::clamp(z + dz, 0, n - 1);
                        sum += static_cast<float>(
                            m_heights[static_cast<size_t>(nz) * n + nx]) / 65535.0f;
                        ++cnt;
                    }
                target = h + (sum / cnt - h) * w * strength;
                break;
            }
            }
            m_heights[idx] = static_cast<uint16_t>(
                std::clamp(std::lround(target * 65535.0f), 0L, 65535L));
        }
    }
    markDirtyRect(x0, z0, x1, z1);
}

void Terrain::rebuildDirtyRegion() {
    if (!m_dirty)
        return;
    if (m_lastRebuildFrame == Time::frameCount())
        return;   // 帧末节流：每帧最多一次（多次 applyBrush 合并到一次重建）
    m_lastRebuildFrame = Time::frameCount();

    // 顶点/法线区域重算（脏矩形已含 1 格扩展）
    computeNormalsInRegion(m_dirtyMin.x, m_dirtyMin.y, m_dirtyMax.x, m_dirtyMax.y);
    ++m_heightVersion;   // TerrainRenderer 检测版本变化 → GPU 高度图重传 + 阴影网格重建
    m_dirty = false;

    // 碰撞重建：HeightFieldShape 不可原地修改 → 销毁重建静态体（PhysicsWorld 自由函数）
    if (collisionEnabled)
        registerTerrainHeightfield(this);
    else
        unregisterTerrainHeightfield(this);
}

bool Terrain::saveHeightmap() const {
    if (heightmapPath.empty()) {
        Logger::error("[Terrain] saveHeightmap 失败：heightmapPath 为空（先设置路径再保存）");
        return false;
    }
    if (m_heights.empty()) {
        Logger::error("[Terrain] saveHeightmap 失败：无高度数据");
        return false;
    }
    const int w = static_cast<int>(m_res);
    // 16bit 灰度 PNG：stride = w * sizeof(uint16_t)，comp = 1 → stb 写 16bit 通道
    if (stbi_write_png(heightmapPath.c_str(), w, w, 1, m_heights.data(),
                       w * static_cast<int>(sizeof(uint16_t))) == 0) {
        Logger::error("[Terrain] saveHeightmap 写回失败: {}", heightmapPath);
        return false;
    }
    Logger::info("[Terrain] 高度图已保存: {}（{}²，16bit）", heightmapPath, m_res);
    return true;
}

void Terrain::onSerialize(nlohmann::json& out) const {
    out["heightmapPath"] = heightmapPath;
    out["splatmapPath"] = splatmapPath;
    out["worldSize"] = nlohmann::json::array({worldSize.x, worldSize.y});
    out["heightScale"] = heightScale;
    out["collisionEnabled"] = collisionEnabled;
    nlohmann::json arr = nlohmann::json::array();
    for (const TerrainLayer& layer : layers) {
        nlohmann::json j;
        j["albedo"] = layer.albedoPath;
        j["normal"] = layer.normalPath;
        j["tileSize"] = layer.tileSize;
        arr.push_back(std::move(j));
    }
    out["layers"] = std::move(arr);
}

void Terrain::onDeserialize(const nlohmann::json& in) {
    heightmapPath = in.value("heightmapPath", std::string());
    splatmapPath = in.value("splatmapPath", std::string());
    if (in.contains("worldSize") && in["worldSize"].is_array() && in["worldSize"].size() >= 2) {
        try {
            worldSize = glm::vec2(in["worldSize"][0].get<float>(), in["worldSize"][1].get<float>());
        } catch (const std::exception&) {
            worldSize = glm::vec2(100.0f);
        }
    }
    heightScale = in.value("heightScale", 10.0f);
    collisionEnabled = in.value("collisionEnabled", true);
    layers = {};   // 全部默认层（c6 补齐逐层解析后保持键名）
    if (in.contains("layers") && in["layers"].is_array()) {
        for (std::size_t i = 0; i < in["layers"].size() && i < layers.size(); ++i) {
            const auto& j = in["layers"][i];
            if (!j.is_object()) continue;
            layers[i].albedoPath = j.value("albedo", std::string());
            layers[i].normalPath = j.value("normal", std::string());
            layers[i].tileSize = j.value("tileSize", 10.0f);
        }
    }
    // 字段就绪后重读高度图（失败 warn 并保持平面，不崩溃）
    loadHeightmap();
}

}  // namespace NovaEngine

// 组件静态注册（§25.0 纪律 4；注册名 "Terrain"）
NE_REGISTER_COMPONENT(NovaEngine::Terrain)
