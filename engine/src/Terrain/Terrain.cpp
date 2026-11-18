// Terrain.cpp —— 骨架日桩（V0.5-8）：接口冻结于 §25.6；高度图/笔刷/碰撞实现归 c6（本文件为 c6 独占）。
// 序列化按契约键名最小往返（缺省兜底；c6 实现后保持键名不变）。
#include <NovaEngine/Terrain/Terrain.h>

#include <NovaEngine/Scene/ComponentRegistry.h>

#include <nlohmann/json.hpp>

#include <cstddef>

namespace NovaEngine {

Terrain::Terrain(GameObject* owner) : Component(owner) {}

bool Terrain::raycast(const glm::vec3& origin, const glm::vec3& dir, glm::vec3& outHit) const {
    (void)origin; (void)dir; (void)outHit;
    return false;   // 骨架日空实现：无高度数据，永不命中
}

void Terrain::applyBrush(const glm::vec3& worldCenter, float radius, float strength, BrushMode mode) {
    (void)worldCenter; (void)radius; (void)strength; (void)mode;
}

void Terrain::rebuildDirtyRegion() {}

float Terrain::heightAt(float x, float z) const {
    (void)x; (void)z;
    return 0.0f;   // 平面 0 高度
}

bool Terrain::saveHeightmap() const { return false; }

uint32_t Terrain::resolution() const { return 512; }   // 契约缺省（512²，自高度图）

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
}

}  // namespace NovaEngine

// 组件静态注册（§25.0 纪律 4；注册名 "Terrain"）
NE_REGISTER_COMPONENT(NovaEngine::Terrain)
