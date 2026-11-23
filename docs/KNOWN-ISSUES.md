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

## 2026-11-16 功能排查修复记录（Debugger Agent）

- **Play 退出后层级被拍平（子对象变同级）已修复**：根因 `Scene::createGameObject(name, parent)` 只把新对象 push 进父级 children 列表、**从未设置 m_parent 指针**（GameObject.cpp:57-62 修复前）。后果：Hierarchy 面板按 children 列表显示为嵌套（看似正常），但 `parent()` 返回 nullptr → Play 快照序列化把 parent 写成 0 → 退出 Play 按快照还原时全部挂根。右键 Create Empty / Create Primitive 等走 createGameObject 的创建路径全部中招；拖拽重父子（setParent）不受影响。修复：push 进父级列表时同步 `raw->m_parent = parent`（一行）。回归验证：新增 tests/PlayCycleTest 无头测试（serialize→deserialize×5 + Play 帧循环往返 + 文件往返，全通过）。
- **UI 文字全部乱码（数字/字母/汉字皆非其字）已修复**：根因 FontAtlas::bakeStb 把**字形索引 gi** 误传给三个期望「码点」的 stb API（`stbtt_GetCodepointBitmap` / `GetCodepointHMetrics` / `GetCodepointBox`）——这些函数内部会再执行一次 FindGlyphIndex，导致双重映射：烘焙出「码点值恰好等于字形索引」的另一个字形（对 msyh 的 cmap 而言几乎所有字符都错，含 ASCII）。gi 本应只用于缺字判断。修复：三个调用改传码点。验证：播放器帧缓冲读回逐字核对（"Click Me 123 ABC 中文测试" 数字/字母/汉字全部正确）。
- **文字渲染成 2 倍大（看起来像乱码）已修复**：根因 kRefPx 在 FontAtlas.cpp 与 UIRenderer.cpp 各维护一份——参考高提升为 128px 时只改了前者，后者仍按 fontSize/64 排版 → 全部字形 2 倍大、间距 2 倍宽。修复：常量收敛为 FontAtlas::kRefPx（FontAtlas.h 内 static constexpr，唯一出处），两侧引用同一常量。验证：播放器帧缓冲读回（32px 文字宽度 358px 与字号匹配）。
- **字形横向条纹/笔画错位/斜切已修复**：根因引擎从未设置 glPixelStorei(GL_UNPACK_ALIGNMENT,1)——OpenGL 默认 4 字节行对齐，而字形位图是宽度任意的 R8 单字节行：宽度非 4 倍数时每行读长被撑到 4 的倍数，行间逐行累积偏移 → 字形斜切、横条纹、笔画错位（用户截图 + 外部分析定位一致）。修复：FontAtlas::uploadRegion 子区域上传前显式 1 字节对齐、结束恢复默认。其余上传点（RGBA 4 字节像素、FLOAT 16 字节像素、图集整图 512²/1024²）天然 4 对齐不受影响。
- **SceneSwitcher 验收脚本从未编译进构建已修复**：ROADMAP 验收物料 samples/SceneSwitcher.cpp 文件存在，但 samples/CMakeLists.txt 未列入 NovaScripts → 挂 SceneSwitcher 的场景反序列化时「未知组件类型，已跳过」、onStart 不执行、DDOL 从未设置——LoadScene E2E 验证首跑即暴露（DDOL 迁移 0 棵子树）。修复：CMakeLists 补 SceneSwitcher.cpp（一行）。教训：验收脚本本身必须先被构建。
- **V0.3 遗留验收项像素级验证闭环（2026-11-16/17）**：Player 临时 --verify 驱动 + outLdrFbo 帧缓冲读回（读回产物与脚本已随验收删除，数字记录如下）：① 按钮 hover/pressed 颜色——真实 OS 光标悬停 + SendInput 点击，idle(78,141,229)/hover(103,166,254)/pressed(53,103,204)/移出还原，与设计色逐像素一致；② Bloom A/B——emission=3 立方体光晕环亮度 on=206.1 vs off=188.0（+18），ACES 高光滚降（253 vs 255），hdrSupported=true 16F 链实测。全部 12 项断言通过。
- **材质库共享材质同步已像素级验证（2026-11-17）**：临时场景三立方体共享同一 .novamat（materialAsset 双轨）——材质 albedo 红 (1,0,0) 时三立方体实测全 (108,0,0) 红；仅改资产文件为绿 (0,1,0) 重跑，三立方体全 (0,108,0) 绿、天空不变——「改一处、多处同步」资产级成立（编辑器内实时刷新交互仍待人工）。
- **gizmo 拖拽失效已修复（2026-11-17，三层根因）**：用户报障「拖彩色箭头没反应、W 切位移模式镜头飞走」。诊断日志（[GizmoDiag]）逐层定位三层缺陷：① EditorCamera 的 WASD 相机平移无右键门槛，与 gizmo 的 W/E/R 快捷键冲突（按 W 边切模式边推镜头）——修复：WASD 须按住右键（Unity flythrough 同款，FEATURES §3.2 已同步）；② ImGuizmo 上游 CanActivate() 要求点击瞬间 !IsAnyItemHovered()，而 SceneView 的 FBO 画面经 ImGui::Image 铺满视口恒为 hovered 项——修复：Manipulate 前临时清零 g.HoveredId、调用后立即恢复（视口 Image 的 IsItemHovered 已在更早处读取，无副作用）；③ 编辑器从未调用 ImGuizmo::BeginFrame()（上游 README 规定的每帧一次调用）——mbOverGizmoHotspot 逐帧状态无复位，鼠标首次悬停手柄后永久粘死，激活路径恒短路（IsOver=true 但 IsUsing 恒 false）。修复：ImGui::NewFrame 后每帧调用。教训：11-15 的 gizmo 验证只证明了「绘制+悬停+切换」、未证明「拖拽生效」，交互验收必须真拖真看（用户实测拖拽通过）。
- **V0.5 波 1 交付（2026-11-17，Architect→4 Coder 并行→整合→2 Reviewer 就地修 7 项→DevOps）**：5 个功能代码交付闭环（ROADMAP §6 顶部已加「波 1 状态」行）：
  - **InputMap+手柄（V0.5-11，c1）**：Input 新增 GLFW joystick 每帧轮询与边沿查询——`isGamepadConnected/connectedGamepadCount/gamepadName/isGamepadButton{Down,Pressed,Released}/gamepadAxis/setGamepadDeadzone`（热插拔日志 + 断开清零防粘连）；InputMap 绑定配置——`load/save/actionNames/axisNames/addAction/addAxis/removeAction/removeAxis/actionBindings/axisBindings/setActionBindings/setAxisBindings/getAxis`（含修饰键；assets/input/default.novainput 默认 4 动作 2 轴）；编辑器 Input Bindings 面板（动作/轴增删、绑定 Combo 改键、Save/Reload、即时生效）。
  - **存档系统（V0.5-12，c2）**：SaveGame 全静态工具类——`save/load/listSlots/deleteSlot/slotExists/setSaveDirectory/saveDirectory/slotPath` + SaveSlotInfo 元数据；slot&lt;N&gt;.novasave（version 1）原子写（tmp→rename）；浅校验（JSON 合法 + version ≤ 当前 + snapshot 含 objects 数组）；读档经 SceneSerializer::deserialize 原地重建同一 Scene（对象 ID 保持）；File 菜单 Save/Load Game（Play 态）。
  - **UI 控件集（V0.5-13，c3）**：Slider/ScrollView 组件（序列化往返）、Image fill（水平/垂直/径向 90/180/360）、Text 换行/对齐、UI 事件冒泡与射线拦截（Play 中 UI 优先于场景拾取）。
  - **性能优化包（V0.5-14，c4）**：Frustum 视锥-AABB 相交剔除 + 静态网格实例化（`Mesh::drawInstanced` + blinn_phong_instanced.vert，2000 同 mesh 立方体 &lt;20 draw calls）；StatsPanel 增 Culled/Instanced draws/Instances 三行。
  - **CLI 批处理（V0.5-2，整合者）**：NovaEditor `--headless/--import/--build/--scene/--out/--quit`，退出码 0/1/2（参数错误 stderr + Logs 文件双通道）。
  - **Reviewer 就地修复 7 项**：① InputMap::getAxis 返回值 clamp [-1,1]（手柄 gamepadAxis×scale 放大越出契约范围）；② SaveGame::load 对 deserialize 补 try/catch（脚本 onAwake 异常不再崩编辑器，「损坏存档不崩溃」）；③ emitRadialFill 扫掠量化修复（Radial360 amount&lt;1 恒不可见、Radial90/180 越界扫过）；④ Slider 拖拽捕获改存 GO id + 每帧重查校验（Play 快照还原/读档销毁重建后解引用悬垂 Slider\* 崩溃）；⑤ findCanvasAncestor 含 go 自身（Canvas 根自带 ScrollView 的 scissor 错位）；⑥ render() 补 GL_TEXTURE0 绑定保存/恢复；⑦ walkCanvas 根级 ScrollView 补冒泡链条目（滚轮输入无处交付——原 P2-10 就此闭环）。
  - **DevOps 修 Review 遗留 P2 低成本项 6 条**：snapshot 浅校验补 objects 数组键（防 {"snapshot":{}} 把场景清空为「合法空场景」，ARCHITECTURE §24.2 同步）；Save/Load 槽模态打开时缓存槽列表（模态期间不再每帧扫盘刷日志）；setGamepadDeadzone NaN/Inf 守卫（钳制比较恒 false 失效）；Input Bindings Add Action/Axis 后本帧刷新列表（新选中项绑定区不再延迟一帧）；ScrollView 反序列化钳制负 scrollSensitivity；main.cpp 文件日志 init 提前到参数解析前（退出码 2 双通道实测）+ windows.h 先于 glfw3.h（C4005 宏重定义清零）。
  - **验证**：Debug 全量构建零错误零第一方警告（4 target）；PlayCycleTest 全部通过（失败 0 项）；NovaPlayer 冒烟（demo 加载 + InputMap 4 动作 2 轴 + SpinningCube 旋转，0 [Error]）；NovaEditor 冒烟（场景打开 + 初始化完成，0 [Error]）；CLI `--headless --import --quit` 退出码 0（成功 3 / 失败 0）；退出码 2 参数错误 stderr+日志双通道实测通过。日志归档 Logs/（gitignore）。
  - **归档越界观察**：ComponentRegistry.cpp link-guard 增补 Slider/ScrollView 两行（include + make_unique，注释带模块/Coder 标记）——整合者已按 UIRenderer 注释纪律同步，无越界问题。
  - **遗留 P2（本波不改，一句话理由）**：① 手柄热插拔首帧 pressed 边沿（连接帧已按住的按钮报 pressed）——语义可辩（连接帧即「按下」）；② InputMap warnedMissing 进程级 warn-once 作用域——契约未定；③ GLFW 扳机轴静止值 -1（|v|=1 不归零），绑扳机做轴时静止输出 -1——契约冻结语义，风险提示存档；④ SceneRenderer 分组 O(n²) 最坏退化（2000 互异 mesh+材质 ≈2M 次比较）——验收场景同 mesh 无问题，改 hash 分组风险大于收益；⑤ wrap=true Text 命中矩形含右侧空白区——冻结裁定；⑥ scrollOffset 钳制在 leave 执行有 1 帧越界滚动——可接受；⑦ drawInstanced 结束未恢复 GL_ARRAY_BUFFER 绑定——实践无害（后续调用均自行绑定）。
- **字体图集扩容后新字形全灭已修复（2026-11-17，验收场景触发）**：根因 growTo1024→recreateTexture 用 glTexImage2D 重分配 512→1024 存储后未重建 mip 链——MIN 过滤为 GL_LINEAR_MIPMAP_LINEAR 时纹理立即 incomplete，之后所有 glTexSubImage2D（uploadRegion）静默失败 → 扩容之后烘焙的新字形全部落在零填充区域不可见（V05UITest 打开时中文量大触发扩容，中文全灭、仅扩容前的 ASCII/白块可见——按钮/血条正常、文字消失的诡异现象即此）。修复：recreateTexture 重传后立即 glGenerateMipmap；growTo1024 重摆期间置 rebuilding 旗标跳过逐字形上传（旧 512 纹理坐标越界无意义，整量重传兜底）。验证：播放器修复前后截屏对比（修复前文字为零星点、修复后标题/换行/滚动行全部清晰渲染）。附带：验收场景配色问题（白字白天空对比度差）非引擎 bug——V05UITest 已加半透明深色底板 + 深色文字。
- **验收场景使用澄清（2026-11-17，非 bug）**：①「SaveCube 无法选中」= 拾取选择仅 Edit 态（设计），Play 态应经 Inspector 改 Transform 数值制造存档差异；② ScrollView 滚轮滚动仅 Play 态生效（Edit 态 UI 为纯预览不处理输入，设计）——引擎侧滚动经校准合成滚轮像素级验证通过（8 格滚轮滚动区 45431 像素变化）。**（② 已被 2026-11-17 晚「Edit 态滚轮补强」修订：Edit 态悬停 ScrollView 也可滚轮滚动，仅点击/拖拽/存档仍 Play 专属，见 ARCHITECTURE §24.3.5-7。）**
- **「场景保存后对象变少」= 用户 Del 操作，非引擎 bug（2026-11-17 日志取证）**：V05UITest 两次「丢失对象」事件（9 对象 / 15 对象）经逐条日志比对确认——用户在 Hierarchy 中选中对象按 Delete 删除（Del 删除选中为设计行为，含子树级联），期间夹杂 Ctrl+Z 部分撤销，随后 Ctrl+S 保存了删除结果。序列化/Play 快照/撤销系统全部无损（无头往返测试 19→19 通过）。**提示**：误删后应连续 Ctrl+Z 直到恢复，再 Ctrl+S。
- **HiDPI 布局放大修复（2026-11-17）**：UI 布局原先「anchor×物理 FBO 尺寸 + offset×dpi 乘数」混用，150% 显示器上全部 UI 元素放大 1.5 倍溢出视口（V05UITest 验收暴露：背景板铺满视口、列表被切出屏幕外、标题画在屏幕外）。已修复为「布局逻辑像素、发射期统一乘 dpi」（UIRenderer 布局空间 = 物理/dpi，pushQuad/pushTri 唯一出口乘 dpi，命中输入入口反向换算），详见 ARCHITECTURE §24.3.5-8。
- **ScrollView 外框补强（2026-11-17）**：v1 的 ScrollView 无背景/边框（纯裁剪窗口），场景里不可见、验收找不到滚动区域。新增 bgColor/borderColor/borderThickness 序列化字段（旧文件缺省 = 默认值，零警告），可视区绘制背景填充 + 四边描边（发射序先于子树 = 内容绘制其上）。
- **V05UITest 场景布局修正（2026-11-17）**：标题原偏移 (0,-320) 在视口外（从未渲染）→ (0,-235) 入画；背景板 760×560 收缩为 700×520 留边距；控件列下移错开；滚动列表上移至 (250,-40)，行 0-6 初始可见、Row7+底部按钮滚动可见（验收「滚到底」路径完整）。

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

1. ~~**双 FBO resize/格式切换分支未经运行期验证**~~ **已验证（2026-11-16 像素级）**：Player 临时 --verify 驱动 4 次 glfwSetWindowSize（1600x900/800x600/1280x720）+ 2 次 pp.enabled 格式切换（RGBA8→16F→RGBA8，FBO 全删重建），全程无崩溃；最终帧读回 1280x720 画面正确（立方体 (103,67,56) 暖橙、天空非黑）。
2. ~~**LoadScene 端到端未验证**~~ **已验证（2026-11-16，另修复 SceneSwitcher 未编译）**：Player --verify 驱动 + SceneSwitcher 脚本 E2E——失败路径 loadScene("NoSuchScene") 返回 false 不崩；切换 Second 后 DDOL 迁移 1 棵子树、VerifyHost 存活、onStart 重跑；切回时文件对象 ID 冲突按设计 warn + 重生成、DDOL 二次迁移存活；天空调性像素证据（Second 青 / 回切蓝）。退出 Play 快照恢复由 PlayCycleTest 回归覆盖（serialize→deserialize 往返 + Play 帧循环还原全通过）。
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
6. ~~**波 3 遗留两项**~~ **已闭环（2026-11-16 像素级验证）**：双 FBO resize/格式切换与 LoadScene E2E（含 DDOL 迁移）均经 Player 验证驱动 + 帧缓冲读回通过（详见波 3 段）。

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

## 2026-11-18 波 2 记录

- **波 2 契约冻结（1cf889f）**：ARCHITECTURE §25 落盘——2a/2b 两轮拆分、骨架日纪律、9 项功能接口签名；#15 保留 spike 独立分支（spike/network-v1，独立 worktree，代码不进 main）、热重载 mtime 自动检测默认开（用户裁定）。
- **骨架日交付（890f475，c0）**：一次性布完 2a+2b 全部冻结头与钩子——16 新头 + 13 桩 cpp + SceneRenderer 遍历扩展点（LOD 替换/地形粒子钩子/IBL 天空接线/SSAO 管线/正交投影/Sprite 收集）+ GBuffer MRT + AO 合成 + IBL 采样段。Debug 全量零错误零警告、编辑器 headless 退出码 0、Player 冒烟正常。4 处落地裁定已回写 ARCHITECTURE §25.0。
- **网络 spike 交付（b2d93f9，c9，spike/network-v1 分支，不进 main）**：锁步帧同步原型（WinSock2 非阻塞、60Hz 固定 tick、3 帧输入缓冲、NetCube 确定性积分）——单机双进程实测 60 秒 3482 帧双方日志逐字节一致；80ms 延迟注入与断开均不崩溃。结论已写入 ARCHITECTURE §25.9 落地结论：可行性成立；跨机 Jolt 物理锁步 V1.0 不纳入（确定性未验证）；局域网双人锁步（无物理同步）可选纳入（2-3 周）。分支保留不合并。
- **既有问题（非波 2 回归）**：PlayCycleTest 在「V05UITest 往返」段崩于 Mesh::create（测试进程无 GL 上下文、glad 指针为空）——c0 已 git stash A/B 验证 HEAD 基线同点同 exit 139 崩溃，系 b0b1454 重建场景加入 MeshRenderer 后遗留。修复选项（测试侧跳过 GPU 上传 / 引擎 Mesh::create GL 守卫 / 场景去 MeshRenderer）留 2a 合波前定，不阻塞 Coder 开工。
- **波 2 2b 交付（2026-11-19）**：四 Coder 并行全部落地——c5 热重载（e2fdcc6：ScriptRuntime 全实现 + 双 DLL 形态 + .def 导出替代 WINDOWS_EXPORT_ALL_SYMBOLS；PoC 17/17、reload×10、Player DLL 形态冒烟一致、无 DLL/编译失败保留旧 DLL 实测）、c6 地形（4d8191c+994367e：高度图/四模式笔刷/Jolt heightfield/TerrainPanel；24 项逻辑测试 + 像素级冒烟）、c7 动画（1071c88：蒙皮全链 + 状态机；31 项无头 + GL 像素验证；测试 harness abort 弹窗根因 = deserialize 读类型不符字段抛 type_error 未捕获，已改安全读取 + try/catch 防弹窗）、c8 2D（c61411f：Physics2D 全边沿 0.199ms<0.5ms + Camera 投影序列化 + 2DTemplate）。
- **波 2 2b 合波（2026-11-19，主会话执行）**：SMR 收集分支（c7 裁定 A，971291b）；Physics2D::step 接 Scene::update 固定点；热重载编辑器接线（Scripts 菜单 + Ctrl+Shift+R + mtime 默认开 1s 轮询 + Auto Reload 开关 + Play 态跳过/退出补触发一次）；TerrainPanel/AnimatorPanel dock 底部 Tab；Inspector 补 SpriteRenderer/BoxCollider2D/SMR 区（texturePath 变更同步刷新运行时贴图）；NovaEngine.h 伞头补 2b 全部头；地形接收阴影（Renderer::setShadowUniforms 统一出口 + 地形绘制点移至阴影 pass 后 + albedo 迁 unit 10-13 让出 unit 1）；File 菜单 2D 模板项。
- **波 2 已知限制备案（v1）**：蒙皮阴影 = 绑定姿势（shadow.vert 无蒙皮，动画姿势不投影——shadow_skinned 为后续候选）；地形高度采样 GL_NEAREST（512² 轻微阶梯感）；HeightField 不支持地形 GO 旋转/非均匀缩放（Jolt 轴对齐限制）；SMR 仅渲染首个含骨子网格；骨骼 >64 截断 warn；粒子/地形不写 SSAO GBuffer（无 AO 贡献）；粒子绘制点在阴影 pass 前（「粒子最后」与冻结钩子点的张力）；c4 输出超限事故（教训：Coder 单次回复限 token + 验证最多两轮 + 测试 harness 必须 try/catch 防弹窗）。
- **合波日待办（c0 骨架日报告 + c9/c2/c3 报告移交）**：Physics2D::step 在 Scene::update 固定点接线（2b 合波）；ParticleRenderer 统计上报通道（Renderer 补 setter）；SceneViewPanel/Player 的 hdrFbo.create 传 normalDepth=true（SSAO 附件接通，2a 合波）；Camera 投影字段序列化键（2b，归 c8）；NovaEngine.h 伞头 include；editor 面板接线（EnvironmentPanel SSAO/IBL 区、InspectorPanel 新组件、EditorApplication Auto Reload 菜单）；**skyRotation→IBL 采样对齐**（c2 接缝：setEnvironment 冻结签名无旋转参数，天空盒转而反射图不转——最小修复在共享文件，归 c0）；**IBL::shutdown 接入 EditorApplication::shutdown**（c2 接缝，一行）；粒子钩子调用点移到透明段之后（若合波人工目测排序不对，一行移动归 c0）。
- **2a 各 Coder 接缝备案（已实现可接受/候选）**：compute 卷积 = 波 3 候选（fragment 全链烘焙 <200ms，收益不足）；SSAO 无 init/resize/shutdown 调用方（render 懒初始化 + 纹理懒建自愈，备案）；IBL 缓存读写器本地镜像 BinaryCache 格式（文件静态 API 装不下 cubemap mip 链，键空间隔离，合并评估为可选）；SSAO 天空判定依赖清屏色法线模长≠1（病态天空色 ≈ 单位向量时误判暗化，shader 注释备案）。

## 2026-11-19/20 波 2 人工验收记录（全部修复并推送）

验收方式：用户编辑器目测 + 主会话 harness 复刻验证（编辑器渲染路径逐变体对比、GL 探针、深度缓冲地图、物理模拟）。

- **演示场景全白（meshPath 字段名）**：V05Wave2Demo/V05TerrainDemo 的 MeshRenderer 网格字段写成 `meshPath`，反序列化读 `mesh` → 网格全空、静默跳过渲染（空路径无警告）——只剩天空+粒子可见。修复：JSON 字段名统一为 `mesh`（4d42f00）。
- **演示场景全白（MRT 无 MSAA FBO 创建必败）**：Framebuffer 非 MSAA + normalDepth 路径首次完整性检查在附件 0 挂载之前 → create 恒失败 → 渲染整体跳过（编辑器 MSAA 路径不受影响；samples=1 调用方全灭，harness 深度探针复现）。修复：附件 0 挂载移至检查前（4d42f00）。
- **演示场景全白（HDR 环境过亮）**：test_ibl.hdr 原为高亮程序化测试图（均值 1.78、太阳斑占宽 50%、峰值 28），IBL 1.0 + 曝光 1.0 把画面洗白。重生成 128×64 标准天空（小太阳+蓝色渐变，RLE 格式）并调优场景（iblIntensity 0.85 / skyExposure 1.1 / bloom 0.2）——首版新图误用非标准 xyze 平铺格式导致 stb 解码失败刷屏，已改标准 32-bit_rle_rgbe（9731c15）。
- **地形碰撞穿透（HeightFieldShape 双重偏移）**：Jolt 高度场表面 = 体位置 + shapeOffset + scale×样本；世界 min 角同时填入两处 → 碰撞面平移出世界范围（x -100..0 vs 地形 -50..50），落点在范围外的物体直接穿透（掉落方块 y 10 直坠 -34 复现）。修复：shape offset 归零，体位置承载 min 角（8ca4981）；修复后落点 0.81 = 地形高 0.33 + 半高 0.5 精确吻合。
- **演示场景相机朝向惯例**：引擎相机 forward=+Z；三个演示场景相机用默认朝向导致 Play 空白——Wave2Demo（水平前望压在地平线太阳上）、SkinnedQuadDemo（背对四边形）、2DTemplate（站在精灵背面被背面剔除）。修复：相机 180° 偏航对准场景，2D 相机移至 z=+10 从精灵正面取景（0b28f2d、3528dc2）。**惯例备案：场景作者给相机摆位须转 180°（四元数 [0,1,0,0]）或面向 -Z。**
- **cubemap 缓存弱引用**：天空盒每帧重建 equirect 转换（转换帧 GL 状态扰动导致全黑）——改强引用缓存（7ea4cc2）。
- **编辑器退出崩溃隐患**：TerrainRenderer::shutdown 未接入 EditorApplication::shutdown——静态地形缓存内 shared_ptr<Mesh> 在 GL 上下文销毁后的静态析构期执行 glDelete → 潜在退出崩溃（harness 复现 exit 139）。已补接（77ca9c2）。
- **AnimatorPanel 默认关闭**：用户选中带 Animator 对象后"看不到面板"——改为启动默认开启（3528dc2）。
- **已知限制确认（非 bug，预期管理）**：骨骼本身不渲染（无骨骼可视化，V1.0 可选）；Edit 态粒子为稳态快照（时间冻结）；编辑器保存场景会用完整浮点精度重排数值格式（无损）。

## 2026-11-20 V1.0 波 1 交付与审查记录（代码闭环，GUI 交互项待人工验收）

四条线并行交付 → Reviewer 审查（有条件通过：无 P0，1 P1 + 13 P2）→ 修复 → 合波接线。机器级验证全绿：NovaTests 56 项 Debug/Release 双配置全过、双 exe 冒烟（编辑器 --quit/--headless --import exit 0、Player 12s 稳定）、CI workflow 本地语法校验。

- **P1：NOVA_HAS_BC7ENC 宏缺失（压缩链路整体惰性）**：cG 的 bc7enc 实现被编译期守卫挡住走占位回退，验收 1/3/5 无法达成。修复：engine/CMakeLists.txt 加 PUBLIC 编译定义（cT）。实测：CLI --import 日志 terrain_smoke 512² BC1 1048576→174776 B、test_ibl.hdr 128×64 32768→5480 B，压缩链路端到端生效。
- **P1（存量引擎 bug）：Transform::localMatrix thread_local 求值顺序**：worldMatrix() 中「父级递归（内部覆写同一 thread_local 静态）× 自身 localMatrix()」两操作数求值顺序不定——父/子同时脏时子级世界矩阵错算（实测 父(10,0,0)+子局部(1,2,3)→(20,0,0)），编辑器遍历父先查故潜伏未爆。修复：localMatrix 改按值返回（无外部调用者，签名变更安全；cT 测试「父先查」规避注释同步失效）。
- **P1（存量引擎 bug）：setParent keepWorldTransform 跳变**：旧实现把旧世界矩阵直接 decompose 成局部 TRS，仅新父级为恒等时世界位置保持成立。修复：先换算 local = inv(newParentWorld) × world 再 decompose（GameObject.cpp）。
- **P2-1：BinaryCache corrupt()「损坏即删」不生效**：ifstream 仍打开时 remove 被 Windows 共享冲突静默吞。修复：先读后删（流析构后再 remove）+ 删除失败 warn 可见（cG，含文件锁句柄回归测试）。
- **P2-2：Profiler::clear() GPU 查询悬挂**：面板 Clear 落在 NE_GPU_SCOPE 段内时查询永不出队，4 帧后槽位复用触发 GL_INVALID_OPERATION。修复：clear() 先补 glEndQuery 收尾（cR，新增回归场景）。
- **P2-3：memorySnapshot 每帧 per-object 堆分配**：getComponents 按值 vector 拷贝 × 每对象每帧。修复：GameObject::componentCount() 直读计数（主会话）。
- **P2-5：导入设置 NormalMap 勾选与 colorSpace 交互覆盖**：勾选后再改回 sRGB 会被下次加载静默覆盖。修复：NormalMap 勾选期间锁定 Color Space 下拉（主会话）。
- **P2-11：FrameRecord::memory 不重置**：异常路径显示旧帧快照。修复：beginFrame 打开新帧槽时重置（cR）。另：P2-6/P2-7（.novameta compression 键锁定 + NormalMap 保真回归 + TestMemory 注释）已由 cT 补用例（新增 10 断言）。
- **契约偏差批准备案（评审实据通过，已回写 ARCHITECTURE §26）**：① 压缩缓存用 payloadType 4（契约初稿写 3，但 3 已被 TextureRGBFloat 占用，用 3 会误删既有 HDR 缓存）；② 能力查询用 glGetInternalformativ（GL_TEXTURE_COMPRESSED 是纹理对象状态非能力查询，首张导入恒 false 会全量误判）；③ TextureImportSettings 追加 isNormalMap（BC5 自动策略使能 + 修复 KNOWN-ISSUES #5 NormalMap 往返有损）。追认 2 项：Texture::createFromCompressed 新入口、Profiling/ProfilerSections.h 新公共头。
- **构建环境**：bc7enc FetchContent 与旧 build 目录平台缓存冲突（configure 报 generator platform 不匹配）——--fresh 重配置解决；全新目录/CI（fresh clone）不受影响。
- **CI 首跑失败（2026-11-23，已修）**：Debug 构建 MSB8066——glad_add_library REPRODUCIBLE 模式在**构建期**经 Python 脚本生成 GL 加载器源码，CI 干净 Python 无 jinja2 → 自定义构建步骤失败（本机 Anaconda 自带 jinja2 掩盖至今）。修复：ci.yml 增补 `pip install jinja2` 步骤。附带发现：windows-latest 镜像实装 VS2026（默认生成器即与本地工具链一致）；排障通道备案——check-runs annotations API 公开可读（构建/测试失败时 ci.yml 以 ::error 注解输出日志尾部，括号已转义防结构化解析截断），artifact 下载需认证不可用。
- **遗留转后续波次**：Packager 收集段不收集 AudioSource.clipPath/normalMap 等贴图（预检会拦缺失但存在资产不进 pak——波 2 #11 发布流水线一并修）；流式 blend∈(0,1) 双分量共享同一 ma_decoder 游标（需听测复现，候选 ma_data_source_splitter）；Texture.cpp 上传路径无 GL 守卫（NovaTests 贴图真值断言前置，后续补）；AssetReferenceScanner 目录级 moveAsset 不重写目录内文件级引用（验收只要求文件级，扩展场景）；resolveAssetAbs 复刻待抽公共入口；Scene.cpp 私有头 include 待提公共入口。
- **待人工验收（GUI 交互）**：① Profiler 面板（Window→Profiler：录制 10s 帧时间线/点击定位/CPU 段堆叠条/GPU 6 段/内存分类）；② 纹理压缩 A/B 目测 + 导入日志 before/after 数字；③ Reference Checker（Assets 菜单/AssetBrowser 右键：删贴图→扫描命中→打包失败清单→Move Asset 重链）；④ Mixer（Audio 菜单：四滑条实时生效 + Save 后重启保持）；⑤ 音频听测（fadeIn/Out 无爆音、流式 BGM 内存平稳、快速进出 Play 无爆音）；⑥ 重父子 keepWorld 世界位置保持（P1 修复目测）。
- **人工验收（2026-11-23，通过）**：波 1 全部 GUI 交互项核验成功——Profiler 面板（帧时间线/CPU 段/GPU 段/内存分类）、纹理压缩画质目测、Reference Checker 扫描、Mixer 滑条实时生效与设置保持、重父子 keepWorld 位置保持（P1 修复目测）。波 1 收官，进入波 2（§27）。

## 2026-11-23 V1.0 波 2 交付与审查记录（代码闭环，交互项待人工验收）

三线并行交付 → Reviewer 审查（无 P0；3 P1 + P2 编号至 12 全部修复或备案）→ 修复 → 合波接线。机器级验证全绿：NovaTests 73 项 Debug/Release 双配置全过、编辑器 --quit/--headless --import exit 0、Player 12s 稳定。交付物：#2 多场景异步加载（AsyncLoader 单线程 + 主线程 flush、延迟卸载、场景参数）、#3 物理增强（CharacterController 胶囊/楼梯/斜坡、射线与形状查询、层碰撞矩阵、Physics Layers 面板）、#4 色彩分级（LUT/DOF/渐晕/颗粒 + RGBA8 直通一致性）、#5 Shader Graph（节点编辑器 + 代码生成 + 独立队列）、#11 发布流水线（RC 版本/图标、buildRelease 发行目录、README/steam 占位、CLI --release、收集补齐波 1 遗留 17 形态 + 新资产形态）。

- **P1-1：unloadScene 同步销毁 = updateAll 遍历迭代器失效**：帧内卸载直接 erase 已加载列表 → 后续遍历悬垂。修复：延迟卸载——unloadScene 校验后入队，flushPendingLoad 头部统一销毁（erase → Scene 析构 → rebuildView → releaseUnused → SceneUnloaded 事件）；updateAll 遍历快照副本 + 迭代兜底。回归测试「V1.0-2 updateAll unloadScene deferred no dangling (P1-1)」。
- **P1-2：physicsCollisionMatrix 序列化复位缺失**：编辑器复用同一 Scene 对象，含 physics 段的旧场景保存后矩阵保留上次值（条件读取兜底挡不住复用态）。修复：复位全 true + 默认层名在条件读取 environment.physics 之前执行。
- **P1-3：实例级 shaderGraphPath/shaderOverride 被材质资产覆盖**：routeItem 以 materialAsset 字段组装材质 → 组件实例上的注入被静默丢弃。修复：MR/SMR 双路由实例字段优先（资产字段兜底）。
- **P2-5：Shader Graph 缓存无上限**：s_graphShaderCache 键含 mtime（每保存图换键），长期编辑泄漏。修复：上限 64 超限淘汰最旧。
- **P2-6（cG）：坏图资产崩溃**：ShaderGraphAsset::load 未捕获 JSON 异常（version 非整数/pos 为字符串等）→ 抛异常。修复：try/catch（json::exception + std::exception）→ nullptr + warn，编译失败缓存防每帧重试。
- **P2-8（cG）：EnvironmentPanel LUT 状态陈旧**：状态文本只随面板操作更新——场景切换/外部改路径后显示旧状态。修复：状态与路径 diff 绑定刷新。
- **P2-9（cR）：非 additive 异步加载与同步加载竞态**：loadSceneAsync(false) 入队时同帧更早的同步加载请求仍生效 → 覆盖语义混乱。修复：非 additive 异步加载清空 s_pendingLoad（互斥不变量 + 断言）。
- **合波日接线（主会话）**：编辑器/Player 主循环按 §27.1 帧序重排（updateAll → flushPendingResults → flushPendingLoad → 音频监听 → renderScenesToOutput → 显示）；AsyncLoader::init 移 GUI 专属路径（**headless 退出 std::terminate 教训**——CLI 分支无 shutdown 调用点，线程存活至进程退出）；SceneViewPanel/InspectorPanel/AssetBrowser 面板接线（Shader Graph 拖放赋值 + Detach、CharacterController 编辑器区、New→Shader Graph）；AsyncLoader::shutdown/reset 接入 shutdown/enterPlayMode；NovaTests 16 个新用例全部改 ASCII 名（中文名经 ctest 控制台代码页过滤 → 「No test cases matched」假失败）。
- **存量引擎修复（合波日）**：Texture::createFromPixels GL 守卫（NovaTests 无 GL 上下文上传段）；TestMain.cpp 防弹窗静态初始化器（_CrtSetReportMode→文件，vector 下标断言不再弹中止对话框）。
- **备案（已回写 ARCHITECTURE §27）**：自定义 shader 队列 v1 范围、物理 v1 范围（蹲伏/攀爬/游泳后续波次）、buildPlayer 收集行为变化；追认 4 项（延迟卸载语义、routeItem 实例级优先级、cG 共享 shader 接触、cT --release CLI 扩展）。
- **验收演示包（2026-11-23，b2aa897）**：为非技术验收准备的现成资产——assets/scenes/V10CharacterDemo（#3：0.3m 三级楼梯 + 0.5m 阻挡台 + 30° 斜坡 + 胶囊角色）、V10MultiSceneDemo/V10AdditiveLevel（#2：大厅+关卡场景对）、assets/shadergraphs/v10demo.novashadergraph（#5 示例图）；samples/CharacterWalk（WASD+Space 驱动角色）、samples/SceneLoadDemo（数字键 1 叠加/2 卸载/3 切换关卡/4 切回大厅）；assets/build.novabuild（15 场景构建列表，#11 收集前置）。
- **待人工验收（GUI 交互）**：① 角色控制（打开 V10CharacterDemo → Play：WASD 行走 + Space 跳跃 + 上绿色楼梯/被红色 0.5m 台阻挡/上蓝色斜坡 + Physics Layers 面板层矩阵切换）；② 多场景异步（打开 V10MultiSceneDemo → Play：按 1 右侧出现橙色关卡方块（additive 无黑屏）→ 2 卸载消失 → 3 异步切换关卡 → 4 切回大厅，Console 面板看加载进度日志）；③ 色彩分级（Environment 面板 Color Grading 区：LUT 选 assets/luts/warm.cube 或 cool.cube + intensity 滑条 A/B 氛围对比、DOF/渐晕/颗粒开关无残留）；④ Shader Graph（V10MultiSceneDemo 中悬浮球 = 示例图渲染（紫色 + 粗糙度随时间波动）；ShaderGraph 面板打开 v10demo.novashadergraph 看节点连线；新建图 → 连 Color→albedo → 保存 → 拖到任意材质生效）；⑤ 发布流水线（已机器验证：buildRelease 双跑字节一致 + 发行目录 Player 冒烟 + exe 版本 1.0.0.0/图标；人工项 = 发行目录 dist_a/NovaEngine_1.0.0/NovaPlayer.exe 双击即玩 + 右键 exe 属性查看版本/图标）。

## 2026-11-23 波 2 验收期修复（用户像素级反馈「屏幕什么都没有」，18e8f7b）

- **白屏根因**：`PostProcessSettings.lutIntensity` 默认 **1.0**（cG 波 2 新字段）——场景 JSON 无该键 → 运行时恒 1.0；无 LUT 加载时 C++ 侧按「永不黑屏契约」绑定 1×1×1 白 3D 兜底纹理 → combine 的 `mix(color, 白, 1.0)` = **纯白**。编辑器 Scene 视口、开发 Player、发行包 Player 三面全白（此前机器冒烟只验「进程存活 + 日志」，从未验像素——**验收教训：渲染功能必须像素级验证，白屏无任何日志报错**）。「白兜底 = 直通」的不变量仅在强度 0 时成立，而默认强度 1.0 恰好打破它。
- **修复**：两处 combine（applyDirectCombine 直通路径 + HDR 主链）在 `ColorGrading::lutTexture()==0` 时强制 `uLutIntensity=0`——白兜底永不参与混合；选了 LUT 后默认全强度语义保留（面板滑条照常）。
- **排障手段备案（可复用）**：临时 GL 读回探针（hdr/out/默认帧缓冲三级 glReadPixels + 环境变量开关，定位到 LUT 混合环节）+ PrintWindow 截窗像素统计（全白 3 色 → 场景 89 色确认修复）。探针已全部移除，仅修复入库。
- **验证**：Debug/Release 双构建 73/73 全绿；编辑器视口（天空蓝 + 深色地形 + 面板布局 ASCII 色图确认）、开发 Player、发行包 Player 三面像素验证正常。
- **候选回归测试**：GL 像素级「无 LUT + 默认设置渲染非白」断言（现有 GL 测试 harness 可承载，波 3 #9 测试域归 cT）。

## 2026-11-23 波 2 验收期修复（第二日，用户验收反馈「按住 WASD 不会移动」，f794671）

- **P0 级引擎 bug：跨 DLL 组件 typeId 不一致（角色不动的根因）**：`Component::componentTypeId<T>()` 按模块内静态计数器 fetch_add——exe 与脚本 DLL（NovaGame.dll）各持一份计数器副本，DLL 侧 `getComponent<CharacterController>()` 算出的 id 与引擎注册值不一致 → 恒空。物理世界用组件注册表能找到 CC（character=1），脚本找不到（静默 return 零报错）——存量脚本从未用过 getComponent<T>()（全走 transform() 便捷路径），该雷由 CharacterWalk 首踩。修复：typeId 改为类型名 FNV-1a 哈希派生（typeid 抹名串由编译器确定，两模块同名同类 → 同 id；运行期 id 不持久化，零兼容风险）。**教训：脚本 DLL 中所有跨模块模板静态（计数器/缓存）都是雷——排障时优先怀疑。**
- **新功能：按键调试日志（用户需求）**：`NOVA_DBG_KEYS=1` 时按下/松开边沿逐键输出「按下 XX 键」（Input::beginFrame 内，与 isKeyPressed/Released 同源判定；编辑器 Console 面板/终端/日志文件三通道可见）。验收/排障确认按键是否到达引擎的第一手段。
- **验收场景镜头构图问题（非引擎 bug）**：原静态相机 (0,3.5,10) 水平朝向把全部场景内容压进画面底部 13%（「一片天+底部一条线」）。重构：相机改为角色 GO 子物体（第三人称跟随）+ 视觉球体拆子物体（避免角色 GO 非均匀缩放污染子相机偏移）+ 两大厅场景相机补俯角（四元数经 Python 数值计算）。
- **新发现引擎 bug（备案，波 3 修复）：阴影相机中心附近物体整体判暗**：角色（位于跟随相机中心 = 阴影盒中心附近）在阴影开启时全暗（纯环境光）。排障：GL 读回探针 + shader 诊断色 + CPU 光空间复算（中心点判 lit 而 GPU 全暗，fragment current ≈ 0.52 > 贴图 0.50，存在系统性 ~0.02 偏移，2.4m 量级）——疑似阴影矩阵/深度在某环不一致（纹素对齐平移或视锥 near/far），待波 3 定位。**V10CharacterDemo 已临时 castShadows=false 保证验收观感**（演示验收项不含阴影）。
- **验证**：Player 按键注入实测角色位移 (x−2.2, z−1.5) + 楼梯步进（着地 y=1.75 于台阶顶）；73/73 测试双配置全绿；画面像素级验证 106 色正常。

## 2026-11-23 波 2 验收期修复（第三日，用户验收反馈「红色台子可以站上去」）

- **物理 v1 备案：自由台面的实际可步进高度 > stepHeight**：用户实测 0.5m 独立台子可**走**上去（无跳跃）——胶囊底部半球可「骑」上自由边缘，实际可爬 ≈ stepHeight(0.35) + 圆底骑边余量（cR 的「0.5m 阻挡」测试在受控条件下通过，自由台面不适用）。机器复现：按键注入 D+W，角色 y 0.90→1.39（= 0.5m 台顶 + 胶囊半高）。**演示修复：阻挡台加高到 1.0m（Blocker10m）**——注入实测角色顶墙止步（z 恒 -5.98、y 恒 0.90）；1.0m ≫ 可步进高度，走路必被挡，跳跃（1.84m 顶点）可上属正常。**波 3 修复方向**：步进判定收紧（自由边缘骑边补偿）或 stepHeight 语义对齐 Unity（步进 + 圆底补偿）。
