// SpriteRenderer.cpp —— 骨架日桩（V0.5-10）：接口冻结于 §25.8；增强实现归 c8（本文件为 c8 独占）。
// 序列化按契约键名往返 + 贴图经 AssetDatabase 解析（渲染收集分支已在 SceneRenderer 布好）。
#include <NovaEngine/Scene/SpriteRenderer.h>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/ComponentRegistry.h>

#include <nlohmann/json.hpp>

namespace NovaEngine {

namespace {

glm::vec4 jsonToVec4Color(const nlohmann::json& j, const glm::vec4& fallback) {
    if (!j.is_array() || j.size() < 4)
        return fallback;
    try {
        return glm::vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
    } catch (const std::exception&) {
        return fallback;
    }
}

}  // namespace

SpriteRenderer::SpriteRenderer(GameObject* owner) : Component(owner) {}

void SpriteRenderer::onSerialize(nlohmann::json& out) const {
    out["texturePath"] = texturePath;
    out["color"] = nlohmann::json::array({color.x, color.y, color.z, color.w});
    out["flipX"] = flipX;
    out["flipY"] = flipY;
    out["sortingLayer"] = sortingLayer;
    out["orderInLayer"] = orderInLayer;
    out["pixelsPerUnit"] = pixelsPerUnit;
}

void SpriteRenderer::onDeserialize(const nlohmann::json& in) {
    texturePath = in.value("texturePath", std::string());
    if (!texturePath.empty()) {
        texture = AssetDatabase::instance().getTexture(std::filesystem::path(texturePath), /*srgb=*/true);
        if (!texture)
            Logger::warn("SpriteRenderer 加载贴图失败：{}（回退白块，不崩溃）", texturePath);
    } else {
        texture = nullptr;   // 空 = 白块（SceneRenderer 精灵路径兜底白纹理）
    }
    color = jsonToVec4Color(in.value("color", nlohmann::json()), glm::vec4(1.0f));
    flipX = in.value("flipX", false);
    flipY = in.value("flipY", false);
    sortingLayer = in.value("sortingLayer", 0);
    orderInLayer = in.value("orderInLayer", 0);
    pixelsPerUnit = in.value("pixelsPerUnit", 100.0f);
}

}  // namespace NovaEngine

// 组件静态注册（§25.0 纪律 4；注册名 "SpriteRenderer"）
NE_REGISTER_COMPONENT(NovaEngine::SpriteRenderer)
