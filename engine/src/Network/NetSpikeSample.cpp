// NetSpikeSample —— 网络多人预研 spike（V0.5-15，分支 spike/network-v1，不进 main）
#include <NovaEngine/Network/NetSpikeSample.h>

#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Network/NetCube.h>
#include <NovaEngine/Network/NetworkManager.h>
#include <NovaEngine/Render/Primitives.h>
#include <NovaEngine/Scene/Camera.h>
#include <NovaEngine/Scene/Canvas.h>
#include <NovaEngine/Scene/DirectionalLight.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/MeshRenderer.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/Text.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scripting/Behaviour.h>

#include <format>

namespace NovaEngine {
namespace {

// HUD 刷新脚本：每渲染帧把 NetworkManager 的锁步状态写进三个 Text
class NetHudBehaviour : public Behaviour {
public:
    explicit NetHudBehaviour(GameObject* owner) : Behaviour(owner) {}

    Text* title = nullptr;     // 角色 / 阶段
    Text* status = nullptr;    // 帧号 / 双方位置 / 停等
    Text* help = nullptr;      // 操作提示 / 校验和

    void onUpdate(float /*dt*/) override {
        const NetworkManager& net = NetworkManager::instance();
        const glm::vec3 hostPos = net.cubePosition(0);
        const glm::vec3 guestPos = net.cubePosition(1);

        if (title != nullptr)
            title->text = std::format("锁步帧同步 Spike | 角色: {} | 状态: {}", net.roleName(),
                                      net.phaseName());
        if (status != nullptr) {
            if (net.syncing()) {
                status->text = std::format("帧={} 主机方块({:.3f}, {:.3f}, {:.3f})  从机方块({:.3f}, {:.3f}, {:.3f})",
                                           net.simFrame(), hostPos.x, hostPos.y, hostPos.z,
                                           guestPos.x, guestPos.y, guestPos.z);
                if (net.stalled()) status->text += "  [等待远端输入…]";
            } else {
                status->text = "尚未开始模拟（按主机/从机连接顺序自动开始）";
            }
            status->color = net.stalled() ? glm::vec4(1.0f, 0.5f, 0.2f, 1.0f)
                                          : glm::vec4(0.8f, 0.95f, 0.8f, 1.0f);
        }
        if (help != nullptr) {
            std::string lag;
            if (net.simulatedLagMs() > 0) lag = std::format(" | 延迟注入 {}ms", net.simulatedLagMs());
            help->text = std::format("WASD/方向键移动己方方块 | 输入延迟 {} 帧 | 停等 {} 帧 | 校验 0x{:08X}{} | Esc 退出",
                                     net.delayFrames(), net.stallFrames(), net.syncChecksum(), lag);
        }
    }
};

GameObject* makeCube(Scene& scene, const char* name, const glm::vec3& pos, int playerIndex,
                     const glm::vec3& albedo) {
    GameObject* go = scene.createGameObject(name);
    go->transform()->setLocalPosition(pos);
    go->transform()->setLocalScale(glm::vec3(1.0f));
    auto* mr = go->addComponent<MeshRenderer>();
    mr->mesh = Primitives::cube();
    mr->material.roughness = 0.55f;
    mr->material.metallic = 0.0f;
    auto* cube = go->addComponent<NetCube>();
    cube->playerIndex = playerIndex;
    cube->albedo = albedo;
    return go;
}

Text* makeText(Scene& scene, GameObject* canvasGo, const char* name, float fontSize,
               const glm::vec2& anchor, const glm::vec2& offset, const glm::vec4& color) {
    GameObject* go = scene.createGameObject(name, canvasGo);
    auto* text = go->addComponent<Text>();
    text->fontSize = fontSize;
    text->anchor = anchor;
    text->offset = offset;
    text->color = color;
    return text;
}

}  // namespace

void buildNetSpikeScene(Scene& scene) {
    // 主相机：斜俯视方块运动平面
    GameObject* camGo = scene.createGameObject("Main Camera");
    camGo->transform()->setLocalPosition(glm::vec3(0.0f, 7.0f, -11.0f));
    camGo->transform()->lookAt(glm::vec3(0.0f, 0.5f, 0.0f));
    auto* cam = camGo->addComponent<Camera>();
    cam->isMain = true;

    // 方向光（斜照出立体感）
    GameObject* lightGo = scene.createGameObject("Sun");
    lightGo->transform()->setLocalEulerAngles(glm::vec3(-50.0f, -35.0f, 0.0f));
    auto* light = lightGo->addComponent<DirectionalLight>();
    light->intensity = 1.1f;

    // 地面（纯视觉，无碰撞体——spike 不用物理）
    GameObject* groundGo = scene.createGameObject("Ground");
    auto* ground = groundGo->addComponent<MeshRenderer>();
    ground->mesh = Primitives::plane(60.0f);
    ground->material.albedo = glm::vec3(0.32f, 0.37f, 0.45f);
    ground->material.roughness = 0.95f;
    ground->material.metallic = 0.0f;

    // 两个同步方块：0=主机（红，左侧）1=从机（蓝，右侧）
    // 构建顺序与 playerIndex 一致 → 双方注册顺序一致（确定性前提）
    makeCube(scene, "HostCube", glm::vec3(-2.5f, 0.5f, 0.0f), 0, glm::vec3(0.9f, 0.30f, 0.30f));
    makeCube(scene, "GuestCube", glm::vec3(2.5f, 0.5f, 0.0f), 1, glm::vec3(0.30f, 0.55f, 0.90f));

    // HUD（Canvas + 三个 Text + 刷新脚本）
    GameObject* canvasGo = scene.createGameObject("NetHud");
    canvasGo->addComponent<Canvas>();
    Text* title = makeText(scene, canvasGo, "HudTitle", 26.0f, glm::vec2(0.5f, 0.03f),
                           glm::vec2(0.0f, 8.0f), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    Text* status = makeText(scene, canvasGo, "HudStatus", 22.0f, glm::vec2(0.5f, 0.10f),
                            glm::vec2(0.0f, 0.0f), glm::vec4(0.8f, 0.95f, 0.8f, 1.0f));
    Text* help = makeText(scene, canvasGo, "HudHelp", 18.0f, glm::vec2(0.5f, 0.94f),
                          glm::vec2(0.0f, 0.0f), glm::vec4(0.75f, 0.8f, 0.85f, 1.0f));
    GameObject* hudGo = scene.createGameObject("NetHudUpdater");
    auto* hud = hudGo->addComponent<NetHudBehaviour>();
    hud->title = title;
    hud->status = status;
    hud->help = help;

    Logger::info("[NetSpike] 锁步样例场景构建完成（两方块 + HUD）");
}

}  // namespace NovaEngine
