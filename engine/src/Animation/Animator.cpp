// Animator.cpp —— 动画状态机组件（V0.5-9，c7）：参数驱动状态过渡 + duration 线性混合 +
// 骨骼矩阵上传（Mesh::setBoneMatrices）。序列化键名逐字 §25.7。
// 无状态机配置（states 空）→ no-op；单状态 = 单动画播放退化形态；
// 无 SMR/骨架空 → no-op warn 一次；clip 名找不到 → warn 保持 T-pose。
#include <NovaEngine/Animation/Animator.h>

#include <NovaEngine/Animation/SkeletalMeshRenderer.h>
#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/ComponentRegistry.h>
#include <NovaEngine/Scene/GameObject.h>

#include <nlohmann/json.hpp>

#include "AnimationInternals.h"

namespace NovaEngine {

namespace {

using namespace AnimationInternals;

// §25.7 序列化 op/type 字符串与枚举互转（未知 → warn + 默认值兜底）
std::string opToString(AnimatorTransition::Condition::Op op) {
    switch (op) {
        case AnimatorTransition::Condition::Op::Greater:  return "Greater";
        case AnimatorTransition::Condition::Op::Less:     return "Less";
        case AnimatorTransition::Condition::Op::Equals:   return "Equals";
        case AnimatorTransition::Condition::Op::BoolTrue: return "BoolTrue";
    }
    return "Greater";
}

AnimatorTransition::Condition::Op opFromString(const std::string& s,
                                               AnimatorTransition::Condition::Op fallback) {
    if (s == "Greater") return AnimatorTransition::Condition::Op::Greater;
    if (s == "Less") return AnimatorTransition::Condition::Op::Less;
    if (s == "Equals") return AnimatorTransition::Condition::Op::Equals;
    if (s == "BoolTrue") return AnimatorTransition::Condition::Op::BoolTrue;
    Logger::warn("Animator 反序列化：未知过渡条件 op \"{}\"，回退 Greater", s);
    return fallback;
}

std::string typeToString(AnimatorParameter::Type type) {
    switch (type) {
        case AnimatorParameter::Type::Float:   return "Float";
        case AnimatorParameter::Type::Bool:    return "Bool";
        case AnimatorParameter::Type::Trigger: return "Trigger";
    }
    return "Float";
}

AnimatorParameter::Type typeFromString(const std::string& s) {
    if (s == "Bool") return AnimatorParameter::Type::Bool;
    if (s == "Trigger") return AnimatorParameter::Type::Trigger;
    if (s != "Float")
        Logger::warn("Animator 反序列化：未知参数类型 \"{}\"，回退 Float", s);
    return AnimatorParameter::Type::Float;
}

// warn 一次集合辅助（v1 规模小，线性查找）
bool warnedBefore(std::vector<std::string>& warned, const std::string& key) {
    if (std::find(warned.begin(), warned.end(), key) != warned.end()) return true;
    warned.push_back(key);
    return false;
}

}  // namespace

Animator::Animator(GameObject* owner) : Component(owner) {}

// ===== 脚本 API（Play 中；对齐 Unity 心智；未知参数 warn 一次）=====

void Animator::setFloat(const std::string& name, float v) {
    for (AnimatorParameter& p : parameters)
        if (p.name == name && p.type != AnimatorParameter::Type::Trigger) { p.value = v; return; }
    if (!warnedBefore(m_warnedParams, name))
        Logger::warn("Animator: setFloat 未找到参数 \"{}\"（忽略）", name);
}

void Animator::setBool(const std::string& name, bool v) {
    for (AnimatorParameter& p : parameters)
        if (p.name == name && p.type != AnimatorParameter::Type::Trigger) { p.value = v ? 1.0f : 0.0f; return; }
    if (!warnedBefore(m_warnedParams, name))
        Logger::warn("Animator: setBool 未找到参数 \"{}\"（忽略）", name);
}

void Animator::setTrigger(const std::string& name) {
    for (AnimatorParameter& p : parameters)
        if (p.name == name) { p.value = 1.0f; return; }   // 置位标记：当帧消费，不序列化
    if (!warnedBefore(m_warnedParams, name))
        Logger::warn("Animator: setTrigger 未找到参数 \"{}\"（忽略）", name);
}

float Animator::getFloat(const std::string& name) const {
    for (const AnimatorParameter& p : parameters)
        if (p.name == name) return p.value;
    return 0.0f;
}

const std::string& Animator::currentStateName() const {
    static const std::string kEmpty;
    if (m_currentState < 0 || m_currentState >= static_cast<int>(states.size())) return kEmpty;
    return states[m_currentState].name;
}

void Animator::play(const std::string& stateName) {
    // 直接跳转（无混合，§25.7）
    m_transitionTarget = -1;
    m_transitionTime = 0.0f;
    m_transitionDuration = 0.0f;
    m_clipTime = 0.0f;
    m_targetClipTime = 0.0f;
    for (size_t i = 0; i < states.size(); ++i)
        if (states[i].name == stateName) { m_currentState = static_cast<int>(i); return; }
    Logger::warn("Animator: play 未找到状态 \"{}\"（保持当前状态）", stateName);
}

// ===== 每帧驱动（Play 态经 Scene::update 广播）=====

void Animator::onUpdate(float dt) {
    // 无状态机配置 → no-op（§25.7 畸形兜底：states 空）
    if (states.empty()) return;

    // 无 SMR/骨架空 → no-op warn 一次
    SkeletalMeshRenderer* smr = gameObject()->getComponent<SkeletalMeshRenderer>();
    if (!smr || !smr->mesh || smr->skeleton.bones.empty()) {
        if (!m_noSmrWarned) {
            Logger::warn("Animator: 同 GameObject 无有效 SkeletalMeshRenderer（蒙皮网格+骨架），跳过动画更新");
            m_noSmrWarned = true;
        }
        return;
    }
    if (smr->meshPath.empty()) return;   // 无模型路径：无剪辑可取（不置 warn——SMR 侧已处理）

    // 剪辑表（与 SMR 同源缓存——AnimationInternals 按绝对路径去重，不重复解析）
    const auto model = loadSkinnedModel(
        [&smr] {
            std::filesystem::path p(smr->meshPath);
            if (p.is_absolute()) return p.string();
            const std::filesystem::path root = AssetDatabase::projectRoot();
            return (root.empty() ? std::filesystem::absolute(p) : root / p).string();
        }());
    if (!model || model->clips.empty()) {
        if (!warnedBefore(m_warnedClips, "<模型无剪辑>"))
            Logger::warn("Animator: \"{}\" 无动画剪辑，保持 T-pose", smr->meshPath);
        return;
    }

    // 当前状态初始化（defaultState 钳制；states 中途变更时自动重钳）
    if (m_currentState < 0 || m_currentState >= static_cast<int>(states.size()))
        m_currentState = std::clamp(defaultState, 0, static_cast<int>(states.size()) - 1);

    // 过渡判定（仅非过渡期；Trigger 本帧置位参与判定）→ 判定后 Trigger 当帧消费清零
    if (m_transitionTarget < 0) {
        const int edge = evaluateTransition(transitions, parameters, m_currentState);
        if (edge >= 0) {
            m_transitionTarget = std::clamp(transitions[edge].to, 0,
                                            static_cast<int>(states.size()) - 1);
            m_transitionDuration = std::max(transitions[edge].duration, 0.0f);
            m_transitionTime = 0.0f;
            m_targetClipTime = 0.0f;   // 目标剪辑从 0 起播（v1 无时间同步）
        }
    }
    for (AnimatorParameter& p : parameters)
        if (p.type == AnimatorParameter::Type::Trigger) p.value = 0.0f;   // 当帧消费

    const AnimatorState& cur = states[m_currentState];
    const AnimationClip* curClip = findClip(*model, cur.clip);
    if (!curClip) {
        if (!warnedBefore(m_warnedClips, cur.clip))
            Logger::warn("Animator: 剪辑 \"{}\" 不存在（保持 T-pose）", cur.clip);
        return;
    }
    m_clipTime = advanceClipTime(m_clipTime, dt * cur.speed, *curClip, cur.loop);

    // 剪辑采样 → 骨骼矩阵（上限 64 由 extractSkeleton 保证）
    std::vector<glm::mat4> boneMats;
    if (m_transitionTarget >= 0) {
        // 过渡混合：两状态骨骼矩阵线性混合（duration 内权重渐变）
        m_transitionTime += dt;
        const AnimatorState& tgt = states[m_transitionTarget];
        const AnimationClip* tgtClip = findClip(*model, tgt.clip);
        if (!tgtClip) {
            if (!warnedBefore(m_warnedClips, tgt.clip))
                Logger::warn("Animator: 剪辑 \"{}\" 不存在（过渡退回源剪辑）", tgt.clip);
            m_transitionTarget = -1;
        } else {
            m_targetClipTime = advanceClipTime(m_targetClipTime, dt * tgt.speed, *tgtClip, tgt.loop);
            std::vector<glm::mat4> srcMats, tgtMats;
            sampleBoneMatrices(smr->skeleton, *curClip, m_clipTime, srcMats);
            sampleBoneMatrices(smr->skeleton, *tgtClip, m_targetClipTime, tgtMats);
            const float alpha = m_transitionDuration > 0.0f
                                    ? std::clamp(m_transitionTime / m_transitionDuration, 0.0f, 1.0f)
                                    : 1.0f;
            blendBoneMatrices(srcMats, tgtMats, alpha, boneMats);
            if (m_transitionTime >= m_transitionDuration) {
                // 过渡完成：切换状态
                m_currentState = m_transitionTarget;
                m_clipTime = m_targetClipTime;
                m_transitionTarget = -1;
            }
        }
    }
    if (boneMats.empty())   // 非过渡路径（或过渡目标剪辑缺失退回）
        sampleBoneMatrices(smr->skeleton, *curClip, m_clipTime, boneMats);

    // 上传骨骼矩阵（懒建 UBO binding 1；空 = 不绑定 → 绑定姿势兜底在 Mesh::draw 侧）
    smr->mesh->setBoneMatrices(boneMats);
}

// ===== 序列化（键名逐字 §25.7）=====

void Animator::onSerialize(nlohmann::json& out) const {
    out["defaultState"] = defaultState;
    nlohmann::json statesJson = nlohmann::json::array();
    for (const AnimatorState& s : states) {
        nlohmann::json sj;
        sj["name"] = s.name;
        sj["clip"] = s.clip;
        sj["speed"] = s.speed;
        sj["loop"] = s.loop;
        statesJson.push_back(std::move(sj));
    }
    out["states"] = std::move(statesJson);

    nlohmann::json transitionsJson = nlohmann::json::array();
    for (const AnimatorTransition& t : transitions) {
        nlohmann::json tj;
        tj["from"] = t.from;
        tj["to"] = t.to;
        tj["duration"] = t.duration;
        nlohmann::json condsJson = nlohmann::json::array();
        for (const AnimatorTransition::Condition& c : t.conditions) {
            nlohmann::json cj;
            cj["parameter"] = c.parameter;
            cj["op"] = opToString(c.op);
            cj["value"] = c.value;
            condsJson.push_back(std::move(cj));
        }
        tj["conditions"] = std::move(condsJson);
        transitionsJson.push_back(std::move(tj));
    }
    out["transitions"] = std::move(transitionsJson);

    nlohmann::json paramsJson = nlohmann::json::array();
    for (const AnimatorParameter& p : parameters) {
        nlohmann::json pj;
        pj["name"] = p.name;
        pj["type"] = typeToString(p.type);
        if (p.type != AnimatorParameter::Type::Trigger)
            pj["value"] = p.value;   // Trigger 运行期置位标记：不序列化（§25.7）
        paramsJson.push_back(std::move(pj));
    }
    out["parameters"] = std::move(paramsJson);
}

void Animator::onDeserialize(const nlohmann::json& in) {
    states.clear();
    transitions.clear();
    parameters.clear();
    m_currentState = -1;   // 反序列化后重新初始化运行期状态
    m_transitionTarget = -1;
    m_transitionTime = 0.0f;
    m_transitionDuration = 0.0f;
    m_clipTime = 0.0f;
    m_targetClipTime = 0.0f;

    // 畸形字段兜底（不崩溃）：类型不符时安全读取——
    // 注意不能用 in.value(key, fallback)：键存在但类型不符时 nlohmann 会抛 type_error
    //（实测 302：defaultState 存成字符串 → 异常 → 未捕获即 abort）
    auto safeFloat = [](const nlohmann::json& j, const char* key, float fallback) -> float {
        if (!j.is_object() || !j.contains(key) || !j[key].is_number()) return fallback;
        try { return j[key].get<float>(); } catch (...) { return fallback; }
    };
    auto safeInt = [](const nlohmann::json& j, const char* key, int fallback) -> int {
        if (!j.is_object() || !j.contains(key) || !j[key].is_number_integer()) return fallback;
        try { return j[key].get<int>(); } catch (...) { return fallback; }
    };
    auto safeString = [](const nlohmann::json& j, const char* key, std::string fallback) -> std::string {
        if (!j.is_object() || !j.contains(key) || !j[key].is_string()) return fallback;
        try { return j[key].get<std::string>(); } catch (...) { return fallback; }
    };
    auto safeBool = [](const nlohmann::json& j, const char* key, bool fallback) -> bool {
        if (!j.is_object() || !j.contains(key) || !j[key].is_boolean()) return fallback;
        try { return j[key].get<bool>(); } catch (...) { return fallback; }
    };
    defaultState = safeInt(in, "defaultState", 0);

    if (in.contains("states") && in["states"].is_array()) {
        for (const nlohmann::json& sj : in["states"]) {
            if (!sj.is_object()) continue;
            AnimatorState s;
            s.name = safeString(sj, "name", "");
            s.clip = safeString(sj, "clip", "");
            s.speed = safeFloat(sj, "speed", 1.0f);
            s.loop = safeBool(sj, "loop", true);
            states.push_back(std::move(s));
        }
    }
    if (in.contains("transitions") && in["transitions"].is_array()) {
        for (const nlohmann::json& tj : in["transitions"]) {
            if (!tj.is_object()) continue;
            AnimatorTransition t;
            t.from = safeInt(tj, "from", 0);
            t.to = safeInt(tj, "to", 0);
            t.duration = safeFloat(tj, "duration", 0.25f);
            if (tj.contains("conditions") && tj["conditions"].is_array()) {
                for (const nlohmann::json& cj : tj["conditions"]) {
                    if (!cj.is_object()) continue;
                    AnimatorTransition::Condition c;
                    c.parameter = safeString(cj, "parameter", "");
                    c.op = opFromString(safeString(cj, "op", "Greater"),
                                        AnimatorTransition::Condition::Op::Greater);
                    c.value = safeFloat(cj, "value", 0.0f);
                    t.conditions.push_back(std::move(c));
                }
            }
            transitions.push_back(std::move(t));
        }
    }
    if (in.contains("parameters") && in["parameters"].is_array()) {
        for (const nlohmann::json& pj : in["parameters"]) {
            if (!pj.is_object()) continue;
            AnimatorParameter p;
            p.name = safeString(pj, "name", "");
            p.type = typeFromString(safeString(pj, "type", "Float"));
            if (p.type != AnimatorParameter::Type::Trigger)
                p.value = safeFloat(pj, "value", 0.0f);
            parameters.push_back(std::move(p));
        }
    }
}

}  // namespace NovaEngine

// 组件静态注册（§25.0 纪律 4；注册名 "Animator"）
NE_REGISTER_COMPONENT(NovaEngine::Animator)
