#pragma once
#include <string>
#include <nlohmann/json_fwd.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include "Component.h"

namespace NovaEngine {

// 水平对齐（V0.5-13）：相对 wrapWidth（wrap=true）或文本块最宽行（wrap=false）
enum class TextHAlign { Left, Center, Right };

// 运行时 UI 文本（V0.3-3）：UTF-8 含中文；stb_truetype 按需烘焙
class Text : public Component {
public:
    explicit Text(GameObject* owner);

    std::string text;                             // UTF-8，含中文
    float fontSize = 32.0f;
    glm::vec4 color{1.0f};
    std::string fontPath;                         // 空=默认系统字体（msyh.ttc）；可指 assets/fonts/*.ttf（可进 pak）
    glm::vec2 anchor{0.5f}, offset{0.0f};

    // ---- wrap/wrapWidth/hAlign（V0.5-13 追加；缺省 false/0/Left = 旧渲染逐像素一致）----
    bool wrap = false;                            // 按 wrapWidth 折行（按码点断行，CJK 安全；保留 '\n'）
    float wrapWidth = 0.0f;                       // 逻辑像素；<=0 时取当前 Canvas 屏幕宽
    TextHAlign hAlign = TextHAlign::Left;

    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

}  // namespace NovaEngine
