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

## V0.3 波 1 审查（2026-11-15，PBR + 材质库）

**已修复**：P1-1 贴图缓存键加 sRGB 维度（先加载者胜出毒化）+ DragDrop albedo srgb=true；P1-2 Edit 阴影收集用解析后 blendMode；P1-3 uGamma=0 全白陷阱（shader 显式直通分支 + 契约修正）；P2-1 G 项 k=α/2 与 D 重映射一致；P2-3 双轨序列化门与渲染门一致；P2-5 readMaterialJson 非对象防御；P2-8 refreshSceneMaterialRefs 失败 warn。

**遗留 backlog**：
1. 副切线手性：B=cross(N,T) 恒右手系，镜像 UV 网格法线贴图 X 轴翻转（建议存 handedness 到 tangent.w，波次待排）
2. Inspector 拖放赋值 .novamat 未实现（现为 Assign 文本输入；AssetBrowser 拖拽源仅模型/贴图）——ARCHITECTURE §14.5 承诺需改契约或补实现
3. PBR 亮度整体下降无补偿（物理正确化；demo 观感变化已写入 FEATURES §11.1）
4. resolveAssetAbs 锚点规则在 MaterialAsset.cpp 重复实现（与 AssetDatabase.cpp 文件内 static 两处同步风险，建议收敛共享内部头）
5. 双轨省略内联段往返语义：Assign 前内联值 save→reload 后丢失（"Assign→重启→Detach"显示默认材质，与同会话 Detach 行为不一致）

## V0.3 波 2 审查（2026-11-15，物理/音频/UI/协程/事件 三线合并）

**总评：需修 → 已修复闭环，可合**。12 项核对清单全部通过（Jolt pimpl 隔离/固定步进反解无 D² 错误/帧序 §15/事件 swap-flush/物理裁定/UI 零 ImGui/CMake 铁律/交叉侵入零越界）。无 P0。

**评审发现（Reviewer）——全部已修复**：

- **P1-1 协程悬挂 UAF**（Behaviour.cpp 帧末回收）：LIFO 处理 pending + 唤醒链重等待已销毁目标 → 下帧解引用已释放节点。修复：FIFO 游标 + 「本 pass 已销毁目标」集合 + 链式唤醒立即续推 + 病态自旋上限 kMaxChainResumes=64（销毁帧+warn）。
- **P1-2 序列化分发缺口**（SceneSerializer.cpp）：componentTypeName 只硬编码 4 旧组件 → 新内置组件误报「脚本未注册」+ 同名脚本静默换型风险。修复：按 typeId 反查 ComponentRegistry 优先于 scriptTypeName；同名冲突 warn。
- **P2-1** stopAllCoroutines 请求在帧末唤醒路径被吞 → 回收循环每迭代复查。
- **P2-2** UI 输入时序：player 侧 processInput 移到 scene.update 前（本帧按钮状态）；编辑器渲染期调用滞后一帧（已知取舍，契约注释分侧表述）。
- **P2-3** 协程推进无 enabled/activeSelf 过滤 → 按架构师 §14.6 裁定修正注释（enabled=false 不暂停=Unity 一致；inactive 子树照常推进=已文档化差异）。
- **P2-4** 残留 [DBG] 日志与临时 SEH 栈回溯脚手架 → 全部清除（grep 归零）。
- **P2-5** player HiDPI 输入坐标不一致 → 逻辑坐标×缩放比换算。
- **P2-6** UIRenderer 坏尺寸早退未复位本帧输入标志 → 消费前移到尺寸校验前。

**运行期缺陷（DevOps 栈回溯定位）——已修复**：

- **两 exe 启动崩溃**：PhysicsWorld.cpp 的 Jolt 分配器注册晚于 Impl 成员区 Jolt 容器构造（初始化列表阶段 JPH::Allocate 空函数指针调用）。修复：JoltInitGuard RAII 声明在成员区最前（A 线已复核）。
- **Player 启动挂死**：FontAtlas.cpp 像素缓冲声明了 size 但从未分配 → 首帧白块烘焙越界写。修复：ensureLoaded 分配 512²（B 线已核对磁盘版本）。

**波 2 遗留 backlog**（设计取舍，均已文档化）：

1. **跨 Behaviour 的 WaitForCoroutine 会悬垂**：WaitForCoroutine 指针依赖 std::list 地址稳定，只保证同 Behaviour 内互等安全；跨对象等待请用 WaitUntil 轮询替代（冻结设计取舍，不修）。
2. **编辑器 SceneView UI 输入滞后一帧**：processInput 在渲染期调用，脚本 onUpdate 读到上一帧按钮状态（player 无此问题；波 3 渲染链重构时评估迁移）。
3. **Text.fontPath v1 不生效**（仅默认 msyh.ttc 图集），字段仅序列化保留（V0.5 字体资产化时启用）。
4. **FontAtlas 固定 64px 参考高**：冻结头 glyph() 无字号参数所致，>64px 字号放大略软；图集满（1024²）后新字形缺失。
5. **物理近似裁定**：sensor 为 body 级（混合 trigger/实体 collider 按实体处理）；子 GO Collider 相对 TRS 注册时烘焙（运行期移动需重挂）；Play 期增删 Collider 走销毁重建丢速度；球/胶囊非均匀缩放取近似轴。
6. **Jolt 进程级初始化随 Scene 构造**：编辑器启动即起 hw-1 job 线程（与 miniaudio 设备线程并列为引擎合法非主线程）。
7. **msyh.ttc 绝对路径 Windows 专用**：缺文件时 warn 降级内置 5x7 ASCII 字体，不崩。
8. **Release 构建未验证**：本波仅 Debug 全量构建+冒烟；Release（含 Jolt 首编）随波 4 全量回归执行。

**验证记录**：74 第一方源文件强制重编译零错误零第一方警告；NovaEditor 冒烟（demo v3 打开、守卫 15 条验证通过、真实拖放 sphere.obj 导入成功）；NovaPlayer 冒烟（场景加载 + SpinningCube 旋转正常）；音频本机有设备 init 成功（无设备降级路径代码化，未实测）。日志归档 build/smoke/。

## V0.3 波 3 审查（2026-11-15，后处理/场景管理/DDOL/PrefabSerializer 三线合并）

**总评：需修 → 已修复闭环，可合**。12 项核对清单全部通过（renderSceneToOutput 管线顺序/setGammaMode 粘性/SerializationInternals 零回归抽取/双 FBO 重线/交叉侵入零越界）。无 P0。

**评审发现（Reviewer）——全部已修复**：

- **P1-1 天空 uGamma 未同步 HDR**（SkyboxRenderer.cpp:64 硬编码 1/2.2）：HDR 模式双重 gamma 发白。修复：Renderer 增 linearOutput() getter，天空绘制时运行时读取（skybox.frag 直通分支原有）。
- **P1-2 apply 降级黑屏**（PostProcess.cpp:211-216）：enabled=true 但 shader 加载失败 → 直接 return 黑屏；16F 探测失败 → 完整链在坏 FBO 上未定义输出。修复：早退统一判定 `!enabled || s_disabled || !s_initialized || !hdrSupported()` → 纯 blit 直通（永不黑屏契约成立）。
- **P1-3 DDOL 标志树格式丢失**（PrefabSerializer.cpp）：SceneManager 用树格式迁移 DDOL，dontDestroyOnLoad 键不落盘 → 只存活一次切换。修复：writePrefabNode 追加仅 true 写的键、instantiateNode safeGet 缺省 false 读回（与平铺格式同键名）。
- **P2-1** BuildSettingsPanel 每帧 rescan 覆盖手动排序 → 合并式 rescan（首开+显式按钮触发，既有顺序/勾选保留）。
- **P2-2** bloomLevels UI 上限 6 vs 引擎 clamp 1-3 → UI 改 3。
- **P2-3** blitPassthrough 恢复 READ FBO 为 0 而非入口值 → 保存/恢复 prevReadFbo。

**DevOps 修复**：BuildSettingsPanel.cpp C4100 未引用参数（参数名注释化，一行）。

**波 3 遗留 backlog**（已文档化，不阻塞）：

1. **双 FBO resize/格式切换分支未经运行期验证**：代码路径已实现（尺寸或 hdr 模式变化全删重建），5s 冒烟未触发窗口 resize——**最终验收手动拉伸窗口 + 切换 pp.enabled 复核**。
2. **LoadScene 端到端未验证**：demo 无脚本触发切换；init/shutdown 冒烟无崩溃——**最终验收挂临时脚本验证 DDOL 存活 + 退出 Play 快照恢复**。
3. **SceneManager 首次使用 assets/build.novabuild 缺失 warn**（良性，三处冒烟一致出现）。
4. **bloom 链单方向降采样级**（stride-2）：依赖前级各向同性，高频细节略少于每级双向；视觉差异小（设计裁定，注释存档）。
5. **PostProcess::resize 在 pp.enabled=false 时仍创建 bloom 纹理**（半分辨率 ×4 张 16F，内存可忽略）。
6. **combine 单 uBloom 输入**：多级 bloom 经链式降采样折叠进末级（冻结 shader 契约下的设计裁定）。
7. **Player 运行期改 pp.enabled 触发 FBO 销毁重建**：Player 无设置 UI 正常不变；编辑器由 SceneViewPanel 双 FBO 管理。
8. **老集显 8x8 探测纹理可创建但渲染极慢**：不影响正确性（探测仅一次）。

**验证记录**：四 target 全绿零第一方警告；postProcess 开启场景实跑 16F FBO 链（无降级 warn、无 shader 错误、旋转正常）；默认 demo 直通路径实跑正常；编辑器守卫 15 条通过；4 个 post shader 首编译零错误。日志归档 build/smoke/（w3-*.log、demo_post.nova.json 为临时测试场景，不进 assets）。

## V0.3 波 4 审查（2026-11-15，Prefab 完整化 + Undo 文件通道，V0.3 收官）

**总评：需修 → 已修复闭环，可合**。12 项核对清单全部通过（applyToAsset 七步语义/fieldCompare 点分路径/嵌套烘焙 ID 重映射/Undo 栈逐位对齐/Inspector 接线/拖放/打包/守卫 16 项/交叉侵入零越界）。无 P0。

**评审发现（Reviewer）——全部已修复**：

- **P1-1 Transform/Behaviour.enabled 的 Revert 静默失效**：revertComponent 走 onDeserialize，但 Transform/Behaviour 公共字段由 SceneSerializer 公共层读写 → 改位置后 Revert no-op 且 markSceneDirty 造成「看似已还原」。修复：applyComponentJson 助手（镜像公共字段语义：Transform setLocalPosition/Rotation/Scale、Behaviour enabled 显式写回 + onDeserialize 扩展字段）。
- **P1-2 未映射新对象重复 Apply 重复追加进源**（资产污染）：instanceToSource 从不更新 → 第二次 Apply 再 push_back。修复：未映射分支按 instId 在源树查重（命中并入合并流程）+ saveAsset 后 syncNewObjectIds 补映射。
- **P2-1 override 比对恒假阳性**：用 onSerialize 输出比对，Transform onSerialize 为空 → 恒判差异（高亮恒亮）。修复：比对/写回改用 componentFullJson（type + 公共字段 + onSerialize，与 writeComponentsJson 同构）——未改不亮、改过才亮。
- **P2-2 clear 基线只入场景栈**：baseline 未同步压文件栈。修复：clear 同步 push_back({})，逐位对齐不变量成立。
- **P2-3 applyToAsset 空映射无防御**：空映射整棵子树以实例 ID 追加进源。修复：空映射 warn + 中止。
- **P2-4（过程性）编辑器二进制缺失**：devops 重建确认链接通过（已覆盖）。

**DevOps 合并期修复（6 处，均 C 线文件）**：DragDrop importPrefabToScene 签名全限定、editor/CMakeLists.txt 补 PrefabManager.cpp（9 个 LNK2019）、PrefabManager.cpp 三处 .string() 误用、erase_if 改 erase-remove、findComponent 前向声明。

**波 4 遗留 backlog**（已文档化，不阻塞）：

1. **Prefab 交互验收项留人工 GUI**：拖放实例化→字段高亮→Apply→其余实例同步→嵌套烘焙→Revert→undoFull 文件恢复、删源资产场景不崩、schema v4 资产往返（自动冒烟不覆盖交互）。
2. **FileRestore 单内容结构**：redo 方向 .novaprefab 由消费方按场景重序列化（undo 方向文件恢复完全正确）；双向双内容需改冻结头（后续裁定）。
3. **applyComponentJson 与 SerializationInternals 公共字段语义双处同步**：编辑器 include 路径约束导致的内联同构实现，升级须两边同步（源码注释互指）。
4. **粗体近似**：override 字段「加粗」为金黄高亮 + `▶ ` 前缀（未加载粗体字体），后续加载粗体字体可升级。
5. **v1 Apply 取舍**：实例新增对象以实例 ID 写入源；源中删除对象不传播回源。
6. **波 3 遗留两项**继续有效：双 FBO resize 分支与 LoadScene 端到端留人工验收。

**验证记录**：Debug 全量构建零错误零第一方警告（NovaEditor.exe 重链接）；**Release 全量构建零错误**（Jolt 首次 Release 编译，Release 双 exe 产出）；编辑器冒烟守卫 16 条通过无 PrefabManager 错误；Player 冒烟旋转正常。日志归档 build/smoke/w4f-*.log。

**波 4 后修复（2026-11-15，用户报障「Scene 看不到 cube」）**：

- **根因**：波 3 重线后 Edit 路径改走 renderSceneToOutput → 内部 collectCamera 优先场景 Main Camera（忽略编辑器相机）；demo 的 Main Camera 为恒等旋转（forward=+Z，背对原点）→ Scene View 只有天空，网格/cube 全部不可见。此前冒烟只验日志未验画面，故未发现（教训：渲染回归必须像素级验证）。
- **修复**：renderSceneToOutput 增 `bool useSceneCamera = true` 尾参——Edit 传 false 强制用编辑器相机（场景相机不得劫持 Scene View 取景）；Play/Player 默认 true 不变。demo.nova.json Main Camera 旋转改 180° 偏航（0,1,0,0）朝向原点（修复前 Play 同样只有天空）。
- **验证**：编辑器窗口截图像素采样——修复前场景区 100% 天空蓝零亮像素；修复后地面亮灰+天空+视口中心橙色描边 cube 齐全。

**波 4 后修复（2026-11-15，用户报障「.novaprefab 拖不进场景」）**：

- **根因**：AssetBrowserPanel 拖拽源条件只含模型/贴图文件，`.novaprefab` 漏在条件外 → 按住 [PF] 条目拖拽根本不启动。接收端（SceneViewPanel 的 kAssetPayload → importPrefabToScene）与拖放链路波 4 已全部就位，仅缺源头一处注册。
- **修复**：拖拽源条件补 `DragDrop::isPrefabFile(e.rel)`（AssetBrowserPanel.cpp）。教训：GUI 验收清单「拖放实例化」正是为捕获这类交互缺口而设——自动冒烟验不到拖拽源是否注册。
- **验证**：Debug 重编译通过；拖放交互待用户 GUI 复验。

**波 4 后修复（2026-11-15，物理验收「Cube 不掉落/穿地」——三层 Jolt 集成缺陷）**：

验收场景 `assets/scenes/PhysicsTest.nova.json`（静态地面 + 落体刚体）暴露三个叠加缺陷，全部修复（PhysicsWorld.cpp）：

1. **动态体未激活**（症状：Cube 悬浮不动）：`CreateBody` 只分配+入管理器，Jolt 要求显式 `AddBody(id, EActivation)`——AddBody 才进 broadphase、Activate 才入活动列表（官方 HelloWorld 用 `CreateAndAddBody(settings, Activate)` 同义）。修复：createBodyFor/rebuildStaticBody 补 AddBody（静态 DontActivate、其余 Activate）。
2. **destroyBody 缺 RemoveBody 前置**（症状：补 1 后重建时断言崩溃）：Jolt 规定先 `RemoveBody`（退出活动列表+移出 broadphase）再 `DestroyBody` 释放；此前 bodies 从未 AddBody 过故断言静默通过。修复：destroyBody 补 RemoveBody。
3. **宽相位跨层表全 FALSE**（症状：补 1/2 后 Cube 穿地直落）：`objectVsBpFilter` 是成员，在初始化列表阶段构造——彼时 pairFilter/layerInterface 尚未配置（EnableCollision/Map 都在构造体）→ 建表全 FALSE → 宽相位拒绝动态(kMoving)与静态(kBpNonMoving)的一切配对。修复：派生类把层配置移进自身构造（ConfiguredLayerInterface/ConfiguredPairFilter），声明顺序自然满足依赖；PhysicsSystem::Init 持表指针，表地址须稳定。

- **验证**：Player 跑 PhysicsTest——step 日志 velY 递增、transformY 逐帧跟随、落地 y=0.50（板顶 0 + 半高 0.5）后 velY=0 进入休眠；**GPU 读回 outLdrFbo 采样 (640,566) = R103 G67 B56（橙色方块落在板上）**。另加两层长期诊断：JoltAssert 断言转发引擎日志（不回断点，无人值守可收集）、syncFromTransforms body 统计。
- **教训**：① 波 2 物理「代码交付」仅日志冒烟——三层集成缺陷均无日志报错、必须像素/读回级验证才暴露，渲染与物理验收须「真跑真看」；② Jolt 生命周期四步（CreateBody→AddBody(激活)→RemoveBody→DestroyBody）不可省任何一步；③ 宽相位表是「构造期快照」，配置必须先于其构造。物理验收清单（ROADMAP §5.1）其余项待人工。

**波 4 后修复（2026-11-15，用户报障「退出 Play 时 abort() has been called」——析构期重建 UAF）**：

- **根因**：场景批量销毁（~Scene / deserialize 原地重建）时，组件析构的 unregister 路径仍执行「重建/重挂」——正在析构的 Collider 被重新注册进 hostByCollider/staticBodyByGo，随后对已释放的 GameObject 再次 rebuildStaticBody → 对悬挂 GO 的 UAF（编辑器 abort / Player 关闭挂死两种表象）。diag 文件日志实证：同一 Ground 指针被重建 3 次、第 3 次挂在 getComponents。
- **修复**：PhysicsWorld::beginTeardown() 批量销毁旗标——~Scene 与 SceneSerializer::deserialize 清空前置位，register/unregister 四入口在旗标期直返（不再重建/重挂）；残留 body 由 ~Impl 的 teardownAll 统一销毁；syncFromTransforms 复位并全量重建（Edit 态本不步进物理，无功能损失；LoadScene 路径的 start() 亦触发复位）。
- **验证**：Player 带物理场景优雅关闭——进程干净退出（0 残留、0 断言），teardownAll→监听清理→Jolt 成员析构→UnregisterTypes 全链路走完；退出 Play 编辑器路径待用户复验。
