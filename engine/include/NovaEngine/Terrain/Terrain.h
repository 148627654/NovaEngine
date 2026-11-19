#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json_fwd.hpp>
#include <NovaEngine/Scene/Component.h>

namespace NovaEngine {

// 地形组件（V0.5-8 v1）：512² 高度图 + 4 层材质 splat 混合 + Jolt heightfield 碰撞。
// 高度图资产 = 16bit 灰度 PNG（assets/terrains/*.png；stb 读 ushort 通道）；
// 笔刷改内存高度 → 保存写回 PNG（16bit，stb_image_write——stb 仓库内含，不引入新库）。
class Terrain : public Component {
public:
    explicit Terrain(GameObject* owner);
    ~Terrain() override;   // 注销 Jolt HeightField 静态体（与 Collider/Rigidbody 同姿势）

    std::string heightmapPath;             // 16bit 灰度 PNG；空 = 平面 0 高度
    std::string splatmapPath;              // RGBA 权重图（4 层）；可空
    glm::vec2 worldSize{100.0f, 100.0f};   // 世界尺寸（米；XZ 平面）
    float heightScale = 10.0f;             // 0..1 高度值 → 米
    struct TerrainLayer {                  // 固定 4 层（v1）；splat R/G/B/A 对应 0..3
        std::string albedoPath;            // 空 = 白
        std::string normalPath;            // 空 = 无
        float tileSize = 10.0f;            // 贴图平铺（米/次）
    };
    std::array<TerrainLayer, 4> layers;
    bool collisionEnabled = true;          // Jolt HeightFieldShape 静态体

    // 笔刷 API（编辑器 Edit 态；TerrainPanel/SceneViewPanel 用）：
    bool raycast(const glm::vec3& origin, const glm::vec3& dir, glm::vec3& outHit) const;
    enum class BrushMode { Raise, Lower, Flatten, Smooth };
    void applyBrush(const glm::vec3& worldCenter, float radius, float strength, BrushMode mode);
    void rebuildDirtyRegion();             // 帧末调用：顶点/法线区域重算 + GPU 上传 + 碰撞重建（每帧最多一次）
    float heightAt(float x, float z) const;
    bool saveHeightmap() const;            // 写回 PNG（失败 Logger::error）
    uint32_t resolution() const;           // 512（自高度图）

    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
    void onAwake() override;   // 注册/重建 Jolt HeightField 静态体（与 Collider 同姿势）

private:
    // ===== 实现状态（c6；公开签名冻结于 §25.6，以下为私有实现细节）=====
    // 高度图内存（行优先：index = z * m_res + x；ushort 0..65535，0 = 平面）
    std::vector<uint16_t> m_heights;
    // 顶点法线（与 m_heights 同分辨率；有限差分 + 边界钳制，与 terrain.vert 同方案）
    std::vector<glm::vec3> m_normals;
    uint32_t m_res = 512;                  // 网格分辨率（自高度图；无图 = 512）
    bool m_dirty = false;                  // 笔刷脏区域待重建
    glm::ivec2 m_dirtyMin{0, 0}, m_dirtyMax{0, 0};   // 含边界（法线重算扩 1 格）
    uint64_t m_lastRebuildFrame = ~0ull;   // 帧末节流：每帧最多一次（Time::frameCount）
    uint32_t m_heightVersion = 0;          // 高度数据版本号（TerrainRenderer 检测重传 GPU）

    bool loadHeightmap();                  // 重读 heightmapPath（stb 16bit；失败 warn 保持平面）
    float sampleNormalized(float gx, float gz) const;   // 网格连续坐标 → 归一化高度（0..1）
    void computeNormalsInRegion(int x0, int z0, int x1, int z1);
    void markDirtyRect(int x0, int z0, int x1, int z1);
    void resetHeightArrays(uint32_t res);

    friend class TerrainRenderer;          // GPU 高度图/阴影网格读取（TerrainRenderer.cpp）
};

}  // namespace NovaEngine
