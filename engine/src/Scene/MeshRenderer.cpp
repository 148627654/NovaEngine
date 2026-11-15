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
    // material 段序列化抽到 SerializationInternals（键名与 V0.2-8 schema 逐字节一致）
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

    const auto& mat = in.value("material", nlohmann::json::object());
    SerializationInternals::readMaterialJson(mat, material);
}

}  // namespace NovaEngine

// 组件静态注册（V0.3 波 0；注册名经 ComponentRegistry 裁剪为 "MeshRenderer"）
NE_REGISTER_COMPONENT(NovaEngine::MeshRenderer)
