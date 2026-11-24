#include "EnvironmentPanel.h"

#include "../DragDrop.h"
#include "../EditorContext.h"
#include "../FileDialog.h"   // 浏览按钮（原生文件选择框）

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>   // InputText(std::string*) 包装（ImGui 1.90+ 移除内建重载）

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Render/PostProcess.h>
#include <NovaEngine/Render/SkyboxRenderer.h>
#include <NovaEngine/Scene/Scene.h>

#include <glm/gtc/type_ptr.hpp>

#include <filesystem>

namespace NovaEditor {

namespace {

// 外部 .cube 复制进 assets/luts/（重名自动加后缀），返回项目内相对路径；失败返回空
std::filesystem::path copyLutIntoProject(const std::filesystem::path& abs) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root = NovaEngine::AssetDatabase::projectRoot();
    // 已在项目内 → 直接取相对路径
    const fs::path rel = fs::relative(abs, root, ec);
    if (!ec && !rel.empty() && rel.native()[0] != '.')
        return rel;
    // 项目外 → 复制进 assets/luts/（目录不存在则创建）
    fs::path lutsDir = root / "assets" / "luts";
    fs::create_directories(lutsDir, ec);
    fs::path dest = lutsDir / abs.filename();
    for (int i = 1; fs::exists(dest, ec); ++i)
        dest = lutsDir / (abs.stem().string() + "_" + std::to_string(i) + abs.extension().string());
    ec.clear();
    if (!fs::copy_file(abs, dest, ec)) return {};
    return fs::relative(dest, root);
}

}  // namespace

EnvironmentPanel::EnvironmentPanel() : Panel("Environment") {}

void EnvironmentPanel::onGui(EditorContext& ctx, EditorCamera&) {
    using namespace NovaEngine;
    ImGui::Begin(m_name.c_str(), &open);
    Scene* scene = ctx.scene;
    if (!scene) {
        ImGui::End();
        return;
    }

    // 基础环境（V0.2-5）：天空色 / 环境光 / 网格开关 / 雾色（网格雾色参数化）
    if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("env");
        if (ImGui::ColorEdit3("Sky Color", glm::value_ptr(scene->skyColor))) ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        if (ImGui::ColorEdit3("Ambient Color", glm::value_ptr(scene->ambientColor)))
            ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        if (ImGui::Checkbox("Show Grid", &scene->showGrid)) ctx.markSceneDirty();
        if (ImGui::ColorEdit3("Fog Color", glm::value_ptr(scene->fogColor))) ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        // 雾色一键回落到天空色（V0.2-5 联动语义）
        if (ImGui::Button("Reset Fog to Sky")) {
            scene->fogColor = scene->skyColor;
            ctx.markSceneDirty();
        }
        ImGui::PopID();
    }

    // 后处理（V0.3-14 波 3）：enabled/bloomThreshold/bloomIntensity/bloomLevels/exposure
    // → scene->postProcess（序列化 environment.postProcess 段）；脏标记漏斗与既有约定一致
    if (ImGui::CollapsingHeader("Post Processing", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("pp");
        NovaEngine::PostProcessSettings& pp = scene->postProcess;
        if (ImGui::Checkbox("Enabled", &pp.enabled)) ctx.markSceneDirty();
        if (ImGui::DragFloat("Bloom Threshold", &pp.bloomThreshold, 0.01f, 0.0f, 10.0f))
            ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        if (ImGui::DragFloat("Bloom Intensity", &pp.bloomIntensity, 0.01f, 0.0f, 5.0f))
            ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        // P2-2 修复：上限 3 与引擎钳制一致（PostProcess 内部 clamp 1-3）
        if (ImGui::DragInt("Bloom Levels", &pp.bloomLevels, 1.0f, 1, 3)) ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        if (ImGui::DragFloat("Exposure", &pp.exposure, 0.01f, 0.01f, 10.0f)) ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        // ===== V0.5-5 SSAO（波 2 合波接线）：开关 + 半径/强度/采样数（采样数就近钳位 8/16/32）=====
        ImGui::Separator();
        if (ImGui::Checkbox("SSAO", &pp.ssaoEnabled)) ctx.markSceneDirty();
        if (ImGui::DragFloat("SSAO Radius", &pp.ssaoRadius, 0.01f, 0.05f, 5.0f)) ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        if (ImGui::DragFloat("SSAO Strength", &pp.ssaoStrength, 0.01f, 0.0f, 5.0f)) ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        {
            const char* ssaoSamples[] = {"8", "16", "32"};
            int ssaoIdx = pp.ssaoSampleCount <= 8 ? 0 : (pp.ssaoSampleCount <= 16 ? 1 : 2);
            if (ImGui::Combo("SSAO Samples", &ssaoIdx, ssaoSamples, 3)) {
                pp.ssaoSampleCount = (ssaoIdx == 0) ? 8 : (ssaoIdx == 1 ? 16 : 32);
                ctx.markSceneDirty();
            }
        }
        // 8bit 降级提示（§14.10：老集显无 RGB16F → 后处理运行期失效，不崩溃）
        if (!NovaEngine::PostProcess::hdrSupported())
            ImGui::TextDisabled("(HDR not supported on this GPU: post-processing disabled at runtime)");
        ImGui::PopID();
    }

    // ===== V1.0-4 色彩分级（§27.3）：LUT / DOF / 渐晕 / 颗粒 → environment.postProcess 段 =====
    if (ImGui::CollapsingHeader("Color Grading", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("cg");
        NovaEngine::PostProcessSettings& pp = scene->postProcess;

        // ---- LUT（.cube）：路径拖放（AssetBrowser/外部文件）+ 手输 + intensity 滑条 ----
        ImGui::TextUnformatted("LUT (.cube)");
        std::string lutPath = pp.lutPath;
        // 应用路径：写序列化字段 + 立即 setLut（面板反馈；apply() 每帧 diff 亦会兜底）
        const auto applyLutPath = [&](const std::string& p) {
            pp.lutPath = p;
            m_lutStatusPath = p;   // 状态与路径绑定（P2-8）
            m_lutStatus = PostProcess::setLut(pp.lutPath)
                              ? std::string("LUT loaded: ") + pp.lutPath
                              : std::string("LUT load FAILED (disabled): ") + pp.lutPath;
            ctx.markSceneDirty();
        };
        // P2-8（Reviewer）：场景切换/外部改路径后状态文本不再陈旧——状态与路径 diff，
        // 变化即对当前路径重算（setLut 幂等；apply() 每帧 diff 已同步缓存，不会重复加载）
        if (m_lutStatusPath != pp.lutPath) {
            m_lutStatusPath = pp.lutPath;
            m_lutStatus = pp.lutPath.empty()
                              ? std::string{}
                              : (PostProcess::setLut(pp.lutPath)
                                     ? std::string("LUT loaded: ") + pp.lutPath
                                     : std::string("LUT load FAILED (disabled): ") + pp.lutPath);
        }
        const bool lutEntered = ImGui::InputTextWithHint(
            "##lutPath", "assets/luts/xxx.cube (empty = off)", &lutPath,
            ImGuiInputTextFlags_EnterReturnsTrue);
        // 浏览按钮（2026-11-24 用户需求）：原生文件框选 .cube——项目内取相对路径，
        // 项目外自动拷入 assets/luts/（与拖放同规则）
        ImGui::SameLine();
        bool lutBrowsed = false;
        if (ImGui::SmallButton("...##lutBrowse")) {
            std::string chosen;
            if (browseOpenFile(chosen, "选择 LUT 文件 (.cube)",
                               std::string("LUT 文件 (*.cube)") + '\0' + "*.cube" + '\0' +
                                   "所有文件 (*.*)" + '\0' + "*.*",
                               "")) {
                const auto rel = copyLutIntoProject(std::filesystem::path(chosen));
                if (!rel.empty()) {
                    lutPath = rel.generic_string();
                    lutBrowsed = true;
                }
            }
        }
        // 拖放目标：kAssetPayload（AssetBrowser 源，绝对路径）
        bool lutDropped = false;
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload(DragDrop::kAssetPayload)) {
                const std::string absStr(static_cast<const char*>(payload->Data));
                const std::filesystem::path abs(absStr);
                if (abs.extension() == ".cube") {
                    const auto rel = copyLutIntoProject(abs);
                    if (!rel.empty()) {
                        lutPath = rel.generic_string();
                        lutDropped = true;
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (lutDropped || lutEntered || lutBrowsed ||
            (ImGui::IsItemDeactivatedAfterEdit() && lutPath != pp.lutPath))
            applyLutPath(lutPath);
        if (!m_lutStatus.empty() && !pp.lutPath.empty())
            ImGui::TextDisabled("%s", m_lutStatus.c_str());
        if (ImGui::DragFloat("LUT Intensity", &pp.lutIntensity, 0.01f, 0.0f, 1.0f))
            ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();

        ImGui::Separator();

        // ---- DOF 简版（HDR 链内；无 HDR → 运行期跳过，见引擎 warn）----
        if (ImGui::Checkbox("DOF", &pp.dofEnabled)) ctx.markSceneDirty();
        if (ImGui::DragFloat("DOF Focus Distance", &pp.dofFocusDistance, 0.1f, 0.1f, 100.0f))
            ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        if (ImGui::DragFloat("DOF Aperture", &pp.dofAperture, 0.001f, 0.0f, 0.5f))
            ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();

        ImGui::Separator();

        // ---- 渐晕 / 颗粒（直通与 HDR 两条路径均生效，§27.7-4）----
        if (ImGui::Checkbox("Vignette", &pp.vignetteEnabled)) ctx.markSceneDirty();
        if (ImGui::DragFloat("Vignette Intensity", &pp.vignetteIntensity, 0.01f, 0.0f, 1.0f))
            ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        if (ImGui::Checkbox("Film Grain", &pp.grainEnabled)) ctx.markSceneDirty();
        if (ImGui::DragFloat("Grain Amount", &pp.grainAmount, 0.001f, 0.0f, 0.5f))
            ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();

        if (!NovaEngine::PostProcess::hdrSupported())
            ImGui::TextDisabled("(HDR 不可用：LUT/渐晕/颗粒仍经直通路径生效；DOF/Bloom 需 HDR)");
        ImGui::PopID();
    }

    // 天空全组（V0.2-7）：模式 / 渐变两色 / cubemap 路径 / 旋转 / 曝光
    if (ImGui::CollapsingHeader("Sky", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("sky");
        int mode = static_cast<int>(scene->skyMode);
        const char* modes[] = {"Gradient", "Cubemap"};
        const int modeCount = static_cast<int>(sizeof(modes) / sizeof(modes[0]));
        if (ImGui::Combo("Sky Mode", &mode, modes, modeCount)) {
            scene->skyMode = static_cast<SkyMode>(mode);
            ctx.markSceneDirty();
        }

        if (ImGui::ColorEdit3("Horizon Color", glm::value_ptr(scene->skyHorizonColor)))
            ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        if (ImGui::ColorEdit3("Zenith Color", glm::value_ptr(scene->skyZenithColor)))
            ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();

        // cubemap 路径（Enter 生效；空 = 渐变回退）
        std::string path = scene->skyCubemapPath;
        const bool skyEntered = ImGui::InputText("Cubemap Path", &path,
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
        // 浏览按钮（2026-11-24 用户需求）：原生文件框（equirect 单文件或六面目录任一文件；
        // 目录模式下引擎按 px/nx/py/ny/pz/nz 探测同目录六面）
        ImGui::SameLine();
        bool skyBrowsed = false;
        if (ImGui::SmallButton("...##skyBrowse")) {
            std::string chosen;
            if (browseOpenFile(chosen, "选择天空盒文件",
                               std::string("天空盒 (*.hdr;*.exr;*.png;*.jpg)") + '\0' +
                                   "*.hdr;*.exr;*.png;*.jpg" + '\0' + "所有文件 (*.*)" + '\0' +
                                   "*.*",
                               "")) {
                path = toProjectRelative(chosen);
                skyBrowsed = true;
            }
        }
        if (skyEntered || skyBrowsed ||
            (ImGui::IsItemDeactivatedAfterEdit() && path != scene->skyCubemapPath)) {
            scene->skyCubemapPath = path;
            ctx.markSceneDirty();
        }
        ImGui::TextDisabled("(6-face dir or equirect file; empty = gradient fallback)");

        if (ImGui::DragFloat("Sky Rotation", &scene->skyRotation, 1.0f, -360.0f, 360.0f))
            ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        if (ImGui::DragFloat("Sky Exposure", &scene->skyExposure, 0.05f, 0.0f, 10.0f))
            ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        // ===== V0.5-4 IBL（波 2 合波接线）：环境光强度（cubemap 天空自动同源烘焙 IBL；
        // 渐变天空无 IBL 源，滑条保留但无效果；0 = 关闭间接光）=====
        if (ImGui::DragFloat("IBL Intensity", &scene->iblIntensity, 0.01f, 0.0f, 5.0f))
            ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        ImGui::PopID();
    }

    ImGui::End();
}

}  // namespace NovaEditor
