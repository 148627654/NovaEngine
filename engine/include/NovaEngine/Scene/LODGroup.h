#pragma once
#include <memory>
#include <string>
#include <vector>
#include <glm/vec3.hpp>
#include <nlohmann/json_fwd.hpp>
#include <NovaEngine/Render/Mesh.h>
#include "Component.h"

namespace NovaEngine {

class GameObject;

// LOD 档位描述（V0.5-3）
struct LODLevel {
    std::string meshPath;               // 资产相对路径；反序列化经 AssetDatabase 解析
    float distance = 0.0f;              // 相机距离 >= distance 时该档可用（升序；LOD0 = 0）
    std::shared_ptr<Mesh> mesh = nullptr;   // 运行时（不序列化）
};

// LOD 组件：挂在带 MeshRenderer 的 GO 上，渲染遍历按相机距离选 mesh。
// 与 MeshRenderer 的关系：enabled 且 lods 非空时，遍历收集用 resolveMeshFor 结果
// 替代 mr->mesh（阴影/剔除/实例化分组全部使用同一替代结果）；
// 无 LODGroup / disabled / lods 空 → 原 MeshRenderer.mesh 路径零变化。
class LODGroup : public Component {
public:
    explicit LODGroup(GameObject* owner);
    bool enabled = true;
    std::vector<LODLevel> lods;         // v1 上限 3 档（0/1/2；距离升序）
    bool crossFade = false;             // v1 预留（dither 过渡本波不实现；序列化保留字段）

    // 冻结入口（渲染遍历扩展点调用；SceneRenderer 不再有 LOD 分支逻辑）：
    // 按相机世界位置与 GO 世界位置距离选档；无可用档返回 nullptr（调用方回退 mr->mesh）
    static std::shared_ptr<Mesh> resolveMeshFor(GameObject* go, const glm::vec3& cameraPos);

    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

}  // namespace NovaEngine
