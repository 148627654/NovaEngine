#pragma once
#include <string>
#include <nlohmann/json_fwd.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include "Component.h"

namespace NovaEngine {

// 运行时 UI 文本（V0.3-3）：UTF-8 含中文；stb_truetype 按需烘焙
class Text : public Component {
public:
    explicit Text(GameObject* owner);

    std::string text;                             // UTF-8，含中文
    float fontSize = 32.0f;
    glm::vec4 color{1.0f};
    std::string fontPath;                         // 空=默认系统字体（msyh.ttc）；可指 assets/fonts/*.ttf（可进 pak）
    glm::vec2 anchor{0.5f}, offset{0.0f};

    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

}  // namespace NovaEngine
