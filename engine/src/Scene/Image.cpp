// Image.cpp —— 运行时 UI 图片（V0.3-3）
// 点锚 + 像素偏移（v1 不做全锚点拉伸）；空 spritePath = 纯色（字体图集白块）
#include <NovaEngine/Scene/Image.h>

#include <NovaEngine/Scene/ComponentRegistry.h>

#include <nlohmann/json.hpp>

namespace NovaEngine {

namespace {

glm::vec2 jsonToVec2(const nlohmann::json& j, const glm::vec2& fallback) {
    if (!j.is_array() || j.size() < 2) return fallback;
    try {
        return glm::vec2(j[0].get<float>(), j[1].get<float>());
    } catch (const std::exception&) {
        return fallback;
    }
}

glm::vec4 jsonToVec4(const nlohmann::json& j, const glm::vec4& fallback) {
    if (!j.is_array() || j.size() < 4) return fallback;
    try {
        return glm::vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(),
                         j[3].get<float>());
    } catch (const std::exception&) {
        return fallback;
    }
}

}  // namespace

Image::Image(GameObject* owner) : Component(owner) {}

void Image::onSerialize(nlohmann::json& out) const {
    out["spritePath"] = spritePath;
    out["color"] = nlohmann::json::array({color.x, color.y, color.z, color.w});
    out["slice9"] = nlohmann::json::array({slice9.x, slice9.y, slice9.z, slice9.w});
    out["anchor"] = nlohmann::json::array({anchor.x, anchor.y});
    out["offset"] = nlohmann::json::array({offset.x, offset.y});
    out["size"] = nlohmann::json::array({size.x, size.y});
}

void Image::onDeserialize(const nlohmann::json& in) {
    spritePath = in.value("spritePath", std::string());
    color = jsonToVec4(in.value("color", nlohmann::json()), glm::vec4(1.0f));
    slice9 = jsonToVec4(in.value("slice9", nlohmann::json()), glm::vec4(0.0f));
    anchor = jsonToVec2(in.value("anchor", nlohmann::json()), glm::vec2(0.5f));
    offset = jsonToVec2(in.value("offset", nlohmann::json()), glm::vec2(0.0f));
    size = jsonToVec2(in.value("size", nlohmann::json()), glm::vec2(100.0f));
}

}  // namespace NovaEngine

NE_REGISTER_COMPONENT(NovaEngine::Image)
