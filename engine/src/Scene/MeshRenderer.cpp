#include <NovaEngine/Scene/MeshRenderer.h>
#include <NovaEngine/Scene/ComponentRegistry.h>
#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Logger.h>
#include <nlohmann/json.hpp>

#include "../Serialization/SerializationInternals.h"

namespace NovaEngine {

MeshRenderer::MeshRenderer(GameObject* owner) : Component(owner) {}

void MeshRenderer::onSerialize(nlohmann::json& out) const {
    out["mesh"] = meshPath;
    // material 段序列化走 SerializationInternals 共享辅助：
    // V0.3-6 双轨——materialAssetPath 非空时只写 "materialAsset" 并省略内联 material 段
    nlohmann::json mat;
    SerializationInternals::writeMaterialJson(mat, material);
    out["material"] = std::move(mat);
}

void MeshRenderer::onDeserialize(const nlohmann::json& in) {
    // mesh 路径经 AssetDatabase 解析（Coder C 的实现，此处只调用冻结头）
    meshPath = in.value("mesh", std::string());
    if (!meshPath.empty()) {
        mesh = AssetDatabase::instance().getMesh(std::filesystem::path(meshPath));
        if (!mesh)
            Logger::warn("MeshRenderer 加载网格失败：{}（保持空网格，不崩溃）", meshPath);
    } else {
        mesh = nullptr;
    }

    // material 段：走共享辅助（路径/标量字段 + 运行时纹理解析，albedo sRGB、其余线性）
    nlohmann::json mat = nlohmann::json::object();
    if (in.contains("material") && in["material"].is_object())
        mat = in["material"];
    SerializationInternals::readMaterialJson(mat, material);

    // V0.3-6 材质库双轨：materialAssetPath 非空 = 整块引用（渲染以资产材质为准，提交路径由
    // SceneRenderer 解析）；资产加载失败 warn 并回退内联材质（readMaterialJson 已读入内联值兜底）
    if (!material.materialAssetPath.empty()) {
        material.materialAsset =
            AssetDatabase::instance().getMaterialAsset(std::filesystem::path(material.materialAssetPath));
        if (!material.materialAsset)
            Logger::warn("MeshRenderer 加载材质资产失败：{}（回退内联材质）", material.materialAssetPath);
    } else {
        material.materialAsset = nullptr;
    }
}

}  // namespace NovaEngine

// 组件静态注册（V0.3 波 0；注册名经 ComponentRegistry 裁剪为 "MeshRenderer"）
NE_REGISTER_COMPONENT(NovaEngine::MeshRenderer)
