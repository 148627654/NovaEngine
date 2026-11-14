# NovaEngine 用户指南：已实现功能与操作方法

> 本文档以**实际代码**为准（快照时间 2026-11-15，已并入 V0.2 第一批功能），面向首次使用的用户回答三个问题：**哪些功能已实现、每个功能怎么用、有哪些已知限制**。
> 规格说明见 [ARCHITECTURE.md](ARCHITECTURE.md) 与 [ARCHITECTURE-V0.2.md](ARCHITECTURE-V0.2.md)（V0.2 第一批：阴影映射 / 天空盒 / Undo-Redo / 未保存确认 / 统计面板），已确认的问题清单见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。规格文档里写了但代码尚未实现的内容统一收在 [§9.4 待实现](#94-规格中有但代码尚未实现待实现)，不会混入「已实现」章节。

---

## 1. 快速上手

### 1.1 环境要求

- Windows + Visual Studio 2026（MSVC，x64）
- CMake 4.4（Visual Studio 多配置生成器）
- Python 3（glad 在 configure 期生成代码，必须可用）
- 首次构建需要网络（FetchContent 拉取 GLFW / glad2 / GLM / ImGui / Assimp / nlohmann/json / stb / ImGuizmo）

### 1.2 构建命令

```bat
:: 在项目根目录（NovaEngine/）执行
cmake --preset vs2026                    :: 配置，生成 build/NovaEngine.sln
cmake --build build --config Debug       :: 编译 Debug
:: 或直接
cmake --build --preset debug
```

Release 用 `cmake --build --preset release`。首次配置中 Assimp 编译耗时最长（已裁剪到 OBJ/FBX/glTF/Collada 四个导入器），预计数分钟，属正常现象。

### 1.3 运行方式

两种方式均可，且**无需手动设置工作目录**（程序启动时自动切换到编译期注入的项目根）：

- **双击运行**：`build\editor\Debug\NovaEditor.exe`。启动后会额外弹出一个控制台窗口（编辑器保持控制台子系统），显示 stdout 日志（已切换 UTF-8 代码页，中文不乱码）。
- **VS 调试**：打开 `build/NovaEngine.sln`，启动项目设为 `NovaEditor`（CMake 已把调试工作目录设为项目根），F5 运行。

启动后窗口为 1600x900、垂直同步开启、OpenGL 4.5 core。若 `assets/scenes/demo.nova.json` 存在则自动加载，否则从空场景（Untitled）开始。统计面板默认关闭，**按 F8** 随时打开（§3.6）。

### 1.4 默认 demo 场景内容

打开后场景里已有四个对象，可以直接用它们体验下文所有功能：

| 对象 | 层级 | 组件与参数 | 用途 |
|---|---|---|---|
| **Main Camera** | 根 | Camera（isMain=true，位置 (0,3,8)） | Play 模式的渲染相机 |
| **Sun** | 根 | DirectionalLight（颜色 (1, 0.95, 0.85)，强度 1.2，位置 (0,5,0)；**阴影默认开启**：castShadows=true / shadowRange 60 / shadowMapSize 2048） | 平行光（方向取自 Transform 朝向），默认在地面投出阴影 |
| **Cube** | 根 | MeshRenderer（cube.obj，橙色 albedo (0.9,0.35,0.2)，roughness 0.5）+ **SpinningCube** 脚本（speed=30） | 按 Play 后绕自身 Y 轴匀速旋转 |
| **SmallCube** | Cube 的子级 | MeshRenderer（cube.obj，白色，scale 0.4，位置 (1.5,0,0)） | 演示父子层级：跟随父级旋转 |

另有 40x40m 网格地面（1m 次线 / 5m 主线）、**渐变天空**（默认 horizon (0.55,0.62,0.72) → zenith (0.22,0.30,0.48)）与环境光（ambientColor (0.15,0.15,0.18)）。demo 文件本身仍是 v1 格式：天空与阴影字段全部按默认值兜底——**启动即可看到渐变天空与立方体投在地面的阴影，无需任何配置**（§7.2）。

---

## 2. 界面总览

首帧由 DockBuilder 摆好五面板布局，之后可自由拖动 dock 标签重排（窗口不能拖出主窗口——多视口功能未启用）：

```
┌─────────────────────────────────────────────────────┐
│ 菜单栏：File | Edit | View                            │
├────────────┬───────────────────────────┬─────────────┤
│ Hierarchy  │         Scene（3D 视口）   │  Inspector  │
│（左 20%）   │                           │ （右 22%）   │
├────────────┴───────────────────────────┴─────────────┤
│ 底部 Tab：Console | Assets | Stats（共 28% 高）        │
└─────────────────────────────────────────────────────┘
```

- **Hierarchy（层级）**：场景对象树。创建/选中/重命名/重父子/删除/复制都从这里发起（§3.1）。
- **Scene（场景视口）**：3D 画面（离屏 MSAA 4x 渲染到 FBO 再贴到面板上）。编辑器相机操作、点击拾取、gizmo 变换、拖放导入、天空与阴影渲染都在这里（§3.2）。
- **Inspector（检查器）**：编辑当前选中对象的组件参数（Transform/材质/相机/灯光/脚本）；无选中时编辑场景 Environment（天空）参数（§3.3）。
- **Console（控制台）**：引擎日志输出，四色分级、可过滤（§3.4）。
- **Assets（资源浏览器）**：浏览 `assets/` 目录，加载场景、拖拽资产（§3.5）。
- **Stats（统计）**：性能面板（FPS/帧时间/渲染计数器/直方图），**默认关闭，F8 开关**（§3.6）。

**主菜单栏**：

- **File**：New Scene（Ctrl+N）、Open Scene...（Ctrl+O）、Save Scene（Ctrl+S）、Save Scene As...（无快捷键）、**Exit**。新建/打开/保存/另存为四项在 Play 模式下全部禁用（灰显）；Exit 始终可用（Play 中会先退出 Play 还原，再走未保存确认流程，§6.2）。
- **Edit**：**Undo（Ctrl+Z）/ Redo（Ctrl+Y）**（Edit 态且 gizmo 未拖拽时可用，§5）、Play / Stop（Ctrl+P，同一菜单项随状态切换文案）、Pause（勾选式开关，仅 Play 中可用）。
- **View**：Reload Assets（清空资产缓存，外部修改模型/贴图文件后点它刷新）；下方列出**六个**面板的显示开关（含 Stats）。

---

## 3. 功能清单（按面板）

### 3.1 Hierarchy

**创建物体**：在空白处**右键**（创建到根层级），或在某个对象行上**右键**（创建为该对象的子级），选择：

- **Create Empty** — 空对象（只有 Transform）
- **Create Cube** — 带 MeshRenderer 的立方体（内建网格 `builtin:cube`）
- **Create Plane** — 10x10m 平面（`builtin:plane`）
- **Create Directional Light** — 平行光组件（阴影默认开启），初始朝向预设为经典斜下方向（欧拉 (50°, -30°, 0)）
- **Create Camera** — 相机组件；**场景中第一个相机自动设为 Main Camera**（isMain=true）

> 内建网格 `builtin:cube / builtin:sphere / builtin:plane` 也是合法的 Mesh 路径，可在 Inspector 里手写，保存场景时可随序列化往返。

**单击选中**：点击对象行选中（多选时 Inspector 只显示数量，不显示参数）。点行首箭头只展开/折叠子树、不改变选择；**Ctrl+单击为加选/减选（toggle）**；点击空白处取消全部选择。

**F2 重命名**（仅恰好单选一个对象时生效）：F2 进入编辑，**Enter 或点击别处确认**，**Esc 取消**，留空不生效。注意：该操作在 Play 模式下未被禁用（见 §9.2-2）。

**拖拽重父子**：按住对象行拖到另一行 = 成为其子级（**保持世界变换**）；拖到空白处 = 脱离父级挂到根。有环保护——不能把父级拖进自己的子树。

**右键菜单（对象行）**：除上述 5 个创建项（创建为子级）外，还有 **Duplicate（Ctrl+D）** 与 **Delete（Del）**。

**Del 删除选中**：删除所有选中对象**及其整棵子树**（延迟到帧末真正释放）。Play 模式下禁用。

**Ctrl+D 复制选中**：为每个选中对象生成一份副本，副本成为原对象的**兄弟**（同父级、新 ID），完成后自动选中全部副本。Play 模式下禁用。**注意：复制不包含子物体**（浅拷贝，见 §9.2-1）。

以上全部结构操作（创建/删除/复制/重命名/重父子）在 Edit 态都会进入 Undo 栈，可 Ctrl+Z 撤销（§5）。

### 3.2 SceneView（场景视口）

**编辑器相机**（仅 Edit 模式，鼠标需在视口内；拖拽中途离开视口仍继续响应）：

| 操作 | 效果 |
|---|---|
| **右键拖拽** | 绕目标点旋转视角（yaw/pitch，俯仰角被钳制在 ±88° 左右防万向锁） |
| **中键拖拽** | 抓取式平移（场景跟随鼠标方向移动） |
| **滚轮** | 缩放，速度随当前距离自适应（近处慢、远处快，距离钳制 0.1–1000m） |
| **W / A / S / D** | 沿相机平面平移（速度随距离变化；**按住 Shift 加速 3 倍**） |
| **F** | 聚焦：有选中则对准选中对象（含子树的整体包围盒，距离≈半径 3 倍），无选中则对准原点上方 (0,1,0)、距离 8m |

**左键拾取选择**（仅 Edit 模式）：单击命中对象（射线 vs 世界 AABB，取最近者）→ 选中；**Ctrl+单击 = 加选/减选**；点击空白（未命中）→ 取消选择。按下与松开位移超过 3px 视为拖拽（相机操作），不触发选择；鼠标悬停在 gizmo 上时不触发。

**选中高亮**：所有选中对象显示**橙色描边**（stencil 两遍，沿法线膨胀约模型尺寸 4%），被其他物体遮挡的部分也可见（Unity 风格）。

**Gizmo 变换**（仅 Edit 模式，且**恰好单选一个对象**时才出现——多选无 gizmo，这是最常见的「gizmo 不见了」原因）：

1. **W** 切换移动 / **E** 切换旋转 / **R** 切换缩放；
2. **X** 在 **Local / World** 坐标空间之间切换（有父级的对象 Local 模式下写回的是局部 TRS）；
3. 直接**拖拽箭头/圆环/方块手柄**即可变换，拖拽期间相机与点击选择被抑制；
4. 修改实时写回 Inspector 的数值框。**拖拽结束瞬间**整体算一步 Undo（§5.3）。

**天空渲染（V0.2）**：视口先画天空再画场景（Edit/Play 两态一致）。默认 **Gradient 渐变天空**（horizon → zenith 两色渐变）；把 Environment 的 Sky Mode 切到 Cubemap 并填好 Cubemap Path 后，改为采样 cubemap（支持 6 面目录或 equirect 全景文件，加载失败自动回退渐变）。`scene.skyColor` 仍保留为**兜底清屏色**（天空绘制失败时可见），不再决定日常看到的天空颜色（§3.3）。

**阴影（V0.2）**：场景第一个 DirectionalLight 的 **castShadows 默认开启**，场景物体会在网格地面投出实时阴影（PCF 3x3 软化；网格地面同样接收阴影，全影压到约 35% 而非全黑）。阴影正交盒沿光轴覆盖**过原点平面 ±shadowRange**（默认 60m），盒中心跟随渲染相机沿光轴投影（固定范围 + 跟随相机，不做场景包围盒）。**相机走远后，离相机超过 shadowRange 的盒外几何不再受影——这是预期行为，不是 bug**。castShadows 关闭即无阴影；Shadow Map Size 修改当帧生效（懒重建深度贴图）。

**接受内部拖放（从 Assets 面板拖来）**：把模型文件拖到视口任意位置 → 自动导入：射线与 y=0 地面求交点作为落点（无交点则落在相机前方 3m），生成「根对象（文件名）+ 每个子网格一个子对象」的层级，导入完成后**自动选中并聚焦**新物体。贴图文件拖入视口会被拒绝（提示「仅支持将模型文件拖入场景视口」）。

**Play 模式**：视口切换为场景相机画面，编辑器相机输入、拾取、F 聚焦、gizmo 全部停用（§4）。

### 3.3 Inspector

单选对象时显示完整参数；多选时只显示「N objects selected」。

**通用**：顶部 **Name** 文本框与 **Active** 勾选框（Active=false 时对象及其子树不渲染、脚本不更新）。

**Transform**：Position / Rotation / Scale 三个 DragFloat3。**Rotation 用欧拉角（度）编辑**，内部存储四元数（保证序列化往返）。Scale 最小值 0.001。

**Mesh Renderer**：

- **Mesh**：网格资产路径，**回车生效**。可填 `builtin:cube / builtin:sphere / builtin:plane` 或项目内相对路径（如 `assets/models/sphere.obj`）。
- **Albedo**：漫反射颜色（ColorEdit3）。
- **Roughness / Metallic**：0–1 滑块。
- **Albedo Map Path**：贴图资产路径（项目内相对路径），**回车生效**；清空 = 取消贴图回纯色；路径无效会在 Console 打警告。上方文字实时回显当前绑定的贴图。
- **Remove Component**：移除该组件（Play 中禁用）。

**Camera**：FOV（10–170°）、Near Plane、Far Plane、Clear Color（Play 时该相机的清屏色）、**Main Camera** 勾选（Play 时优先使用 isMain 的相机）。Remove Component 移除。

**Directional Light**：Color、Intensity（0–100）。**光照方向由对象 Transform 的朝向决定**（forward = +Z），旋转该对象即可改变光照方向。V0.2 新增阴影四控件（Play 中可 live edit，当帧生效）：

- **Cast Shadows**：勾选框，默认开；
- **Shadow Bias**：深度偏置，0–0.01，步进 0.0001（显示 5 位小数）；
- **Shadow Range**：阴影正交盒半宽（米），5–200，步进 1；
- **Shadow Map Size**：深度贴图边长（px），512–4096，步进 512（修改当帧重建贴图）。

Remove Component 移除。

**Script（脚本组件）**：折叠头显示「Script (脚本名)」，包含：

- **Enabled** 勾选（false 时 Play 中该脚本不执行）；
- **脚本字段**：通过「伪反射」自动生成编辑器——bool → 勾选框、float → 拖拽框、int → 整数拖拽框、string → 文本框；改完立即写回脚本对象（§10.3）。
- **Remove Component** 移除脚本（Play 中禁用；实现上走整场景重建，移除后其余对象参数不变）。

**Add Component**：下拉框 = 内置组件 `Camera / DirectionalLight / MeshRenderer` + 所有已注册脚本（当前有 `SpinningCube`、`OrbitCamera`）。选中即挂载。Play 中禁用。**注意：下拉框不会过滤已挂载的同类型组件**（见 §9.2-5）。

**Environment（天空）折叠区（V0.2，仅无选中时显示）**：选中集为空时 Inspector 底部显示 Environment 折叠区（多选时只有「N objects selected」，不显示）：

- **Sky Mode**：Gradient / Cubemap 下拉；
- **Horizon Color / Zenith Color**：渐变天空两色；
- **Cubemap Path**：cubemap 资产路径（6 面目录或 equirect 文件），**回车生效**；清空 = 回退渐变；路径无效/加载失败在 Console 打警告并回退渐变；
- **Sky Rotation**：-360–360°（cubemap 绕 +Y 旋转，度）；
- **Sky Exposure**：0–10（HDR 曝光）。

注意：skyColor / ambientColor / showGrid 仍没有 UI，只能手改 .nova.json（§9.2-9）；本折叠区是 V0.2-5 独立环境面板落地前的临时替身（§9.3-8）。

### 3.4 Console

- **四级分色**：Debug 灰 / Info 白 / Warn 黄 / Error 红。
- 顶部四个级别**过滤勾选框**（Debug/Info/Warn/Error），可单独开关某一级。
- **Auto Scroll**：有新日志自动滚到底（默认开）。
- **Clear**：清空面板。
- 容量上限 **500 条**（环形缓冲，超出丢最旧的）。
- 同一份日志同时写入控制台窗口（stdout）与 OutputDebugString（VS 调试输出可见）。

### 3.5 Asset Browser（Assets）

- 只浏览项目 `assets/` 目录；顶部**面包屑**导航（点击任意一级跳转），列表上方 `[ .. ]` 返回上级。
- 条目前缀标记类型：`[D]` 目录、`[M]` 模型、`[T]` 贴图、`[S]` 场景、`[F]` 其他文件；悬停显示相对路径。
- **双击目录**进入；**双击 .nova.json 场景文件**直接加载该场景（Play 中不可，会提示；Edit 态场景未保存时先弹「未保存更改」确认，§6）。
- **模型 / 贴图文件是拖拽源**：按住拖到 SceneView 即导入/引用（§3.2）。双击模型/贴图文件本身无操作。
- 该面板是外部文件拖放的目标区域之一（§3.7）。

### 3.6 Stats（统计面板，V0.2）

- **F8 开关**（默认关闭）；也可在 View 菜单勾选。面板停靠在底部 Tab 区（Console | Assets | Stats）。
- 显示内容：
  - **FPS**：120 帧滚动均值的倒数（1000/avg），避免单帧抖动；
  - **Frame avg / peak / last**：最近 120 帧窗口的均值 / 最大值 / 最新帧时间（ms）；
  - **Update / Render / UI** 三段耗时（最近一帧；首帧为 -1.0 属预热帧）——Update = 场景更新段，Render = SceneView 离屏渲染段（FBO 绑定到 resolve），UI = ImGui 绘制段；
  - **Draw calls / Submitted / Triangles / Shadow items**（上一帧结算值）。Draw calls **不含阴影 pass**（阴影项数单列为 Shadow items）；
  - **120 帧直方图**：帧时间历史曲线，高 80px，纵轴按峰值上限缩放，叠字显示最后一帧 ms。
- **Clear 按钮**：清空统计历史（均值/尖峰/直方图归零重计）。
- **口径说明**：帧时间在 **swapBuffers 之前**结算——**不含 vsync 等待**；垂直同步开启时 FPS 是 CPU 侧口径，会比真实渲染帧率偏高（预期行为，§9.3-5）。

### 3.7 外部拖放（Windows 资源管理器拖入）

程序用 GLFW 的窗口级拖放回调接收外部文件，按**松开鼠标时落在哪个面板**分发：

| 落点            | 模型 (.obj/.fbx/.gltf/.glb/.dae)                                            | 贴图 (.png/.jpg/.jpeg/.bmp/.tga) | 场景 (.nova.json) | 其他文件  |
| ------------- | ------------------------------------------------------------------------- | ------------------------------ | --------------- | ----- |
| **SceneView** | **导入场景**：项目外文件先自动拷贝进 `assets/models/`（重名自动加 `_1` 后缀），再生成对象层级并聚焦（Play 中禁止） | 仅拷贝进 `assets/textures/`        | 提示不支持           | 提示不支持 |
| **Assets 面板** | 仅拷贝进 `assets/models/`                                                     | 仅拷贝进 `assets/textures/`        | **打开该场景**（未保存时先走确认，§6） | 提示不支持 |
| 其他区域          | 提示「文件拖放目标无效」                                                              | 同左                             | 同左              | 同左    |

- 已在项目内的文件不会被重复拷贝，直接使用相对路径。
- 模型导入会同时解析其材质与贴图（贴图在项目外时同样自动拷贝进 `assets/textures/`），保证保存场景后可移植。
- 同一模型重复拖入不重复解析（内存缓存），外部改过模型文件后点 **View → Reload Assets** 手动刷新。

---

## 4. Play 模式

### 4.1 进入 / 退出 / 暂停

- **Ctrl+P** 或菜单 Edit → Play 进入；再按一次（菜单变 Stop）**退出**。
- Play 中可用 **Edit → Pause** 暂停（勾选式）。暂停时 `scene->update()` 被整体跳过——所有脚本的 onUpdate 停摆，画面静止。

### 4.2 快照与恢复机制

- **进入 Play**：把当前场景完整序列化成 JSON 字符串保存在内存（快照），然后广播所有组件的 `onStart()`（深度优先，跳过非激活子树与 enabled=false 的脚本）。
- **退出 Play**：用快照 JSON **原地重建同一场景对象**——层级、Transform、材质、组件参数、脚本字段全部还原为进入 Play 前的值；**选中集按对象 ID 天然保留**（仍选中同样的对象）；编辑器相机恢复。
- **退出 Play 时脏标记一并还原**为进入 Play 前的状态（V0.2-2）：Play 中 live edit 产生的「未保存」不会在 Stop 后残留（§6.3）。
- 快照不落盘，与 Ctrl+S 互不干扰。
- **脚本组件的 C++ 运行时状态不保留**：例如 OrbitCamera 的当前环绕角 `m_angle` 是「运行时中间变量」（不参与序列化），退出 Play 后重置为进入前的值。这是 C++ 内嵌脚本方案的固有取舍，示例脚本都按此约束设计。

### 4.3 Play 中允许与禁止的操作

**渲染切换**：SceneView 改用场景相机（isMain 优先，无则第一个 Camera，再无则回退编辑器相机）；光照取场景第一个 DirectionalLight（阴影同样生效）；**天空沿用场景环境设置**（渐变/cubemap 与 Edit 态一致）。

**禁止（面板灰显或代码拦截）**：

- File 菜单的新建/打开/保存/另存为（快捷键同样静默失效；**Exit 例外**——会先退出 Play 还原再判脏确认）
- Undo/Redo（Ctrl+Z/Ctrl+Y 与 Edit 菜单项，§5）
- 结构修改：创建/删除对象（右键菜单创建项、Del、Ctrl+D）、拖拽重父子、Add/Remove Component
- 模型导入、加载场景（双击 .nova.json、拖入场景文件）
- gizmo（W/E/R/X 无效）、编辑器相机输入、左键拾取、F 聚焦

**允许（live edit，当帧生效）**：

- 修改 Transform 数值、材质参数、灯光参数（**含阴影四参数**）、相机参数
- 脚本字段与 Enabled 勾选（如 Play 中调 SpinningCube 的 speed 立即变速）
- 选中/展开等纯浏览操作、F8 统计面板

**注意**：Hierarchy 的 F2 重命名与 Inspector 顶部的 Name/Active 输入框在 Play 中**未**被禁用（已知问题，见 §9.2-2）。

### 4.4 示例脚本

**SpinningCube**（挂在大立方体 Cube 上）——最简脚本演示：

- 用途：Play 中让对象绕自身 Y 轴以 `speed`（度/秒，默认 30）匀速旋转。
- 挂载：选中对象 → Inspector → Add Component → 选 `SpinningCube`；字段 `speed` 可直接拖拽调整。
- 行为：仅 Play 中生效；Stop 后姿态还原为进入 Play 前的值（快照机制）；`Enabled` 取消勾选可临时停转。

**OrbitCamera**（demo 场景未挂载，需手动使用）——相机环绕演示：

- 用途：Play 中让相机绕原点（世界坐标 (0,0,0)）做圆周环绕并始终 lookAt 原点。
- 字段：`radius`（环绕半径，默认 8）、`speed`（角速度 度/秒，默认 30）、`height`（高度，默认 3）。
- 挂载方式：选中一个**带 Camera 组件的对象**（如 Main Camera）→ Add Component → 选 `OrbitCamera` → Ctrl+P。进入 Play 时 `onStart` 会从相机当前世界位置反推起始角度，保证从摆放处无缝开始环绕。
- 如果场景里没有相机，可以右键 Hierarchy → Create Camera（首个自动成为 Main Camera）再挂脚本。

---

## 5. Undo / Redo（撤销 / 重做，V0.2）

### 5.1 操作

- **Ctrl+Z** 撤销上一步；**Ctrl+Y** 或 **Ctrl+Shift+Z** 重做。
- 菜单 **Edit → Undo（Ctrl+Z）/ Redo（Ctrl+Y）** 等效；菜单灰显条件：Play 态、gizmo 拖拽中、无可撤销/可重做的步。
- **Play 态整体禁用**（快捷键与菜单都无效）。Play 中的修改不入栈，退出 Play 后栈保持进入 Play 前的内容。

### 5.2 原理（快照式）

一句话：每一步操作在「**编辑结束瞬间**」把整场景序列化为**紧凑 JSON 快照**压栈；undo/redo 时把对应快照 **deserialize 回同一个 Scene**——对象 ID 不变，选中集仅清理已失效 ID（仍选中同样的对象）。

- **64 步环形栈**：上限 64；**栈顶恒为当前态**（因此深度 ≥2 才可撤销）；超出上限丢最旧一步；任何新操作清空 redo 栈（撤销后改东西，就再也回不到被撤销前了）。
- **New / Open 场景后清空两栈**并压入新基线——此前场景的历史无法撤销。

### 5.3 什么入栈、什么不入栈

**入栈**（仅 Edit 态）：Hierarchy 的创建 / 删除 / 复制 / 重命名 / 重父子；gizmo 变换；Inspector 的全部参数修改（Transform / 材质 / 相机 / 灯光含阴影参数 / 脚本字段）；Add / Remove Component；模型导入。

- 防刷栈：拖拽 / 输入**中间帧只置脏**（不推快照），「编辑结束」（松开鼠标 / 回车 / 勾选等瞬时操作）才推快照——一次拖拽整体 = 一步撤销。
- **不入栈**：选中集、编辑器相机、面板开关与布局、进入 / 退出 Play。
- **Play 态的 live edit 只置脏不入栈**（退出 Play 时场景由快照还原，栈不动）。

已知限制（极短拖拽不入栈 / 同帧重复快照 / undo 后标题仍带 * / 相机值在快照里）见 §9.3-1~4。

---

## 6. 未保存更改确认（V0.2）

### 6.1 脏标记与标题栏 *

场景有未保存修改时，标题栏显示 **「场景名 * - NovaEditor」**；Ctrl+S 保存成功后 `*` 消失。undo 回到最初基线后标题仍带 `*`（无条件置脏，Unity 同行为，§9.3-3）。

### 6.2 五处拦截

以下五个入口在场景「脏」时都会**先弹确认模态、再继续**：

1. **Ctrl+N / File → New Scene**
2. **Ctrl+O / File → Open Scene...**
3. **Assets 面板双击 .nova.json 场景文件**
4. **外部拖入场景文件到 Assets 面板**
5. **关闭窗口（点标题栏 X）/ File → Exit**

模态（"Unsaved Changes"）三选：

- **Save**：保存后继续原动作；保存失败**不关模态**（可重试或改选）。
- **Don't Save**：丢弃修改，继续原动作。
- **Cancel**：取消本次动作，回到编辑。按 **ESC** 或点模态右上角 X 等同 Cancel。

细节：

- 模态期间**全部全局快捷键被抑制**、外部文件拖放被忽略（防止覆盖挂起动作）。
- 模态打开时点主窗口 X 视为 **Cancel**（不会绕过确认直接退出）。
- 关闭窗口 / File → Exit 的特殊流：Play 态**先自动退出 Play**（快照还原），再判脏弹模态。

### 6.3 与 Play 模式的关系

进入 Play 时记录**进 Play 前的脏状态**；Play 中 live edit 置的脏标记在退出 Play 时一并还原——所以 Play 里改参数**不会**误触发「未保存」提示（这些改动本身也会被快照还原）。此行为修复了旧版「Play 里动一下，Stop 后标题永远带 *」的问题（§9.2-4）。

---

## 7. 场景存取

### 7.1 操作

- **Ctrl+S**：保存到当前场景路径；**首次保存**（或 Ctrl+N 后的未命名场景）自动弹出**另存为**对话框。
- **File → Save Scene As...**：另存为。保存对话框为 Windows 原生对话框，默认定位 `assets/scenes/`，自动补全 `.nova.json` 扩展名。
- **Ctrl+O** / File → Open Scene...：打开场景（原生对话框，过滤器 `*.nova.json`）。
- **Ctrl+N**：新建空场景（Untitled），Undo/Redo 栈重置为新基线（§5.2）。
- 打开/新建/双击加载场景在场景**未保存时会先弹「未保存更改」确认**（§6）；Play 中以上操作全部禁止。

### 7.2 .nova.json 文件格式要点

场景就是一份 JSON，可以手改（改完用 Ctrl+O 重新加载）：

```jsonc
{
  "version": 2,                       // 版本号（保存写 2；读取兼容 1/2）
  "name": "DemoScene",                // 场景名
  "environment": {                    // 环境参数
    "skyColor": [0.18, 0.22, 0.35],   // 兜底清屏色（天空绘制失败时可见）
    "ambientColor": [0.15, 0.15, 0.18],
    "showGrid": true,                  // 网格地面开关
    // ---- V0.2 天空 6 字段 ----
    "skyMode": "Gradient",            // "Gradient" / "Cubemap"
    "skyHorizonColor": [0.55, 0.62, 0.72],
    "skyZenithColor": [0.22, 0.30, 0.48],
    "skyCubemapPath": "",             // 空 = 渐变回退；否则 6 面目录或 equirect 文件
    "skyRotation": 0.0,               // cubemap 绕 +Y 旋转（度）
    "skyExposure": 1.0                // HDR 曝光
  },
  "editorCamera": {                   // 编辑器相机状态（随场景保存/恢复）
    "target": [0.0, 1.0, 0.0], "distance": 8.0, "yaw": 0.6, "pitch": -0.35
  },
  "objects": [                        // 对象平铺列表（不是嵌套树）
    {
      "id": 16840732146653812735,     // 64 位唯一 ID
      "name": "Sun",
      "active": true,
      "parent": 0,                    // 父级 ID；0 = 根层级
      "components": [                 // 组件数组，第一项必须是 Transform
        { "type": "Transform", "position": [...], "rotation": [...], "scale": [...] },
        { "type": "DirectionalLight", "color": [...], "intensity": 1.2,
          // ---- V0.2 阴影 4 字段 ----
          "castShadows": true, "shadowBias": 0.0005,
          "shadowRange": 60, "shadowMapSize": 2048 }
      ]
    }
  ]
}
```

要点：

- **层级用 parent 引 ID 表达**（平铺 + 引用），加载时先建全部对象再按 parent 重建父子关系。
- **rotation 是四元数 `[x, y, z, w]`**（UI 里编辑的是度，存储是四元数）。
- **资产路径一律相对项目根**（如 `assets/models/cube.obj`、`assets/textures/checker.png`），换机器整目录拷贝即可移植。
- MeshRenderer 的材质写在 `material` 对象里：`albedo`（颜色）/ `roughness` / `metallic` / `albedoMap`（贴图相对路径，空串 = 纯色）。
- 脚本组件 = `type` 为脚本注册名 + 公共字段 `enabled` + 脚本自定义字段（如 `"speed": 30`）。
- **向后兼容**：v1 旧文件（如当前 demo.nova.json）没有天空/阴影字段，加载时**全部按默认值兜底**——渐变天空、空 cubemap、曝光 1；DirectionalLight 的 castShadows=true / shadowBias=0.0005 / shadowRange=60 / shadowMapSize=2048，开箱即用。旧文件不自动升级，直到下次 Ctrl+S（写 v2）。
- `skyMode` 存字符串；未知字符串 → 警告并回退渐变。version 既非 1 也非 2 时告警但仍尝试加载。
- 容错：未知组件类型 → 警告并跳过该组件（不崩溃）；JSON 非法 → 报错且**场景保持不变**；字段类型错 → 回退默认值。
- **编辑器相机位置随场景保存**：Edit 模式下每帧把相机状态回写到场景字段，Ctrl+S 即持久化；打开场景时恢复视角。
- Undo/Redo 的内部快照是同一格式的**紧凑单行 JSON**（不落盘，仅内存）。

---

## 8. 快捷键总表

> 全部来自代码（`EditorApplication::handleGlobalShortcuts`、`HierarchyPanel`、`SceneViewPanel`、`EditorCamera`）。输入框聚焦（如重命名中）时全局快捷键被抑制，防止误触；**未保存确认模态期间全部全局快捷键被抑制**（§6.2）。

| 快捷键 | 作用 | 生效条件 |
|---|---|---|
| **Ctrl+P** | 进入 Play / 退出 Play | 全局（输入框聚焦除外） |
| **Ctrl+S** | 保存场景（未命名时弹另存为） | Edit |
| **Ctrl+N** | 新建空场景 | Edit |
| **Ctrl+O** | 打开场景对话框 | Edit |
| **F8** | 开关 Stats 统计面板 | 全局（输入框聚焦除外） |
| **Ctrl+Z** | 撤销上一步 | Edit，且 gizmo 未在拖拽 |
| **Ctrl+Y** | 重做 | Edit，且 gizmo 未在拖拽 |
| **Ctrl+Shift+Z** | 重做（等价 Ctrl+Y） | Edit，且 gizmo 未在拖拽 |
| **W** | gizmo 切换为移动 | Edit |
| **E** | gizmo 切换为旋转 | Edit |
| **R** | gizmo 切换为缩放 | Edit |
| **X** | gizmo 切换 Local / World 空间 | Edit |
| **F** | 聚焦选中对象（无选中聚焦原点） | Edit，鼠标悬停 SceneView |
| **F2** | 重命名（Enter 确认 / Esc 取消） | Hierarchy 恰好单选 |
| **Del** | 删除所有选中对象（含子树） | Edit |
| **Ctrl+D** | 复制选中对象（副本为兄弟节点） | Edit |
| **Ctrl+左键** | 加选/减选 | Hierarchy 行 / SceneView 拾取 |
| 右键拖拽 | 编辑器相机旋转 | Edit，SceneView |
| 中键拖拽 | 编辑器相机平移（抓取式） | Edit，SceneView |
| 滚轮 | 编辑器相机缩放 | Edit，SceneView |
| WASD | 编辑器相机平移（Shift 加速 3 倍） | Edit，SceneView |
| Esc | 取消重命名 | 重命名输入框内 |
| 左键单击 | SceneView 拾取选择（位移 >3px 视为相机拖拽） | Edit |

未实现的常规快捷键：**Ctrl+Shift+S 另存为**（只能走菜单）。

---

## 9. 已知限制

### 9.1 KNOWN-ISSUES.md 已记录的（摘要 + 当前状态）

| # | 问题 | 现状 |
|---|---|---|
| 1 | 网格地面雾色硬编码，与场景 skyColor 不联动 | 存在 |
| 2 | Window 析构未调 glfwTerminate（单窗口无实际影响） | 存在 |
| 3 | 内置图元（builtin:cube/sphere/plane）不缓存，整场景重建时重传 GPU 网格 | 存在 |
| 4 | TextureLoader 用 ANSI 路径，**中文路径贴图加载失败** | 存在 |
| 5 | 「双击 exe 缺 shader/资产（依赖工作目录）」 | **已被代码修复**：main.cpp 用编译期注入的项目根（NOVA_PROJECT_ROOT）自动切换工作目录，双击运行即可用 |
| 6 | 模型首次导入双解析（网格走缓存 + 材质另解析一遍） | 存在 |
| 7 | Duplicate / 移除脚本组件走整场景 JSON 重建（多选复制 O(n²)、脚本运行时状态清零） | 存在 |
| 8 | Hierarchy F2 重命名在 Play 态未禁用 | 存在 |
| 9 | 脚本类型名反查依赖 MSVC typeid().name() 格式 | 存在 |
| 10 | 畸形 JSON（对象无 Transform 组件）可能导致解引用空指针 | 存在（Inspector 有判空，SceneView 的 Play 相机路径无判空） |
| 11 | Input 滚轮注册依赖首帧上下文（多窗口脆弱） | 存在（当前单窗口无影响） |
| 12 | Gizmo 用前景 drawlist，与面板重叠时会画到面板之上 | **已修复**：改用窗口 drawlist（连带修复了悬停判定恒假、手柄无法拖拽的 bug） |

### 9.2 阅读代码发现的缺口（KNOWN-ISSUES.md「新增缺口」+ 后续状态）

1. **Ctrl+D 复制是浅拷贝（不含子树）**：`serializeGameObject` 只输出对象自身节点（无 children 数组），有子物体的对象复制后子物体不会跟随。Delete 含子树、Duplicate 不含，两者语义不对称。
2. **Play 中 Inspector 仅禁用 Add/Remove Component 按钮**：顶部的 Name、Active 输入框、Mesh 路径、Albedo Map 路径在 Play 中仍可编辑（Transform/材质/灯光/相机参数是设计上的 live edit，但改名、改网格属于结构语义变更，未按「Play 禁结构修改」的原则禁用）。Hierarchy 的 F2 同理（即 KNOWN-ISSUES #8）。
3. ~~sceneDirty 只写不读~~ **已修复（V0.2-2）**：标题栏 `*` 标记 + New/Open/双击加载/关闭窗口/File→Exit 五处未保存确认（§6）。
4. ~~Play 中 live edit 会置脏标记，Stop 后标记残留~~ **已修复（V0.2-2）**：进入 Play 记录脏状态，退出 Play 还原（§6.3）。
5. **Add Component 不检查同类型重复**：可以给同一对象挂两个 Camera / MeshRenderer / 同名脚本（规格注释写了 assert，实际代码没有）。`getComponent` 只返回第一个，Inspector 也只编辑第一个，重复组件会成为「看不见的僵尸组件」。
6. **脚本类名与内置组件名冲突**：若脚本注册名恰好叫 Camera / Transform / MeshRenderer / DirectionalLight，序列化会按内置组件处理，反序列化时类型错乱。
7. **外部拖放贴图到 SceneView 在 Play 中未被禁止**：Play 中外部拖模型到视口会被拒绝，但拖贴图仍会拷贝进 `assets/textures/`（内部拖贴图到视口则被拒绝，两条路径行为不一致）。
8. **Pause 跳过整个 `scene->update()`**（规格写的是「dt=0」，实现是整帧跳过，含销毁队列 flush）；因 Play 禁结构修改，实际无感知差异。
9. **场景环境参数无编辑器 UI** → **部分修复（V0.2-7）**：天空 6 字段已有 Inspector Environment 区（§3.3）；skyColor / ambientColor / showGrid 仍只能手改 .nova.json 后重新加载。
10. **Play 中脚本 C++ 运行时成员不随快照恢复**（如 OrbitCamera 的当前环绕角）：这是设计取舍（规格 §7.2 已说明），但 KNOWN-ISSUES.md 未列出，特此记录。
11. **拾取用世界 AABB 粗测**：大平面（如 10x10m Plane）的包围盒会遮挡其后方物体，点击穿不过去（规格 §6.1 已知，暂不做三角形级精确拾取）。
12. **Hierarchy 的 Ctrl+单击是 toggle（会减选）**，SceneView 的 Ctrl+单击未命中时保留已有选择——两处 Ctrl 语义略有差异，按上述描述理解即可。

### 9.3 V0.2 第一批审查遗留 P2（来自 KNOWN-ISSUES.md，全部待办 backlog）

| # | 问题 | 说明 |
|---|---|---|
| 1 | 同帧按下-拖动-释放的极短拖拽不入 Undo 栈 | 「编辑结束」判定（IsItemDeactivatedAfterEdit）需跨帧才能命中；罕见——值已变但撤销覆盖不到 |
| 2 | 同帧多次 markSceneDirty 推重复快照 | undo 一次可能「看起来没变」（恢复的是相同状态） |
| 3 | undo 回基线后标题仍带 * | 无条件置脏；Unity 同行为 |
| 4 | 编辑器相机不入 Undo（按设计） | undo 后**同帧** Ctrl+S 会保存快照里的旧相机值；下一帧起会被当前相机回写覆盖 |
| 5 | Stats 帧时间不含 swapBuffers | vsync 等待前结算 → vsync 下 FPS 虚高（仅 CPU 侧口径，§3.6） |
| 6 | Hierarchy Duplicate 失败（nullptr）仍置脏并推快照 | 产生多余 undo 步 |
| 7 | ShadowCameraParams.lightDirection 赋值后未使用 | 死字段，无功能影响 |
| 8 | Inspector 无选中时的 Environment 区是 V0.2-5 环境面板的临时替身 | 该面板落地时吸收迁移 |

### 9.4 规格中有但代码尚未实现（待实现）

| 功能 | 规格出处 | 说明 |
|---|---|---|
| 内置组件（MeshRenderer/Camera/Light）的 enabled 勾选 | ARCHITECTURE §6.5 | 代码只有脚本组件有 Enabled 勾选 |
| Component::enabled 提升到基类 | KNOWN-ISSUES 改进候选 | 同上配套 |
| 场景环境剩余字段（skyColor/ambientColor/showGrid）编辑器 UI | Scene 已有字段 | 天空 6 字段已有 Environment 区（§3.3）；这三项仍仅 JSON 可改 |
| 独立环境面板（V0.2-5） | KNOWN-ISSUES V0.2 遗留 P2-8 | 当前 Inspector Environment 区为临时替身，落地时迁移 |
| ImGui ViewportsEnable（窗口拖出主窗口） | ARCHITECTURE §11-2 | 明确推迟，当前关闭 |
| 资产 mtime 自动重载 | ARCHITECTURE §6.4 | 推迟，用 View → Reload Assets 手动刷新 |
| 磁盘二进制资产缓存（.novaasset） | ARCHITECTURE §6.4 | 推迟，当前纯内存缓存 |
| 三角形级精确拾取 | ARCHITECTURE §6.1 | AABB 拾取有遮挡误判（§9.2-11） |
| AssetDatabase::loadModel 返回材质信息 | KNOWN-ISSUES 改进候选 | 当前导入双解析（§9.1-6） |
| GameObject::removeComponent(Component*) 按指针移除 | KNOWN-ISSUES 改进候选 | 当前移除脚本走整场景重建（§9.1-7） |
| drawGrid 颜色参数化 | KNOWN-ISSUES 改进候选 | 当前雾色硬编码（§9.1-1） |

---

## 10. 架构速览（给后续开发定位文件）

### 10.1 引擎模块（engine/，静态库 NovaEngine）

| 模块 | 目录 | 一句话职责 |
|---|---|---|
| **Core** | `engine/include/NovaEngine/Core` + `engine/src/Core` | Window（GLFW 封装/拖放回调）、Input（键鼠边沿状态）、Time（deltaTime）、Logger（分级日志 + sink）、UUID（64 位随机 ID）、**Stats（帧时间均值/尖峰/120 帧历史/分段计时，V0.2-11）** |
| **Render** | `.../Render` | Renderer（渲染队列/UBO/全局 GL 状态/**阴影 pass/统计计数器**）、Shader、Mesh/MeshData、Texture、Material、Framebuffer（MSAA 4x）、Light 数据、Primitives 图元工厂、**ShadowMap（深度 FBO + 纹素对齐正交投影，V0.2-6）**、**SkyboxRenderer（渐变/cubemap 全屏三角形，V0.2-7）**、**Cubemap（6 面/equirect 创建，内部 RGB16F）** |
| **Scene** | `.../Scene` | GameObject（父子层级/组件容器/销毁队列）、Component 基类、Transform（局部/世界+脏标记）、Scene（**+天空 6 字段**）、MeshRenderer、Camera、DirectionalLight（**+阴影 4 字段**） |
| **Assets** | `.../Assets` | AssetDatabase（相对路径→GPU 资产缓存，**+getCubemap cubemap 缓存**）、ModelLoader（Assimp 导入）、TextureLoader（stb 解码） |
| **Scripting** | `.../Scripting` | Behaviour 脚本基类、ScriptRegistry（名字→工厂 + NE_REGISTER_SCRIPT 宏） |
| **Serialization** | `.../Serialization` | SceneSerializer（场景 ↔ .nova.json 往返、ID 重映射、组件工厂分发、**写 version 2 / 读 1+2、v1 默认值兜底**） |

依赖方向严格单向：core ← render/scene ← assets/scripting/serialization；**render 不包含 scene 头**，渲染队列由编辑器遍历场景填充。

### 10.2 编辑器（editor/src/，可执行 NovaEditor）

| 文件 | 职责 |
|---|---|
| `main.cpp` | 入口：窗口 → Renderer::init → 切工作目录 → ImGui/字体 → 主循环 |
| `EditorApplication.h/.cpp` | 应用外壳：主循环、DockBuilder 布局、主菜单、**Play 状态机（快照/恢复）**、全局快捷键（**含 F8 / Undo-Redo**）、外部拖放分发、打开/保存场景、**未保存确认模态（V0.2-2）** |
| `EditorContext.h/.cpp` | 全局状态：选中集、Edit/Play 状态、场景指针、**脏标记漏斗（markDirtyOnly / markSceneDirty 推快照）**、快照、**Undo-Redo 栈**、面板区域上报 |
| `UndoRedoManager.h/.cpp` | 快照式 Undo/Redo：64 步 JSON 字符串环形栈，undo/redo/clear/canUndo（V0.2-1） |
| `EditorCamera.h/.cpp` | 轨道相机：右键旋转/WASD 与中键平移/滚轮缩放/F 聚焦/屏幕射线 |
| `GizmoController.h/.cpp` | ImGuizmo 封装：W/E/R 操作、Local/World、列主序矩阵直接传递与 decompose 写回、**拖拽结束推 Undo 快照** |
| `DragDrop.h/.cpp` | payload 常量、文件类型判断、copyIntoAssets、importModelToScene、duplicateGameObject |
| `Panel.h` | 面板抽象基类 |
| `panels/SceneViewPanel.cpp` | FBO 渲染 + **天空/阴影组装** + 拾取 + gizmo + 聚焦 + 拖放接受（最复杂的文件，约 340 行） |
| `panels/HierarchyPanel.cpp` | 层级树、增删改、重命名、重父子、右键菜单、Del/Ctrl+D |
| `panels/InspectorPanel.cpp` | 组件参数编辑、Add Component、脚本伪反射字段编辑、**无选中时的 Environment 天空区（V0.2-7）** |
| `panels/ConsolePanel.cpp` | Logger sink、四色过滤、500 条环形缓冲 |
| `panels/AssetBrowserPanel.cpp` | assets/ 浏览、面包屑、双击加载场景、拖拽源 |
| `panels/StatsPanel.cpp` | F8 统计面板：FPS/三段帧时间/渲染计数器/120 帧直方图（V0.2-11） |

### 10.3 其他目录

| 目录 | 内容 |
|---|---|
| `samples/` | 示例脚本 OBJECT 库 NovaScripts：`SpinningCube.h/.cpp`、`OrbitCamera.h/.cpp`（.obj 直接并入 NovaEditor，保证静态注册不被链接器裁剪） |
| `assets/shaders/` | 12 个 GLSL：blinn_phong / grid / outline / shadow / skybox / equirect_to_cube（各 .vert/.frag；blinn_phong 与 grid 含阴影采样，skybox 单着色器双分支渐变/cubemap） |
| `assets/models/` | 手写测试 OBJ：cube.obj / sphere.obj / plane.obj |
| `assets/scenes/` | demo.nova.json（默认示例场景，**文件仍为 v1 格式**：天空/阴影按默认值兜底加载，Ctrl+S 后升级为 v2） |
| `assets/textures/` | 用户拖入的贴图（导入时自动拷贝至此；demo 未用贴图） |
| `cmake/` | Dependencies.cmake（FetchContent 全部依赖）、Common.cmake（nova_configure_target 编译选项） |

---

*本文档由文档 Agent 基于 editor/src 与 engine/src 的实际实现撰写。发现文档与代码不符时，以代码为准并欢迎更新本文件。*
