// SkeletalMeshRenderer.cpp —— 骨架日桩（V0.5-9）：接口冻结于 §25.7；蒙皮网格上传/资产解析实现归 c7（独占）。
// material 段复用 SerializationInternals 共享辅助（与 MeshRenderer 同 schema）。
#include <NovaEngine/Animation/SkeletalMeshRenderer.h>

#include <NovaEngine/Scene/ComponentRegistry.h>

#include <nlohmann/json.hpp>

#include "../Serialization/SerializationInternals.h"

namespace NovaEngine {

SkeletalMeshRenderer::SkeletalMeshRenderer(GameObject* owner) : Component(owner) {}

void SkeletalMeshRenderer::onSerialize(nlohmann::json& out) const {
    out["mesh"] = meshPath;
    nlohmann::json mat;
    SerializationInternals::writeMaterialJson(mat, material);
    out["material"] = std::move(mat);
}

void SkeletalMeshRenderer::onDeserialize(const nlohmann::json& in) {
    meshPath = in.value("mesh", std::string());
    // 骨架日桩：mesh/skeleton 的 AssetDatabase 解析与蒙皮上传归 c7 实现
    mesh = nullptr;
    skeleton = Skeleton{};
    nlohmann::json mat = nlohmann::json::object();
    if (in.contains("material") && in["material"].is_object())
        mat = in["material"];
    SerializationInternals::readMaterialJson(mat, material);
}

}  // namespace NovaEngine

// 组件静态注册（§25.0 纪律 4；注册名 "SkeletalMeshRenderer"）
NE_REGISTER_COMPONENT(NovaEngine::SkeletalMeshRenderer)
