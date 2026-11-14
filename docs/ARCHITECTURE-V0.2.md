# V0.2 第一批功能设计摘要（2026-11-15 冻结）

对应功能：V0.2-1 快照式 Undo/Redo、V0.2-2 未保存确认、V0.2-6 方向光阴影映射、V0.2-7 天空盒、V0.2-11 场景统计+Profiler lite。
完整规格见 docs/ROADMAP.md；精确接口以 engine/include 冻结头为准。

## 并行边界（硬边界）

- **Coder A（渲染线）**独占：engine/include 全部、engine/src 全部、assets/shaders 全部。不碰 editor/。
- **Coder C（编辑器线）**独占：editor/ 全部 + editor/CMakeLists.txt。不碰 engine/。
- SceneSerializer.cpp 唯一改动方 = A（天空字段 + version 2 + indent 参数）。

## 冻结头变更清单

| 头文件 | 变更 |
|---|---|
| Render/ShadowMap.h | 新增：ShadowCameraParams、computeLightViewProj（纹素对齐正交）、ShadowMap 深度 FBO |
| Render/SkyboxRenderer.h | 新增：SkyMode 枚举、SkyboxRenderer 静态类（全屏三角形） |
| Render/Cubemap.h | 新增：6 面/equirect 创建、内部 RGB16F |
| Render/Renderer.h | +drawShadowPass、ShadowItem、4 个统计访问器 |
| Render/Mesh.h | +indexCount() 内联 |
| Scene/DirectionalLight.h | +castShadows/shadowBias/shadowRange/shadowMapSize |
| Scene/Scene.h | +skyMode/skyHorizonColor/skyZenithColor/skyCubemapPath/skyRotation/skyExposure |
| Assets/AssetDatabase.h | +getCubemap + m_cubemapCache |
| Serialization/SceneSerializer.h | serialize 增 `int indent = 4` 参数（-1=紧凑，Undo 用） |
| Core/Stats.h | 新增：帧时间均值/尖峰/120 帧环形历史/分段计时 |
| NovaEngine.h | 伞头 +4 |

## 关键裁定

1. **阴影**：独立入口 drawShadowPass（主 pass 一行不改语义）；固定范围+跟随相机（不做场景 AABB）；PCF 3x3 手动比较（不用 sampler2DShadow）；正面剔除（GL_FRONT）防 acne；blinn_phong.frag 与 grid.frag 各持 shadowFactor（grid 接收阴影，V0.3 PBR 阶段合并）。
2. **天空**：全屏三角形（空 VAO + gl_VertexID），z=w 深度 1.0 + GL_LEQUAL；渐变/cubemap 共用一个 shader（uUseCubemap 分支）；场景环境字段方案（非独立组件）；clearColor 保持兜底清屏色不改签名。
3. **Undo**：快照式环形栈（上限 64），栈顶恒为当前态；入栈点 = 全部结构/Transform/材质/组件编辑的「编辑结束瞬间」（拖拽中用 markDirtyOnly 防刷栈）；不入栈 = 选择/相机/面板开关；恢复 = deserialize 回同一 Scene（ID 不变）；选中集仅清理失效 ID。
4. **脏标记**：markSceneDirty（置脏+推快照）为统一漏斗；消费点 = 标题栏 *、New/Open/双击加载模态、窗口关闭拦截、退出 Play 还原进 Play 前脏状态（修复 7.2-4）。
5. **统计**：Core/Stats（帧时序）+ Renderer 计数器（数据产生地），编辑器 StatsPanel（F8 开关）为纯消费者。
6. **schema**：version 1→2（写 2 读 1/2）；全部新字段默认值兜底，旧 demo.nova.json 开箱即用。

## 每帧渲染顺序（SceneViewPanel）

```
m_fbo.bind()
Renderer::beginFrame(renderCam, light, clearColor)
SkyboxRenderer::draw(invViewProj, camPos, mode, horizon, zenith, cubemap, rotationDeg)
forEachObject 单次遍历：收集 ShadowItem + 提交主队列
if (shadowEnabled) Renderer::drawShadowPass(shadowParams, shadowItems)
if (scene->showGrid) Renderer::drawGrid()
drawOutline 循环
Renderer::endFrame()
m_fbo.resolveMsaa(); m_fbo.unbind()
```

## GLSL 清单

新增：shadow.vert/.frag、skybox.vert/.frag、equirect_to_cube.vert/.frag
修改：blinn_phong.frag（+5 uniform + shadowFactor + 直射光乘阴影）、grid.frag（同款 shadowFactor，全影压到 35%）

## 验收要点

- 立方体在地面投出阴影且随 Sun 旋转；castShadows 关闭无阴影；空场景不崩溃
- 渐变天空可见；天空模式/旋转随场景序列化往返
- Ctrl+Z/Y 混合 10 步操作正确；Play 态禁用
- 标题 * 与三选模态（Save/Don't Save/Cancel）；窗口关闭被拦截
- F8 Stats 面板：FPS/帧时间直方图/draw call 数与直觉一致
