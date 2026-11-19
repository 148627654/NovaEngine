// SkeletalMeshRenderer.cpp —— 蒙皮网格渲染组件（V0.5-9，c7）：
// 反序列化时解析 meshPath → 骨架 + 每实例自建 GPU Mesh（独立骨骼 UBO + 指针唯一防
// 实例化分组，§25.7 合波裁定 A）；渲染收集走 SceneRenderer 的 SMR 分支（c0 裁定 A 布，
// 与 MeshRenderer 同路径 submit/剔除/阴影）。
#include <NovaEngine/Animation/SkeletalMeshRenderer.h>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/ComponentRegistry.h>

#include <nlohmann/json.hpp>

#include "../Serialization/SerializationInternals.h"
#include "AnimationInternals.h"

namespace NovaEngine {

namespace {

// 相对资产路径 → 绝对路径（编辑器/Player 启动时 cwd 已切项目根；AssetDatabase 存有根目录）
std::filesystem::path resolveAssetAbs(const std::string& relativePath) {
    std::filesystem::path p(relativePath);
    if (p.is_absolute()) return p;
    const std::filesystem::path root = AssetDatabase::projectRoot();
    return root.empty() ? std::filesystem::absolute(p) : root / p;
}

// 解析蒙皮网格 + 骨架：SMR 每个实例自建 GPU Mesh（不共享 AssetDatabase 缓存网格）。
// 无蒙皮数据 → 回退第一个静态子网格（同样经 SMR 分支提交；骨架空 → Animator 判定 no-op）。
void resolveSkinnedMesh(SkeletalMeshRenderer& smr) {
    smr.mesh = nullptr;
    smr.skeleton = Skeleton{};
    if (smr.meshPath.empty()) return;

    const std::filesystem::path abs = resolveAssetAbs(smr.meshPath);
    const auto model = AnimationInternals::loadSkinnedModel(abs.string());
    if (!model || !model->loaded) {
        Logger::warn("SkeletalMeshRenderer: 模型加载失败 \"{}\"", smr.meshPath);
        return;
    }
    if (const MeshData* skinned = model->firstSkinned()) {
        smr.mesh = Mesh::create(*skinned);
        smr.skeleton = skinned->skeleton;
        if (!smr.mesh) {
            Logger::warn("SkeletalMeshRenderer: 蒙皮网格上传失败 \"{}\"", smr.meshPath);
            return;
        }
        Logger::info("SkeletalMeshRenderer: \"{}\" 加载成功（{} 骨，{} 剪辑）", smr.meshPath,
                     skinned->skeleton.bones.size(), skinned->clips.size());
        return;
    }
    // 模型存在但无蒙皮数据：静态回退网格（T-pose 语义 = 原始顶点；无骨架动画）
    if (!model->meshes.empty()) {
        Logger::warn("SkeletalMeshRenderer: \"{}\" 无蒙皮数据，回退静态网格（骨架空，无动画）",
                     smr.meshPath);
        smr.mesh = Mesh::create(model->meshes.front());
    }
}

}  // namespace

SkeletalMeshRenderer::SkeletalMeshRenderer(GameObject* owner) : Component(owner) {}

void SkeletalMeshRenderer::onSerialize(nlohmann::json& out) const {
    out["mesh"] = meshPath;
    nlohmann::json mat;
    SerializationInternals::writeMaterialJson(mat, material);
    out["material"] = std::move(mat);
}

void SkeletalMeshRenderer::onDeserialize(const nlohmann::json& in) {
    // 安全读取：键存在但类型不符时 in.value 会抛 type_error（畸形文件不崩溃约定）
    meshPath.clear();
    if (in.is_object() && in.contains("mesh") && in["mesh"].is_string())
        meshPath = in["mesh"].get<std::string>();
    nlohmann::json mat = nlohmann::json::object();
    if (in.contains("material") && in["material"].is_object())
        mat = in["material"];
    SerializationInternals::readMaterialJson(mat, material);

    // c7：mesh/skeleton 解析与蒙皮上传（反序列化时 GL 上下文已存活）
    resolveSkinnedMesh(*this);
}

}  // namespace NovaEngine

// 组件静态注册（§25.0 纪律 4；注册名 "SkeletalMeshRenderer"）
NE_REGISTER_COMPONENT(NovaEngine::SkeletalMeshRenderer)
