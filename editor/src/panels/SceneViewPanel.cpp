#include "SceneViewPanel.h"

#include "../DragDrop.h"
#include "../EditorCamera.h"
#include "../EditorContext.h"
#include "../GizmoController.h"

#include <imgui.h>

#include <NovaEngine/Assets/AssetDatabase.h>
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
#include <NovaEngine/Scene/Transform.h>

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

// Play 模式渲染相机（§7.3）：isMain 优先，无则第一个 Camera，再无返回 nullptr（回退编辑器相机）
NovaEngine::Camera* pickPlayCamera(NovaEngine::Scene* scene) {
    auto cameras = scene->findComponents<NovaEngine::Camera>();
    if (cameras.empty()) return nullptr;
    for (auto* c : cameras)
        if (c->isMain) return c;
    return cameras.front();
}

}  // namespace

SceneViewPanel::SceneViewPanel() : Panel("Scene") {}

void SceneViewPanel::onGui(EditorContext& ctx, EditorCamera& camera) {
    using namespace NovaEngine;
    Scene* scene = ctx.scene;

    ImGui::Begin(m_name.c_str(), &open);

    // 上报屏幕区域（外部拖放目标判定用）
    ctx.viewportScreenMin = ImGui::GetWindowPos();
    ctx.viewportScreenMax = ImGui::GetWindowPos() + ImGui::GetWindowSize();

    const ImVec2 vpPos = ImGui::GetCursorScreenPos();
    const ImVec2 vpSize = ImGui::GetContentRegionAvail();
    if (vpSize.x <= 1.0f || vpSize.y <= 1.0f || !scene) {
        // 折叠/最小化时尺寸为 0：跳过整个 FBO 渲染路径，避免 GL 报错（§11.8）
        ImGui::End();
        return;
    }

    // FBO 尺寸同步（尺寸 0 已跳过；变化时全删重建）
    const int w = static_cast<int>(vpSize.x), h = static_cast<int>(vpSize.y);
    if (!m_fboCreated) {
        if (!m_fbo.create(w, h, 4)) {
            Logger::error("SceneView: Framebuffer 创建失败");
            ImGui::End();
            return;
        }
        m_fboCreated = true;
    } else if (w != m_fbo.width() || h != m_fbo.height()) {
        m_fbo.resize(w, h);
    }

    // 渲染相机：Play 切场景主相机（isMain 优先，无则第一个，再无回退编辑器相机）；Edit 用编辑器相机
    RenderCamera renderCam;
    glm::vec4 clearColor(scene->skyColor, 1.0f);  // §3.2：Edit 用 scene.skyColor
    if (ctx.state == EditorContext::EditorState::Play) {
        if (Camera* cam = pickPlayCamera(scene)) {
            Transform* t = cam->transform();
            const glm::vec3 camPos = t->worldPosition();
            // 引擎约定 forward = +Z：用 lookAt 让相机看向 Transform.forward。
            // （inverse(world) 的视空间看向 -Z，与 forward=+Z 约定矛盾，相机会看到反方向）
            renderCam.view = glm::lookAt(camPos, camPos + t->forward(), glm::vec3(0.0f, 1.0f, 0.0f));
            renderCam.projection = glm::perspective(glm::radians(cam->fov), static_cast<float>(w) / h,
                                                    cam->nearPlane, cam->farPlane);
            renderCam.position = camPos;
            clearColor = cam->clearColor;
        } else {
            renderCam.view = camera.viewMatrix();
            renderCam.projection = camera.projectionMatrix(static_cast<float>(w) / h);
            renderCam.position = camera.position();
        }
    } else {
        renderCam.view = camera.viewMatrix();
        renderCam.projection = camera.projectionMatrix(static_cast<float>(w) / h);
        renderCam.position = camera.position();
    }

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

    // 离屏渲染（严格按 ARCHITECTURE-V0.2 顺序：天空 → 单次遍历收集 → 阴影 → grid → 描边）
    const auto renderT0 = std::chrono::steady_clock::now();  // Stats "render" 段：FBO bind 前取 t0
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
        Renderer::submit(mr->mesh, mr->material, world);
        if (shadowEnabled) shadowItems.push_back(ShadowItem{mr->mesh, world});
    });
    if (shadowEnabled) Renderer::drawShadowPass(shadowParams, shadowItems);
    if (scene->showGrid) Renderer::drawGrid();
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
    const auto renderT1 = std::chrono::steady_clock::now();
    Stats::setSectionMs("render",
                        std::chrono::duration<float, std::milli>(renderT1 - renderT0).count());

    // FBO 纹理相对 ImGui 上下颠倒：UV 必须 (0,1)-(1,0)（§11.8）
    ImGui::Image((ImTextureID)(intptr_t)m_fbo.colorTextureId(), vpSize,
                 ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    const bool hovered = ImGui::IsItemHovered();
    const bool windowHovered = ImGui::IsWindowHovered();
    const bool editMode = ctx.state == EditorContext::EditorState::Edit;
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

    ImGui::End();
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
