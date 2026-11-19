// AnimatorPanel.cpp —— 动画状态机面板（V0.5-9，c7）：列表式（状态/参数/过渡三列表，无节点图）。
// 编辑当前选中 GameObject 的 Animator 组件；变更经 EditorContext::markSceneDirty 入撤销栈。
#include "AnimatorPanel.h"

#include "../EditorContext.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>   // InputText(std::string*) 包装

#include <NovaEngine/Animation/Animator.h>
#include <NovaEngine/Scene/GameObject.h>

#include <algorithm>
#include <string>
#include <vector>

namespace NovaEditor {

namespace {

using namespace NovaEngine;

// ImGui::Combo 用的 const char* 表（v 生命周期内 strings 不变）
std::vector<const char*> toCstr(const std::vector<std::string>& v) {
    std::vector<const char*> out;
    out.reserve(v.size());
    for (const std::string& s : v) out.push_back(s.c_str());
    return out;
}

std::vector<std::string> stateNames(const std::vector<AnimatorState>& states) {
    std::vector<std::string> names;
    names.reserve(states.size());
    for (const AnimatorState& s : states) names.push_back(s.name);
    return names;
}

std::vector<std::string> paramNames(const std::vector<AnimatorParameter>& params) {
    std::vector<std::string> names;
    names.reserve(params.size());
    for (const AnimatorParameter& p : params) names.push_back(p.name);
    return names;
}

const char* paramTypeLabel(AnimatorParameter::Type type) {
    switch (type) {
        case AnimatorParameter::Type::Float:   return "Float";
        case AnimatorParameter::Type::Bool:    return "Bool";
        case AnimatorParameter::Type::Trigger: return "Trigger";
    }
    return "Float";
}

const char* condOpLabel(AnimatorTransition::Condition::Op op) {
    switch (op) {
        case AnimatorTransition::Condition::Op::Greater:  return "Greater";
        case AnimatorTransition::Condition::Op::Less:     return "Less";
        case AnimatorTransition::Condition::Op::Equals:   return "Equals";
        case AnimatorTransition::Condition::Op::BoolTrue: return "BoolTrue";
    }
    return "Greater";
}

}  // namespace

AnimatorPanel::AnimatorPanel() : Panel("Animator") {}

void AnimatorPanel::onGui(EditorContext& ctx, EditorCamera& /*camera*/) {
    if (!open) return;

    ImGui::Begin(m_name.c_str(), &open);
    NovaEngine::GameObject* go = ctx.activeObject();
    Animator* animator = go ? go->getComponent<Animator>() : nullptr;
    if (!animator) {
        ImGui::TextDisabled("选中一个带 Animator 组件的对象以编辑状态机");
        ImGui::End();
        return;
    }

    bool changed = false;

    // ===== 状态列表 =====
    if (ImGui::CollapsingHeader("状态（States）", ImGuiTreeNodeFlags_DefaultOpen)) {
        int defaultState = std::clamp(animator->defaultState, 0,
                                      static_cast<int>(animator->states.size()) - 1);
        for (size_t i = 0; i < animator->states.size(); ++i) {
            AnimatorState& s = animator->states[i];
            ImGui::PushID(static_cast<int>(i));
            const std::string label = "状态 #" + std::to_string(i);
            changed |= ImGui::InputText("名称", &s.name);
            changed |= ImGui::InputText("剪辑", &s.clip);
            changed |= ImGui::DragFloat("速度", &s.speed, 0.05f, 0.0f, 10.0f);
            changed |= ImGui::Checkbox("循环", &s.loop);
            bool isDefault = (i == static_cast<size_t>(defaultState));
            if (ImGui::RadioButton("默认状态", isDefault)) {
                animator->defaultState = static_cast<int>(i);
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("删除")) {
                animator->states.erase(animator->states.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                ImGui::PopID();
                break;   // 迭代中删除：跳出重建
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        if (ImGui::Button("添加状态")) {
            AnimatorState s;
            s.name = "State" + std::to_string(animator->states.size());
            s.clip = animator->states.empty() ? "" : animator->states.front().clip;
            animator->states.push_back(std::move(s));
            changed = true;
        }
    }

    // ===== 参数列表 =====
    if (ImGui::CollapsingHeader("参数（Parameters）", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (size_t i = 0; i < animator->parameters.size(); ++i) {
            AnimatorParameter& p = animator->parameters[i];
            ImGui::PushID(static_cast<int>(i + 1000));
            changed |= ImGui::InputText("名称", &p.name);
            int typeIdx = static_cast<int>(p.type);
            if (ImGui::Combo("类型", &typeIdx, "Float\0Bool\0Trigger\0")) {
                p.type = static_cast<AnimatorParameter::Type>(typeIdx);
                changed = true;
            }
            if (p.type == AnimatorParameter::Type::Float)
                changed |= ImGui::DragFloat("值", &p.value, 0.05f);
            else if (p.type == AnimatorParameter::Type::Bool) {
                bool b = p.value != 0.0f;
                if (ImGui::Checkbox("值", &b)) { p.value = b ? 1.0f : 0.0f; changed = true; }
            } else {
                ImGui::TextDisabled("Trigger：运行时置位（当帧消费），无持久值");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("删除")) {
                animator->parameters.erase(
                    animator->parameters.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                ImGui::PopID();
                break;
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        if (ImGui::Button("添加参数")) {
            AnimatorParameter p;
            p.name = "Param" + std::to_string(animator->parameters.size());
            animator->parameters.push_back(std::move(p));
            changed = true;
        }
    }

    // ===== 过渡列表 =====
    if (ImGui::CollapsingHeader("过渡（Transitions）", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (animator->states.empty()) {
            ImGui::TextDisabled("先添加状态再配置过渡");
        } else {
        const std::vector<std::string> sNames = stateNames(animator->states);
        const std::vector<const char*> sCstr = toCstr(sNames);
        const std::vector<std::string> pNames = paramNames(animator->parameters);
        const std::vector<const char*> pCstr = toCstr(pNames);
        const int stateCount = static_cast<int>(animator->states.size());

        for (size_t i = 0; i < animator->transitions.size(); ++i) {
            AnimatorTransition& t = animator->transitions[i];
            ImGui::PushID(static_cast<int>(i + 2000));
            int from = std::clamp(t.from, 0, stateCount - 1);
            int to = std::clamp(t.to, 0, stateCount - 1);
            if (ImGui::Combo("从", &from, sCstr.data(), stateCount)) {
                t.from = from;
                changed = true;
            }
            if (ImGui::Combo("到", &to, sCstr.data(), stateCount)) {
                t.to = to;
                changed = true;
            }
            changed |= ImGui::DragFloat("混合时长(秒)", &t.duration, 0.05f, 0.0f, 10.0f);

            ImGui::TextUnformatted("条件（全部满足才触发）：");
            for (size_t c = 0; c < t.conditions.size(); ++c) {
                AnimatorTransition::Condition& cond = t.conditions[c];
                ImGui::PushID(static_cast<int>(c));
                int paramIdx = -1;
                for (size_t k = 0; k < pNames.size(); ++k)
                    if (pNames[k] == cond.parameter) paramIdx = static_cast<int>(k);
                if (ImGui::Combo("参数", &paramIdx, pCstr.data(),
                                 static_cast<int>(pNames.size()))) {
                    if (paramIdx >= 0) {
                        cond.parameter = pNames[paramIdx];
                        changed = true;
                    }
                }
                int opIdx = static_cast<int>(cond.op);
                if (ImGui::Combo("操作", &opIdx, "Greater\0Less\0Equals\0BoolTrue\0")) {
                    cond.op = static_cast<AnimatorTransition::Condition::Op>(opIdx);
                    changed = true;
                }
                if (cond.op != AnimatorTransition::Condition::Op::BoolTrue)
                    changed |= ImGui::DragFloat("阈值", &cond.value, 0.05f);
                ImGui::SameLine();
                if (ImGui::SmallButton("删条件")) {
                    t.conditions.erase(t.conditions.begin() + static_cast<std::ptrdiff_t>(c));
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("添加条件")) {
                AnimatorTransition::Condition c;
                c.parameter = pNames.empty() ? std::string() : pNames.front();
                t.conditions.push_back(std::move(c));
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("删除过渡")) {
                animator->transitions.erase(
                    animator->transitions.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                ImGui::PopID();
                break;
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        if (ImGui::Button("添加过渡")) {
            AnimatorTransition t;
            t.from = 0;
            t.to = std::min(1, stateCount - 1);
            animator->transitions.push_back(std::move(t));
            changed = true;
        }
        }   // states 非空
    }

    // 当前状态（Play 态查看；Edit 态显示默认状态）
    ImGui::Separator();
    ImGui::TextUnformatted(("当前状态：" + animator->currentStateName()).c_str());

    if (changed) ctx.markSceneDirty();   // 变更入撤销栈 + 置脏标记
    ImGui::End();
}

}  // namespace NovaEditor
