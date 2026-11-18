#pragma once
#include <string>
#include <vector>

namespace NovaEngine {

// 动画状态机数据（V0.5-9；结构按 §25.7 冻结，Animator.h 经本头取用）：
// 参数驱动状态过渡 + 过渡混合；v1 列表式配置（内联 JSON，无独立资产）。

struct AnimatorParameter {
    enum class Type { Float, Bool, Trigger } type = Type::Float;
    std::string name; float value = 0.0f;   // Trigger：运行期置位标记（当帧消费，不序列化）
};

struct AnimatorTransition {
    int from = 0, to = 0;
    float duration = 0.25f;                  // 过渡混合时长（秒）
    struct Condition { std::string parameter; enum class Op { Greater, Less, Equals, BoolTrue } op = Op::Greater; float value = 0.0f; };
    std::vector<Condition> conditions;       // 全部满足才触发（v1 无 Exit Time）
};

struct AnimatorState { std::string name; std::string clip; float speed = 1.0f; bool loop = true; };

}  // namespace NovaEngine
