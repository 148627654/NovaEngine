# NovaEngine 用户指南：已实现功能与操作方法

> 本文档以**实际代码**为准（快照时间 2026-11-15，收官批次后：V0.2 第一批 + 收官批次 Wave 1/Wave 2 全部并入），面向首次使用的用户回答三个问题：**哪些功能已实现、每个功能怎么用、有哪些已知限制**。
> 规格说明见 [ARCHITECTURE.md](ARCHITECTURE.md)、[ARCHITECTURE-V0.2.md](ARCHITECTURE-V0.2.md) 与 [ARCHITECTURE-FINAL.md](ARCHITECTURE-FINAL.md)（收官批次设计摘要：透明渲染 / 环境面板 / 多选 / 吸附 / 游戏分辨率 / Play 增强 / .novameta / 二进制缓存 / 打包 exe）。已确认的问题清单见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。规格文档里写了但代码尚未实现的内容统一收在 [§9.4 待实现](#94-规格中有但代码尚未实现待实现)，不会混入「已实现」章节。

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

Release 用 `cmake --build --preset release`。首次配置中 Assimp 编译耗时最长（已裁剪到 OBJ/FBX/glTF/Collada 四个导入器），预计数分钟，属正常现象。**NovaPlayer**（打包播放器）是独立 target：同一解决方案编译即产出 `build\player\Debug\NovaPlayer.exe`，用 File → Build Player... 打包前必须先构建它（打包时会预检并提示，§7.4）。

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

首帧由 DockBuilder 摆好面板布局（左 Hierarchy / 右 Inspector+Environment 并排 Tab / 底部 Console+Assets+Stats / 中央 Scene），之后可自由拖动 dock 标签重排（窗口不能拖出主窗口——多视口功能未启用）：

```
┌─────────────────────────────────────────────────────┐
│ 菜单栏：File | Edit | View                            │
├────────────┬───────────────────────────┬─────────────┤
│ Hierarchy  │         Scene（3D 视口）   │ Inspector   │
│（左 20%）   │                           │ + Environment│
│            │                           │ （右 22%）   │
├────────────┴───────────────────────────┴─────────────┤
│ 底部 Tab：Console | Assets | Stats（共 28% 高）        │
└─────────────────────────────────────────────────────┘
```

- **Hierarchy（层级）**：场景对象树。创建/选中/重命名/重父子/删除/复制都从这里发起（§3.1）。
- **Scene（场景视口）**：3D 画面（离屏 MSAA 4x 渲染到 FBO 再贴到面板上）。编辑器相机操作、点击拾取、gizmo 变换（含多选/吸附）、拖放导入、天空与阴影渲染都在这里（§3.2）；Play 态顶部还有游戏分辨率工具栏（§4.5）。
- **Inspector（检查器）**：编辑当前选中对象的组件参数（Transform/材质/相机/灯光/脚本）；多选时批量编辑公共值（§3.3.2）；无选中时编辑 gizmo 吸附参数（§3.3.3）。
- **Environment（环境）**：dockRight 与 Inspector 并排 Tab 的独立面板，编辑场景环境（天空色/环境光/网格开关/雾色）与天空全组（§3.8）。
- **Console（控制台）**：引擎日志输出，四色分级、可过滤（§3.4）。
- **Assets（资源浏览器）**：浏览 `assets/` 目录，加载场景、拖拽资产、贴图右键编辑导入设置（§3.5）。
- **Stats（统计）**：性能面板（FPS/帧时间/渲染计数器/直方图），**默认关闭，F8 开关**（§3.6）。

**主菜单栏**：

- **File**：New Scene（Ctrl+N）、Open Scene...（Ctrl+O）、Save Scene（Ctrl+S）、Save Scene As...（无快捷键）、**Build Player...**（打包独立 exe，§7.4）、**Exit**。新建/打开/保存/另存为/打包五项在 Play 模式下全部禁用（灰显）；Exit 始终可用（Play 中会先退出 Play 还原，再走未保存确认流程，§6.2）。
- **Edit**：**Undo（Ctrl+Z）/ Redo（Ctrl+Y）**（Edit 态且 gizmo 未拖拽时可用，§5）、Play / Stop（Ctrl+P，同一菜单项随状态切换文案）、Pause（勾选式开关，仅 Play 中可用）、**Step（F10）**（仅 Play+Paused 可用，推进一帧，§4.1）。
- **View**：Reload Assets（清空资产缓存，外部修改模型/贴图文件后点它刷新）；下方列出**七个**面板的显示开关（含 Stats 与 Environment）。

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

**单击选中**：点击对象行选中。点行首箭头只展开/折叠子树、不改变选择；**Ctrl+单击为加选/减选（toggle）**；**点击空白处取消全部选择（Ctrl 按住时点击空白保留选择**，与 SceneView 拾取语义统一）。

**F2 重命名**（仅恰好单选一个对象时生效）：F2 进入编辑，**Enter 或点击别处确认**，**Esc 取消**，留空不生效。Play 模式下禁用（与 Inspector 的 Name/Active 一致）。

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

**Gizmo 变换**（仅 Edit 模式；单选与多选都会出现）：

1. **W** 切换移动 / **E** 切换旋转 / **R** 切换缩放；
2. **X** 在 **Local / World** 坐标空间之间切换（有父级的对象 Local 模式下写回的是局部 TRS）；
3. 直接**拖拽箭头/圆环/方块手柄**即可变换，拖拽期间相机与点击选择被抑制；
4. 修改实时写回 Inspector 的数值框。**拖拽结束瞬间**整体算一步 Undo（§5.3）。
5. **吸附（snap）**：按住 **Ctrl** 拖拽手柄即临时启用吸附（左右 Ctrl 均可）；勾选 Inspector 无选中时的 Snap Settings → Enable Snap 后吸附**常开**（§3.3.3）。默认步长：平移 0.5m / 旋转 15° / 缩放 0.5，可在 Snap Settings 里自定义。吸附走 ImGuizmo 原生 snap。
6. **多选 gizmo**：选中 2 个及以上对象时，gizmo 出现在**共同 pivot**（全部选中对象世界 AABB 的并集中心；全部无 MeshRenderer 时为世界位置平均），拖拽以 `delta = 新矩阵 × 逆(初始矩阵)` 整体作用于全部对象——对象间相对位置保持不变；父/子同选时子对象只叠加一次 delta（不会双重叠加）。Local 模式以第一个选中对象的旋转为参考。**一次拖拽整体算一步 Undo**（§3.3.2、§5.3）。注意：多选下的**非均匀缩放**会引入剪切变形（shear，gizmo 写回时丢弃 skew 分量），这是 v1 已知限制（§9.1）。

**天空渲染（V0.2）**：视口先画天空再画场景（Edit/Play 两态一致）。默认 **Gradient 渐变天空**（horizon → zenith 两色渐变）；把 Environment 面板的 Sky Mode 切到 Cubemap 并填好 Cubemap Path 后，改为采样 cubemap（支持 6 面目录或 equirect 全景文件，加载失败自动回退渐变）。`scene.skyColor` 仍保留为**兜底清屏色**（天空绘制失败时可见），不再决定日常看到的天空颜色（§3.8）。

**透明渲染**：材质 Blend Mode 为 AlphaBlend 的物体进入**透明队列**——所有物体先按 opaque/transparent 分段，透明段按**视空间 AABB 中心深度后→前**排序绘制（画家算法近似，§9.1-13），开启标准 alpha 混合（`GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`）、**不写深度**（深度照常测试，仍被不透明物体正确遮挡）；透明物体**不进阴影深度 pass**（不投射阴影）。

**阴影（V0.2）**：场景第一个 DirectionalLight 的 **castShadows 默认开启**，场景物体会在网格地面投出实时阴影（PCF 3x3 软化；网格地面同样接收阴影，全影压到约 35% 而非全黑）。阴影正交盒沿光轴覆盖**过原点平面 ±shadowRange**（默认 60m），盒中心跟随渲染相机沿光轴投影（固定范围 + 跟随相机，不做场景包围盒）。**相机走远后，离相机超过 shadowRange 的盒外几何不再受影——这是预期行为，不是 bug**。castShadows 关闭即无阴影；Shadow Map Size 修改当帧生效（懒重建深度贴图）。

**接受内部拖放（从 Assets 面板拖来）**：把模型文件拖到视口任意位置 → 自动导入：射线与 y=0 地面求交点作为落点（无交点则落在相机前方 3m），生成「根对象（文件名）+ 每个子网格一个子对象」的层级，导入完成后**自动选中并聚焦**新物体。贴图文件拖入视口会被拒绝（提示「仅支持将模型文件拖入场景视口」）。

**Play 模式**：视口切换为场景相机画面，编辑器相机输入、拾取、F 聚焦、gizmo 全部停用（§4）。顶部出现**游戏分辨率工具栏**（§4.5），画面按目标分辨率渲染并**等比 letterbox 居中**；勾选 Follow Selected 后主相机跟随选中对象（§4.6）。

### 3.3 Inspector

单选对象时显示完整参数；多选时进入**批量编辑**（§3.3.2）；无选中时显示 **Snap Settings**（§3.3.3）。

**通用**：顶部 **Name** 文本框与 **Active** 勾选框（Active=false 时对象及其子树不渲染、脚本不更新）。Play 模式下 Name/Active 输入禁用。

**Transform**：Position / Rotation / Scale 三个 DragFloat3。**Rotation 用欧拉角（度）编辑**，内部存储四元数（保证序列化往返）。Scale 最小值 0.001。

**Mesh Renderer**：

- **Mesh**：网格资产路径，**回车生效**。可填 `builtin:cube / builtin:sphere / builtin:plane` 或项目内相对路径（如 `assets/models/sphere.obj`）。
- **Albedo**：漫反射颜色（ColorEdit3）。
- **Roughness / Metallic**：0–1 滑块。
- **Alpha / Blend Mode（透明参数，V0.2-8）**：Alpha 为 0–1 混合系数；Blend Mode 下拉选择 **Opaque**（默认，走不透明队列）或 **AlphaBlend**（进入透明队列：后→前排序、混合绘制、不写深度、无阴影，§3.2）。注意：alpha < 1 但 Blend Mode 仍为 Opaque 时**不产生混合**（引擎强制按 1 写入，防止 FBO alpha 通道出现幽灵半透明）——想透明必须切 Blend Mode。
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

#### 3.3.2 多选批量编辑（V0.2-15）

选中 2 个及以上对象时，Inspector 显示「N objects selected」+ 批量编辑区：

- **Active**：全部激活显示勾选；全部非激活显示未勾选；混合状态显示未勾选 + 灰字 "(mixed)"（点击勾选 = 全部激活）。
- **Transform**：Position / Rotation（欧拉角，度）/ Scale **逐轴**编辑——所有对象该轴值相同时显示公共值，拖拽即把新值写进**全部对象**；值不相同时灰字显示 **"(…)"** 标记，从 0 开始拖动会把绝对值写入全部对象。**一次拖拽整体算一步 Undo**（拖拽中间帧只置脏不推快照）。
- **Mesh Renderer (Material)**：全部选中对象都有 MeshRenderer 时显示公共材质区（以第一个对象的材质为编辑缓冲，Albedo/Roughness/Metallic/Alpha/Blend Mode 逐字段应用到全部）；仅部分对象有 MeshRenderer 时灰字显示「x/y have Mesh Renderer」。
- 范围说明：批量编辑只覆盖 Active / Transform / 材质；相机、灯光、脚本字段**不支持**多选批量编辑（选中多对象时不可见，属于设计范围外，见 §9.2-13）。
- 与 gizmo 的配合：多选 gizmo 在视口整体变换（§3.2），Inspector 逐轴改精确值，两者互补。

#### 3.3.3 无选中时的 Snap Settings（V0.2-12）

选中集为空时 Inspector 显示 **Snap Settings** 折叠区（运行期参数，**不序列化**、不置脏、不入 Undo）：

- **Enable Snap**：勾选后 gizmo 吸附**常开**；
- **Position**：平移吸附步长（米，逐轴，默认 0.5）；
- **Rotation (deg)**：旋转吸附步长（度，默认 15）；
- **Scale**：缩放吸附步长（默认 0.5）。

不勾选 Enable Snap 时，**拖拽中按住 Ctrl** 同样临时吸附（左右 Ctrl 均可，§3.2）。场景环境参数已不在 Inspector——请用独立 **Environment 面板**（§3.8）。

### 3.4 Console

- **四级分色**：Debug 灰 / Info 白 / Warn 黄 / Error 红。
- 顶部四个级别**过滤勾选框**（Debug/Info/Warn/Error），可单独开关某一级。
- **Auto Scroll**：有新日志自动滚到底（默认开）。
- **Clear**：清空面板。
- 容量上限 **500 条**（环形缓冲，超出丢最旧的）。
- 同一份日志同时写入控制台窗口（stdout）与 OutputDebugString（VS 调试输出可见）。

### 3.5 Asset Browser（Assets）

- 只浏览项目 `assets/` 目录；顶部**面包屑**导航（点击任意一级跳转），列表上方 `[ .. ]` 返回上级。
- 条目前缀标记类型：`[D]` 目录、`[M]` 模型、`[T]` 贴图、`[S]` 场景、`[F]` 其他文件；悬停显示相对路径。隐藏点开头条目（`.novacache` 等）与贴图的 `.novameta` 伴随文件，不参与浏览。
- **双击目录**进入；**双击 .nova.json 场景文件**直接加载该场景（Play 中不可，会提示；Edit 态场景未保存时先弹「未保存更改」确认，§6）。
- **模型 / 贴图文件是拖拽源**：按住拖到 SceneView 即导入/引用（§3.2）。双击模型/贴图文件本身无操作。
- **贴图右键菜单 → Texture Import Settings...**：打开贴图导入设置模态（§7.3）。
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

### 3.8 Environment 面板（独立环境面板，V0.2-5）

停靠在 dockRight、与 Inspector 并排 Tab（也可从 View 菜单开关、自由拖动 dock 标签）。**两个折叠区**：

**Environment 折叠区（基础环境）**：

- **Sky Color**：兜底清屏色（天空绘制失败时可见，§3.2）；
- **Ambient Color**：环境光；
- **Show Grid**：网格地面开关；
- **Fog Color**：网格地面远处雾色（与网格线颜色区分）；
- **Reset Fog to Sky**：一键把雾色重置为天空色（雾色缺省语义 = 随 skyColor 联动，改了天空色后点它即可恢复联动值）。

**Sky 折叠区（天空全组，原 Inspector 替身区迁移）**：

- **Sky Mode**：Gradient / Cubemap 下拉；
- **Horizon Color / Zenith Color**：渐变天空两色；
- **Cubemap Path**：cubemap 资产路径（6 面目录或 equirect 文件），**回车生效**；清空 = 回退渐变；路径无效/加载失败在 Console 打警告并回退渐变；
- **Sky Rotation**：-360–360°（cubemap 绕 +Y 旋转，度）；
- **Sky Exposure**：0–10（HDR 曝光）。

环境参数全部随场景序列化（`environment` 段，§7.2），修改走与 Inspector 相同的脏标记漏斗（拖拽中间帧只置脏，编辑结束推一步 Undo）。

---

## 4. Play 模式

### 4.1 进入 / 退出 / 暂停 / 单步

- **Ctrl+P** 或菜单 Edit → Play 进入；再按一次（菜单变 Stop）**退出**。
- Play 中可用 **Edit → Pause** 暂停（勾选式）。暂停时 `scene->update()` 被整体跳过——所有脚本的 onUpdate 停摆，画面静止。
- **Step 单步（F10 / Edit → Step）**：仅 Play **且 Paused** 时可用。按一次推进**一帧**（`scene->update(dt)` 执行一次，dt = Time::deltaTime），画面前进一格后继续暂停。F10 与菜单项等价；Edit 态或未暂停时无效。

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

**注意**：结构修改守卫已补全——Hierarchy 的 F2 重命名、Inspector 的 Name/Active/Mesh 路径/贴图路径输入框在 Play 态全部禁用（收官批次修复，原为已知问题 §9.2-2）。

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

### 4.5 游戏分辨率与 Render Scale（V0.2-13）

进入 Play 后 SceneView 顶部出现工具栏（Edit 态不显示）：

- **Resolution 预设**：**Free**（默认，跟随面板尺寸）/ **1920x1080** / **1280x720** / **Custom**（自定义，选中后出现 W/H 输入框，64–8192）。
- **Render Scale**：0.5–2.0 滑块（步长 0.1）。FBO 实际渲染尺寸 = 目标分辨率 × Render Scale，与 MSAA 正交（可叠加）。
- 固定分辨率时：渲染相机 aspect 用**目标分辨率**宽高比；画面按面板可用区域**等比缩放（fit）居中显示**，四周留黑（letterbox）。
- 安全钳制：渲染宽高每维 clamp 到 [16, 4096]（Custom 8192 × Render Scale 2.0 也不会因显存炸裂而崩溃）；FBO 重建失败当帧跳过渲染、下帧自动重试。
- 注意：Render Scale 2.0 + MSAA 4x 时 FBO 显存占用约 130MB（4096² 上限时），属预期开销。分辨率与 Render Scale 是运行期设置，不随场景保存。

### 4.6 Play 增强（V0.3-10 子集）

- **Follow Selected（跟随选中）**：Play 工具栏勾选后，**主相机**（isMain 优先，§4.3 同规则）每帧渲染前移动到「选中对象世界位置 + 记录偏移」——勾选瞬间记录当前 相机位置 − 选中对象位置 的偏移并保持（朝向不变），此后选中谁跟谁。未勾选/退出 Play 时偏移重置。实现为直接写场景相机 Transform，**退出 Play 由快照自动还原**，不留脏。无选中对象时保持原地。
- **进入 Play 未保存提示（toast）**：场景有未保存修改时按 Ctrl+P 进入 Play，视口顶部显示 **3 秒**非阻塞提示「Scene has unsaved changes (Play snapshot will restore)」，同时 Console 打警告。提示自动消失，不打断任何输入（退出 Play 时快照照常还原，§4.2/§6.3）。

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

- 防刷栈：拖拽 / 输入**中间帧只置脏**（不推快照），「编辑结束」（松开鼠标 / 回车 / 勾选等瞬时操作）才推快照——一次拖拽整体 = 一步撤销（**多选 gizmo 拖拽与多选 Inspector 逐轴拖拽同样整体单步**）。
- **不入栈**：选中集、编辑器相机、面板开关与布局、进入 / 退出 Play、**运行期设置（Snap Settings、游戏分辨率/Render Scale、Follow Selected 开关）**。
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
  "version": 3,                       // 版本号（保存写 3；读取兼容 1/2/3）
  "name": "DemoScene",                // 场景名
  "environment": {                    // 环境参数
    "skyColor": [0.18, 0.22, 0.35],   // 兜底清屏色（天空绘制失败时可见）
    "ambientColor": [0.15, 0.15, 0.18],
    "showGrid": true,                 // 网格地面开关
    "fogColor": [0.18, 0.22, 0.35],   // ---- V3：网格雾色（旧文件缺省随 skyColor 联动）----
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
- MeshRenderer 的材质写在 `material` 对象里：`albedo`（颜色）/ `roughness` / `metallic` / `albedoMap`（贴图相对路径，空串 = 纯色）/ **`alpha`（0–1 混合系数）** / **`blendMode`（"Opaque" / "AlphaBlend"，v3 新增）**。旧文件（v1/v2）无这两字段 → 按默认值兜底（alpha=1 / Opaque）。
- 脚本组件 = `type` 为脚本注册名 + 公共字段 `enabled` + 脚本自定义字段（如 `"speed": 30`）。
- **向后兼容**：v1/v2 旧文件没有天空/阴影/雾色/透明字段，加载时**全部按默认值兜底**——渐变天空、空 cubemap、曝光 1、fogColor=skyColor（联动语义）；DirectionalLight 的 castShadows=true / shadowBias=0.0005 / shadowRange=60 / shadowMapSize=2048；材质 alpha=1 / blendMode=Opaque，开箱即用。旧文件不自动升级，直到下次 Ctrl+S（写 v3）。
- `skyMode` / `blendMode` 存字符串；未知字符串 → 警告并回退（Gradient / Opaque）。version 非 1/2/3 时告警但仍尝试加载。
- 容错：未知组件类型 → 警告并跳过该组件（不崩溃）；JSON 非法 → 报错且**场景保持不变**；字段类型错 → 回退默认值。
- **编辑器相机位置随场景保存**：Edit 模式下每帧把相机状态回写到场景字段，Ctrl+S 即持久化；打开场景时恢复视角。
- Undo/Redo 的内部快照是同一格式的**紧凑单行 JSON**（不落盘，仅内存）。

### 7.3 贴图导入设置（.novameta）与二进制缓存（.novacache）

**中文路径全面支持**：所有资产读取（shader / 模型 / 贴图 / cubemap / 场景 / meta）统一经 **AssetStream**——磁盘走 `std::ifstream` 的 path 重载、解码走 `stbi_load_from_memory`，零编码转换；项目路径、资产路径、保存对话框含中文字符均可正常工作（旧版贴图中文路径加载失败的问题已修复，§9.1-4）。

**贴图导入设置（V0.3-5）**：

- 每张贴图可以有伴随文件 `<贴图名>.<扩展名>.novameta`（JSON：`{"version":1,"type":"sRGB"|"Linear","wrap":"Repeat"|"Clamp","filter":"Bilinear"|"Trilinear","generateMipmaps":bool}`），与贴图同目录。
- **无 meta 自动生成**：编辑器首次加载贴图时自动写默认 meta（sRGB / Repeat / Trilinear / 生成 mipmap）落盘。meta 损坏/字段非法 → 警告 + 回退默认值，不崩溃、不覆盖。
- **编辑入口**：Asset Browser 中**右键贴图 → Texture Import Settings...**，弹出模态：
  - **Color Space**：sRGB / Linear（决定贴图 GPU 内部格式；NormalMap 语义上等同 Linear——注意 UI 不提供 NormalMap 选项，meta 里手写的 NormalMap 经模态 Save 会改写为 Linear，§9.1-16）；
  - **Wrap Mode**：Repeat / Clamp To Edge；**Filter Mode**：Bilinear / Trilinear；**Generate Mipmaps** 勾选；
  - **Save** 落盘并**立即清空资产缓存**（贴图按新设置重导，当帧生效）；**Reset to Default** 恢复默认值（不落盘，需再点 Save）；**Close** 放弃修改。
- meta 参与**二进制缓存键**：改 meta 即缓存失效重导（§7.3 下文）。

**资产二进制缓存（V0.3-7）**：

- 目录 `assets/.novacache/`（首次导入时懒创建；Asset Browser 不显示）。模型与贴图首次导入/解码后按 **FNV-1a 64 内容哈希**落盘二进制缓存（贴图键 = 源字节 + meta 文本，改 meta 即失效），**第二次启动**时直接读缓存跳过 Assimp/解码，启动提速。
- 格式自带校验（magic "NVBI" + version + payloadSize + 载荷尺寸），**损坏即删即重导**，不会加载到坏数据；**删除整个目录可安全重建**（下次导入自动重写）；目录超过 1GB 打一次警告，防无限膨胀。
- 贴图缓存只存 mip0（GPU 侧 glGenerateMipmap 重建）；HDR equirect 单独走浮点缓存。
- 一个已知取舍：模型二进制缓存命中**必须**与内存材质旁表完整对应，否则回退 Assimp 重解析（新进程旁表为空 → 每次启动仍走一次 Assimp——正确性优先，§9.1-14）。

### 7.4 打包独立播放器（Build Player，V0.5-1）

**File → Build Player...**（Play 态禁用；无当前场景路径时提示先保存；场景脏时打包上次保存的版本并警告）：

1. 选择输出目录（Windows 原生目录选择对话框）；
2. **收集场景引用资源**：场景文件本身、每个 MeshRenderer 的 mesh（跳过 `builtin:` 内置图元）与 material.albedoMap（贴图 + 其 .novameta 伴随文件，.hdr 不生成/不收集 meta）、天空 cubemap（目录 → px/nx/py/ny/pz/nz 六面，文件 → equirect）、`assets/shaders/` 全部 .vert/.frag；
3. **缺失资源打包即报错**：逐条列出缺失项并失败，不产出半成品 pak；
4. 输出 `场景名.novapak`（v1 无压缩单文件包）+ 复制 `NovaPlayer.exe` 到同一目录——**发布目录 = exe + pak**，双击即可运行。

**NovaPlayer 运行参数**：

- `--scene <相对路径>`：要播放的场景（pak 内相对路径；默认 `assets/scenes/demo.nova.json`）；
- `--pak <路径>`：显式指定 .novapak；默认自动挂载 **exe 同目录第一个 \*.novapak**（没找到则走磁盘读取）；
- `--width / --height`：窗口尺寸（默认 1280x720，1–16384）；
- **Esc 退出**；播放器项目根 = exe 目录，资产经 pak 内存读取（pak 未命中回退磁盘）。无 ImGui，纯引擎渲染（天空/阴影/网格/脚本与编辑器 Play 同一条 `SceneRenderer::renderScene` 路径）。

---

## 8. 快捷键总表

> 全部来自代码（`EditorApplication::handleGlobalShortcuts`、`HierarchyPanel`、`SceneViewPanel`、`GizmoController`、`EditorCamera`）。输入框聚焦（如重命名中）时全局快捷键被抑制，防止误触；**未保存确认模态期间全部全局快捷键被抑制**（§6.2）。

| 快捷键 | 作用 | 生效条件 |
|---|---|---|
| **Ctrl+P** | 进入 Play / 退出 Play | 全局（输入框聚焦除外） |
| **Ctrl+S** | 保存场景（未命名时弹另存为） | Edit |
| **Ctrl+N** | 新建空场景 | Edit |
| **Ctrl+O** | 打开场景对话框 | Edit |
| **F8** | 开关 Stats 统计面板 | 全局（输入框聚焦除外） |
| **F10** | Step 单步推进一帧 | Play 且 Paused（菜单 Edit → Step 等效） |
| **Ctrl+Z** | 撤销上一步 | Edit，且 gizmo 未在拖拽 |
| **Ctrl+Y** | 重做 | Edit，且 gizmo 未在拖拽 |
| **Ctrl+Shift+Z** | 重做（等价 Ctrl+Y） | Edit，且 gizmo 未在拖拽 |
| **W** | gizmo 切换为移动 | Edit |
| **E** | gizmo 切换为旋转 | Edit |
| **R** | gizmo 切换为缩放 | Edit |
| **X** | gizmo 切换 Local / World 空间 | Edit |
| **F** | 聚焦选中对象（无选中聚焦原点） | Edit，鼠标悬停 SceneView |
| **F2** | 重命名（Enter 确认 / Esc 取消） | Edit 且 Hierarchy 恰好单选 |
| **Del** | 删除所有选中对象（含子树） | Edit |
| **Ctrl+D** | 复制选中对象（副本为兄弟节点） | Edit |
| **Ctrl+左键** | 加选/减选 | Hierarchy 行 / SceneView 拾取 |
| **Ctrl+左键（空白）** | 保留选择不取消（两面板语义统一） | Hierarchy 空白 / SceneView 未命中 |
| **Ctrl 按住拖拽 gizmo** | 临时开启吸附（左右 Ctrl 均可） | Edit，gizmo 拖拽中 |
| 右键拖拽 | 编辑器相机旋转 | Edit，SceneView |
| 中键拖拽 | 编辑器相机平移（抓取式） | Edit，SceneView |
| 滚轮 | 编辑器相机缩放 | Edit，SceneView |
| WASD | 编辑器相机平移（Shift 加速 3 倍） | Edit，SceneView |
| Esc | 取消重命名 | 重命名输入框内 |
| 左键单击 | SceneView 拾取选择（位移 >3px 视为相机拖拽） | Edit |

未实现的常规快捷键：**Ctrl+Shift+S 另存为**（只能走菜单）。

NovaPlayer 内快捷键：**Esc** 退出（其余无）。

---

## 9. 已知限制

### 9.1 KNOWN-ISSUES.md 已记录的（摘要 + 当前状态）

| # | 问题 | 现状 |
|---|---|---|
| 1 | 网格地面雾色硬编码，与场景 skyColor 不联动 | **已修复**：drawGrid 雾色参数化（scene.fogColor），Environment 面板可编辑 + Reset Fog to Sky 一键回落天空色 |
| 2 | Window 析构未调 glfwTerminate（单窗口无实际影响） | **已修复**：析构补 glfwTerminate 释放 GLFW 进程级资源 |
| 3 | 内置图元（builtin:cube/sphere/plane）不缓存，整场景重建时重传 GPU 网格 | **已修复**：故意泄漏的函数内 static 缓存（跳过静态析构防 GL 上下文时序问题） |
| 4 | TextureLoader 用 ANSI 路径，**中文路径贴图加载失败** | **已修复**：全部资产读取收敛 AssetStream（`ifstream` 的 path 重载 + `stbi_load_from_memory`），中文路径全面支持 |
| 5 | 「双击 exe 缺 shader/资产（依赖工作目录）」 | **已被代码修复**：main.cpp 用编译期注入的项目根（NOVA_PROJECT_ROOT）自动切换工作目录，双击运行即可用（NovaPlayer 进一步以 exe 目录为项目根 + pak 挂载，§7.4） |
| 6 | 模型首次导入双解析（网格走缓存 + 材质另解析一遍） | **已修复**：`AssetDatabase::importModel` 一次 Assimp 解析同时取回网格+材质+贴图路径；loadModel 兼容转发 |
| 7 | Duplicate / 移除脚本组件走整场景 JSON 重建（多选复制 O(n²)、脚本运行时状态清零） | 存在 |
| 8 | Hierarchy F2 重命名在 Play 态未禁用 | **已修复**：F2 仅 Edit 态生效（Inspector 的 Name/Active/Mesh 路径/贴图路径输入在 Play 态同样禁用） |
| 9 | 脚本类型名反查依赖 MSVC typeid().name() 格式 | 存在 |
| 10 | 畸形 JSON（对象无 Transform 组件）可能导致解引用空指针 | **已修复**：Gizmo 显示、Play 相机选择（pickPlayCamera）、SceneRenderer::collectCamera 三处均有无 Transform 防御（回退/跳过，不崩溃） |
| 11 | Input 滚轮注册依赖首帧上下文（多窗口脆弱） | 存在（当前单窗口无影响） |
| 12 | Gizmo 用前景 drawlist，与面板重叠时会画到面板之上 | **已修复**：改用窗口 drawlist（连带修复了悬停判定恒假、手柄无法拖拽的 bug） |
| 13 | 透明排序用 AABB 中心近似（非逐像素/逐三角形） | 存在（交错透明面可能排序错误，KNOWN-ISSUES Wave 1/2-9） |
| 14 | 模型二进制缓存命中需材质旁表完整 | 存在（新进程旁表为空 → Player 每次启动仍走一次 Assimp；正确性优先的取舍，KNOWN-ISSUES Wave 1/2-1） |
| 15 | pak 内无 meta 的贴图在 Player 首载时于 exe 旁生成默认 meta | 存在（只读介质下仅警告不影响功能，KNOWN-ISSUES Wave 1/2-2） |
| 16 | meta 模态 Save 把手写 "NormalMap" 重写为 "Linear" | 存在（语义等价但往返有损，KNOWN-ISSUES Wave 1/2-5） |
| 17 | 多选 gizmo 非均匀缩放引入 shear | 存在（v1 已知限制，KNOWN-ISSUES Wave 1/2-10） |
| 18 | Cubemap 六面 LDR 不消费 meta 的 colorSpace / LDR equirect 的 meta 为 pak 内死重 | 存在（P3，KNOWN-ISSUES Wave 1/2-3/4） |
| 19 | 二进制缓存读失败误删好缓存 / getTexture 未命中读两遍 / m_materialCache 无 releaseUnused | 存在（效率/健壮性 P3，KNOWN-ISSUES Wave 1/2-6/7/8） |

### 9.2 阅读代码发现的缺口（KNOWN-ISSUES.md「新增缺口」+ 后续状态）

1. **Ctrl+D 复制是浅拷贝（不含子树）**：`serializeGameObject` 只输出对象自身节点（无 children 数组），有子物体的对象复制后子物体不会跟随。Delete 含子树、Duplicate 不含，两者语义不对称。
2. ~~Play 中 Inspector 仅禁用 Add/Remove Component 按钮~~ **已修复（收官批次）**：Name、Active、Mesh 路径、Albedo Map 路径输入框在 Play 态全部禁用；Hierarchy F2 同理（KNOWN-ISSUES #8）。
3. ~~sceneDirty 只写不读~~ **已修复（V0.2-2）**：标题栏 `*` 标记 + New/Open/双击加载/关闭窗口/File→Exit 五处未保存确认（§6）。
4. ~~Play 中 live edit 会置脏标记，Stop 后标记残留~~ **已修复（V0.2-2）**：进入 Play 记录脏状态，退出 Play 还原（§6.3）。
5. **Add Component 不检查同类型重复**：可以给同一对象挂两个 Camera / MeshRenderer / 同名脚本（规格注释写了 assert，实际代码没有）。`getComponent` 只返回第一个，Inspector 也只编辑第一个，重复组件会成为「看不见的僵尸组件」。
6. **脚本类名与内置组件名冲突**：若脚本注册名恰好叫 Camera / Transform / MeshRenderer / DirectionalLight，序列化会按内置组件处理，反序列化时类型错乱。
7. **外部拖放贴图到 SceneView 在 Play 中未被禁止**：Play 中外部拖模型到视口会被拒绝，但拖贴图仍会拷贝进 `assets/textures/`（内部拖贴图到视口则被拒绝，两条路径行为不一致）。
8. **Pause 跳过整个 `scene->update()`**（规格写的是「dt=0」，实现是整帧跳过，含销毁队列 flush）；因 Play 禁结构修改，实际无感知差异。收官批次新增 **F10/Edit→Step 单步**（仅 Paused 时），一次推进一帧（§4.1）。
9. ~~场景环境参数无编辑器 UI~~ **已修复（V0.2-5）**：独立 Environment 面板覆盖 skyColor / ambientColor / showGrid / fogColor + 天空全组（§3.8）；Inspector 的临时替身区已删除。
10. **Play 中脚本 C++ 运行时成员不随快照恢复**（如 OrbitCamera 的当前环绕角）：这是设计取舍（规格 §7.2 已说明），但 KNOWN-ISSUES.md 未列出，特此记录。
11. **拾取用世界 AABB 粗测**：大平面（如 10x10m Plane）的包围盒会遮挡其后方物体，点击穿不过去（规格 §6.1 已知，暂不做三角形级精确拾取）。
12. ~~Hierarchy 与 SceneView 的 Ctrl 语义不一致~~ **已修复**：Hierarchy 点击空白处在 Ctrl 按住时保留选择，与 SceneView Ctrl+单击未命中语义统一。
13. **多选批量编辑范围有限**：批量编辑只覆盖 Active / Transform / 材质（§3.3.2）；相机、灯光、脚本字段在多选时不可见、不可批量编辑——设计范围外，如需批量调脚本字段请逐对象编辑。

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
| 8 | Inspector 无选中时的 Environment 区是 V0.2-5 环境面板的临时替身 | **已落地（V0.2-5）**：Environment 面板独立（§3.8），Inspector 替身区删除、无选中时改显 Snap Settings |

### 9.4 规格中有但代码尚未实现（待实现）

| 功能 | 规格出处 | 说明 |
|---|---|---|
| 内置组件（MeshRenderer/Camera/Light）的 enabled 勾选 | ARCHITECTURE §6.5 | 代码只有脚本组件有 Enabled 勾选 |
| Component::enabled 提升到基类 | KNOWN-ISSUES 改进候选 | 同上配套 |
| ImGui ViewportsEnable（窗口拖出主窗口） | ARCHITECTURE §11-2 | 明确推迟，当前关闭 |
| 资产 mtime 自动重载 | ARCHITECTURE §6.4 | 推迟，用 View → Reload Assets 手动刷新 |
| 三角形级精确拾取 | ARCHITECTURE §6.1 | AABB 拾取有遮挡误判（§9.2-11） |
| GameObject::removeComponent(Component*) 按指针移除 | KNOWN-ISSUES 改进候选 | 当前移除脚本走整场景重建（§9.1-7） |
| .novapak 流式读取（按需加载 entry，不整包进内存） | ARCHITECTURE-FINAL 裁定 §11 | v1 全量读入内存（面向 <100MB 包体） |
| Add Component 同类型去重 | KNOWN-ISSUES 新增缺口 5 | 当前可重复挂载同类组件（§9.2-5） |

---

## 10. 架构速览（给后续开发定位文件）

### 10.1 引擎模块（engine/，静态库 NovaEngine）

| 模块 | 目录 | 一句话职责 |
|---|---|---|
| **Core** | `engine/include/NovaEngine/Core` + `engine/src/Core` | Window（GLFW 封装/拖放回调/**析构 glfwTerminate**）、Input（键鼠边沿状态）、Time（deltaTime）、Logger（分级日志 + sink）、UUID（64 位随机 ID）、**Stats（帧时间均值/尖峰/120 帧历史/分段计时，V0.2-11）** |
| **Render** | `.../Render` | Renderer（渲染队列/UBO/全局 GL 状态/**阴影 pass/统计计数器/透明两段队列（opaque 稳定分段 + 透明后→前 + 深度遮罩，V0.2-8）**）、Shader、Mesh/MeshData、Texture（**+TextureWrap/TextureFilter 枚举**）、Material（**+alpha/blendMode**）、Framebuffer（MSAA 4x）、Light 数据、Primitives 图元工厂（**故意泄漏的 static 缓存**）、**ShadowMap（深度 FBO + 纹素对齐正交投影，V0.2-6）**、**SkyboxRenderer（渐变/cubemap 全屏三角形，V0.2-7）**、**Cubemap（6 面/equirect 创建，内部 RGB16F，LDR 面走二进制缓存）** |
| **Scene** | `.../Scene` | GameObject（父子层级/组件容器/销毁队列）、Component 基类、Transform（局部/世界+脏标记）、Scene（**+天空 6 字段 + fogColor**）、MeshRenderer、Camera、DirectionalLight（**+阴影 4 字段**）、**SceneRenderer（Play/Player 共用渲染序列，V0.5-1）** |
| **Assets** | `.../Assets` | AssetDatabase（相对路径→GPU 资产缓存，**+importModel 单解析网格/材质/贴图路径、+getCubemap、+二进制缓存接入**）、ModelLoader（Assimp 内存导入）、TextureLoader（stb 内存解码）、**AssetStream（磁盘/pak 统一读取，中文路径安全）**、**TextureMeta（.novameta 读写）**、**BinaryCache（.novacache，FNV-1a 键 + 48B Header 校验）**、**PakArchive（.novapak v1 无压缩打包/挂载）** |
| **Scripting** | `.../Scripting` | Behaviour 脚本基类、ScriptRegistry（名字→工厂 + NE_REGISTER_SCRIPT 宏） |
| **Serialization** | `.../Serialization` | SceneSerializer（场景 ↔ .nova.json 往返、ID 重映射、组件工厂分发、**写 version 3 / 读 1+2+3、旧版本默认值兜底（含 fogColor/alpha/blendMode）**） |

依赖方向严格单向：core ← render/scene ← assets/scripting/serialization；**render 不包含 scene 头**，渲染队列由编辑器遍历场景填充。

### 10.2 编辑器（editor/src/，可执行 NovaEditor）

| 文件 | 职责 |
|---|---|
| `main.cpp` | 入口：窗口 → Renderer::init → 切工作目录 → ImGui/字体 → 主循环 |
| `EditorApplication.h/.cpp` | 应用外壳：主循环、DockBuilder 布局（**+Environment dockRight Tab**）、主菜单（**+Build Player.../Step**）、**Play 状态机（快照/恢复/未保存 toast）**、全局快捷键（**含 F8 / F10 / Undo-Redo**）、外部拖放分发、打开/保存场景、**打包入口 buildPlayer**、**未保存确认模态（V0.2-2）** |
| `EditorContext.h/.cpp` | 全局状态：选中集、Edit/Play 状态、场景指针、**脏标记漏斗（markDirtyOnly / markSceneDirty 推快照）**、快照、**Undo-Redo 栈**、面板区域上报、**运行期设置：snapSettings / gameViewSettings / followSelected / stepRequested / playToastUntil** |
| `UndoRedoManager.h/.cpp` | 快照式 Undo/Redo：64 步 JSON 字符串环形栈，undo/redo/clear/canUndo（V0.2-1） |
| `EditorCamera.h/.cpp` | 轨道相机：右键旋转/WASD 与中键平移/滚轮缩放/F 聚焦/屏幕射线 |
| `GizmoController.h/.cpp` | ImGuizmo 封装：W/E/R 操作、Local/World、列主序矩阵直接传递与 decompose 写回、**多选 pivot + delta 批量变换（V0.2-15）**、**ImGuizmo 原生 snap + Ctrl 临时吸附（V0.2-12）**、**拖拽结束推 Undo 快照（单选/多选统一单步）** |
| `DragDrop.h/.cpp` | payload 常量、文件类型判断、copyIntoAssets、importModelToScene（**单解析 importModel**）、duplicateGameObject |
| `Packager.h/.cpp` | **Build Player（V0.5-1）**：场景资源收集（mesh/贴图+meta/cubemap/shaders）、缺失资源逐条报错、.novapak 打包、NovaPlayer.exe 预检与复制 |
| `Panel.h` | 面板抽象基类 |
| `panels/SceneViewPanel.cpp` | FBO 渲染 + **天空/阴影组装** + 拾取 + gizmo + 聚焦 + 拖放接受 + **Play 分辨率/Render Scale/letterbox（V0.2-13）** + **Follow Selected（V0.3-10 子集）** + **未保存 toast**（约 500 行，最复杂的面板） |
| `panels/HierarchyPanel.cpp` | 层级树、增删改、重命名（**F2 仅 Edit**）、重父子、右键菜单、Del/Ctrl+D、**Ctrl 点击空白保留选择** |
| `panels/InspectorPanel.cpp` | 组件参数编辑（**+材质 Alpha/Blend Mode**）、Add Component、脚本伪反射字段编辑、**多选批量编辑（逐轴公共值/"(…)" 标记/公共材质区，V0.2-15）**、**无选中时 Snap Settings（V0.2-12）** |
| `panels/EnvironmentPanel.cpp` | **独立环境面板（V0.2-5）**：skyColor/ambient/showGrid/fogColor + Reset Fog to Sky + 天空全组（Sky Mode/渐变两色/cubemap 路径/旋转/曝光） |
| `panels/ConsolePanel.cpp` | Logger sink、四色过滤、500 条环形缓冲 |
| `panels/AssetBrowserPanel.cpp` | assets/ 浏览（**隐藏 .novacache/.novameta**）、面包屑、双击加载场景、拖拽源、**贴图右键 → Texture Import Settings 模态（V0.3-5）** |
| `panels/StatsPanel.cpp` | F8 统计面板：FPS/三段帧时间/渲染计数器/120 帧直方图（V0.2-11） |

### 10.3 其他目录

| 目录 | 内容 |
|---|---|
| `player/` | **NovaPlayer（V0.5-1）**：独立播放器 target（NovaEngine + NovaScripts + `src/main.cpp` 约 160 行，无 ImGui）。--scene/--pak/--width/--height 参数、默认挂载 exe 旁 .novapak、Esc 退出 |
| `samples/` | 示例脚本 OBJECT 库 NovaScripts：`SpinningCube.h/.cpp`、`OrbitCamera.h/.cpp`（.obj 并入 NovaEditor 与 NovaPlayer，保证静态注册不被链接器裁剪） |
| `assets/shaders/` | 12 个 GLSL：blinn_phong（**+uAlpha 透明**）/ grid / outline / shadow / skybox / equirect_to_cube（各 .vert/.frag；blinn_phong 与 grid 含阴影采样，skybox 单着色器双分支渐变/cubemap） |
| `assets/models/` | 手写测试 OBJ：cube.obj / sphere.obj / plane.obj |
| `assets/scenes/` | demo.nova.json（默认示例场景，**文件仍为 v1 格式**：天空/阴影/雾色/透明按默认值兜底加载，Ctrl+S 后升级为 v3） |
| `assets/textures/` | 用户拖入的贴图（导入时自动拷贝至此；demo 未用贴图；首次加载自动生成 `.novameta` 伴随文件） |
| `assets/.novacache/` | **二进制资产缓存（V0.3-7）**：运行时自动生成，可整体删除，Asset Browser 不显示 |
| `cmake/` | Dependencies.cmake（FetchContent 全部依赖）、Common.cmake（nova_configure_target 编译选项） |

---

## 11. V0.3 波 1 新增：PBR 材质 + 材质库（2026-11-15）

### 11.1 PBR 材质升级
- 光照模型升级为 Cook-Torrance GGX + Fresnel-Schlick 金属工作流（能量守恒）
- Inspector 材质区新增：**Normal / Metallic / Roughness / Emission 四张贴图路径**（Enter 生效，空=清除）+ **Emission Color / Emission Intensity**
- 贴图约定：albedo 按 sRGB 解码；normal/metallic/roughness/emission 一律线性解码（法线贴图强制）
- **注意：PBR 迁移后画面整体变暗**（物理正确化，无补偿项）；demo 如需提亮可调 DirectionalLight intensity 或环境光

### 11.2 材质库 .novamat
- **创建**：Asset Browser 空白处右键 → Create → Material（输入名字，生成 `.novamat`，条目带 `[MA]` 前缀）
- **编辑**：双击 .novamat 打开模态编辑（Save 才落盘）；保存后场景内所有引用该材质的物体**当帧同步变色**
- **引用**：Inspector 材质区 Assign 输入 .novamat 路径（Enter 生效）→ 内联字段灰显（整块引用）；Detach 回内联
- 场景保存时引用态只写 `"materialAsset"` 路径键；跨场景复用（加载时经 AssetDatabase 解析）
- 缺失资产加载不崩溃（warn + 回退内联默认材质）

---

## 12. V0.3 波 2 新增：物理 / 音频 / 运行时 UI / 协程 / 事件（2026-11-15）

> 本波引入两个新第三方库：Jolt Physics v5.7.0（物理）、miniaudio 0.11.26（音频）。全部组件经 ComponentRegistry 注册（内置组件现共 15 个），Inspector Add Component 下拉自动出现。

### 12.1 物理（Jolt）

- **组件**：Rigidbody（mass/useGravity/isKinematic/线性与角度阻尼）+ BoxCollider/SphereCollider/CapsuleCollider/MeshCollider（isTrigger/center/形状参数）
- **挂载规则**：仅 Collider → 静态体（地面类）；Rigidbody（可无 Collider）→ 动态/kinematic 体；Collider 可挂同 GO 或子 GO（子 GO 相对 TRS 烘焙为附加形状）；isTrigger 只发事件不参与碰撞求解；MeshCollider 仅 static/kinematic
- **脚本 API**：`addForce/addImpulse/velocity()/setVelocity`（body 未创建时安全 no-op）；`scene->physics()->raycast(origin, dir, maxDist, outHit)` 返回 RaycastHit{hit,gameObject,point,normal,distance}
- **固定步进**：60Hz 累加器（每帧最多 3 步防死亡螺旋）；**动态刚体直改 Transform 会被物理覆盖**——用 addForce/setVelocity 驱动；kinematic 由脚本改 Transform 驱动
- **触发器 = 事件**：脚本 `subscribeEvent<TriggerEvent>`（TriggerEvent{entered, gameObjectA, gameObjectB}）帧末投递
- **Play 调试**：Play 工具栏「Show Colliders」开关绘制碰撞体绿色线框（Edit 态不显示）
- 退出 Play 物理状态随快照还原；进入 Play 自动 syncFromTransforms

### 12.2 3D 音频（miniaudio）

- **组件**：AudioSource（clipPath 支持 wav/mp3/flac/ogg、volume/loop/spatialBlend/minDistance/maxDistance/playOnAwake + Play()/Stop()）；AudioListener 无字段（挂主相机 GO）
- **空间化**：spatialBlend=0 纯 2D、=1 全 3D（min/maxDistance 线性衰减）、中间值双分量混合；位置每帧跟随 Transform
- **生命周期**：编辑器启动即初始化设备；进出 Play 自动 stopAll 防爆音；无音频设备时优雅降级（warn + Play() no-op）
- 资产经 AssetStream 加载（pak 兼容）；v1 全量解码 PCM f32（流式 BGM 列 V1.0-8）

### 12.3 运行时 UI（与编辑器 ImGui 完全分离，可打包进 Player）

- **组件**：Canvas（UI 子树根，屏幕空间全屏）+ Image（spritePath/color/slice9 九宫格/anchor/offset/size）+ Text（UTF-8 中文/fontSize/color/fontPath）+ Button（三色组 + onClick 脚本回调 + hovered()/pressedThisFrame() 查询）
- **布局**：点锚 v1（位置 = anchor×屏幕 + offset 像素）；全锚点拉伸列 V0.5-13
- **字体**：stb_truetype 按需烘焙图集（中文）；默认系统 msyh.ttc，失败降级内置 5x7 ASCII（warn）；Text.fontPath v1 仅序列化保留
- **渲染**：单动态 VBO + per-atlas 批处理 → 每 Canvas 内容走字体图集时 1 draw call（每张精灵贴图 +1）；Edit 预览与 Play 同路径，无 Canvas 零开销
- **输入**：Play 态 letterbox fit-rect 坐标换算 → Button hover/press/onClick 边沿触发；Edit 态不处理（点击不穿透场景拾取）
- **时序契约**：Player 主循环在 scene.update **前**调 processInput（脚本 onUpdate 读到本帧按钮状态）；编辑器 SceneView 为渲染期调用（脚本侧滞后一帧，已知取舍）

### 12.4 协程（C++20）

- `Behaviour::startCoroutine(...)` 内 `co_yield WaitForSeconds(2)` / `WaitForEndOfFrame()` / `WaitUntil(条件)` / `WaitForCoroutine(另一协程指针)`（嵌套等待）
- 帧内顺序：物理 step → onUpdate 广播 → 协程推进（普通 yield）→ 事件 flush → 协程推进（WaitForEndOfFrame）
- **语义**：enabled=false 不暂停协程（与 Unity 一致）；非激活 GO 上的协程照常推进（与 Unity 差异，已知取舍）；对象销毁/退出 Play 协程自动终止
- 同 Behaviour 内嵌套等待完全安全；**跨 Behaviour 的 WaitForCoroutine 可能悬垂（设计取舍，勿跨对象等待）**

### 12.5 事件系统

- `Events::subscribe<E>(handler)` / `publish<E>(event)`（同步）/ `post<E>(event)`（延迟，帧末 flush）/ `unsubscribe(sub)`
- `Behaviour::subscribeEvent<E>(handler)` 成员便捷版——组件析构自动退订（无悬挂）
- 类型 key = std::type_index；publish 快照防重入；flush swap 模式防投递期间重入；物理 TriggerEvent 经 post 投递

### 12.6 架构变化速览

- **帧序**：flushDestroyQueue → 物理 step(60Hz) → onUpdate → 协程(普通) → Events flush → 协程(帧末) → 渲染 → UI
- **ComponentRegistry**：内置组件 15 个（Transform/MeshRenderer/Camera/DirectionalLight + 物理 5 + UI 4 + 音频 2）；序列化分发按注册表反查（同名脚本冲突会 warn，不再静默替换）
- **依赖**：cmake/Dependencies.cmake 新增 Jolt v5.7.0 + miniaudio 0.11.26（FetchContent）
- **着色器**：新增 ui.vert/.frag、debug_line.vert/.frag
- 构建冒烟：74 源文件零错误零第一方警告；编辑器守卫 15 条验证通过；Player 场景加载+旋转正常

---

## 13. V0.3 波 3 新增：后处理（Bloom+ACES）/ 场景管理 / Prefab 引擎侧前置（2026-11-15）

### 13.1 后处理管线（Bloom + ACES Tonemapping）

- **开关与参数**：Environment 面板 →「Post Processing」折叠区：enabled / bloomThreshold(1.0) / bloomIntensity(0.5) / bloomLevels(1-3) / exposure(1.0)，随场景序列化（environment.postProcess 段）
- **管线**：`SceneRenderer::renderSceneToOutput`（编辑器与 Player 共用）——HDR 场景 FBO(MSAA4x RGBA16F) → 3D → 描边 → 注入点（Show Colliders 线框）→ resolve → [Bloom：brightpass 阈值提取 → 9-tap 高斯 ping-pong ×3 级 → 合成] → Narkowicz ACES + gamma → 输出 LDR FBO → **游戏 UI 画在 LDR FBO 内**（不受 Bloom 影响，Unity 语义）→ 显示
- **直通**：pp.enabled=false 时 hdrFbo 用 RGBA8 + blit 直通——与旧画面逐像素一致
- **永不黑屏三级保障**：post shader 加载失败 / RGBA16F 探测失败 / 未初始化 → 一律纯 blit 直通（不依赖 shader）；编辑器 Environment 面板有 hdrSupported 降级提示
- **HDR 正确性**：HDR 模式下场景/天空/网格全部线性输出（uGamma=0 直通分支），gamma 只在最后 combine 统一做一次（无双重校正）
- 编辑器 Play/Edit 双路径同链；Player 同链（最终帧 blit 回默认帧缓冲）
- **待人工验收**：手动拉伸窗口/切换 pp.enabled 复核双 FBO resize 分支；emission>1 物体 Bloom 目测；8bit 降级路径

### 13.2 场景管理（SceneManager + Build Settings + DDOL）

- **Build Settings**：File → Build Settings…（第 8 面板）——扫描 assets/scenes/*.nova.json，勾选 + Up/Down 排序 + Save 写 `assets/build.novabuild`（合并式扫描：保留既有勾选与顺序，新文件按名追加）
- **脚本 API**：`SceneManager::loadScene("场景名")`（帧末执行，Play 中调用安全）；`dontDestroyOnLoad(go)`；`Events::subscribe<SceneLoaded>` 收切换事件
- **LoadScene 帧末流程**：收集 DDOL 子树（保留原 ID）→ 原地重建场景（外部 Scene* 不失效）→ 挂回 DDOL → 停全部音频 → start → post(SceneLoaded)
- **DDOL**：GameObject 的 Inspector「Dont Destroy On Load」勾选（Play 态禁用）；非根对象勾选时以该对象为子树根迁移（父级不迁移）；多次 LoadScene 持续存活
- **Player**：启动读 build.novabuild 注册场景列表（无则 --scene 单场景）；`--scene` 开发模式回退 `assets/scenes/<name>.nova.json`
- **Packager**：读 build.novabuild 收集全部场景 + 引用资产 + 清单本身进 pak
- 查无场景/清单缺失损坏：Logger::error + 不崩溃
- **待人工验收**：LoadScene 端到端（挂临时脚本触发切换，验证 DDOL 存活 + 退出 Play 快照恢复）

### 13.3 Prefab 引擎侧前置（波 3，编辑器侧波 4）

- `PrefabSerializer`：serializeSubtree（树形 JSON）/ instantiateSubtree（ID 重生成 + 实例→源映射）/ instantiateSubtreeKeepingIds（DDOL 迁移用）/ loadAsset/saveAsset（AssetStream，pak 兼容）
- 树形格式含 dontDestroyOnLoad 键（与场景平铺格式一致）；序列化内部辅助与场景序列化共享（SerializationInternals.h）
- 波 4 将补：PrefabInstance 组件、拖入实例化、Apply/Revert/override 加粗、Undo 文件通道

### 13.4 架构变化速览

- **渲染链**：SceneViewPanel 与 Player 统一走 renderSceneToOutput（原两处直接 UIRenderer::render 调用已删除，UI pass 在管线内部）
- **uGamma 统一**：Renderer::setGammaMode/linearOutput 驱动 blinn_phong/grid/skybox 三处（HDR 线性输出 vs 旧路径 1/2.2），无硬编码旁路
- **Framebuffer**：create 增 hdrColor 参数（RGBA16F 附件+resolve，默认 RGBA8 旧行为不变）
- **新面板**：Build Settings（第 8 面板）；Environment 面板增 Post Processing 折叠区；Inspector 增 DDOL 勾选
- **CMake 纪律**：编辑器新增源文件必须同步改 editor/CMakeLists.txt 显式源清单
- 构建冒烟：四 target 全绿零第一方警告；16F 开启/直通双路径实跑通过；4 个 post shader 首编译零错误

---

*本文档由文档 Agent 基于 editor/src 与 engine/src 的实际实现撰写。发现文档与代码不符时，以代码为准并欢迎更新本文件。*
