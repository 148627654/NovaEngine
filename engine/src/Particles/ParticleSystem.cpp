// ParticleSystem.cpp —— 粒子系统组件实现（V0.5-6，c4）：
// CPU 粒子池模拟（发射/积分/死亡回收）+ Edit 态稳态快照（按参数确定性重建、时间冻结）。
// 序列化键名逐字遵循 §25.4 契约（缺省兜底、未知键忽略、schema version 4 不变）。
#include <NovaEngine/Particles/ParticleSystem.h>

#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/ComponentRegistry.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Transform.h>

#include <nlohmann/json.hpp>

#include <glm/common.hpp>          // mix
#include <glm/geometric.hpp>       // normalize / cross
#include <glm/gtc/constants.hpp>   // pi

#include <algorithm>
#include <cmath>

namespace NovaEngine {

namespace {

// 发射圆锥半角（v1 固定 30°；无序列化字段——§25.4 冻结头未含形状参数）
constexpr float kConeHalfAngle = 0.5235988f;   // radians(30°)

// 32 位 LCG（模拟用伪随机；不追求统计质量，分布够均匀即可）
uint32_t lcgNext(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return state;
}

// [0,1) 均匀浮点（取高 24 位，低位循环性不影响均匀度）
float lcgUnit(uint32_t& state) {
    return static_cast<float>(lcgNext(state) >> 8) / 16777216.0f;
}

// GO id → 稳定种子：Edit 稳态快照帧间确定性（同场景同参数同画面，无闪烁）
uint32_t stableSeed(uint64_t id) {
    uint32_t s = static_cast<uint32_t>(id ^ (id >> 32));
    s ^= s >> 16;
    s *= 0x7feb352du;
    s ^= s >> 15;
    return s ? s : 0x12345678u;
}

// 圆锥内均匀方向：axis 为单位轴；u1/u2 ∈ [0,1)。半径按 sqrt(u1) 保证盘面均匀
glm::vec3 coneDirection(const glm::vec3& axis, float u1, float u2) {
    const glm::vec3 helper =
        std::abs(axis.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 b1 = glm::normalize(glm::cross(axis, helper));
    const glm::vec3 b2 = glm::cross(axis, b1);
    const float radius = std::tan(kConeHalfAngle) * std::sqrt(u1);
    const float azimuth = 2.0f * glm::pi<float>() * u2;
    return glm::normalize(axis + b1 * (radius * std::cos(azimuth)) +
                          b2 * (radius * std::sin(azimuth)));
}

glm::vec4 jsonToVec4(const nlohmann::json& j, const glm::vec4& fallback) {
    if (!j.is_array() || j.size() < 4)
        return fallback;
    try {
        return glm::vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
    } catch (const std::exception&) {
        return fallback;
    }
}

}  // namespace

ParticleSystem::ParticleSystem(GameObject* owner) : Component(owner) {
    // 实时模拟 RNG：按 GO id 播种（同一场景内各系统序列独立）；
    // Edit 稳态快照在 buildSteadyState 内另用固定种子，两者互不干扰
    m_rngState = stableSeed(owner ? owner->id() : 0u) ^ 0x9e3779b9u;
}

// ===== 运行期控制（§25.4）=====

void ParticleSystem::play() {
    // 从当前状态（重）开始发射：发射计时/积压清零——loop=false 时 play() 重播一遍
    m_playing = true;
    m_playTime = 0.0f;
    m_emitAccumulator = 0.0f;
}

void ParticleSystem::stop() {
    // 停止发射；已存活粒子继续推进直至自然消亡（不立即清空，便于「衰减余烬」表现）
    m_playing = false;
}

void ParticleSystem::clear() {
    m_particles.clear();
    m_emitAccumulator = 0.0f;
}

bool ParticleSystem::isPlaying() const {
    return m_playing;   // 语义 = 发射中（不是「还有粒子」；两者在 loop=false 播完后区分）
}

uint32_t ParticleSystem::aliveCount() const {
    return static_cast<uint32_t>(m_particles.size());
}

// ===== 生命周期（Play 态）=====

void ParticleSystem::onStart() {
    // 进入 Play：标记模拟态（渲染改走实时池——即使 playOnAwake=false 空池不绘制），
    // playOnAwake → play()。onStart 只对 Play 入口时已存在的组件广播。
    m_started = true;
    m_simulated = true;   // 进入 Play 模拟态（渲染改走实时池）
    if (playOnAwake)
        play();
}

void ParticleSystem::onUpdate(float dt) {
    // Play 每帧广播（Scene::update；Edit 态不调用）。含 stop 后——残留粒子继续消亡。
    m_simulated = true;   // Play 每帧确认模拟态（Edit 态无 onUpdate 广播）
    if (!m_started) {
        // Play 中挂载的组件收不到 onStart：按 playOnAwake 补播一次
        m_started = true;
        if (playOnAwake)
            play();
    }
    // 长帧钳制（挂起恢复/断点等）：防一次发射与积分爆炸
    advance(std::clamp(dt, 0.0f, 0.25f));
}

void ParticleSystem::advance(float dt) {
    // 1. 发射（m_playing；loop=false 发射 lifetime 秒后自动停发——「播一遍」语义，
    //    总量 emissionRate×lifetime 与 Edit 稳态快照一致）
    if (m_playing) {
        m_playTime += dt;
        if (!loop && m_playTime >= lifetime) {
            m_playing = false;
            Logger::info("[ParticleSystem] \"{}\" loop=false 播完停发（剩余 {} 粒子）",
                         gameObject() ? gameObject()->name() : "?", aliveCount());
        }
    }
    if (m_playing && emissionRate > 0.0f) {
        m_emitAccumulator += emissionRate * dt;
        const Transform* t = transform();
        const glm::vec3 origin = t ? t->worldPosition() : glm::vec3(0.0f);
        const glm::vec3 axis = t ? t->up() : glm::vec3(0.0f, 1.0f, 0.0f);
        const int cap = std::max(1, maxParticles);
        while (m_emitAccumulator >= 1.0f && static_cast<int>(m_particles.size()) < cap) {
            m_emitAccumulator -= 1.0f;
            spawnParticle(origin, axis);
        }
        if (static_cast<int>(m_particles.size()) >= cap)
            m_emitAccumulator = 0.0f;   // 打满丢弃积压（防腾出空位后瞬时爆发）
    }

    // 2. 积分 + 死亡回收（swap-remove 保序无关；含 stop 后残留粒子的自然消亡）
    const glm::vec3 gravityAccel(0.0f, -gravity, 0.0f);
    for (size_t i = 0; i < m_particles.size();) {
        Particle& p = m_particles[i];
        p.age += dt;
        if (p.age >= p.lifetime) {
            p = m_particles.back();
            m_particles.pop_back();
            continue;
        }
        p.velocity += gravityAccel * dt;
        p.position += p.velocity * dt;
        ++i;
    }
}

void ParticleSystem::spawnParticle(const glm::vec3& origin, const glm::vec3& axis) {
    Particle p;
    const glm::vec3 dir = coneDirection(axis, lcgUnit(m_rngState), lcgUnit(m_rngState));
    p.velocity = dir * (initialSpeed * (0.6f + 0.4f * lcgUnit(m_rngState)));
    p.position = origin;
    p.age = 0.0f;
    p.lifetime = std::max(lifetime, 0.01f) * (0.8f + 0.4f * lcgUnit(m_rngState));
    m_particles.push_back(p);
}

// ===== Edit 稳态快照（渲染器经 collectInstances 消费）=====

std::vector<ParticleSystem::Particle> ParticleSystem::buildSteadyState() const {
    std::vector<Particle> out;
    if (emissionRate <= 0.0f || lifetime <= 0.0f)
        return out;
    const int count = std::min(std::max(1, maxParticles),
                               static_cast<int>(std::ceil(emissionRate * lifetime)));
    out.reserve(static_cast<size_t>(count));

    const Transform* t = transform();
    const glm::vec3 origin = t ? t->worldPosition() : glm::vec3(0.0f);
    const glm::vec3 axis = t ? t->up() : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 gravityAccel(0.0f, -gravity, 0.0f);

    // 固定种子 = GO id：帧间确定性（无闪烁）；参数变更当帧生效（每帧全量重建）
    uint32_t rng = stableSeed(gameObject() ? gameObject()->id() : 0u);
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.lifetime = lifetime * (0.8f + 0.4f * lcgUnit(rng));
        p.age = lcgUnit(rng) * p.lifetime;
        const glm::vec3 dir = coneDirection(axis, lcgUnit(rng), lcgUnit(rng));
        const glm::vec3 v0 = dir * (initialSpeed * (0.6f + 0.4f * lcgUnit(rng)));
        // 与 advance 同一运动学：p = p0 + v0·t + ½g·t²（时间冻结的稳态分布）
        p.position = origin + v0 * p.age + gravityAccel * (0.5f * p.age * p.age);
        p.velocity = v0 + gravityAccel * p.age;
        out.push_back(p);
    }
    return out;
}

ParticleInstance ParticleSystem::instanceFrom(const Particle& p) const {
    const float t = p.lifetime > 0.0f
        ? std::clamp(p.age / p.lifetime, 0.0f, 1.0f)
        : 1.0f;
    const float tiles = static_cast<float>(std::max(tileX, 1) * std::max(tileY, 1));
    ParticleInstance inst;
    inst.position = p.position;
    inst.color = glm::mix(startColor, endColor, t);
    inst.size = glm::mix(startSize, endSize, t);
    inst.tile = std::min(std::floor(t * tiles), tiles - 1.0f);   // 纹理条按序播放
    return inst;
}

void ParticleSystem::collectInstances(std::vector<ParticleInstance>& out) const {
    if (!m_simulated) {
        // Edit 态（或 Play 首帧前）：稳态快照——每帧按当前参数确定性重建、时间冻结
        const std::vector<Particle> steady = buildSteadyState();
        out.reserve(out.size() + steady.size());
        for (const Particle& p : steady)
            out.push_back(instanceFrom(p));
        return;
    }
    out.reserve(out.size() + m_particles.size());
    for (const Particle& p : m_particles)
        out.push_back(instanceFrom(p));
}

// ===== 序列化（键名逐字遵循 §25.4；缺省兜底；未知键忽略）=====

void ParticleSystem::onSerialize(nlohmann::json& out) const {
    out["emissionRate"] = emissionRate;
    out["lifetime"] = lifetime;
    out["initialSpeed"] = initialSpeed;
    out["gravity"] = gravity;
    out["startColor"] = nlohmann::json::array({startColor.x, startColor.y, startColor.z, startColor.w});
    out["endColor"] = nlohmann::json::array({endColor.x, endColor.y, endColor.z, endColor.w});
    out["startSize"] = startSize;
    out["endSize"] = endSize;
    out["texturePath"] = texturePath;
    out["tileX"] = tileX;
    out["tileY"] = tileY;
    out["maxParticles"] = maxParticles;
    out["loop"] = loop;
    out["playOnAwake"] = playOnAwake;
    out["billboard"] = billboard;
    out["blendAdditive"] = blendAdditive;
}

void ParticleSystem::onDeserialize(const nlohmann::json& in) {
    emissionRate = in.value("emissionRate", 30.0f);
    lifetime = in.value("lifetime", 1.0f);
    initialSpeed = in.value("initialSpeed", 2.0f);
    gravity = in.value("gravity", 0.0f);
    startColor = jsonToVec4(in.value("startColor", nlohmann::json()), glm::vec4(1.0f));
    endColor = jsonToVec4(in.value("endColor", nlohmann::json()), glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    startSize = in.value("startSize", 0.2f);
    endSize = in.value("endSize", 0.05f);
    texturePath = in.value("texturePath", std::string());
    tileX = in.value("tileX", 1);
    tileY = in.value("tileY", 1);
    maxParticles = in.value("maxParticles", 1000);
    loop = in.value("loop", true);
    playOnAwake = in.value("playOnAwake", true);
    billboard = in.value("billboard", true);
    blendAdditive = in.value("blendAdditive", false);

    // 反序列化 = 全新实例（场景加载 / 退出 Play 快照还原）：模拟状态清零，
    // 渲染回到 Edit 稳态快照路径（§25.4 验收 4「退出 Play 粒子清零」）
    m_particles.clear();
    m_playing = false;
    m_simulated = false;
    m_started = false;
    m_emitAccumulator = 0.0f;
    m_playTime = 0.0f;

    // 非法值防御钳制（契约缺省兜底之外的稳健性；不影响合法数据往返）
    lifetime = std::max(lifetime, 0.01f);
    maxParticles = std::max(maxParticles, 1);
    tileX = std::max(tileX, 1);
    tileY = std::max(tileY, 1);
}

}  // namespace NovaEngine

// 组件静态注册（§25.0 纪律 4；注册名 "ParticleSystem"）
NE_REGISTER_COMPONENT(NovaEngine::ParticleSystem)
