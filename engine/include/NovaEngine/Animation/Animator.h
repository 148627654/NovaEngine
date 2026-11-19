#pragma once
#include <string>
#include <vector>
#include <nlohmann/json_fwd.hpp>
#include <NovaEngine/Scene/Component.h>
#include "AnimatorController.h"

namespace NovaEngine {

// 动画状态机组件（V0.5-9）：参数驱动状态过渡 + 过渡混合；v1 列表式配置（内联 JSON，无独立资产）。
class Animator : public Component {
public:
    explicit Animator(GameObject* owner);
    std::vector<AnimatorState> states;
    int defaultState = 0;
    std::vector<AnimatorTransition> transitions;
    std::vector<AnimatorParameter> parameters;

    void setFloat(const std::string& name, float v);    // 脚本 API（Play 中；对齐 Unity 心智）
    void setBool(const std::string& name, bool v);
    void setTrigger(const std::string& name);
    float getFloat(const std::string& name) const;
    const std::string& currentStateName() const;
    void play(const std::string& stateName);   // 直接跳转（无混合）

    void onUpdate(float dt) override;          // 过渡判定/参数消费/混合推进/骨骼矩阵上传
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;

private:
    // ===== 运行期状态（V0.5-9 c7 私有实现细节；不序列化——公共接口保持 §25.7 冻结）=====
    int m_currentState = -1;                // 当前状态下标；-1 = 未初始化（首帧用 defaultState）
    int m_transitionTarget = -1;            // 进行中过渡的目标状态；-1 = 无过渡
    float m_transitionTime = 0.0f;          // 过渡已进行时间（秒）
    float m_transitionDuration = 0.0f;      // 当前过渡总时长（秒；transition.duration）
    float m_clipTime = 0.0f;                // 当前状态剪辑播放时间（秒）
    float m_targetClipTime = 0.0f;          // 过渡目标剪辑播放时间（秒，过渡开始归零）
    bool m_noSmrWarned = false;             // 无 SMR → warn 一次（§25.7）
    std::vector<std::string> m_warnedClips; // clip 名找不到 → warn 一次（保持 T-pose）
    std::vector<std::string> m_warnedParams;// 未知参数名 → warn 一次（§25.7）
};

}  // namespace NovaEngine
