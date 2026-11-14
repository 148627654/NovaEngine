#pragma once
#include <memory>
#include <string>
#include <nlohmann/json_fwd.hpp>
#include <NovaEngine/Render/Material.h>
#include <NovaEngine/Render/Mesh.h>
#include "Component.h"

namespace NovaEngine {

class MeshRenderer : public Component {
public:
    explicit MeshRenderer(GameObject* owner);

    std::shared_ptr<Mesh> mesh = nullptr;    // 运行时 GPU 网格
    std::string meshPath;                    // 资产相对路径（如 "assets/models/cube.obj"，序列化用）
    Material material;

    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;   // 经 AssetDatabase::getMesh 解析
};

}  // namespace NovaEngine
