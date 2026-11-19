#pragma once
// AnimationInternals.h —— 动画模块内部头（非公共 API，不放 include/NovaEngine；对齐
// SerializationInternals.h 模式）：SMR 与 Animator 共用的蒙皮模型加载/采样/混合/状态机判定。
// header-only inline（函数内静态缓存跨翻译单元唯一，C++17 inline 语义）。
#include <NovaEngine/Animation/AnimatorController.h>
#include <NovaEngine/Assets/ModelLoader.h>
#include <NovaEngine/Render/MeshData.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace NovaEngine {
namespace AnimationInternals {

// 蒙皮模型加载结果（含原始 MeshData：每个 SMR 实例自建 GPU Mesh——独立骨骼 UBO +
// 指针唯一防实例化分组，§25.7 合波裁定 A）
struct SkinnedModelData {
    bool loaded = false;                    // 加载失败（文件不存在/解析失败）标记
    Skeleton skeleton;
    std::vector<AnimationClip> clips;
    std::vector<MeshData> meshes;           // 全部子网格原始数据（含蒙皮布局）

    // 第一个含蒙皮顶点的子网格；无 → nullptr（调用方回退静态网格）
    const MeshData* firstSkinned() const {
        for (const MeshData& md : meshes)
            if (!md.skinnedVertices.empty()) return &md;
        return nullptr;
    }
};

// 加载 + 缓存（key = 绝对路径；失败同样缓存 loaded=false 防每帧重试）。ModelLoader 直读
// （绕 AssetDatabase 网格缓存——见 SkinnedModelData 注释），内部已记日志。
inline std::shared_ptr<const SkinnedModelData> loadSkinnedModel(const std::string& absPath) {
    static std::unordered_map<std::string, std::shared_ptr<const SkinnedModelData>> s_cache;
    auto it = s_cache.find(absPath);
    if (it != s_cache.end()) return it->second;

    auto data = std::make_shared<SkinnedModelData>();
    const ModelImportResult res = ModelLoader::load(std::filesystem::path(absPath));
    if (!res.meshes.empty()) {
        data->loaded = true;
        data->meshes = res.meshes;
        if (const MeshData* skinned = data->firstSkinned()) {
            data->skeleton = skinned->skeleton;
            data->clips = skinned->clips;
        }
    }
    s_cache[absPath] = data;
    return data;
}

// 剪辑按名查找（未命中 → nullptr，调用方 warn 保持 T-pose）
inline const AnimationClip* findClip(const SkinnedModelData& model, const std::string& name) {
    for (const AnimationClip& clip : model.clips)
        if (clip.name == name) return &clip;
    return nullptr;
}

// 最近关键帧插值（keys 时间升序——extractAnimations 保证；越界钳制端点）
inline AnimKey sampleKey(const AnimChannel& channel, float timeTicks) {
    const std::vector<AnimKey>& keys = channel.keys;
    if (keys.empty()) return AnimKey{};
    if (keys.size() == 1 || timeTicks <= keys.front().time) return keys.front();
    if (timeTicks >= keys.back().time) return keys.back();
    size_t lo = 0, hi = keys.size();
    while (lo + 1 < hi) {
        const size_t mid = (lo + hi) / 2;
        if (keys[mid].time <= timeTicks) lo = mid; else hi = mid;
    }
    const AnimKey& a = keys[lo];
    const AnimKey& b = keys[lo + 1];
    const float dt = b.time - a.time;
    const float t = dt > 0.0f ? (timeTicks - a.time) / dt : 0.0f;
    AnimKey out;
    out.time = timeTicks;
    out.position = glm::mix(a.position, b.position, t);
    out.scale = glm::mix(a.scale, b.scale, t);
    const float d = glm::dot(a.rotation, b.rotation);
    out.rotation = std::abs(d) > 0.9995f ? glm::normalize(glm::mix(a.rotation, b.rotation, t))
                                         : glm::slerp(a.rotation, b.rotation, t);
    return out;
}

// 采样：最近关键帧插值 TRS → 局部矩阵 → 全局（父链递归）× inverseBind → 骨骼矩阵数组
// （§25.7 行为 9.1；骨骼数 = skeleton.bones.size()，上限 64 由 extractSkeleton 保证）
inline void sampleBoneMatrices(const Skeleton& skeleton, const AnimationClip& clip,
                               float timeSeconds, std::vector<glm::mat4>& out) {
    const size_t n = skeleton.bones.size();
    out.assign(n, glm::mat4(1.0f));   // 默认恒等 = 绑定姿势（T-pose）
    if (n == 0 || clip.channels.empty() || clip.ticksPerSecond <= 0.0f) return;
    const float timeTicks = timeSeconds * clip.ticksPerSecond;

    // 通道 → 局部矩阵（无通道覆盖的骨骼保持恒等局部）
    std::vector<glm::mat4> locals(n, glm::mat4(1.0f));
    for (const AnimChannel& channel : clip.channels) {
        int idx = -1;
        for (size_t i = 0; i < n; ++i)
            if (skeleton.bones[i].name == channel.boneName) { idx = static_cast<int>(i); break; }
        if (idx < 0 || channel.keys.empty()) continue;
        const AnimKey key = sampleKey(channel, timeTicks);
        locals[idx] = glm::translate(glm::mat4(1.0f), key.position) *
                      glm::mat4_cast(key.rotation) *
                      glm::scale(glm::mat4(1.0f), key.scale);
    }

    // 全局矩阵：父先子后递归（骨骼表顺序不保证拓扑序；环由 resolved 防死循环）
    std::vector<glm::mat4> globals(n);
    std::vector<bool> resolved(n, false);
    std::function<glm::mat4(size_t)> resolveGlobal = [&](size_t i) -> glm::mat4 {
        if (resolved[i]) return globals[i];
        resolved[i] = true;   // 先标记防环（父环时退化为局部）
        const int parent = skeleton.bones[i].parent;
        if (parent >= 0 && static_cast<size_t>(parent) < n && parent != static_cast<int>(i))
            globals[i] = resolveGlobal(static_cast<size_t>(parent)) * locals[i];
        else
            globals[i] = locals[i];
        return globals[i];
    };
    for (size_t i = 0; i < n; ++i) resolveGlobal(i);
    for (size_t i = 0; i < n; ++i) out[i] = globals[i] * skeleton.bones[i].inverseBind;
}

// 两套骨骼矩阵线性混合（过渡混合，§25.7：duration 线性混合两状态骨骼矩阵）
inline void blendBoneMatrices(const std::vector<glm::mat4>& a, const std::vector<glm::mat4>& b,
                              float alpha, std::vector<glm::mat4>& out) {
    const size_t n = std::min(a.size(), b.size());
    out.resize(n);
    const float w = std::clamp(alpha, 0.0f, 1.0f);
    for (size_t i = 0; i < n; ++i) out[i] = a[i] * (1.0f - w) + b[i] * w;
}

// ===== 状态机判定（纯函数；Animator::onUpdate 与自测共用）=====

// 单条条件判定（参数按名匹配；未知参数名 → false——warn 由 Animator 负责）
inline bool evaluateCondition(const AnimatorTransition::Condition& cond,
                              const std::vector<AnimatorParameter>& parameters) {
    for (const AnimatorParameter& p : parameters) {
        if (p.name != cond.parameter) continue;
        switch (cond.op) {
            case AnimatorTransition::Condition::Op::Greater:  return p.value > cond.value;
            case AnimatorTransition::Condition::Op::Less:     return p.value < cond.value;
            case AnimatorTransition::Condition::Op::Equals:   return std::abs(p.value - cond.value) < 1e-5f;
            case AnimatorTransition::Condition::Op::BoolTrue: return p.value != 0.0f;
        }
        return false;
    }
    return false;   // 未知参数
}

// 当前状态出边中条件全部满足的第一条 → 返回下标；无 → -1。
// 空条件集不触发（防误配瞬切）；from/to 越界与未知参数视为不满足。
inline int evaluateTransition(const std::vector<AnimatorTransition>& transitions,
                              const std::vector<AnimatorParameter>& parameters,
                              int currentState) {
    for (size_t i = 0; i < transitions.size(); ++i) {
        const AnimatorTransition& t = transitions[i];
        if (t.from != currentState || t.conditions.empty()) continue;
        bool all = true;
        for (const AnimatorTransition::Condition& c : t.conditions) {
            bool known = false;
            for (const AnimatorParameter& p : parameters)
                if (p.name == c.parameter) { known = true; break; }
            if (!known || !evaluateCondition(c, parameters)) { all = false; break; }
        }
        if (all) return static_cast<int>(i);
    }
    return -1;
}

// 剪辑播放时间推进（秒）：loop 环绕；非 loop 停在末尾；duration <= 0 停在 0
inline float advanceClipTime(float time, float dt, const AnimationClip& clip, bool loop) {
    const float durSec = clip.ticksPerSecond > 0.0f ? clip.duration / clip.ticksPerSecond : 0.0f;
    if (durSec <= 0.0f) return 0.0f;
    float t = time + dt;
    if (loop) {
        t = std::fmod(t, durSec);
        if (t < 0.0f) t += durSec;
    } else {
        t = std::min(t, durSec);
    }
    return t;
}

}  // namespace AnimationInternals
}  // namespace NovaEngine
