#pragma once
#include <string>
#include <nlohmann/json_fwd.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include "Component.h"

namespace NovaEngine {

// 运行时 UI 图片（V0.3-3）：点锚 + 像素偏移（v1 不做全锚点拉伸）
class Image : public Component {
public:
    explicit Image(GameObject* owner);

    std::string spritePath;                       // 空 = 纯色（atlas 白块）
    glm::vec4 color{1.0f};
    glm::vec4 slice9{0.0f};                       // 左/上/右/下像素边距；全 0 = 普通拉伸
    glm::vec2 anchor{0.5f};                       // 归一化锚点
    glm::vec2 offset{0.0f};                       // 像素偏移（相对锚点）
    glm::vec2 size{100.0f};

    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

}  // namespace NovaEngine
