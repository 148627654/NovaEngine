// ParticleSystem.cpp —— 骨架日桩（V0.5-6）：接口冻结于 §25.4；粒子模拟实现归 c4（本文件为 c4 独占）。
// 序列化按契约键名最小往返（缺省兜底；c4 实现模拟后保持键名不变）。
#include <NovaEngine/Particles/ParticleSystem.h>

#include <NovaEngine/Scene/ComponentRegistry.h>

#include <nlohmann/json.hpp>

namespace NovaEngine {

namespace {

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

ParticleSystem::ParticleSystem(GameObject* owner) : Component(owner) {}

void ParticleSystem::play() {}
void ParticleSystem::stop() {}
void ParticleSystem::clear() {}
bool ParticleSystem::isPlaying() const { return false; }
uint32_t ParticleSystem::aliveCount() const { return 0; }
void ParticleSystem::onUpdate(float dt) { (void)dt; }

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
}

}  // namespace NovaEngine

// 组件静态注册（§25.0 纪律 4；注册名 "ParticleSystem"）
NE_REGISTER_COMPONENT(NovaEngine::ParticleSystem)
