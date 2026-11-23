#pragma once
#include "../Panel.h"

namespace NovaEditor {

// 物理碰撞层面板（V1.0-3，§27.2）：16 层命名 + 16×16 对称碰撞矩阵勾选（只展示三角区，
// 点击自动镜像到对称位）+ 恢复默认。
// 写 environment.physics（场景字段 = 权威存储：Scene::physicsLayerNames /
// Scene::physicsCollisionMatrix，SceneSerializer 随场景持久化）；Play 起始
// PhysicsWorld::syncFromTransforms 读入生效。
// 不 dock（合波日接线；与 MixerPanel 同纪律）。
class PhysicsLayersPanel : public Panel {
public:
    explicit PhysicsLayersPanel();
    void onGui(EditorContext& ctx, EditorCamera& camera) override;
};

}  // namespace NovaEditor
