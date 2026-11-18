// NetCube —— 网络多人预研 spike（V0.5-15，分支 spike/network-v1，不进 main）
#include <NovaEngine/Network/NetCube.h>

#include <NovaEngine/Network/NetworkManager.h>
#include <NovaEngine/Scene/ComponentRegistry.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/MeshRenderer.h>
#include <nlohmann/json.hpp>   // 头只 fwd 声明；完整类型在 .cpp（onSerialize/onDeserialize）

namespace NovaEngine {

NetCube::NetCube(GameObject* owner) : Component(owner) {}

NetCube::~NetCube() {
    // 场景销毁/组件移除时注销：NetworkManager 不再触碰本方块 Transform
    NetworkManager::instance().unregisterCube(this);
}

void NetCube::onStart() {
    // 方块外观（颜色取自组件配置；网格由样例场景构建器提供）
    if (auto* mr = gameObject()->getComponent<MeshRenderer>())
        mr->material.albedo = albedo;
    // 注册进锁步模拟：此后每个固定 tick 由 NetworkManager 确定性积分并写 Transform
    NetworkManager::instance().registerCube(this);
}

void NetCube::onSerialize(nlohmann::json& out) const {
    out["playerIndex"] = playerIndex;
    out["albedo"] = {albedo.x, albedo.y, albedo.z};
    out["speed"] = speed;
}

void NetCube::onDeserialize(const nlohmann::json& in) {
    if (in.contains("playerIndex") && in["playerIndex"].is_number_integer())
        playerIndex = in["playerIndex"].get<int>();
    if (in.contains("albedo") && in["albedo"].is_array() && in["albedo"].size() >= 3) {
        albedo.x = in["albedo"][0].get<float>();
        albedo.y = in["albedo"][1].get<float>();
        albedo.z = in["albedo"][2].get<float>();
    }
    if (in.contains("speed") && in["speed"].is_number())
        speed = in["speed"].get<float>();
}

}  // namespace NovaEngine

// 组件静态注册（注册名经 ComponentRegistry 裁剪为 "NetCube"）
NE_REGISTER_COMPONENT(NovaEngine::NetCube)
