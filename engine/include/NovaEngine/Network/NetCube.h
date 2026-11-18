#pragma once
#include <glm/vec3.hpp>
#include <nlohmann/json_fwd.hpp>
#include <NovaEngine/Scene/Component.h>

// NetCube —— 网络多人预研 spike（V0.5-15，分支 spike/network-v1，不进 main）
// 锁步同步的方块：只负责「挂到 GameObject 上并注册进 NetworkManager」。
// 移动不在 onUpdate 里做（onUpdate 是变帧率，跨机不保证一致）——
// 位置由 NetworkManager 在固定步进 tick 内做确定性积分后直接写 Transform。
namespace NovaEngine {

class NetCube : public Component {
public:
    explicit NetCube(GameObject* owner);
    ~NetCube() override;

    int playerIndex = 0;                    // 0=主机方块 1=从机方块（两台机器场景一致）
    glm::vec3 albedo = glm::vec3(0.9f, 0.35f, 0.35f);
    float speed = 3.0f;                     // 米/秒（确定性积分用，双方场景配置一致）

    void onStart() override;                // 注册进 NetworkManager（移动开始生效）
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

}  // namespace NovaEngine
