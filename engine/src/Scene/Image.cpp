// Image.cpp —— 运行时 UI 图片（V0.3-3；V0.5-13 +fill 三字段）
// 点锚 + 像素偏移（v1 不做全锚点拉伸）；空 spritePath = 纯色（字体图集白块）
#include <NovaEngine/Scene/Image.h>

#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/ComponentRegistry.h>

#include <nlohmann/json.hpp>

#include <algorithm>
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

// fillType ↔ 字符串（序列化键名冻结：§24.3.6）
std::string fillTypeToString(ImageFillType t) {
    switch (t) {
        case ImageFillType::None: return "None";
        case ImageFillType::Horizontal: return "Horizontal";
        case ImageFillType::Vertical: return "Vertical";
        case ImageFillType::Radial90: return "Radial90";
        case ImageFillType::Radial180: return "Radial180";
        case ImageFillType::Radial360: return "Radial360";
    }
    return "None";
}

ImageFillType fillTypeFromString(const std::string& s) {
    if (s == "Horizontal") return ImageFillType::Horizontal;
    if (s == "Vertical") return ImageFillType::Vertical;
    if (s == "Radial90") return ImageFillType::Radial90;
    if (s == "Radial180") return ImageFillType::Radial180;
    if (s == "Radial360") return ImageFillType::Radial360;
    if (s != "None")
        Logger::warn("Image 反序列化：未知 fillType \"{}\"，回退 None", s);
    return ImageFillType::None;
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
    // V0.5-13：fill 三字段（旧版读取时缺省兜底，见 onDeserialize）
    out["fillType"] = fillTypeToString(fillType);
    out["fillAmount"] = fillAmount;
    out["fillClockwise"] = fillClockwise;
}

void Image::onDeserialize(const nlohmann::json& in) {
    spritePath = in.value("spritePath", std::string());
    color = jsonToVec4(in.value("color", nlohmann::json()), glm::vec4(1.0f));
    slice9 = jsonToVec4(in.value("slice9", nlohmann::json()), glm::vec4(0.0f));
    anchor = jsonToVec2(in.value("anchor", nlohmann::json()), glm::vec2(0.5f));
    offset = jsonToVec2(in.value("offset", nlohmann::json()), glm::vec2(0.0f));
    size = jsonToVec2(in.value("size", nlohmann::json()), glm::vec2(100.0f));
    // V0.5-13：缺省 = None/1.0/true（旧场景行为零变化）；未知字符串 warn 回退 None
    fillType = fillTypeFromString(jsonToString(in, "fillType", "None"));
    fillAmount = std::clamp(jsonToFloat(in, "fillAmount", 1.0f), 0.0f, 1.0f);
    fillClockwise = jsonToBool(in, "fillClockwise", true);
}

}  // namespace NovaEngine

NE_REGISTER_COMPONENT(NovaEngine::Image)
