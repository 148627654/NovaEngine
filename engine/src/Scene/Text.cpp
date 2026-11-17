// Text.cpp —— 运行时 UI 文本（V0.3-3；V0.5-13 +wrap/wrapWidth/hAlign）
// UTF-8 含中文；渲染走 FontAtlas 按需烘焙（默认 msyh.ttc；fontPath 预留序列化，
// v1 仅默认图集，见 UIRenderer.cpp 顶部「v1 已知限制」）
#include <NovaEngine/Scene/Text.h>

#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/ComponentRegistry.h>

#include <nlohmann/json.hpp>

#include <string>

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

// 布尔字段读取：键缺失或类型不符回退默认值（畸形文件不崩溃）
bool jsonToBool(const nlohmann::json& j, const char* key, bool fallback) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_boolean()) return fallback;
    return j[key].get<bool>();
}

// 字符串字段读取：键缺失或类型不符（如数字）回退默认值
std::string jsonToString(const nlohmann::json& j, const char* key, const std::string& fallback) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_string()) return fallback;
    return j[key].get<std::string>();
}

// 浮点字段读取：键缺失或类型不符回退默认值
float jsonToFloat(const nlohmann::json& j, const char* key, float fallback) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_number()) return fallback;
    try {
        return j[key].get<float>();
    } catch (const std::exception&) {
        return fallback;
    }
}

// hAlign ↔ 字符串（序列化键名冻结：§24.3.6）
std::string hAlignToString(TextHAlign a) {
    switch (a) {
        case TextHAlign::Left: return "Left";
        case TextHAlign::Center: return "Center";
        case TextHAlign::Right: return "Right";
    }
    return "Left";
}

TextHAlign hAlignFromString(const std::string& s) {
    if (s == "Center") return TextHAlign::Center;
    if (s == "Right") return TextHAlign::Right;
    if (s != "Left")
        Logger::warn("Text 反序列化：未知 hAlign \"{}\"，回退 Left", s);
    return TextHAlign::Left;
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
    // V0.5-13：wrap 三字段（旧版读取时缺省兜底，见 onDeserialize）
    out["wrap"] = wrap;
    out["wrapWidth"] = wrapWidth;
    out["hAlign"] = hAlignToString(hAlign);
}

void Text::onDeserialize(const nlohmann::json& in) {
    text = in.value("text", std::string());
    fontSize = in.value("fontSize", 32.0f);
    color = jsonToVec4(in.value("color", nlohmann::json()), glm::vec4(1.0f));
    fontPath = in.value("fontPath", std::string());
    anchor = jsonToVec2(in.value("anchor", nlohmann::json()), glm::vec2(0.5f));
    offset = jsonToVec2(in.value("offset", nlohmann::json()), glm::vec2(0.0f));
    // V0.5-13：缺省 = false/0/Left（旧渲染逐像素一致）；未知字符串 warn 回退 Left
    wrap = jsonToBool(in, "wrap", false);
    wrapWidth = jsonToFloat(in, "wrapWidth", 0.0f);
    hAlign = hAlignFromString(jsonToString(in, "hAlign", "Left"));
}

}  // namespace NovaEngine

NE_REGISTER_COMPONENT(NovaEngine::Text)
