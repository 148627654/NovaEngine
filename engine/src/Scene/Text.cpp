// Text.cpp —— 运行时 UI 文本（V0.3-3）
// UTF-8 含中文；渲染走 FontAtlas 按需烘焙（默认 msyh.ttc；fontPath 预留序列化，
// v1 仅默认图集，见 UIRenderer.cpp 顶部「v1 已知限制」）
#include <NovaEngine/Scene/Text.h>

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

Text::Text(GameObject* owner) : Component(owner) {}

void Text::onSerialize(nlohmann::json& out) const {
    out["text"] = text;
    out["fontSize"] = fontSize;
    out["color"] = nlohmann::json::array({color.x, color.y, color.z, color.w});
    out["fontPath"] = fontPath;
    out["anchor"] = nlohmann::json::array({anchor.x, anchor.y});
    out["offset"] = nlohmann::json::array({offset.x, offset.y});
}

void Text::onDeserialize(const nlohmann::json& in) {
    text = in.value("text", std::string());
    fontSize = in.value("fontSize", 32.0f);
    color = jsonToVec4(in.value("color", nlohmann::json()), glm::vec4(1.0f));
    fontPath = in.value("fontPath", std::string());
    anchor = jsonToVec2(in.value("anchor", nlohmann::json()), glm::vec2(0.5f));
    offset = jsonToVec2(in.value("offset", nlohmann::json()), glm::vec2(0.0f));
}

}  // namespace NovaEngine

NE_REGISTER_COMPONENT(NovaEngine::Text)
