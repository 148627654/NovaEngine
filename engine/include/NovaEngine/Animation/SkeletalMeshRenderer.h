#pragma once
#include <memory>
#include <string>
#include <nlohmann/json_fwd.hpp>
#include <NovaEngine/Render/Material.h>
#include <NovaEngine/Render/Mesh.h>
#include <NovaEngine/Render/MeshData.h>   // Skeleton（蒙皮数据，V0.5-9）
#include <NovaEngine/Scene/Component.h>

namespace NovaEngine {

class SkeletalMeshRenderer : public Component {
public:
    explicit SkeletalMeshRenderer(GameObject* owner);
    std::shared_ptr<Mesh> mesh = nullptr;      // 蒙皮布局 GPU 网格
    std::string meshPath;                      // 资产相对路径
    Material material;
    Skeleton skeleton;                         // 反序列化自 mesh 缓存
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

}  // namespace NovaEngine
