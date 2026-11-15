# NovaEngine 产品路线图（ROADMAP）

> 快照时间：2026-11-15。当前基线 = V0.1（M1–M5 全部功能已实现，含选择/gizmo/Inspector/序列化/拖拽导入/Play 模式/脚本组件）。
> 本文档站在「**逐步替代 Unity/UE 做独立游戏**」的立场，规划 V0.2 → V0.3 → V0.5 → V1.0 四个阶段。
> 与 docs/ARCHITECTURE.md（接口契约）、docs/KNOWN-ISSUES.md（已知缺口）、docs/FEATURES.md（已实现功能）配套阅读；**修改冻结接口前必须先更新 ARCHITECTURE.md**。

---

## 0. 阅读说明

- **工作量口径**：S = 半天~1 天；M = 2~3 天；L = 1 周+。按「单人 + AI Agent 并行辅助」的节奏估算，允许 ±50% 浮动。
- **依赖列**：写「已有」= 直接复用现有模块；写「新增」= 需要新模块/新目录/新第三方库（新第三方库需 Architect 同意，见 CLAUDE.md 约束）。
- **验收标准**：每点都写可执行、可目测、可对照统计数字的验收项，风格对齐 KNOWN-ISSUES.md 的验收清单。
- **排期原则**：每个阶段的功能点相互解耦，允许打乱顺序实现；但表内标注的依赖必须先行。

---

## 1. 定位与边界

### 1.1 NovaEngine 是什么

- **一个 Windows PC 单机游戏引擎 + Unity 风格编辑器**，服务对象是独立开发者/1–3 人小团队，规模上限：**中型 3D/2D 游戏**（数千 draw call、几 GB 资产、数百个场景对象，典型如：第一人称探索、3D 平台跳跃、恐怖解谜、俯视角射击、2D 平台/解谜）。
- 技术路线：C++20 / OpenGL 4.5 / 单线程主循环（音频除外）/ 组件式场景模型 / ImGui 编辑器。务实优先，不为架构而架构（CLAUDE.md 已约定：不要 ECS、不要反射、不要多线程渲染）。

### 1.2 替代范围的边界（能做 / 不做）

| 维度 | 替代范围（做） | 明确不做（超期） |
|---|---|---|
| 渲染 | 前向 PBR + 阴影 + IBL + 后处理 + 粒子 + 骨骼动画 + LOD | UE5 Nanite 虚拟几何、Lumen 动态 GI、光线追踪、GPU-driven/bindless |
| 内容 | 场景/预制体/材质资产、高度图地形（512²–1024²）、2D 精灵 | 开放世界流式地形、程序化世界生成 |
| 脚本 | C++ 组件脚本 + DLL 热重载 + 协程 + 事件 | Bolt 级全可视化脚本（仅做 Shader Graph 简版） |
| 平台 | Windows x64 独立 exe | 主机移植（Switch/PS/Xbox）、移动端、Web |
| 网络 | V0.5 帧同步 spike，按结论决策 | MMO 级万人同屏、云游戏 |
| 编辑器 | Unity 核心工作流子集（层级/Inspector/预制体/打包/Profiler） | 资产商店、团队协作（多人同时编辑同一场景） |

**一句话原则**：每个功能进入路线图前先问「没有它，目标品类游戏做得出来吗？」——答不出就推迟。路线图不是 UE 克隆清单，而是「能做完一个游戏」的最小完整集。

### 1.3 演进原则

1. **组件扩展优先**：能用「新 Component / 新 Scene 级服务」解决的，不动 GameObject/Component 架构。
2. **渲染器渐进改造**：从单 pass 前向逐步演进为「shadow → sky → opaque → transparent → post」多 pass 编排，**保持单线程**，每一步以场景统计层数字回归验收。
3. **序列化即资产**：Prefab/材质/材质库/存档全部复用 SceneSerializer 的 JSON 路径 + `version` 字段做 schema 演进，不发明第二种格式（二进制缓存仅是解码产物缓存，见 V0.3-7）。
4. **第三方库按需引入**：Jolt（物理）、miniaudio（音频）、stb_truetype（已在 stb 内）之外不再扩张依赖树；引入前必须有「自研成本 vs 集成成本」结论。

---

## 2. 当前基线（V0.1 = M1–M5）与吞债清单

已实现：窗口/ImGui docking 五面板、Assimp 模型导入（OBJ/FBX/glTF/Collada）、Blinn-Phong 前向渲染 + 网格地面、选择/描边/gizmo、Inspector 伪反射编辑、场景 JSON 序列化、拖拽导入、Play 快照/恢复、脚本组件（静态 C++）。

**路线图直接吞并的既有债务**（详见 KNOWN-ISSUES.md，编号沿用）：

- #1 网格雾色硬编码 → 并入 V0.2-5 环境面板；#3 内置图元无缓存、#4 中文路径、#6 导入双解析、#10 畸形 JSON 防御、#8/7.2-2 Play 态禁用、7.2-7 Play 拖贴图、7.2-12 Ctrl 语义 → 并入 V0.2-14 修复包；#7/7.2-1 复制浅拷贝+整场景重建 → 并入 V0.2-3；#2 场景环境无 UI → V0.2-5；#9 typeid 反查 → 并入 V0.5-7 热重载重构；#11 AABB 拾取 → V1.0 编辑器细节完善里评估三角形级拾取。

---

## 3. 版本总览

| 版本 | 主题 | 交付后能做什么 | 预估周期* |
|---|---|---|---|
| **V0.2** | 可信赖的编辑器 + 第一眼观感 | 编辑器敢用（撤销/未保存保护）、画面有阴影天空、能做完一个 jam 级 3D 小游戏 | 3–4 周 |
| **V0.3** | 游戏化核心能力 | 物理/音频/UI/动画/预制体齐备，能做完整 3D 平台跳跃/解谜/步行模拟 | 4–6 周（2026-11-15 达成，动画顺延 V0.5） |
| **V0.5** | 发布能力与规模内容 | 打包独立 exe、粒子/LOD/地形/2D、脚本热重载，能发布中型 3D 游戏 | 6–8 周 |
| **V1.0** | 商业独立游戏发布级 | 多场景/存档/完整 Profiler/示例游戏/文档，达到「用它做下一款游戏」的信心线 | 8–12 周 |

\* 周期为估算，按 AI 辅助开发节奏，随实际进度滚动调整。

---

## 4. V0.2 「可信赖的编辑器 + 第一眼观感」

> 目标：把 M1–M5 的「功能演示」变成「敢用不丢活」的工具，同时让画面第一次接近主流引擎默认观感。本阶段全部不引入新第三方库。

| # | 功能 | 说明与价值 | 依赖 | 工作量 | 验收标准 |
|---|---|---|---|---|---|
| 1 | **快照式 Undo/Redo（Ctrl+Z/Y）** | 复用 SceneSerializer 做整场景 JSON 快照环形栈（上限 64 步）；Transform 修改、增删、重父子、重命名、材质/组件参数均入栈；New/Open 清栈。**痛点**：一次误删/误拖全毁，Unity 用户肌肉记忆 Ctrl+Z，这是「替代 Unity」的第一门面 | 已有：SceneSerializer、EditorContext；改：InspectorPanel/HierarchyPanel/GizmoController 操作入口 | M | 连续 64 步混合操作（gizmo 拖拽、Del、Ctrl+D、重父子、重命名、材质改色）完整还原；Ctrl+Y 重做；撤销恢复选中集；New/Open 后栈清空；撤销后可正常 Ctrl+S |
| 2 | **脏标记消费：标题 \* + 未保存确认** | 消费已有 sceneDirty：标题栏加 `*`；New/Open/双击加载/退出前弹「保存/不保存/取消」；修复 Play live edit 置脏后 Stop 不还原（KNOWN-ISSUES 7.2-4）。**痛点**：目前脏标记只写不读，随时误丢工作 | 已有：EditorContext::sceneDirty；改：EditorApplication 菜单路径 + ImGui 模态框 | S | 修改后标题出现 `*`、保存后消失；Ctrl+N/Ctrl+O/双击场景/关窗口在有未保存更改时弹三选对话框且三种选择行为正确；Play 中拖 Transform 后 Stop，脏标记还原到进 Play 前 |
| 3 | **Duplicate 深拷贝 + removeComponent 按指针移除** | serializeGameObject 递归子树（含 children 数组）实现深拷贝；新增 `GameObject::removeComponent(Component*)`，终结「整场景 JSON 重建」hack（#7、7.2-1）。**痛点**：复制组合物体丢子物体；移除脚本清零其他脚本运行时状态且 O(n²)。这是 Prefab（V0.3-9）与 Undo 的前置 | 已有：SceneSerializer、GameObject、Scene 销毁队列；改：DragDrop::duplicateGameObject | M | 选中含 3 层子物体的对象 Ctrl+D，副本含完整子树且全部为新 ID；Play 中移除脚本仅移除目标组件，其余对象 onUpdate 状态不变；多选复制 100 对象 < 1s；深拷贝对象可正常保存/加载 |
| 4 | **组件系统卫生包** | Add Component 同类型去重（下拉置灰+日志警告）；`Component::enabled` 提升基类，MeshRenderer/Camera/DirectionalLight 加 Inspector 勾选；脚本注册名与内置名冲突校验（#6）。**痛点**：可挂出「僵尸组件」（两个 Camera 只编辑第一个）；内置组件无 enabled | 已有：ScriptRegistry、Component、Scene::update；改：InspectorPanel | S | 已有 Camera 的对象 Add Component 列表中 Camera 置灰/拦截并打警告；取消 MeshRenderer enabled 后物体不渲染；注册名为 "Camera" 的脚本启动即警告且序列化不误判；旧场景 JSON 向后兼容 |
| 5 | **场景环境设置面板** | 菜单/面板编辑 skyColor/ambientColor/showGrid/雾色并随场景序列化；`Renderer::drawGrid` 颜色参数化，修复雾色硬编码（#1）。**痛点**：目前只能手改 JSON；Unity 的 Lighting 设置是每个场景必用入口 | 已有：Scene 字段、SceneSerializer；改：Renderer::drawGrid 签名、EditorApplication 菜单 | S | 面板改天空色/环境光/雾色当帧生效（Edit 与 Play 一致）；Ctrl+S 往返无损；showGrid 开关与 JSON 一致；雾色默认值与天空色联动 |
| 6 | **方向光阴影映射** | 新增 Render/ShadowMap 模块：正交深度 pass（2048²，光空间矩阵 snap 到纹素防抖动）+ PCF 3x3 采样接入 blinn_phong.frag；DirectionalLight 加 castShadows/shadowStrength/shadowBias；Renderer 增加阴影 pre-pass。**痛点**：无阴影没有体积感/落地感，是当前画面与 Unity 默认观感的最大差距 | 已有：Framebuffer、Shader、DirectionalLight；改：Renderer（多 pass 化，架构影响 §10-C）、新增 assets/shaders/shadow.vert/.frag | L | demo 场景立方体在地面投出清晰阴影且随 Sun 旋转方向正确；shadowBias 可调，目测无 acne/peter-panning；castShadows 关闭后无阴影；无灯光的空场景不崩溃；Debug/Release 均 60 FPS 回退 < 10% |
| 7 | **天空盒** | 新增天空渲染路径：程序化渐变天空（默认，替换纯色清屏）+ cubemap 采样（6 面图或等距柱状 HDR 转 cubemap）；环境面板选择模式与旋转。**痛点**：纯色天空无方向感；天空盒是 Unity 场景默认观感，也是 V0.5 IBL 的数据源 | 已有：Renderer、Texture；改：TextureLoader（HDR 加载路径）、环境面板（V0.2-5） | M | 默认渐变天空随天空色变化；导入 HDR/6 面图后正确显示且旋转可调；天空画在最远且不写深度（不遮挡任何物体）；模式选择随场景序列化往返 |
| 8 | **透明渲染队列** | RenderItem 增加 blendMode/队列标记；endFrame 分两段：opaque 前→后、transparent 后→前按视距排序；Material 加 alpha 与混合开关，Inspector 可编辑。**痛点**：玻璃/水面/粒子全做不了，透明是大多数游戏的基础需求 | 已有：Renderer 渲染队列、Material；改：MeshRenderer Inspector | M | 两块交叉半透明平面从各角度观察排序正确（目测）；不透明物体行为不变；透明物体不写深度；100 个透明物体排序无肉眼错误；性能回退 < 5%（统计层验证） |
| 9 | **Input 映射系统** | 新增 Core/InputMap 模块：动作（Jump）+ 轴（Horizontal，键盘/鼠标，预留手柄）+ 修饰键；JSON 配置（`assets/input/*.novainput`）+ 编辑器绑定窗口；脚本 API `InputMap::getAxis/getActionDown/getActionUp`。**痛点**：脚本直接查 Key 枚举，改键位=改代码；Unity Input 的动作抽象是游戏可配置性的起点 | 已有：Input；新增：InputMap 模块 + 绑定编辑窗口；改：示例脚本迁移 | M | 脚本用 `getAxis("Horizontal")` 驱动物体移动；编辑器改绑定（WASD→方向键）即时生效并持久化；缺失动作打警告不崩溃；SpinningCube/OrbitCamera 迁移到 InputMap 后行为不变 |
| 10 | **Time::timeScale** | Time 增加 timeScale 与 unscaledDeltaTime；`Scene::update` 传缩放后 dt；脚本可取未缩放时间。**痛点**：暂停菜单/慢动作/击杀回放全靠它，Unity Time.timeScale 是必用 API | 已有：Time、Scene::update 调用点 | S | timeScale=0 时 onUpdate 停摆但编辑器 UI/相机正常；0.5 慢速生效；脚本读到的 deltaTime 已缩放、unscaledDeltaTime 未缩放；恢复 1.0 行为正常且无累积误差 |
| 11 | **场景统计 + Profiler lite** | Stats 覆盖层：FPS、帧时间、update 时间、渲染时间、draw calls、三角形数、透明队列时间；Profiler 分段计时（update/render/UI 三段起步）；F8 开关。**痛点**：无反馈无法优化；也是后续 LOD/剔除/后处理的验收工具 | 已有：Time、ImGui；改：Renderer（计数器）、EditorApplication | S | F8 切换统计层显示 FPS/ms/draw calls/tris；拖 100 个物体后数字变化符合直觉；三段计时与观察一致；Release 下统计开销 < 0.5ms |
| 12 | **gizmo 吸附** | 位置/角度/缩放吸附（预设 0.5m/15°/0.5x + 自定义值），按住 Ctrl 临时启用；用 ImGuizmo 原生 snap。**痛点**：手工对齐几乎不可能；Unity Snap Settings + Ctrl 吸附是关卡拼装基本功 | 已有：GizmoController、ImGuizmo；改：Inspector 或新 Settings 面板 | S | 开启吸附后拖 gizmo 按 0.5m 步进；Ctrl 按住单次吸附、松开恢复自由；旋转/缩放同理；吸附值可配置且重启保留（并入 V1.0-12 设置持久化） |
| 13 | **游戏视图分辨率** | Game View 分辨率预设（1920x1080/1280x720/自由宽高比）与 render scale（0.5–2.0）；Play 时 FBO 按目标分辨率重建、面板内 letterbox 居中显示。**痛点**：目前 Play 用面板任意尺寸，UI 布局/构图无法验证 | 已有：SceneViewPanel FBO、EditorContext | S | 选 1920x1080 后 Play 画面按该分辨率渲染且面板内居中；render scale 0.5 画面变糊、FPS 提升；自由宽高比下无拉伸变形；退出 Play 恢复正常 |
| 14 | **P2 修复包（吞债）** | 集中修复 KNOWN-ISSUES 遗留：内置图元缓存（#3）、TextureLoader 中文路径（#4，全部改 std::filesystem 路径）、模型导入双解析（#6，loadModel 返回材质信息）、JSON 无 Transform 防御（#10）、Play 态禁用 F2/Name/Active/mesh 路径（#8、7.2-2）、Play 中外部拖贴图禁止（7.2-7）、Hierarchy 与 SceneView Ctrl 语义统一（7.2-12）。**痛点**：M1–M5 技术债不还清会持续咬人（中文路径、崩溃防御尤其） | 已有：AssetDatabase/TextureLoader/ModelLoader/SceneSerializer/各面板 | M | 逐条对照 KNOWN-ISSUES 复测并标注「已修/遗留」：中文名贴图加载成功；畸形 JSON 打开报错且场景保持原样；Play 中上述输入框灰显；两处 Ctrl 行为一致；内置图元重复创建不再重传 GPU |
| 15 | **多物体批量编辑** | 多选时 Inspector 显示公共参数并批量应用（Transform/Active/enabled/材质）；gizmo 支持多选（以共同中心为 pivot，整体平移/旋转/缩放）。**痛点**：摆一排柱子要一个个改；Unity 多选编辑是场景搭建效率核心 | 已有：InspectorPanel、GizmoController、EditorContext | M | 选 5 个物体统一改 Scale 全部生效；多选 gizmo 整体平移且相对位置不变；无公共组件时仅显示数量提示；多选含异质组件不崩溃；多选编辑可撤销（接入 V0.2-1） |

### 4.1 V0.2 人工验收清单

- [ ] KNOWN-ISSUES P2 清单 14 项逐项复核，更新 KNOWN-ISSUES.md 标注「已修/遗留」
- [ ] Ctrl+Z 连续 10 步混合操作（gizmo/删除/复制/重命名/重父子）完整还原，Ctrl+Y 正向重放
- [ ] 阴影目测无 acne/peter-panning，旋转 Sun 阴影方向正确
- [ ] 天空盒 + 阴影 + 透明同屏：demo 场景画面接近「Unity 默认观感」的第一档
- [ ] 未保存确认三种选择（保存/不保存/取消）各验一遍
- [ ] 统计层数字与任务管理器/直觉一致，F8 开关无残留
- [ ] **全量回归 M3–M5 验收要点**（选择/描边/gizmo/序列化/拖拽导入/Play 快照恢复），Release 构建通过

---

## 5. V0.3 「游戏化核心能力」

> 目标：物理、音频、运行时 UI、骨骼动画、预制体——让引擎具备「做完整游戏」的核心闭环。本阶段引入两个新第三方库：Jolt（物理）、miniaudio（音频）。

| # | 功能 | 说明与价值 | 依赖 | 工作量 | 验收标准 |
|---|---|---|---|---|---|
| 1 | ✅ **物理引擎集成（Jolt Physics）**（波 2 完成） | 新增 Physics 模块封装 Jolt：Rigidbody（质量/重力/kinematic/休眠）+ Box/Sphere/Capsule/MeshCollider + PhysicsWorld（Scene 级服务，固定步进集成进 update）；射线查询 API；Play 中绘制碰撞体线框。**痛点**：无物理做不了真实游戏；自研刚体接触求解需数月且易炸。**选型结论**：Jolt（MIT、API 现代、CMake/FetchContent 友好、heightfield/character 内置）优于 Bullet（接口散、构建重）；组件层自研抽象（Physics 模块），后端可替换，2D 简版碰撞可自研（V0.5-10） | 新增：Physics 模块 + Jolt（cmake/Dependencies.cmake 加 FetchContent）；新增组件 Rigidbody/Collider；改：Scene::update、SceneViewPanel（调试绘制） | L | 立方体自由落体砸在地面后停稳不穿透；球滚下斜坡；触发器回调（onTriggerEnter）脚本事件；kinematic 刚体推得动动态刚体；30/60/144 FPS 下固定步进行为一致；退出 Play 物理状态随快照还原；无 Jolt 编译失败之外无新增启动依赖 |
| 2 | ✅ **3D 音频（miniaudio）**（波 2 完成） | 新增 Audio 模块：AudioListener（挂相机）、AudioSource（clip/音量/空间混合/衰减/循环）、AudioClip 资产（wav/mp3/flac/ogg 经 miniaudio 解码）；miniaudio 内置 3D 定位（ma_sound 衰减+多普勒）。**痛点**：无声音不成立。**选型结论**：miniaudio（公有领域、单头文件、自带设备管理与 3D 空间化、零外部依赖）优于 OpenAL-Soft（C API + 额外 DLL）与 SoLoud（活跃度低） | 新增：Audio 模块 + miniaudio；新增组件 AudioListener/AudioSource；改：AssetDatabase（AudioClip 缓存）、Inspector | M | 挂 AudioSource 的物体 Play 中播放音效；相机靠近音量大、远离变小；循环开关；多实例同时播放无杂音；退出 Play 停止且无崩溃/爆音；无音频设备时优雅降级并打警告 |
| 3 | ✅ **运行时 UI 系统 v1（与编辑器 ImGui 分离）**（波 2 完成） | 新增 UI 模块：Canvas（屏幕空间，Play 渲染、Edit 可选预览）、Image（含 9-slice）、Text（stb_truetype 图集，含中文）、Button（hover/press + 脚本回调）、锚点+偏移简版布局；批处理 quad 渲染（新增 ui shader），每 Canvas 一次 draw call。**痛点**：血条/按钮/背包是 90% 游戏的必要组成，且必须与编辑器 ImGui 分离（否则无法打包进 Player）。**选型结论**：自研保留模式（对齐 UGUI 心智模型），不把 ImGui 当游戏 UI（风格/输入抢占/打包三宗罪） | 新增：UI 模块 + assets/shaders/ui.vert/.frag；stb_truetype（stb 已有）；改：Renderer（UI 批处理路径）、SceneSerializer（新组件） | L | Canvas 上摆 Image/Text/Button 并随场景序列化往返；Play 中按钮点击触发脚本回调；9-slice 拉伸正确；中文文本渲染；3 种分辨率下锚点布局稳定；编辑器无 Canvas 时不渲染任何游戏 UI |
| 4 | ✅ **PBR 材质升级**（波 1 完成） | blinn_phong 升级为 Cook-Torrance GGX + Fresnel-Schlick 金属工作流；Material 扩展 normalMap/metallicMap/roughnessMap/emission（颜色+贴图）；旧材质 JSON 字段自动映射新模型（向后兼容）。**痛点**：当前 Blinn-Phong 只有三个标量，金属/塑料质感全糊；PBR 是 Unity Standard 画质基准，PBR 贴图套装才能被正确消费 | 已有：Material、Shader、ModelLoader（Assimp 提取 normal 通道）；改：blinn_phong.frag 整体重写、Inspector | M | 金属球/粗糙塑料对比明显可辨；法线贴图凹凸可见；emission 发光生效；旧场景 JSON 打开参数不变；性能退化 < 10%（统计层验证） |
| 5 | **贴图导入设置（.meta 伴随文件）** | 每个导入贴图生成 `.novameta` 伴随文件：类型（sRGB/线性/法线）、wrap（repeat/clamp）、filter、mip 开关；TextureLoader/AssetDatabase 消费；Asset Browser 右键编辑。**痛点**：目前所有贴图同一套参数，法线贴图按 sRGB 读就错了；Unity Texture Import Settings 是资产正确性保障 | 已有：Texture/TextureLoader/AssetDatabase；新增：meta 读写器；改：AssetBrowserPanel | M | 标记为法线贴图的贴图以线性加载且切向采样正确；clamp 设置生效；外部改 meta 后 Reload Assets 生效；meta 随项目目录拷贝可移植；无 meta 文件时按默认规则自动生成 |
| 6 | ✅ **材质库（.novamat）**（波 1 完成） | 材质资产文件（JSON：PBR 参数+贴图引用）；Asset Browser 创建材质、双击编辑；MeshRenderer 改为「引用材质资产 or 内联」，Inspector 拖放赋值。**痛点**：每个物体内联材质，改一堵墙要改 100 个物体；Unity 材质资产共享是内容生产基本盘 | 已有：SceneSerializer、AssetDatabase、Inspector；改：Schema（material 段加 asset 引用） | M | 创建材质资产改 albedo，所有引用该材质的物体同步变色；场景引用材质资产可保存/加载/跨场景复用；内联材质仍可用（向后兼容）；材质资产缺失时场景加载不崩溃并提示路径 |
| 7 | **资产二进制缓存** | 首次导入把解码结果（像素、MeshData、材质、meta）序列化到 `assets/.novacache`（内容 hash + 格式版本命名）；命中直接读盘跳过 Assimp/stb。**痛点**：大场景启动重跑 Assimp，启动秒级→十秒级；Unity 的 Library 缓存同理 | 已有：AssetDatabase/ModelLoader/TextureLoader；新增：缓存读写器 | M | 二次启动加载 50 个模型 < 2s；删除缓存后重建正确；源文件修改（hash 变化）后缓存自动失效重导入；缓存损坏回退重导不崩溃；缓存目录可安全删除 |
| 8 | ✅ **协程**（波 2 完成） | 基于 C++20 coroutine 的最小任务类型 + `Behaviour` 内 StartCoroutine（yield 帧/秒/条件：WaitForSeconds/WaitForEndOfFrame/WaitUntil/WaitForCoroutine）；协程随对象销毁/退出 Play 自动终止。**痛点**：延时/序列逻辑靠 onUpdate 手写计时状态机，代码碎且易错；Unity 协程是脚本核心习惯 | 已有：Behaviour/Component 生命周期、Scene::update | M | StartCoroutine 内 `yield WaitForSeconds(2)` 后执行后续逻辑；对象 destroy 后协程不再执行；退出 Play 无悬挂协程；协程内再 StartCoroutine 不崩；1 万并发协程帧开销 < 2ms |
| 9 | ✅ **Prefab 系统 v1**（波 3 引擎侧 + 波 4 编辑器侧完成） | `.novaprefab` 资产 = 子树 JSON（复用 SceneSerializer）；PrefabInstance 记录（源路径+每对象源 ID）；实例化（拖入场景）、Apply（写回）、Revert、override 标记（Inspector 加粗）；v1 支持嵌套（Apply 时展开写回）。**痛点**：没有 Prefab 无法复用敌人/道具/UI 组件树，内容生产瘫痪；Unity Prefab 是组件化内容工作流心脏 | 已有：SceneSerializer（V0.2-3 深拷贝已修复）、AssetDatabase、undo 栈；改：Hierarchy 右键、Inspector override 显示 | L | 拖 Prefab 进场景生成实例；改实例参数 Apply 后其余实例同步；override 字段加粗、Revert 恢复；删除源资产后场景打开不崩溃（提示缺失）；嵌套 Prefab 可用；Apply/Revert 均可撤销 |
| 10 | ✅ **Play 模式增强**（收官批次+波 2 完成） | 帧步进 Step（Pause 中逐帧推进物理+脚本）；进入 Play 未保存提醒；Play 中勾选显示碰撞体/触发器线框；Play 相机跟随选中对象（可选）。**痛点**：逐帧调试物理是 Unity 老用户核心依赖；未保存进 Play 误丢编辑态 | 已有：EditorApplication 状态机、SceneViewPanel；改：物理模块联调（V0.3-1） | S | Pause 中按 Step 场景精确前进一帧（物理与脚本各一帧）；Play 中可显示 collider 线框（开关式）；进入 Play 前有未保存更改时提示；Stop 后一切还原 |
| 11 | ✅ **事件系统**（波 2 完成） | 新增 Events 模块：类型化事件 + subscribe/unsubscribe + 延迟投递（队列式，帧末 flush）；对象销毁自动清理订阅。**痛点**：脚本间通信靠 findScript 硬引用，耦合脆弱；Unity C# event 模式是标准解耦手段 | 新增：Events 模块；改：Behaviour 生命周期钩子 | M | 脚本 A 广播 OnDamage 事件，B/C 均收到；B 销毁后不再收到（无悬挂）；帧内投递不重入崩溃；1 万次/帧 < 1ms；事件对象可携带数据 |
| 12 | ✅ **游戏状态管理（SceneManager）**（波 3 完成） | 场景索引（Build Settings 列表）、`LoadScene(name)` 运行时加载/卸载、additive 预留、DontDestroyOnLoad 标记（GameObject 标志位）。**痛点**：目前 Play 中不能换场景；游戏=多场景（菜单/关卡/结算），Unity SceneManager 是游戏结构骨架 | 已有：SceneSerializer、Scene；新增：SceneManager 模块 + Build Settings 窗口 | M | Play 中脚本调用 LoadScene 切换到另一场景；DontDestroyOnLoad 对象跨场景存活；加载失败打错误日志不崩溃；Build Settings 配置场景列表并持久化；退出 Play 回到编辑场景 |
| 13 | **骨骼动画与蒙皮**（V0.3 顺延 V0.5，用户裁定：Prefab 优先） | SkeletalMeshRenderer 组件 + ModelLoader 提取 aiBone/aiNodeAnim/aiMeshAnim（骨骼、蒙皮权重、动画通道）+ 顶点格式扩展（骨索引/权重）+ Renderer skinning 路径（骨骼矩阵 UBO）+ Animator 组件（播放/循环/速度/简版混合）。**痛点**：无骨骼动画只能做静态场景；角色动画是 3D 游戏入场券，也是 Assimp 利用率最大提升点 | 已有：ModelLoader/MeshData、AssetDatabase；新增：Animation 模块 + SkeletalMeshRenderer/Animator 组件；改：Renderer（skinning 着色路径，MeshData 顶点格式扩展——属冻结接口变更，须先更新 ARCHITECTURE.md，见 §10） | L | 导入 Mixamo 角色 FBX/glTF 正确蒙皮显示；播放 walk 动画循环流畅、顶点无脱离；两段动画简版混合；非骨骼模型路径不受影响；序列化往返无损；统计层确认骨骼 UBO 更新开销 < 0.2ms/角色 |
| 14 | ✅ **后处理管线（Bloom + Tonemapping）**（波 3 完成） | SceneView 渲染重构为 HDR 中间 FBO → 后处理链（Bloom：亮度阈值+降采样高斯+合成；ACES Tonemapping）→ 输出；参数进环境设置面板。**痛点**：无 HDR/后处理画面「平」；Bloom+Tonemap 是 Unity 默认观感的另一半（配合 PBR/阴影） | 已有：Framebuffer；新增：PostProcess 模块 + 3~4 个 GLSL；改：SceneViewPanel 渲染路径（依赖 V0.2-6 多 pass 编排） | L | 高亮物体（emission>1）产生 Bloom 光晕；ACES 映射后不过曝；开关后处理 A/B 对比明显；浮点 FBO 不支持时降级 8bit 路径不崩溃；1080p 下链开销 < 3ms |
| 15 | **项目模板** | 「新建项目」向导：模板（空/第一人称控制器/第三人称跟随）生成标准目录（assets/scenes/scripts）+ 预置脚本（角色控制器 = 物理 + InputMap + 音频脚步）+ README 构建说明。**痛点**：每项目重搭控制器；Unity 模板让「今天立项今天跑起来」 | 已有：V0.2 Input/V0.3 物理音频；新增：模板目录生成器 + 编辑器菜单 | M | 新建第一人称模板项目开箱 Ctrl+P 可 WASD+鼠标操作角色走动、带脚步声；目录规范符合资产管线；模板场景可保存/加载；V0.5 打包落地后模板可一键出 exe |

### 5.1 V0.3 人工验收清单

> **波 2 状态（2026-11-15）**：V0.3-1/2/3/8/11 代码已交付闭环——74 源文件全量构建零错误零第一方警告、两 exe 冒烟通过（编辑器守卫 15 条验证、Player 场景加载+旋转正常）。以下物理/音频/UI 交互项为**人工 Play 态验收项**，待实际搭建测试场景目测确认（自动冒烟不覆盖交互手感）。
> **波 3 状态（2026-11-15）**：V0.3-12/14 代码已交付闭环——四 target 全绿、后处理双路径（16F 开启 / RGBA8 直通）实跑通过、4 个 post shader 首编译零错误。**最终验收待补两项**：① 双 FBO resize/格式切换分支（手动拉伸窗口 + 切换 pp.enabled 复核）；② LoadScene 端到端脚本触发（临时脚本验证 DDOL 存活 + 退出 Play 快照恢复）。
> **波 4 状态（2026-11-15，V0.3 收官）**：V0.3-9 Prefab 全链路代码闭环——Debug+Release 双配置构建全绿（Jolt 首次 Release 编译通过、Release 双 exe 产出）、守卫 16 条验证、双 exe 冒烟通过、Reviewer 2 P1+4 P2 全部修复。Prefab 交互验收项（拖放实例化/字段高亮/Apply/Revert/undoFull 文件恢复/删源不崩）为 GUI 人工验收范畴，不阻塞合波。

- [ ] 物理：10 个刚体塔堆叠 10 秒不塌穿；球滚斜坡；kinematic 推挤；30/60/144 FPS 三档固定步进行为一致（波 2 代码完成，待人工验收）
- [ ] 音频：近大远小、多音源混音、循环、快速进出 Play 无爆音（波 2 代码完成，待人工验收；无设备降级路径已代码化）
- [ ] UI：血条/按钮/文本在 16:9 与 21:9 下布局稳定、序列化往返、点击不穿透（波 2 代码完成，待人工验收）
- [ ] 动画：Mixamo 角色 walk/idle 切换、蒙皮无顶点脱离、编辑器 Play 反复进出无泄漏（顺延 V0.5，见 V0.3-13 批注）
- [ ] **画面总验收**：PBR + 阴影 + 天空盒 + Bloom/Tonemap 同屏，目测达到「Unity 默认观感」可接受档（波 3 代码完成，待人工目测）
- [ ] 场景管理：Play 中 LoadScene 切换、DDOL 存活、失败不崩、Build Settings 持久化、退出 Play 回编辑场景（波 3 代码完成，LoadScene 端到端待人工验收）
- [ ] Prefab 四路径：Apply/Revert/嵌套/缺失资产，全部不崩溃且行为符合预期（波 4 代码完成，交互验收待人工）
- [ ] 材质库：改共享材质一处、百处同步；场景引用材质资产跨场景复用（波 1 完成）
- [ ] 全量回归 V0.2 验收清单

---

## 6. V0.5 「发布能力与规模内容」

> 目标：一键打包独立 exe（质变点）、脚本热重载、粒子/LOD/地形/2D/IBL 等规模内容能力。

| # | 功能 | 说明与价值 | 依赖 | 工作量 | 验收标准 |
|---|---|---|---|---|---|
| 1 | **构建与打包独立 exe** | 新增 NovaPlayer target（引擎+运行时主循环，无 ImGui 编辑器依赖，复用渲染/脚本/物理/音频）；资源收集器扫描场景引用 → 收集 assets（含二进制缓存产物）→ `.novapak` 单文件打包；Player 支持 `--scene` 启动参数；发布目录 = exe + pak。**痛点**：目前「游戏」只能在编辑器里 Play；能交付独立 exe 是「替代 Unity」的质变点 | 新增：Player 工程 + Packager 工具；已有：V0.3-7 二进制缓存；改：引擎与编辑器解耦（UI 模块不依赖 ImGui——V0.3-3 已保证） | L | 一键打包 demo 场景为独立 exe，双击运行画面/脚本/音频/UI/物理与编辑器 Play 一致；打包时资源缺失直接报错（不带到运行时）；pak 资源加载正确；Release 产物 < 100MB；目标机无开发环境可运行 |
| 2 | **命令行批处理** | NovaEditor 支持 headless 命令：`import`（全量导入生成缓存）、`build`（打包）、`--quit`；退出码规范；供 CI 调用。**痛点**：无 CLI 无法自动化；Unity `-batchmode` 是 CI/量产流水线基础 | 已有：V0.5-1 Packager、资产管线；改：EditorApplication 无窗口模式 | M | 命令行全量导入 500 资产退出码 0；无窗口模式不弹 GUI；build 命令产出完整发布目录；失败退出码非 0 且 stderr 有日志；同输入两次 build 产物一致（可复现） |
| 3 | **LOD 系统** | LODGroup 组件：按距离阈值切换网格（手动指定 LOD0/1/2 网格资产）；相机距离驱动切换；可选 cross-fade；以统计层验证收益。**痛点**：大量重复植被/建筑下 draw call/三角形爆炸；Unity LOD Group 是大场景基本工具 | 新增：LODGroup 组件；改：MeshRenderer 联动、SceneViewPanel 渲染遍历 | M | 三档 LOD 按距离正确切换（目测+统计层三角形数变化）；切换无闪烁（可选 dither）；LODGroup 随场景序列化往返；每对象组切换开销 < 0.1ms |
| 4 | **IBL 环境光照** | HDRI 加载（等距柱状→cubemap）→ 辐照度卷积（diffuse irradiance）+ 预滤波 specular mip 链（compute shader）；PBR 采样间接光；天空盒与 IBL 同源；卷积结果磁盘缓存。**痛点**：PBR 只有直接光时金属全黑；IBL 是 PBR 的间接光来源与场景氛围决定因素 | 已有：V0.2-7 天空盒/HDRI、V0.3-4 PBR；新增：IBL 卷积（GL 4.3+ compute shader）+ 2~3 个 GLSL | L | 金属球在 HDRI 环境中呈现反射图像；粗糙度 0→1 间接高光由锐利变柔和（mip 链正确）；无 HDRI 时回退程序化环境不黑屏；卷积结果缓存后二次启动 < 1s；统计层开销 < 1ms |
| 5 | **SSAO** | 屏幕空间 AO：深度+法线重建 → 半球采样遮蔽 → 模糊 → 合成；与后处理链集成；参数（半径/强度）进环境设置。**痛点**：物体接触处缺乏遮蔽层次；SSAO 让角落「沉下去」，是体积感第三件套（阴影+IBL+AO） | 已有：V0.3-14 后处理链；新增：SSAO shader（2 个）；改：渲染路径加深度/法线输出 | M | 墙角/物体交接处出现柔和暗部；开关 A/B 明显；1080p 开销 < 2ms；无闪烁抖动（模糊后）；随场景序列化 |
| 6 | **粒子系统 v1** | ParticleSystem 组件：发射率/寿命/初速/重力/颜色渐变/大小渐变/纹理条（flipbook）；GPU 实例化渲染（单 draw call）；Inspector 参数面板 + 预览播放。**痛点**：火花/烟雾/雨雪是氛围必备；Unity Particle System 是独立开发者最常用内容工具之一 | 新增：Particles 模块 + 2 个 GLSL；改：Renderer instancing 路径、Inspector | L | 火焰粒子（纹理条动画）Play 中发射/消亡正确；1000 粒子 < 2ms 且单 draw call；参数编辑即时预览；序列化往返；退出 Play 粒子清零 |
| 7 | **脚本 DLL 热重载** | NovaScripts 从 OBJECT 静态库改为 DLL：编辑器 LoadLibrary 加载、脚本列表动态注册；重载 = 快照脚本字段（复用序列化）→ FreeLibrary → 重新加载 → 恢复字段。**复杂度评估结论**：可行（L 级）——字段经 JSON 保真、对象指针跨 DLL 稳定（引擎内存不被卸载）、CRT /MD 全项目一致（已保证）；需接受「脚本静态变量重置」并写入文档；KNOWN-ISSUES #9（MSVC typeid 反查）随注册表重构一并解决。**痛点**：改脚本必须重启编辑器+重载场景，脚本迭代是独立开发最大时间杀手 | 已有：ScriptRegistry、序列化、CMake；改：samples 变 DLL target、注册机制（DLL 导出注册函数替代静态初始化）、编辑器检测重编译 | L | 改 SpinningCube 代码重编译后编辑器内自动热重载、场景对象脚本字段（speed=30）保留、无需重启；热重载后 Play 正常；编译失败时旧 DLL 保留运行并报错；连续 10 次热重载无内存增长/崩溃 |
| 8 | **地形 v1** | Terrain 组件：高度图（512² 起）+ 多材质混合（splat）+ 法线；编辑器笔刷（抬升/压低/平整/平滑，半径/强度）；碰撞用 Jolt heightfield。**痛点**：大地面目前只有 40m 网格；高度图地形是室外场景基础 | 已有：Renderer、Jolt（heightfield）、SceneView 射线求交；新增：Terrain 组件 + 地形 shader + 笔刷交互 | L | 512x512 高度图地形渲染+纹理混合正确；笔刷抬升/平整即时生效并可保存；刚体在地形上行走碰撞正确；中等画质 60 FPS；序列化往返 |
| 9 | **动画控制器（Animator 状态机）** | 升级 V0.3-13 简版播放：状态机（状态/参数/过渡条件/过渡混合时长）+ 编辑面板（列表式起步，节点图可选）；角色控制器动画集成示例。**痛点**：单动画播放做不出角色控制（走/跑/跳混合）；Unity Animator 是角色游戏标准解 | 已有：V0.3-13 骨骼动画模块；新增：Animator 状态机组件 + 编辑器面板 | L | walk→run→idle 按速度参数自动过渡且混合平滑；过渡不跳变；状态机配置随场景/资产序列化；示例角色走跑跳组合正确 |
| 10 | **2D 模式** | 正交相机（Camera 加 projection 模式）+ SpriteRenderer（四边形+贴图+排序层）+ 2D 碰撞简版（AABB 重叠 + 2D 物理模式）；新建场景可选 2D 模板。**痛点**：2D 游戏是独立游戏最大品类，Unity 2D 工作流极其成熟 | 已有：Camera、Renderer、Physics 模块；新增：SpriteRenderer、2D 碰撞组件 | L | 正交场景中 sprite 正确渲染与排序（排序层生效）；2D AABB 碰撞回调正确；2D 模板场景开箱可玩；2D 与 3D 场景共存不互扰 |
| 11 | **游戏手柄输入** | GLFW joystick API → InputMap 轴/按钮源；热插拔处理；震动（可选）。**痛点**：键鼠之外手柄是平台/动作游戏标配 | 已有：Input、InputMap（V0.2-9） | S | Xbox 手柄左摇杆驱动移动、A 键触发动作；热插拔不崩溃；无手柄时回退键鼠；绑定窗口可配置手柄映射 |
| 12 | **存档系统** | SaveGame：场景运行时状态（Transform、脚本字段、激活态）序列化为存档文件（复用 SceneSerializer + 运行时实例映射）；存档槽管理（多槽/新建/覆盖）。**痛点**：没有存档做不了任何有进度的游戏；引擎提供骨架省掉重复劳动 | 已有：SceneSerializer、SceneManager（V0.3-12）、文件 I/O | M | Play 中存档→退出→重载场景→读档恢复 Transform/字段/激活态；损坏存档不崩溃；多槽位管理正常；跨版本存档有 version 校验 |
| 13 | **UI 系统增强** | 锚点布局完善（全锚点拉伸）、文本换行/对齐、Slider/ScrollView/Image fill、UI 事件冒泡与射线拦截（Play 中 UI 优先于场景拾取）。**痛点**：背包/设置菜单需要完整控件集；UGUI 布局体系是 UI 生产力关键 | 已有：UI 模块（V0.3-3）；改：SceneViewPanel 输入分发 | M | 设置菜单（滑条+按钮+滚动列表）在 3 种分辨率下布局正确；Play 中点击 UI 不穿透触发场景选择；fill 型血条随脚本更新；文本换行与对齐正确 |
| 14 | **性能优化包** | 视锥剔除（相机 frustum-AABB 相交）+ 静态网格实例化（同 mesh+材质 instanced draw）+ 队列状态排序优化；全部以统计层数字回归验证。**痛点**：内容规模上来后需要硬优化才能 60FPS；Unity 剔除/合批是场景性能底线 | 已有：Renderer、Camera、场景统计；改：SceneViewPanel 遍历（剔除前置） | M | 2000 个立方体场景：无剔除 30FPS → 剔除+实例化后 60FPS（draw call 从 2000 → <20）；剔除无漏绘（目测+统计）；Release 回归通过 |
| 15 | **网络多人预研（spike，可选）** | 评估性任务：确定性帧同步原型（P2P 局域网，固定步进输入同步）1 周 spike；产出结论文档（成本/收益/是否纳入 V1.0+）。**痛点**：多人需求高发但成本极高，先 spike 后决策，避免路线图被拖垮 | 已有：固定步进（物理模块）；新增：Net spike 代码（不进主分支） | L（spike，按结论决定后续） | 两台机器局域网同步移动方块（输入帧同步）可运行；结论文档给出 V1.0 是否纳入的明确建议与成本拆解 |

### 6.1 V0.5 人工验收清单

- [ ] 打包 exe 在干净机器（无 VS/CMake/开发环境）双击运行完整 demo，画面/音频/物理/UI 与编辑器一致
- [ ] 性能：2000 物体场景 Release 60FPS（统计层证据）；LOD 切换三角形数变化符合预期
- [ ] 角色 demo：Mixamo 角色 + 动画状态机 + 物理角色控制 + 地形行走 + 手柄，一套连贯可用
- [ ] 热重载：连续 10 次改脚本热重载，无内存增长/崩溃，字段保留
- [ ] 2D 模板游戏 30 分钟内搭建跑通
- [ ] 命令行：headless 导入+打包全程退出码正确，产物可复现
- [ ] 全量回归 V0.2/V0.3 验收清单

---

## 7. V1.0 「商业独立游戏发布级」

> 目标：把「能用」变成「敢押注下一款商业游戏」：完整 Profiler、多场景流式加载、存档、示例游戏、文档、CI。

| # | 功能 | 说明与价值 | 依赖 | 工作量 | 验收标准 |
|---|---|---|---|---|---|
| 1 | **Profiler 完整版** | CPU 火焰图（脚本级耗时、模块分段）、GPU timer query（各 pass 耗时）、内存统计（资产/场景占用）、帧时间线抓取回放。**痛点**：调不动性能=发不了版；Unity Profiler 是发布前必经之路 | 已有：V0.2-11 统计层；新增：Profiling 模块（环形帧缓冲+UI） | L | 录制 10 秒帧数据定位到最慢脚本函数；GPU 各 pass 耗时可见；内存分类统计与实际一致；Profiler 自身开销 < 1ms |
| 2 | **多场景工作流** | additive 场景加载/卸载（流式）、场景间参数传递、异步加载进度（后台资产加载线程）。**痛点**：单场景做不了开放关卡/大厅+关卡结构 | 已有：SceneManager（V0.3-12）、资产管线；新增：可选加载工作线程 | L | 大厅+关卡 additive 共存、切换无全黑帧（异步进度条）；卸载后内存回落；后台加载不卡主线程；场景间参数传递正确 |
| 3 | **物理增强** | CharacterController（胶囊+楼梯步进）、完整射线/形状查询 API（hit 信息、过滤）、层碰撞矩阵配置、物理材质（摩擦/弹性）。**痛点**：角色控制/拾取/视线检测的物理 API 完备性 | 已有：Physics 模块（Jolt）；新增：CharacterController 组件、层矩阵配置 | M | 角色控制器上下楼梯/斜坡不卡住；射线查询返回完整命中信息；层过滤生效；摩擦/弹性参数效果可见 |
| 4 | **色彩分级与后期完善** | LUT 色彩分级（.cube 加载）、DOF 简版（可选）、渐晕/胶片颗粒；后期链参数随场景保存。**痛点**：画面风格化的最后一公里 | 已有：V0.3-14/V0.5-5 后处理链 | M | LUT 切换氛围对比明显；后期设置序列化往返；链上各环节开销可在 Profiler 查看 |
| 5 | **可视化着色器编辑器（Shader Graph 简版）** | 节点编辑器（纹理采样/数学/UV/混合等常用节点）生成 GLSL；材质引用生成的 shader。**痛点**：非程序员也想调材质表现；这是 Unity 用户习惯的可视化入口 | 已有：ImGui（节点 UI 自绘）、Shader、材质库；新增：.novashadergraph 资产 + 代码生成 | L | 用节点连出「颜色×纹理+发光」shader 并赋给材质生效；生成 shader 编译错误可定位到节点；保存/加载图资产；不支持的特性明确提示而非静默错误 |
| 6 | **纹理压缩与批量导入** | BC7/BC1/BC5 压缩（生成 .dds 或 GL 压缩上传）、批量导入重缓存、纹理内存统计。**痛点**：大项目 GPU 内存与包体控制 | 已有：TextureLoader、二进制缓存（V0.3-7） | M | 压缩后 GPU 内存显著下降（Profiler 可见）；画质损失可接受；批量导入时间可接受；压缩格式不支持时自动回退 |
| 7 | **资产引用完整性** | 缺失引用扫描（场景/材质/预制体中指向不存在的路径）、资产移动重链、打包前完整性检查（并入 V0.5-1）。**痛点**：删资产留断链是项目事故高发区 | 已有：序列化、AssetDatabase；新增：引用扫描工具窗口 | M | 删除贴图后扫描器列出所有引用缺失场景；移动资产后引用自动更新；打包遇缺失引用直接失败并列出清单 |
| 8 | **音频完善** | 混音器（主音量/分组总线）、背景音乐流式播放、淡入淡出、音频设置持久化。**痛点**：音量/总线是音频工作流的收尾 | 已有：Audio 模块（V0.3-2） | M | 音量总线调节实时生效；长 BGM 流式播放内存平稳；淡入淡出无爆音；设置保存 |
| 9 | **测试框架 + CI** | 核心模块单元测试（数学/序列化往返/场景操作/资产缓存）+ 冒烟脚本化；GitHub Actions 构建（Debug/Release）+ 测试。**痛点**：回归靠人肉，破坏性改动防不住 | 已有：CMake；新增：测试 target（Catch2 或手写断言）、CI 配置 | M | CI 全绿（构建+测试）；序列化往返测试覆盖全部组件类型；破坏性改动被 CI 拦截 |
| 10 | **示例游戏项目** | 完整小游戏（第一人称探索或平台跳跃，1–2 关）：作为模板/回归基准/宣传材料，全部用引擎自带功能实现。**痛点**：引擎行不行的最终证明；也是每次发版前的回归样本 | 已有：全部已交付功能 | L | 游戏可打包独立运行；通关流程完整；每次发版前通关冒烟；代码作为最佳实践范本 |
| 11 | **发布流水线** | 版本信息注入（版本号/图标/公司名，Windows RC 资源）、窗口元数据、发行目录整理（exe+pak+文档）、Steam 上传目录结构准备。**痛点**：从「能跑」到「能发行」的收尾工程 | 已有：V0.5-1 Packager、CMake | M | exe 属性含正确版本/图标；发行目录双击即玩；打包产物可复现；目录结构符合 Steam 上传要求 |
| 12 | **编辑器细节完善** | ImGui ViewportsEnable 多视口（评估 DPI 后开启）、最近文件列表、编辑器设置持久化（吸附值/主题/快捷键自定义）、三角形级拾取（替代 AABB 粗测，#11）。**痛点**：编辑体验细节决定长期使用意愿 | 已有：ImGui docking、EditorContext；改：拾取路径 | M | 面板可拖出主窗口；重启编辑器设置保留；最近文件列表可用；大平面不再遮挡后方物体拾取 |
| 13 | **内存与泄漏审计** | 资产生命周期审计（releaseUnused 自动化、LRU 淘汰可选）、场景切换泄漏检测、脚本卸载路径验证（配合热重载）。**痛点**：长跑泄漏会毁掉发布 | 已有：AssetDatabase、SceneManager、Profiler | M | 100 次场景切换内存回到基线；Play 100 次无增长；VS 诊断无未释放资产；热重载路径无泄漏 |
| 14 | **文档化与迁移指南** | 脚本 API 手册、新手教程（从零到打包一个游戏）、Unity→Nova 迁移对照表（常用 API 对照）。**痛点**：没有文档引擎不可传播、不可被他人使用 | 已有：全部功能 | M | 新用户按教程 1 天完成一个小游戏打包；Unity 用户按对照表完成 API 迁移；文档与代码行为一致（以实际代码为准） |

### 7.1 V1.0 人工验收清单

- [ ] 发行验收：干净 Win11 机器双击 exe 通关示例游戏（含存档/读档）
- [ ] 性能验收：示例游戏 1080p 60FPS（中端集显可玩）
- [ ] 稳定性：8 小时编辑器马拉松（连续编辑/Play/热重载/打包）无崩溃、内存无泄漏
- [ ] CI 全绿；文档与代码同步
- [ ] 全量回归 V0.2/V0.3/V0.5 验收清单

---

## 8. 明确不做 / 超期可选

| 项目 | 理由 |
|---|---|
| Nanite 级虚拟几何、Lumen/光追 GI、GPU-driven/bindless | 超出 OpenGL 4.5 + 单线程 + 小型团队的能力圈；收益对中型单机游戏有限 |
| 开放世界流式地形（多块 LOD 地形流式加载） | V0.5-8 的 512²–1024² 高度图足以覆盖目标品类 |
| Bolt 级完整可视化脚本 | V0.5 只做 Shader Graph 简版；逻辑可视化编程成本极高、收益存疑 |
| 控制台/移动端/Web 移植 | Windows x64 单平台策略 |
| MMO 级网络 | V0.5-15 spike 出结论前不承诺任何多人能力 |
| VR/XR、资产商店、多人协作编辑 | 与「1–3 人独立团队、中型单机」定位不符，会稀释核心路线 |

---

## 9. 实现顺序建议（低垂果实）

> 排序维度：用户感知强度（对「替代 Unity」的贡献）/ 实现成本。**先做工具链可信，再做画面，再做游戏性，最后做规模。**

| 优先级 | 功能 | 归属 | 理由 |
|---|---|---|---|
| 1 | Undo/Redo（快照式） | V0.2-1 | 感知最高（Unity 用户第一肌肉记忆）、成本最低（快照复用现有序列化，M 级） |
| 2 | 方向光阴影映射 | V0.2-6 | 单点改动带来最大画面质变，直接改变「这引擎行不行」的第一印象 |
| 3 | 脏标记消费（未保存确认） | V0.2-2 | 小时级成本，数据安全信任基础，不做的代价是用户持续丢活 |
| 4 | 场景统计 + Profiler lite | V0.2-11 | 一切后续性能工作的验收工具，越早越好 |
| 5 | 天空盒 | V0.2-7 | M 级成本、观感提升大、且是 V0.5 IBL 的数据源前置 |
| 6 | Input 映射 + timeScale | V0.2-9/10 | 成本低，直接解锁「写真正游戏逻辑」的脚本体验 |
| 7 | 透明排序 | V0.2-8 | M 级成本，解锁玻璃/水面/粒子等内容类型 |
| 8 | 物理（Jolt） | V0.3-1 | 游戏性门槛；集成 Jolt 而非自研，风险可控 |
| 9 | PBR + 材质库 + .meta | V0.3-4/5/6 | 画质基准线与资产正确性，是后续一切渲染特性的底座 |
| 10 | 打包独立 exe | V0.5-1 | 感知极高但依赖多（缓存/UI 分离/Player），故排到 V0.5 期首 |

**顺序铁律**：V0.2-3（深拷贝）必须先于 V0.3-9（Prefab）；V0.2-6（多 pass 编排）先于 V0.3-14（后处理）与 V0.5-4/5（IBL/SSAO）；V0.3-7（二进制缓存）先于 V0.5-1（打包）；V0.3-4（PBR）先于 V0.5-4（IBL）。

---

## 10. 架构影响评估

### A. 直接在现有组件系统上扩展（新 Component / Scene 级服务，不动架构）

| 项目 | 落点 |
|---|---|
| Rigidbody / Collider / CharacterController | 新 Component（engine/Scene/），PhysicsWorld 为 Scene 级服务 |
| AudioSource / AudioListener | 新 Component；AudioEngine 为引擎级单例（miniaudio 内建线程） |
| Canvas / Image / Text / Button | 新 Component 体系（engine/UI/），Canvas 为 UI 子树根组件 |
| SkeletalMeshRenderer / Animator / LODGroup / ParticleSystem / SpriteRenderer / Terrain | 全部新 Component；渲染遍历处扩展（SceneViewPanel 提交路径 + 未来 Player 共享的 RenderScene 函数） |
| DontDestroyOnLoad | GameObject 标志位 + SceneManager 迁移逻辑 |
| InputMap / timeScale / EventBus | Core 扩展 + 独立小模块，无架构冲击 |

### B. 需要新模块 / 新目录 / 新第三方库

Physics（Jolt）、Audio（miniaudio）、UI（stb_truetype）、Animation、Particles、PostProcess、ShadowMap、Profiling、SceneManager、Packaging（编辑器侧工具）、Player（新可执行 target）。新第三方库仅 Jolt + miniaudio 两个，需 Architect 批准并更新 cmake/Dependencies.cmake。

### C. 渲染器改造（本路线图最大的架构风险点，需逐步演进）

1. **多 pass 编排**：`Renderer` 从「beginFrame → submit → endFrame」单 pass 演进为「shadow depth → sky → opaque → transparent → post → UI」的帧内 pass 序列（V0.2-6 起步）。保持单线程、保持静态接口风格，仅内部重构。
2. **顶点格式扩展**：骨骼蒙皮（V0.3-13）要求 `MeshData::Vertex` 增加骨索引/骨权重——**冻结接口变更**，必须先更新 ARCHITECTURE.md 并给非蒙皮网格留兼容路径（蒙皮网格用独立顶点布局）。
3. **材质系统升级**：PBR（V0.3-4）扩展 Material 贴图槽与 uniform 块；材质资产（V0.3-6）引入「引用 or 内联」双轨。
4. **FBO 链**：后处理（V0.3-14）把 SceneView 直接渲染改为 HDR 中间缓冲 + 链式处理；Framebuffer 需支持浮点颜色附件与降级路径。
5. **Instancing 与 UI 批处理**：粒子（V0.5-6）与 UI（V0.3-3）各加一条专用绘制路径。
6. **风险控制**：每一步以场景统计数字回归；渲染遍历逻辑从 SceneViewPanel 抽出为「场景渲染函数」（Player 复用），避免编辑器与运行时渲染漂移。

### D. 新线程

仅两处：miniaudio 设备线程（库内部，API 单线程）；V1.0 异步资产加载线程（可选，加载队列+主线程提交 GPU 上传）。**物理、渲染、脚本、UI 全部保持主线程**（确定性、可调试性，符合 CLAUDE.md「不要多线程渲染」）。

### E. 脚本架构变化（静态链接 → DLL）

V0.5-7：samples 从 OBJECT 库改 DLL target；注册机制从静态初始化改为 DLL 导出注册函数（沿用 NE_REGISTER_SCRIPT 形态）；ScriptRegistry 支持卸载/重注册；MSVC typeid 反查（#9）随重构解决。风险点：DLL 边界 ABI（/MD 已全项目一致）、脚本静态变量重置（文档化取舍）、链接器裁剪（导出注册函数无此问题）。

### F. 序列化 schema 演进

`version` 字段已预留：材质资产引用（V0.3-6）、蒙皮/物理/UI 等新组件、环境面板字段（V0.2-5）均 bump version 并保持「未知字段忽略 + 未知组件警告跳过」的容错策略；`.meta`/`.novamat`/`.novaprefab`/`.novainput` 为新资产类型，与 `.nova.json` 场景格式并列；二进制缓存（V0.3-7）仅是解码缓存，不承载源数据。

---

## 11. 关键技术选型结论汇总

| 领域 | 结论 | 一句话理由 | 备选（否决原因） |
|---|---|---|---|
| 物理 | **集成 Jolt Physics**，组件层自研抽象（Physics 模块） | MIT、API 现代、CMake 友好、固定步进确定性好、heightfield/character 内置 | Bullet（接口散、构建重）；完全自研刚体（数月起步、易炸） |
| 音频 | **miniaudio** | 公有领域、单头文件、自带设备管理与 3D 空间化、零外部依赖 | OpenAL-Soft（C API+额外 DLL）；SoLoud（维护活跃度低） |
| 运行时 UI | **自研保留模式 UI 模块**（Canvas/Image/Text/Button，stb_truetype），与编辑器 ImGui 分离 | 打包独立性、输入不抢占、风格可控 | 复用 ImGui 当游戏 UI（风格违和/输入冲突/无法进 Player） |
| 协程 | **C++20 coroutine 最小封装** | 标准库原生支持、语法接近 Unity yield | 手写状态机（代码碎）；引入第三方协程库（多余） |
| 脚本热重载 | **DLL 化 + 序列化状态保真**（V0.5 落地，复杂度 L，可行） | 字段经 JSON 保真、指针跨 DLL 稳定、/MD 已一致；接受静态变量重置 | 解释型脚本语言（偏离 C++ 定位，成本更大） |
| 网络 | V0.5 spike（确定性帧同步原型）后决策 | 先验证再承诺，防止路线图被多人拖垮 | 直接立项多人（风险过高） |
| 2D | V0.5 引擎内 2D 模式（正交相机+SpriteRenderer） | 复用渲染/物理/序列化底座 | 独立 2D 引擎（重复建设） |

---

## 12. 风险与依赖注意事项

1. **Jolt FetchContent 编译时间**：首次构建 +2~3 分钟，可接受；与 Assimp 一样裁剪测试/工具 target。
2. **骨骼动画顶点格式扩展**是冻结接口（MeshData::Vertex）变更，开工前必须先改 ARCHITECTURE.md 并经评审。
3. **后处理浮点 FBO** 在老集显可能不支持 → 必须实现 8bit 降级路径（V0.3-14 验收项已含）。
4. **热重载 DLL**：导出注册函数代替静态初始化；重载失败必须保留旧 DLL 运行；脚本静态变量重置写入文档。
5. **打包资源**：pak 内路径一律相对路径；着色器/字体清单化；二进制缓存产物随 pak 打包（V0.5-1 依赖 V0.3-7）。
6. **schema 演进**：每次 JSON 结构变化 bump version 并保留旧版读取路径（当前 version:1 → 未来 2/3…）。
7. **性能回归**：从 V0.2-11 起，任何渲染改动必须附统计层数字（draw calls/帧时间）作为验收证据，杜绝「感觉快了」。

---

*本文档由产品规划 Agent 基于 docs/FEATURES.md、docs/ARCHITECTURE.md、docs/KNOWN-ISSUES.md 与 engine/include、editor/src 实际代码撰写。排期与工作量是估算，随实际进度滚动更新；与代码冲突时以代码为准。*
