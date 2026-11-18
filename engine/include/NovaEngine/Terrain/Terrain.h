#pragma once
#include <array>
#include <cstdint>
#include <string>
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
};

}  // namespace NovaEngine
