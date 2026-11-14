#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/vec3.hpp>
#include <NovaEngine/Render/SkyboxRenderer.h>
#include "GameObject.h"

namespace NovaEngine {

class Scene {
public:
    explicit Scene(const std::string& name = "Untitled");
    const std::string& name() const;
    void setName(const std::string& name);

    // 对象管理
    GameObject* createGameObject(const std::string& name = "GameObject",
                                 GameObject* parent = nullptr);    // 自动附带 Transform
    GameObject* instantiate(uint64_t id, const std::string& name); // 反序列化用：不自动加 Transform
    void destroyGameObject(uint64_t id);       // 入销毁队列
    void flushDestroyQueue();                  // Edit/Play 每帧都调用
    GameObject* findGameObject(uint64_t id) const;
    std::vector<GameObject*> findGameObjectsByName(const std::string& name) const;
    std::vector<GameObject*> rootObjects() const;
    void forEachObject(const std::function<void(GameObject*)>& fn) const;  // 深度优先
    template<typename T>
    std::vector<T*> findComponents() const {   // 遍历所有 GO 过滤（含派生类）
        std::vector<T*> out;
        forEachObject([&out](GameObject* go) {
            for (auto& c : go->m_components)
                if (auto* p = dynamic_cast<T*>(c.get()))
                    out.push_back(p);
        });
        return out;
    }
    void update(float dt);      // Play 态：flush + 深度优先广播 onUpdate
    void start();               // Play 开始时调用一次：DFS 广播 onStart（跳过非激活子树与 enabled=false 的 Behaviour）

    // 环境（SceneSerializer 直接读写）
    glm::vec3 skyColor = glm::vec3(0.18f, 0.22f, 0.35f);
    glm::vec3 ambientColor = glm::vec3(0.15f, 0.15f, 0.18f);
    bool showGrid = true;

    // 天空（V0.2-7；SceneSerializer 直接读写）
    SkyMode skyMode = SkyMode::Gradient;                          // 天空模式
    glm::vec3 skyHorizonColor = glm::vec3(0.55f, 0.62f, 0.72f);   // 渐变地平线色
    glm::vec3 skyZenithColor = glm::vec3(0.22f, 0.30f, 0.48f);    // 渐变天顶色
    std::string skyCubemapPath;                                   // 空=渐变回退；否则 6 面目录或 equirect 文件
    float skyRotation = 0.0f;                                     // cubemap 绕 +Y 旋转（度）
    float skyExposure = 1.0f;                                     // HDR 曝光

    // 编辑器相机状态（随场景保存/恢复；SceneSerializer 直接读写）
    glm::vec3 editorCameraTarget = glm::vec3(0.0f, 1.0f, 0.0f);
    float editorCameraDistance = 8.0f;
    float editorCameraYaw = 0.0f;
    float editorCameraPitch = -0.35f;

private:
    std::string m_name;
    std::vector<std::unique_ptr<GameObject>> m_rootObjects;
    std::unordered_map<uint64_t, GameObject*> m_objectById;   // O(1) 查找
    std::vector<uint64_t> m_destroyQueue;                     // 避免 update 中迭代器失效
    friend class GameObject;
};

}  // namespace NovaEngine
