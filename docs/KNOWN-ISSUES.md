# 已知问题与后续里程碑（2026-11-15）

## 当前状态

M1（窗口+渲染+五面板）+ M2（模型渲染+层级）已达成。V0.2 第一批（阴影/天空盒/Undo-Redo/未保存确认/统计面板）与**收官批次 Wave 1/Wave 2**（透明渲染 / 中文路径 / 独立环境面板 / 多选批量编辑 / gizmo 吸附 / 游戏分辨率 / Play 增强 / .novameta / 二进制缓存 / 打包独立 exe）已全部实现。

Wave 2 只读审查结论：**无 P0**；两条 P1 均已修复（meta 的 pak 读取、importModel 二进制缓存命中的材质旁表），剩余遗留见「Wave 1/2 审查遗留」段。功能用法见 [FEATURES.md](FEATURES.md)，收官批次设计摘要见 [ARCHITECTURE-FINAL.md](ARCHITECTURE-FINAL.md)。

## P2 打磨清单（早期 Reviewer 审查报告，状态随版本更新）

1. ~~网格地面雾色硬编码（Renderer.cpp kGridFogColor）~~ **已修复（V0.2-5）**：drawGrid 雾色参数化（scene.fogColor），Environment 面板可编辑 + Reset Fog to Sky 回落天空色
2. ~~Window 析构未调 glfwTerminate（单窗口无影响）~~ **已修复**：Window.cpp 析构补 glfwTerminate
3. ~~内置图元（builtin:cube/sphere/plane）无缓存~~ **已修复（收官批次裁定 4）**：故意泄漏的函数内 static 缓存，跳过静态析构防 GL 上下文时序问题
4. ~~TextureLoader 用 ANSI 路径（stb），中文路径贴图加载失败~~ **已修复（V0.2-14）**：全部资产读取收敛 AssetStream（ifstream path 重载 + stbi_load_from_memory），中文路径全面支持
5. ~~运行依赖工作目录 = 项目根（双击 exe 缺 shader/资产）~~ **已修复（cc0e865）**：NOVA_PROJECT_ROOT 编译期注入 + 启动切工作目录 + SetConsoleOutputCP；NovaPlayer 进一步以 exe 目录为项目根 + pak 挂载（V0.5-1）
6. ~~模型首次导入双解析（loadModel 不含材质信息）~~ **已修复（V0.2-14）**：importModel 一次 Assimp 解析取回网格+材质+贴图路径，loadModel 兼容转发
7. Duplicate/移除脚本组件走整场景 JSON 重建（O(n²) 多选复制、脚本状态清零）——**存在**（Duplicate 且为浅拷贝，不含子树）
8. ~~Hierarchy F2 重命名未在 Play 态禁用~~ **已修复**：F2 仅 Edit 态生效；Inspector 的 Name/Active/Mesh 路径/贴图路径输入在 Play 态同样禁用
9. 脚本类型名反查依赖 MSVC typeid().name() 格式——**存在**
10. ~~畸形 JSON（对象无 Transform）会导致编辑器解引用 nullptr~~ **已修复**：Gizmo 显示、Play 相机选择（pickPlayCamera 跳过无 Transform/非激活对象）、SceneRenderer::collectCamera 三处防御（回退 fallback 相机，不崩溃）
11. Input 滚轮注册依赖首帧当前上下文（多窗口脆弱）——**存在**（当前单窗口无影响）
12. ~~Gizmo 用前景 drawlist，与面板重叠时画到面板之上~~ **已修复**：改用窗口 drawlist（连带修复悬停判定恒假、手柄无法拖拽）

## DevOps 遗留建议

- GameObject::setParent 防御分支可能双归属（GameObject.cpp:91-94，正常流程不触发）
- MSVC 14.51 下整数→指针 static_cast 一律用 reinterpret_cast

## M3–M5 人工验收要点（历史记录，已随收官批次人工验收覆盖）

- **M3**：点击选择/橙色描边/Ctrl 加选、W/E/R gizmo（Local/World 切换）、F 聚焦、Inspector 数值编辑、Add Component 挂 SpinningCube 调 speed
- **M4**：Ctrl+S 保存/打开 .nova.json 往返无损、Asset Browser 浏览、Explorer 拖 .obj/.fbx/.gltf 导入、重复拖入走缓存
- **M5**：Play/Stop（Ctrl+P）快照恢复、SpinningCube 旋转、Play 相机切换、Console 三色 500 条、Release 构建

## 接口改进候选（后续版本）

- GameObject::removeComponent(Component*)
- Component::enabled 提升到基类（内置组件 Enabled 勾选配套）
- Add Component 同类型去重
- .novapak 流式读取（v1 全量读入内存，面向 <100MB 包体）

## 2026-11-15 功能排查修复记录（Debugger Agent）

- **gizmo 双 bug 已修复**：①ImGuizmo 矩阵直传（不转置）——glm 与 ImGuizmo 同为列主序内存布局，原转置破坏平移并使 gizmo 被静默裁剪（ARCHITECTURE §11.7 原结论有误，已修正）；②gizmo drawlist 改 ImGui::GetWindowDrawList()——前台 drawlist 的 _OwnerName 不匹配导致悬停判定恒假、手柄无法拖拽。验证：自动化点击 + 截屏像素确认 gizmo 三色轴真实绘制，W/E/R/X 切换正常。
- **Explorer 拖拽导入**：链路代码原本完整（回调已接线），端到端 WM_DROPFILES 验证通过。若"没反应"：确认鼠标落在场景视口/资产面板区域内、且不在 Play 态（均有 warn 日志）。
- **Play 旋转**：链路原本正确（每帧 scene->update(dt)），运行时验证 SpinningCube 持续旋转。旧构建可能造成误判；关键路径已加诊断日志。

## 新增缺口（2026-11-15，Docs Writer 通读代码发现，状态已更新）

1. Ctrl+D 复制是浅拷贝：serializeGameObject 不含子树，复制不跟随子物体（Delete 含子树，语义不对称）——**存在**
2. ~~Play 中 Inspector 仅禁用 Add/Remove 按钮：Name/Active/mesh 路径/贴图路径输入框仍可编辑~~ **已修复（收官批次）**：这些输入框与 Hierarchy F2 在 Play 态全部禁用
3. ~~sceneDirty 只写不读：无标题 \*、无未保存确认~~ **已修复（V0.2-2）**：五处未保存确认（§6）
4. ~~Play 中 live edit 置脏后 Stop 不还原脏标记~~ **已修复（V0.2-2）**：进 Play 记录脏状态，退出还原
5. Add Component 无同类型去重：可重复挂 Camera/MeshRenderer/同名脚本成「僵尸组件」——**存在**
6. 脚本类名与内置组件名冲突会被序列化误判——**存在**
7. 外部拖贴图到 SceneView 在 Play 中未禁止（与内部拖贴图路径不一致）——**存在**
8. Pause 整帧跳过 scene->update()（含 flush；Play 禁结构修改故无实际影响）——**存在**；收官批次新增 F10/Edit→Step 单步推进一帧（仅 Paused 时），实现仍是整帧跳过 + 单次 update
9. ~~场景环境参数（skyColor/ambientColor/showGrid）无编辑器 UI~~ **已修复（V0.2-5）**：独立 Environment 面板全覆盖（含 fogColor + Reset Fog to Sky）
10. 脚本 C++ 运行时成员（如 OrbitCamera 的 m_angle）不随快照恢复（规格 §7.2 已知）——设计取舍
11. 拾取 AABB 粗测：大平面遮挡后方物体（规格 §6.1 已知）——**存在**
12. ~~Hierarchy Ctrl+单击是 toggle（可减选），SceneView Ctrl+单击未命中时保留选择——两处 Ctrl 语义不一致~~ **已修复**：Hierarchy 点击空白在 Ctrl 按住时保留选择，两面板语义统一

## 2026-11-15 V0.2 第一批审查（Reviewer）——已修复项

- **P0 阴影数学自洽性**：光相机摆在光源对侧 + 片元比较方向反 → 阴影整体失效、地面自阴影全黑。修复：eye 改光源侧 + 两 frag 比较改 `<`（ShadowMap.h / blinn_phong.frag / grid.frag）
- **P1 equirect 转换 FBO 缺深度附件** → 6 面互相污染。修复：加 GL_DEPTH_COMPONENT24 renderbuffer（Cubemap.cpp）
- **P1 equirect 竖直翻转**（天顶采到图像底行）。修复：v 取负（equirect_to_cube.frag）
- **P2 模态打开时点窗口 X 直接退出**（绕过三选确认）。修复：视作 Cancel（EditorApplication.cpp）
- **P2 GizmoController 早退路径不推进 m_wasUsing** → 残留 true 造成 phantom 快照。修复：早退路径结算快照 + 推进状态（GizmoController.cpp）

## V0.2 第一批审查遗留 P2（待办 backlog）

1. 同帧按下-拖动-释放的极短拖拽不入 Undo 栈（IsItemDeactivatedAfterEdit 需跨帧；罕见，值已变但撤销覆盖不到）
2. 同帧多次 markSceneDirty 推重复快照（undo 一次可能"看起来没变"）
3. undo 回基线后标题仍带 *（无条件置脏；Unity 同行为）
4. 编辑器相机不入 Undo（按设计）——undo 后同帧 Ctrl+S 会保存快照里的旧相机值，建议后续 undo 后回写当前相机
5. Stats 帧时间不含 swapBuffers（vsync 等待前结算）→ vsync 下 FPS 虚高（仅 CPU 侧口径）
6. Hierarchy Duplicate 失败（nullptr）仍置脏并推快照 → 多余 undo 步
7. ShadowCameraParams.lightDirection 赋值后未使用（死字段）
8. ~~Inspector 无选中时 Environment 区为 V0.2-5 环境面板的临时替身~~ **已落地（V0.2-5）**：Environment 面板独立（dockRight 与 Inspector 并排 Tab），替身区删除、无选中时改显 Snap Settings

## Wave 1/2 审查遗留（收官批次，2026-11-15 Reviewer 报告归纳）

Wave 2 审查无 P0；两条 P1 已修复（见下段「已修复项」）。以下为遗留项，按影响排序：

1. **importModel 二进制缓存命中需材质旁表完整**（P1-2 修复后的既定取舍）：二进制缓存命中必须与内存材质旁表（m_materialCache + 贴图路径旁表）逐子网格对应且数量一致，否则回退 Assimp 重解析并覆写缓存。新进程旁表为空 → **Player 每次启动仍走一次 Assimp 解析**（编辑器同会话内二次导入才有缓存收益）。正确性优先的取舍——旁表缺失时宁可不提速也不静默丢材质/丢贴图。
2. **pak 内无 meta 的贴图在 Player 首载时于 exe 旁生成默认 meta**（P2-3 确认存在）：打包时 meta 生成失败的贴图（或手工组装的 pak）在 Player 首载时，若磁盘上能定位到贴图文件，会向发布目录写默认 .novameta；只读介质/Program Files 下 create_directories 失败仅警告，不影响功能。
3. **Cubemap 六面 LDR 不消费 meta 的 colorSpace**（P3）：六面 LDR 内部格式硬编码 sRGB（srgb=true），meta 的 colorSpace 仅参与二进制缓存键、不改变内部格式——NormalMap/Linear 设置在天空盒六面不生效（与 getTexture 语义不一致）。
4. **LDR equirect 的 meta 为死重**（P3）：Packager 为 LDR equirect（如 png 全景图）收集 .novameta 进 pak，但 createFromEquirect 的 LDR 路径不读 meta（直接解码为 sRGB），pak 内多出的 meta 文件不产生任何作用。
5. **meta 模态 Save 把 "NormalMap" 重写为 "Linear"**（P3）：Texture Import Settings 模态的颜色空间下拉只有 sRGB/Linear（无 NormalMap 选项）；meta 里手写的 type "NormalMap" 经模态打开后读为 Linear，点 Save 落盘即被改写。语义等价（NormalMap 本就按 Linear 采样），但往返有损。
6. **二进制缓存读失败误删好缓存**（P3）：readCacheFile 的读错误（瞬时 IO/文件锁等，非文件损坏）也走 corrupt() 删除路径——好缓存可能被误删一次，下次自动重导。
7. **m_materialCache 无 releaseUnused**（P3）：releaseUnused 只清理网格/贴图/cubemap 三个 weak_ptr 缓存，材质旁表与贴图路径旁表随模型键增长，仅 Reload Assets（clearCache）可清。
8. **getTexture 未命中时源文件读两遍**（P3，纯效率）：先 AssetStream::readAllBytes 读一遍算缓存键，未命中后 TextureLoader::loadRgba8 再读一遍解码（Cubemap 六面 LDR 同理）。
9. **透明排序为 AABB 中心近似**（Wave 1 设计已知）：透明队列按视空间 **AABB 中心**深度后→前排序（画家算法），非逐像素/逐三角形——大体积或互相穿插的透明物体可能排序错误（无 OIT 计划）。
10. **多选非均匀缩放 shear**（Wave 1 设计已知，v1 限制）：多选 gizmo 把同一 delta 矩阵作用于全部对象，非均匀缩放叠加旋转差异会产生剪切变形，decompose 写回时丢弃 skew 分量（ARCHITECTURE-FINAL 风险节已注明）。

其余 P3 备注（不阻塞）：

- **COM MTA 场景下打包目录对话框静默失败**：线程已被初始化为 MTA 时 IFileDialog 创建失败会静默返回空路径 → 日志误报「已取消打包」（EditorApplication.cpp:98-133）。
- **编辑器首载贴图自动生成 .novameta**（设计行为），但 .gitignore 未忽略 → git status 出现大量未跟踪文件，仅提示。

## Wave 1/2 审查已修复项 / 验证通过（代码核实）

**已修复**：

- **P1-1 .novameta 的 pak 读取**：TextureMeta::loadOrCreate 收敛 AssetStream::readAllText（pak 命中优先、磁盘回退）——打包游戏不再静默丢失贴图导入设置。
- **P1-2 importModel 二进制缓存命中丢材质**：命中路径增加旁表完整性校验（逐子网格对应 + 数量一致），不完整一律回退 Assimp 重解析并覆写缓存（遗留取舍见上 1）。
- **P2-1 getCubemap 面探测锚点不一致**：六面探测一律传绝对路径（AssetStream 内部对绝对路径相对项目根降维），不再依赖 CWD。
- **P2-2 importModel bin 缓存部分命中无完整性校验**：并入 P1-2 的旁表完整性校验（子网格缺失/数量不符即回退重导）。
- **中文路径全面支持**：Shader/TextureLoader/ModelLoader/Cubemap/SceneSerializer 读取路径全部收敛 AssetStream（ifstream path 重载 + stbi_load_from_memory，零编码转换，同时服务 pak）。
- **模型导入双解析**：importModel 一次 Assimp 解析返回网格+材质+贴图路径（贴图路径解析为项目内相对路径），DragDrop 导入不再二次解析。
- **F2 Play 禁用**：Hierarchy F2 仅 Edit 态生效；Inspector 的 Name/Active/Mesh 路径/Albedo Map 路径输入在 Play 态禁用（结构修改守卫）。
- **无 Transform 防御**：Gizmo 显示、Play 相机选择（跳过非激活/无 Transform）、SceneRenderer::collectCamera（回退 fallback + 警告）三处防御，畸形 JSON 不再解引用空指针。
- **环境 UI**：独立 EnvironmentPanel（skyColor/ambientColor/showGrid/fogColor + Reset Fog to Sky + 天空全组），Inspector 替身区删除。
- **内置图元缓存**：Primitives 函数内 static 故意泄漏（防 GL 上下文时序问题），整场景重建不再重传 GPU 网格。
- **Ctrl 语义统一**：Hierarchy 点击空白在 Ctrl 按住时保留选择，与 SceneView Ctrl+单击未命中语义一致；gizmo 吸附左右 Ctrl 均生效。
- **拖放目标注册**：SceneView/Hierarchy 的 BeginDragDropTarget 不依赖 hovered 判定（拖拽中 IsItemHovered 可能因 gizmo 手柄/重叠窗口误判），payload 不再被 ImGui 丢弃。
- **双击 exe 资产路径**：编辑器 NOVA_PROJECT_ROOT + 启动切工作目录；NovaPlayer 以 exe 目录为项目根、自动挂载 exe 旁 .novapak、AssetStream 绝对路径降维（含 ".." 逃逸防护）。
- **Play 分辨率安全钳制**：渲染宽高每维 clamp [16, 4096]（Custom 8192 × renderScale 2.0 不再炸显存）；FBO resize 失败销毁重建、下帧重试（SceneViewPanel.cpp）。

**Wave 2 审查逐项验证通过**：

- 二进制缓存：FNV-1a 常量正确；48B Header 与三种载荷写读逐字段一致；校验链完整（magic/version/payloadType/payloadSize==剩余长度/载荷内部尺寸/末尾 nameLen 精确校验）；损坏即删即重导；1GB 告警与尺寸上限防荒谬分配均在。
- 缓存键一致性：getTexture 与 Cubemap 六面同键空间（同 makeKey(字节, serialize(meta))、同源字节、同 meta 文件）；HDR equirect 用 makeKey(bytes) 无 meta、LDR equirect 不缓存——均与设计一致。
- pak 读写对称：16B Header + entry 表 + payload offset 连续性、pack/open 对称；pack 端 normPath 与查询端 pakLookupPath 归一一致；越界防护完整。
- Player：mountPak 先于 Renderer::init（shader 经 AssetStream 加载）成立；--scene/--pak/--width/--height 解析、默认扫描 exe 旁 .novapak、Esc 退出、shutdown 顺序（Skybox→Renderer→窗口析构）均正确；projectRoot=exeDir 与 pak 相对键降维配合正确。
- Packager：builtin 跳过、albedoMap、六面探测（扩展名顺序与引擎一致）、shaders 递归收集、.hdr 跳过 meta、缺失清单逐条报错、std::map 排序确定性、NovaPlayer.exe 预检、pak 命名（场景 stem）全部正确；场景 JSON 键名与 SceneSerializer/MeshRenderer 实际序列化逐键核对一致。
- meta 消费：sRGB 判定（meta==Srgb 且请求 srgb）、wrap/filter/mip 传递、NormalMap→Linear、serialize 稳定文本（nlohmann std::map 键序）均正确。
- COM 初始化配对（S_OK/S_FALSE 均配 CoUninitialize）、clearCache 语义（内存清空 + bin 缓存按内容键自然失效）、meta 模态 Reset 不落盘——复核通过。
