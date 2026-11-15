#include "SceneViewPanel.h"

#include "../DragDrop.h"
#include "../EditorCamera.h"
#include "../EditorContext.h"
#include "../GizmoController.h"

#include <imgui.h>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/MaterialAsset.h>   // 双轨材质解析：materialAsset->material()（完整类型）
#include <NovaEngine/Core/Input.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Core/Stats.h>
#include <NovaEngine/Core/Time.h>
#include <NovaEngine/Core/UUID.h>
#include <NovaEngine/Render/Renderer.h>
#include <NovaEngine/Render/ShadowMap.h>
#include <NovaEngine/Render/SkyboxRenderer.h>
#include <NovaEngine/Scene/Camera.h>
#include <NovaEngine/Scene/DirectionalLight.h>
#include <NovaEngine/Scene/MeshRenderer.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/SceneRenderer.h>
#include <NovaEngine/Scene/Transform.h>

// glfwGetTime（Play toast 时间戳）用；顺序要求与 EditorApplication 一致：
// glad 先行（__gl_h_ 守卫令 GLFW 内嵌 <GL/gl.h> 变 no-op），GLFW 头在 glad 之后。
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace NovaEditor {

namespace {

// 世界 AABB：局部 AABB 的 8 角点经世界矩阵变换后的包围盒（拾取与聚焦用）
NovaEngine::AABB transformAabb(const NovaEngine::AABB& local, const glm::mat4& world) {
    const glm::vec3 mn = local.min, mx = local.max;
    const glm::vec3 corners[8] = {
        {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z},
        {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mn.x, mx.y, mx.z}, {mx.x, mx.y, mx.z},
    };
    NovaEngine::AABB out;
    for (const glm::vec3& c : corners) {
        const glm::vec3 p = glm::vec3(world * glm::vec4(c, 1.0f));
        out.min = glm::min(out.min, p);
        out.max = glm::max(out.max, p);
    }
    return out;
}

// 射线-AABB slab 测试（§6.1）
bool rayAabb(const glm::vec3& origin, const glm::vec3& dir, const NovaEngine::AABB& box, float& outT) {
    float tmin = 0.0f, tmax = std::numeric_limits<float>::max();
    for (int i = 0; i < 3; ++i) {
        const float o = origin[i], d = dir[i];
        if (std::abs(d) < 1e-8f) {
            if (o < box.min[i] || o > box.max[i]) return false;
            continue;
        }
        const float inv = 1.0f / d;
        float t1 = (box.min[i] - o) * inv, t2 = (box.max[i] - o) * inv;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }
    outT = tmin;
    return true;
}

// 拾取：遍历全部 MeshRenderer，取 t 最小者
uint64_t pickObject(NovaEngine::Scene* scene, const glm::vec3& origin, const glm::vec3& dir) {
    float bestT = std::numeric_limits<float>::max();
    uint64_t bestId = NovaEngine::UUID::kInvalid;
    scene->forEachObject([&](NovaEngine::GameObject* go) {
        if (!go->activeInHierarchy()) return;
        auto* mr = go->getComponent<NovaEngine::MeshRenderer>();
        if (!mr || !mr->mesh) return;
        const NovaEngine::AABB world = transformAabb(mr->mesh->aabb(), go->transform()->worldMatrix());
        float t = 0.0f;
        if (rayAabb(origin, dir, world, t) && t < bestT) {
            bestT = t;
            bestId = go->id();
        }
    });
    return bestId;
}

// 子树（含自身）全部 MeshRenderer 的世界包围盒并集
void accumulateAabb(NovaEngine::GameObject* go, NovaEngine::AABB& out, bool& any) {
    auto* mr = go->getComponent<NovaEngine::MeshRenderer>();
    if (mr && mr->mesh) {
        const NovaEngine::AABB world = transformAabb(mr->mesh->aabb(), go->transform()->worldMatrix());
        out.min = glm::min(out.min, world.min);
        out.max = glm::max(out.max, world.max);
        any = true;
    }
    for (const auto& child : go->children()) accumulateAabb(child.get(), out, any);
}

// Play 模式渲染相机（§7.3）：isMain 优先，无则第一个 Camera，再无返回 nullptr（回退编辑器相机）。
// Reviewer 追加：跳过非激活层级（!activeInHierarchy）的对象，与引擎侧 collectCamera 逻辑对齐——
// followSelected 的相机选择与 Play 渲染保持一致。
NovaEngine::Camera* pickPlayCamera(NovaEngine::Scene* scene) {
    auto cameras = scene->findComponents<NovaEngine::Camera>();
    if (cameras.empty()) return nullptr;
    for (auto* c : cameras)
        if (c->isMain && c->gameObject()->activeInHierarchy()) return c;
    for (auto* c : cameras)
        if (c->gameObject()->activeInHierarchy()) return c;
    return nullptr;   // 无激活相机：回退编辑器相机
}

}  // namespace

SceneViewPanel::SceneViewPanel() : Panel("Scene") {}

void SceneViewPanel::onGui(EditorContext& ctx, EditorCamera& camera) {
    using namespace NovaEngine;
    Scene* scene = ctx.scene;
    const bool playMode = ctx.state == EditorContext::EditorState::Play;
    const bool editMode = !playMode;

    ImGui::Begin(m_name.c_str(), &open);

    // 上报屏幕区域（外部拖放目标判定用）
    ctx.viewportScreenMin = ImGui::GetWindowPos();
    ctx.viewportScreenMax = ImGui::GetWindowPos() + ImGui::GetWindowSize();

    // Play 态顶部工具栏（V0.2-13/V0.3-10 子集）：分辨率预设 / Render Scale / Follow Selected。
    // Edit 态完全不变（不显示工具栏）。
    if (playMode) drawPlayToolbar(ctx);

    const ImVec2 vpPos = ImGui::GetCursorScreenPos();
    const ImVec2 vpSize = ImGui::GetContentRegionAvail();
    if (vpSize.x <= 1.0f || vpSize.y <= 1.0f || !scene) {
        // 折叠/最小化时尺寸为 0：跳过整个 FBO 渲染路径，避免 GL 报错（§11.8）
        ImGui::End();
        return;
    }

    // 游戏分辨率（V0.2-13，仅 Play 生效）：free = 面板尺寸；否则固定 (width,height)。
    // 相机投影 aspect 用「目标分辨率」宽高比；FBO 实际尺寸 = 目标 × renderScale（0.5–2.0，取整）。
    int targetW = static_cast<int>(vpSize.x), targetH = static_cast<int>(vpSize.y);
    if (playMode && !ctx.gameViewSettings.free) {
        targetW = std::max(1, ctx.gameViewSettings.width);
        targetH = std::max(1, ctx.gameViewSettings.height);
    }
    const float renderScale =
        playMode ? std::clamp(ctx.gameViewSettings.renderScale, 0.5f, 2.0f) : 1.0f;
    int w = std::max(1, static_cast<int>(std::round(targetW * renderScale)));
    int h = std::max(1, static_cast<int>(std::round(targetH * renderScale)));
    if (playMode) {
        // Reviewer P2-4：Custom 分辨率 × renderScale 2.0 可达 16384² MSAA（数 GB 显存必挂），
        // 每维 clamp [16, 4096]（Edit 态尺寸即面板尺寸，不受影响）
        w = std::clamp(w, 16, 4096);
        h = std::clamp(h, 16, 4096);
    }
    const float targetAspect = static_cast<float>(targetW) / static_cast<float>(targetH);

    // FBO 尺寸同步（尺寸 0 已跳过；变化时全删重建）
    bool fboReady = true;
    if (!m_fboCreated) {
        if (!m_fbo.create(w, h, 4)) {
            Logger::error("SceneView: Framebuffer 创建失败 ({}x{})", w, h);
            fboReady = false;   // m_fboCreated 保持 false，下帧重试
        } else {
            m_fboCreated = true;
        }
    } else if (w != m_fbo.width() || h != m_fbo.height()) {
        m_fbo.resize(w, h);
        if (m_fbo.width() != w || m_fbo.height() != h) {
            // resize 无返回值：尺寸未生效视为重建失败，销毁并下帧重试（Reviewer P2-4）
            Logger::error("SceneView: Framebuffer 重建失败 ({}x{})", w, h);
            m_fbo.destroy();
            m_fboCreated = false;
            fboReady = false;
        }
    }
    if (!fboReady) {
        // FBO 不可用：跳过本帧 3D 渲染（不画图、不跑渲染管线），下帧自动重试
        ImGui::End();
        return;
    }

    // Follow Selected（V0.3-10 子集）：每帧渲染前把主相机 Transform 设到 选中世界位置+偏移
    if (playMode && ctx.followSelected) {
        applyFollowSelected(ctx);
    } else {
        m_followOffsetValid = false;   // 未跟随时重置：退出 Play / 关闭开关后偏移失效
    }

    // 离屏渲染（Stats "render" 段：FBO bind 前取 t0）
    const auto renderT0 = std::chrono::steady_clock::now();
    RenderCamera renderCam;  // Edit 路径 + gizmo 用
    if (playMode) {
        // Play 路径（V0.5-1）：整帧渲染交给 SceneRenderer（天空/阴影/grid/submit/endFrame 全在引擎内；
        // play 相机选择逻辑也在引擎内，无场景相机时回退编辑器相机）
        RenderCamera fallback;
        fallback.view = camera.viewMatrix();
        fallback.projection = camera.projectionMatrix(targetAspect);
        fallback.position = camera.position();
        m_fbo.bind();
        SceneRenderer::renderScene(*scene, fallback);
        m_fbo.resolveMsaa();
        m_fbo.unbind();
    } else {
        // Edit 路径：手写（保持原实现；V0.2-5 drawGrid 传雾色；V0.2-8 阴影收集跳过透明物体）
        renderCam.view = camera.viewMatrix();
        renderCam.projection = camera.projectionMatrix(targetAspect);
        renderCam.position = camera.position();
        glm::vec4 clearColor(scene->skyColor, 1.0f);  // §3.2：Edit 用 scene.skyColor

        // 灯光：场景第一个 DirectionalLight（方向取自 Transform.forward），无则默认方向
        DirectionalLightData light;
        DirectionalLight* dirLight = nullptr;
        auto lights = scene->findComponents<DirectionalLight>();
        if (!lights.empty()) {
            dirLight = lights.front();
            light.direction = dirLight->transform()->forward();
            light.color = dirLight->color;
            light.intensity = dirLight->intensity;
        }
        light.ambient = scene->ambientColor;

        // 阴影参数（V0.2-6）：castShadows 时组装；阴影盒中心 = 相机位置沿光轴投影到过原点平面（跟随相机）
        ShadowCameraParams shadowParams;
        std::vector<ShadowItem> shadowItems;
        const bool shadowEnabled = dirLight && dirLight->castShadows;
        if (shadowEnabled) {
            shadowParams.lightDirection = light.direction;
            shadowParams.mapSize = dirLight->shadowMapSize;
            shadowParams.bias = dirLight->shadowBias;
            const glm::vec3 camCenter =
                renderCam.position - light.direction * glm::dot(renderCam.position, light.direction);
            shadowParams.lightViewProj = computeLightViewProj(light.direction, camCenter,
                                                              dirLight->shadowRange,
                                                              dirLight->shadowMapSize);
        }

        // 天空 cubemap（V0.2-7）：Cubemap 模式且路径非空时加载；失败/渐变走 nullptr（内部回退渐变）
        std::shared_ptr<Cubemap> skyCube;
        if (scene->skyMode == SkyMode::Cubemap && !scene->skyCubemapPath.empty())
            skyCube = AssetDatabase::instance().getCubemap(scene->skyCubemapPath);

        // 严格按 ARCHITECTURE-V0.2 顺序：天空 → 单次遍历收集 → 阴影 → grid → 描边
        m_fbo.bind();
        Renderer::beginFrame(renderCam, light, clearColor);
        SkyboxRenderer::draw(glm::inverse(renderCam.projection * renderCam.view), renderCam.position,
                             scene->skyMode, scene->skyHorizonColor, scene->skyZenithColor,
                             skyCube, scene->skyRotation, scene->skyExposure);
        scene->forEachObject([&](GameObject* go) {
            if (!go->activeInHierarchy()) return;
            auto* mr = go->getComponent<MeshRenderer>();
            if (!mr || !mr->mesh) return;
            const glm::mat4 world = go->transform()->worldMatrix();
            // V0.3-6 材质资产双轨：引用 .novamat 时整块提交资产材质；否则内联材质
            const Material& mat = mr->material.materialAsset
                                      ? mr->material.materialAsset->material()
                                      : mr->material;
            Renderer::submit(mr->mesh, mat, world);
            // V0.2-8：透明物体（blendMode != Opaque）不进阴影深度 pass
            //（Reviewer P1-2：判定用解析后的 mat，与 SceneRenderer 一致——资产引用 blendMode 语义正确）
            if (shadowEnabled && mat.blendMode == BlendMode::Opaque)
                shadowItems.push_back(ShadowItem{mr->mesh, world});
        });
        if (shadowEnabled) Renderer::drawShadowPass(shadowParams, shadowItems);
        if (scene->showGrid) Renderer::drawGrid(scene->fogColor);  // V0.2-5：网格雾色参数化
        const glm::vec3 kOutlineColor(1.0f, 0.55f, 0.1f);  // 选中橙色描边
        for (uint64_t id : ctx.selectedIds) {
            GameObject* go = scene->findGameObject(id);
            if (!go) continue;
            auto* mr = go->getComponent<MeshRenderer>();
            if (mr && mr->mesh)
                Renderer::drawOutline(mr->mesh, go->transform()->worldMatrix(), kOutlineColor);
        }
        Renderer::endFrame();
        m_fbo.resolveMsaa();
        m_fbo.unbind();
    }
    const auto renderT1 = std::chrono::steady_clock::now();
    Stats::setSectionMs("render",
                        std::chrono::duration<float, std::milli>(renderT1 - renderT0).count());

    // 显示：Edit 与旧行为逐像素一致（FBO = 面板尺寸，直接铺满）；
    // Play 在面板内容区内等比 fit-rect 居中（letterbox），FBO 纹理相对 ImGui 上下颠倒：
    // UV 必须 (0,1)-(1,0)（§11.8）
    bool hovered = false;
    if (editMode) {
        ImGui::Image((ImTextureID)(intptr_t)m_fbo.colorTextureId(), vpSize,
                     ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        hovered = ImGui::IsItemHovered();
    } else {
        const float fitScale = std::min(1.0f, std::min(vpSize.x / targetW, vpSize.y / targetH));
        const ImVec2 dstSize(targetW * fitScale, targetH * fitScale);
        const ImVec2 offset((vpSize.x - dstSize.x) * 0.5f, (vpSize.y - dstSize.y) * 0.5f);
        ImGui::SetCursorPos(ImGui::GetCursorPos() + offset);  // 居中偏移
        ImGui::Image((ImTextureID)(intptr_t)m_fbo.colorTextureId(), dstSize,
                     ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        hovered = ImGui::IsItemHovered();
    }
    const bool windowHovered = ImGui::IsWindowHovered();
    const glm::vec2 vpPosF(vpPos.x, vpPos.y), vpSizeF(vpSize.x, vpSize.y);

    // gizmo（ImGui::Image 之后绘制进前景 drawlist，覆盖在视口之上）
    if (editMode && m_gizmo) {
        m_gizmo->setCamera(renderCam.view, renderCam.projection);
        m_gizmo->update(ctx, vpPosF, vpSizeF);
    }

    // 编辑器相机输入（gizmo 拖拽期间抑制；鼠标离开视口后若仍在拖拽则继续）
    const bool gizmoActive = m_gizmo && m_gizmo->isUsing();
    if (editMode && !gizmoActive && (windowHovered || camera.isInteracting()))
        camera.update(Time::deltaTime(), vpPosF, vpSizeF);

    // 射线-AABB 拾取（位移阈值 3px、Ctrl 加选、gizmo 悬停抑制；仅 Edit 态）
    handlePicking(ctx, camera, vpPosF, vpSizeF, hovered);

    // F 聚焦（§6.2）
    if (editMode && hovered && Input::isKeyPressed(Key::F) && !ImGui::GetIO().WantCaptureKeyboard)
        focusSelected(ctx, camera);

    // 接受资产拖放（AssetBrowser → SceneView；落点 = 射线与 y=0 平面交点）
    // 注意：目标注册不依赖 hovered（与 HierarchyPanel 一致）——拖拽过程中 IsItemHovered
    // 可能因 gizmo 手柄/重叠窗口误判为 false，导致目标未注册、payload 被 ImGui 丢弃
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DragDrop::kAssetPayload)) {
            const std::filesystem::path srcPath(std::string(static_cast<const char*>(payload->Data)));
            Logger::info("视口收到拖放资产: {}", srcPath.string());
            if (ctx.state != EditorContext::EditorState::Edit) {
                Logger::warn("Play 模式下禁止导入模型");
            } else if (DragDrop::isModelFile(srcPath)) {
                const ImVec2 m = ImGui::GetIO().MousePos;
                const glm::vec3 spawn =
                    camera.intersectGround(glm::vec2(m.x, m.y), vpPosF, vpSizeF);
                GameObject* root = DragDrop::importModelToScene(*scene, srcPath, spawn);
                if (root) {
                    ctx.setSelected(root->id());
                    focusGameObject(camera, root);  // 自动聚焦新物体（§6.4）
                    ctx.markSceneDirty();
                }
            } else {
                Logger::warn("仅支持将模型文件拖入场景视口: {}", srcPath.filename().string());
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Play toast（V0.3-10 子集）：进入 Play 未保存提示，3 秒非阻塞。
    // 纯文本绘制用前景 drawlist 无妨（非 gizmo 交互，无悬停判定依赖）。
    if (playMode && ctx.playToastUntil > glfwGetTime()) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const char* text = "Scene has unsaved changes (Play snapshot will restore)";
        const ImVec2 ts = ImGui::CalcTextSize(text);
        const float pad = 10.0f;
        const ImVec2 center(vpPos.x + vpSize.x * 0.5f, vpPos.y + ts.y + pad);
        const ImVec2 mn(center.x - ts.x * 0.5f - pad, center.y - ts.y * 0.5f - pad * 0.5f);
        const ImVec2 mx(center.x + ts.x * 0.5f + pad, center.y + ts.y * 0.5f + pad * 0.5f);
        dl->AddRectFilled(mn, mx, IM_COL32(30, 30, 38, 235), 4.0f);
        dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f),
                    IM_COL32(240, 220, 140, 255), text);
    }

    ImGui::End();
}

void SceneViewPanel::drawPlayToolbar(EditorContext& ctx) {
    // V0.2-13：Free / 1920x1080 / 1280x720 / Custom 预设；Custom 时 W/H 可编辑；Render Scale 0.5–2.0
    auto& gv = ctx.gameViewSettings;
    int preset = 0;
    if (!gv.free) {
        if (gv.width == 1920 && gv.height == 1080) preset = 1;
        else if (gv.width == 1280 && gv.height == 720) preset = 2;
        else preset = 3;   // Custom
    }
    const char* presets[] = {"Free", "1920x1080", "1280x720", "Custom"};
    if (ImGui::Combo("Resolution", &preset, presets,
                     static_cast<int>(sizeof(presets) / sizeof(presets[0])))) {
        if (preset == 0) gv.free = true;
        else {
            gv.free = false;
            if (preset == 1) { gv.width = 1920; gv.height = 1080; }
            else if (preset == 2) { gv.width = 1280; gv.height = 720; }
            // Custom：保留当前 W/H
        }
    }
    const bool custom = !gv.free && preset == 3;
    if (custom) {
        // Custom：W/H 可编辑（DragInt 直接改运行期设置）
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::DragInt("W", &gv.width, 1.0f, 64, 8192);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::DragInt("H", &gv.height, 1.0f, 64, 8192);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::SliderFloat("Render Scale", &gv.renderScale, 0.5f, 2.0f, "%.1f"))
        gv.renderScale = std::round(gv.renderScale * 10.0f) / 10.0f;   // 步长 0.1
    // Follow Selected（V0.3-10 子集）：开关切换时重置偏移
    ImGui::SameLine();
    if (ImGui::Checkbox("Follow Selected", &ctx.followSelected)) m_followOffsetValid = false;
    ImGui::Separator();
}

void SceneViewPanel::applyFollowSelected(EditorContext& ctx) {
    // V0.3-10 子集：主相机跟随选中对象。启用时记录 相机世界位置 − 选中对象世界位置 偏移，
    // 每帧渲染前把相机 Transform 设到 选中世界位置+偏移（朝向不变；退出 Play 快照自动还原）。
    using namespace NovaEngine;
    Scene* scene = ctx.scene;
    if (!scene) return;
    Camera* cam = pickPlayCamera(scene);   // isMain 优先，无则第一个
    if (!cam) return;
    const auto selected = ctx.selectedObjects();
    if (selected.empty()) return;
    Transform* ct = cam->transform();
    Transform* st = selected.front()->transform();
    if (!ct || !st) return;

    if (!m_followOffsetValid) {
        m_followOffset = ct->worldPosition() - st->worldPosition();
        m_followOffsetValid = true;
    }
    const glm::vec3 targetWorld = st->worldPosition() + m_followOffset;
    // 有父级：反解局部（朝向不变，只改位置）
    GameObject* camGo = cam->gameObject();
    if (camGo->parent() && camGo->parent()->transform())
        ct->setLocalPosition(
            glm::vec3(glm::inverse(camGo->parent()->transform()->worldMatrix()) *
                      glm::vec4(targetWorld, 1.0f)));
    else
        ct->setLocalPosition(targetWorld);
}

void SceneViewPanel::handlePicking(EditorContext& ctx, EditorCamera& camera,
                                   const glm::vec2& vpPos, const glm::vec2& vpSize, bool hovered) {
    if (ctx.state != EditorContext::EditorState::Edit) return;
    const ImGuiIO& io = ImGui::GetIO();

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_pickPressPos = glm::vec2(io.MousePos.x, io.MousePos.y);
        m_pickArmed = true;
    }
    if (m_pickArmed && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_pickArmed = false;
        if (!hovered || (m_gizmo && m_gizmo->isOver())) return;  // gizmo 悬停抑制点击选择
        const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
        if (glm::length(mouse - m_pickPressPos) > 3.0f) return;  // 拖动超过 3px：视为相机操作

        const auto [origin, dir] = camera.screenPointToRay(mouse, vpPos, vpSize);
        const uint64_t hitId = pickObject(ctx.scene, origin, dir);
        if (hitId != NovaEngine::UUID::kInvalid) {
            if (io.KeyCtrl) ctx.toggleSelected(hitId);  // Ctrl 加选/减选
            else ctx.setSelected(hitId);
            // 拾取成功日志（配合 GizmoController 的激活日志排查「选中但 gizmo 不显示」）
            if (NovaEngine::GameObject* hit = ctx.scene->findGameObject(hitId))
                NovaEngine::Logger::info("拾取命中: {}（id={}）", hit->name(), hitId);
        } else if (!io.KeyCtrl) {
            ctx.clearSelection();  // 点击空白取消选择
        }
    }
}

void SceneViewPanel::focusSelected(EditorContext& ctx, EditorCamera& camera) {
    NovaEngine::GameObject* go = ctx.activeObject();
    if (!go && !ctx.selectedIds.empty())
        go = ctx.scene->findGameObject(*ctx.selectedIds.begin());
    if (!go) {
        camera.focus(glm::vec3(0.0f, 1.0f, 0.0f), 8.0f);  // 无选中：聚焦原点
        return;
    }
    focusGameObject(camera, go);
}

void SceneViewPanel::focusGameObject(EditorCamera& camera, NovaEngine::GameObject* go) {
    // 世界 AABB 中心 c 与半径 r → focus(c, max(r*3, 0.5))（§6.2）
    NovaEngine::AABB box;
    bool any = false;
    accumulateAabb(go, box, any);
    if (!any) {
        camera.focus(go->transform()->worldPosition(), 3.0f);
        return;
    }
    const glm::vec3 center = (box.min + box.max) * 0.5f;
    const float radius = glm::length(box.max - box.min) * 0.5f;
    camera.focus(center, std::max(radius * 3.0f, 0.5f));
}

}  // namespace NovaEditor
