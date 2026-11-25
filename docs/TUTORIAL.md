# NovaEngine 新手教程:从零到打包一个小游戏(1 天)

> 目标:一个完全没有引擎经验的新用户,按本教程**1 天完成一个小游戏并打包成独立 exe**。
> 所有命令与操作步骤均在 2026-11-25 实机验证通过(编辑器/Player/CLI 冒烟)。参照样例:示例游戏《霓虹收集者》(源码 `samples/NovaSampleGame/`,场景 `assets/scenes/NeonLobby.nova.json` / `NeonLevel1.nova.json`——每个步骤后附「参照样例」指路)。
> 本教程默认 Windows 10/11。脚本 API 详细说明见 [API-REFERENCE.md](API-REFERENCE.md),编辑器操作细节见 [FEATURES.md](FEATURES.md)。

---

## 第 0 步:你将做的东西

一个迷你「收集跑酷」:一个平台 + 会旋转的收集物 + 玩家(第三人称)+ 计分 HUD + 开始按钮 → 收集满后切到结算场景 → 打包成双击即玩的 exe。涉及引擎功能:场景编辑、C++ 脚本、角色控制器、运行时 UI、场景切换传参、打包。全程约 6–8 小时(不含首次构建等待)。

## 第 1 步:环境准备与构建引擎(约 1–2 小时,首次构建等待期可并行读文档)

**环境要求**:Windows 10/11 · Visual Studio 2026(MSVC,x64)· CMake 4.4+ · Python 3(构建期 glad 生成器需要;若 `pip` 可用,提前 `pip install jinja2` 更稳)。第三方依赖(glad/GLFW/GLM/ImGui/Assimp/Jolt/miniaudio 等)全部经 CMake FetchContent 自动拉取,**首次构建需要网络**。

在项目根目录打开终端(PowerShell 或 Git Bash 均可):

```bash
# 1. 配置(生成 Visual Studio 解决方案;首次编译 Assimp 需数分钟,属正常)
cmake -S . -B build

# 2. 编译编辑器与播放器(Debug;首次全量约 10–20 分钟)
cmake --build build --config Debug
```

产物:

| 文件 | 说明 |
|---|---|
| `build/editor/Debug/NovaEditor.exe` | 编辑器 |
| `build/player/Debug/NovaPlayer.exe` | 打包运行时播放器 |
| `build/samples/Debug/NovaGame.dll` | 游戏脚本 DLL(含示例游戏脚本) |

**验证构建可用**(三条命令都应正常退出,退出码 0):

```bash
# 编辑器 GUI 启动即退(CI 冒烟,验证环境初始化全链路)
build/editor/Debug/NovaEditor.exe --quit

# headless 批量导入资产(全量导入模型/贴图缓存;退出码 0 = 成功)
build/editor/Debug/NovaEditor.exe --headless --import

# 播放器直接跑示例游戏大厅(打开后按 Esc 退出)
build/player/Debug/NovaPlayer.exe --scene assets/scenes/NeonLobby.nova.json
```

> 程序启动时自动切换工作目录到项目根,**无需手动 cd**;双击 exe 同样可用。日志文件在 `<项目根>/Logs/`,出问题先看最新日志(逐行落盘,崩溃原因也会写进去)。

## 第 2 步:认识编辑器(约 30 分钟)

```bash
build/editor/Debug/NovaEditor.exe
```

启动后自动打开默认场景 `assets/scenes/demo.nova.json`(立方体 + 光源 + 相机)。对照 [FEATURES.md](FEATURES.md) 熟悉界面:

- **Hierarchy(左)**:场景对象树。**右键空白处 = 创建到根层级**(Create Empty / Create Cube / Create Plane / Create Directional Light / Create Camera);右键对象行 = 创建为该对象的子级,另有 Duplicate(Ctrl+D)/ Delete(Del)/ Create Prefab。
- **Scene(中)**:3D 视口。右键拖拽旋转视角、中键平移、滚轮缩放;**按住右键 + WASD 飞行(Shift 加速)**;左键点击拾取对象;选中后 **W/E/R** 切换移动/旋转/缩放 gizmo,**X** 切换 Local/World,按住 **Ctrl** 拖拽 = 吸附;F 聚焦。
- **Inspector(右)**:选中对象的组件参数(Transform/材质/脚本字段)。**Add Component** 下拉 = 全部内置组件(26 个)+ 已注册脚本。
- **Environment 面板**(与 Inspector 并排 Tab):天空/环境光/雾/后处理。
- **Console(底部)**:日志,四色分级。

**立刻体验 Play**:按 **Ctrl+P** 进入 Play(场景里的 Cube 挂着 SpinningCube 脚本,会绕 Y 轴旋转);再按 **Ctrl+P** 退出(场景由快照还原)。

**看一遍示例游戏**:`Ctrl+O` 打开 `assets/scenes/NeonLobby.nova.json` → Ctrl+P → 点「开始游戏」按钮 → WASD 移动、空格跳、Shift 冲刺 → 收集 10 颗能量球 → 冲进传送门。这就是你一天后能做出东西的完整形态。

## 第 3 步:创建第一个场景(约 30 分钟)

1. **Ctrl+N** 新建空场景 → **Ctrl+S** 保存为 `MyFirstLevel`(自动定位 `assets/scenes/`,自动补 `.nova.json`)。
2. Hierarchy 右键 → **Create Plane**(地面,10×10m)。选中它 → Inspector 把 Position 设为 (0, -0.5, 0)。
3. Hierarchy 右键 → **Create Cube**(平台)。Position (0, 0.5, -2)、Scale (4, 1, 4)——一个 4×4 的高台。
4. 右键 → **Create Directional Light**(经典斜下方向已自动摆好)。
5. 右键 → **Create Camera**(场景第一个相机自动成为 Main Camera)。Position (0, 4, 12),Rotation 略俯视:X ≈ -15。
6. 给平台一点颜色:选中 Cube → Inspector → Mesh Renderer → Albedo 改成 (0.1, 0.7, 0.9)。
7. **Ctrl+S 保存**。再按 Ctrl+P 看效果,没问题 Ctrl+P 退出。

> 球体没有专属创建项:Create Cube 后把 Inspector 的 Mesh 路径改成 `builtin:sphere` 即可(或 Create Empty → Add Component → MeshRenderer 填 `builtin:sphere`)。场景文件是纯 JSON(`assets/scenes/MyFirstLevel.nova.json`),可手改(改完 Ctrl+O 重新加载)。

## 第 4 步:第一个脚本(约 1 小时)

脚本是 C++ 类,放进 `samples/`,注册后经 Add Component 挂到对象上。

**4.1 写脚本**:新建 `samples/MyFirstScript.h` 与 `samples/MyFirstScript.cpp`(完整代码见 [API-REFERENCE.md §2](API-REFERENCE.md) 的 MyRotator 示例,把类名改成 `MyFirstScript`,保留 `speed` 字段、`onUpdate` 里 `transform()->rotate(...)` 绕 Y 旋转的逻辑)。

要点:继承 `NovaEngine::Behaviour`;构造函数转发 `owner`;`onSerialize/onDeserialize` 成对写字段(否则 Inspector 看不到);.cpp 末尾 `NE_REGISTER_SCRIPT(MyGame::MyFirstScript)`。

**4.2 注册进构建**:编辑 `samples/CMakeLists.txt`,在 `NOVA_GAME_SOURCES` 列表末尾追加一行:

```cmake
    MyFirstScript.cpp        # 我的第一个脚本
```

**4.3 构建脚本 DLL**(增量,秒级):

```bash
cmake --build build --config Debug --target NovaGame
```

**4.4 热重载**:回编辑器,菜单 **Scripts → Reload Scripts**(Ctrl+Shift+R;勾上 **Auto Reload** 后每次构建自动重载)。Console 应出现「脚本 DLL 已加载…注册脚本 15 个」之类的日志。

**4.5 挂载与验证**:选中高台 Cube → Inspector → **Add Component → MyFirstScript** → 字段 `speed` 出现(默认 30,可直接拖拽改)→ Ctrl+P → 高台开始旋转。

> 排障:Add Component 下拉里没有你的脚本 = DLL 没构建或没 Reload;Play 中脚本报错看 Console/Logs。按键是否到达引擎可用 `NOVA_DBG_KEYS=1` 启动编辑器(每键一条「按下 XX 键」日志)。

**参照样例**:`samples/SpinningCube.cpp`(36 行的最小完整脚本)。

## 第 5 步:玩家控制(约 1 小时)

引擎自带角色控制器组件 + 现成的 `CharacterWalk` 脚本(已编译进 NovaGame.dll,无需自己写)。

1. Hierarchy 右键 → **Create Empty**,命名 `Player`。Position (0, 0.9, 4)。
2. Inspector → Add Component → **CharacterController**:radius 0.4 / height 1.8 / stepHeight 0.35 / slopeLimitDeg 45 / mass 70 / gravityScale 1(默认值即可)。
3. Add Component → **CharacterWalk**:speed 4.0 / jumpSpeed 6.0。
4. 第三人称相机:选中 Main Camera 对象 → Hierarchy 里**拖到 Player 行上**(成为其子级,保持世界变换)→ Inspector 设 Position (0, 2.5, 5)、Rotation X ≈ -14。
5. Ctrl+P:WASD 移动、空格跳、Shift 冲刺(角色控制器自动处理重力/着地/楼梯步进)。在平台边缘跳下去试试——角色会掉进虚空(下一步处理)。

**输入是怎么接上的**:`CharacterWalk` 用 `InputMap::getAxis("Horizontal"/"Vertical")` 读 WASD、`getActionDown("Jump")` 读空格——绑定文件 `assets/input/default.novainput` 里定义,编辑器 **File → Input Bindings...** 可视化编辑。你要加自定义按键(如 E 交互),在 Input Bindings 里加动作,脚本里 `InputMap::getActionDown("Interact")` 即可。

**参照样例**:`samples/CharacterWalk.cpp`(移动 + 二段跳 + 冲刺 + 起跳锁存,可直接抄)。

## 第 6 步:收集物 + 计分(约 1.5 小时)

**6.1 摆一个收集球**:Create Cube → 改名 `Orb` → Mesh 路径改 `builtin:sphere` → Position (0, 1.5, -2) → Scale 0.3 → Albedo (0.05, 0.95, 1.0)、Emission Color (0.05, 0.45, 0.55)、Emission Intensity 1.5(自发光霓虹感)。

**6.2 写拾取脚本**(复制 `samples/NovaSampleGame/NeonCollectible.cpp` 改一改——它做的事:每帧上浮摆动自转;与玩家世界坐标做 3D 距离轮询(半径 1.8m,平方距离比较免开方);命中 → 播粒子/音效 → 计分 → `gameObject()->setActiveSelf(false)` 隐藏)。序列化字段 `pickupRadius/bobHeight/bobSpeed/spinSpeedDeg`。注册进 CMakeLists → 构建 → Reload → 挂到 Orb 上。

**6.3 计分 HUD**:运行时 UI 与编辑器 ImGui 无关,是场景里的组件:

1. Create Empty → 改名 `Canvas` → Add Component → **Canvas**。
2. 选中 Canvas 行右键 → Create Empty(成为 Canvas 子级)→ 改名 `ScoreText` → Add Component → **Text**:text「得分 0」、fontSize 32、color 青色 (0.4,1,1,1)、anchor (0, 0)、offset (24, 24)(= 屏幕左下角)。
3. 在你的游戏管理脚本 onStart 里 `scene->findGameObjectsByName("ScoreText")[0]->getComponent<Text>()` 缓存指针,计分时 `txt->text = std::format("得分 {}", score);`。

> **UI 布局注意(与 Unity 不同)**:位置 = anchor × 屏幕尺寸 + offset 像素,anchor 是相对**屏幕**的——UI 子节点的 anchor 不随父元素走。按钮文字标签不要做按钮子对象,做同 offset 的兄弟节点。

**参照样例**:`NeonCollectible.cpp`(拾取)、`NeonLevelHud.cpp`(HUD 文本刷新)。

## 第 7 步:双场景闭环(约 1.5 小时)

**7.1 结算场景**:另存一个场景 `MyGameOver`(Ctrl+N → 摆 Text「通关!」+ 返回按钮 → Ctrl+S)。Canvas/Text/Button 组件与上面一致(Button 的 onClick 由脚本接线)。

**7.2 场景切换 + 传参**:游戏管理脚本里(参见 `NeonGameManager::switchScene`):

```cpp
NovaEngine::SceneManager::setSceneParam("score", std::format(R"({{\"s\":{}}})", score));
// BGM 若有,先 Stop() 淡出,协程等 0.5s 再切(防爆音)
startCoroutine(switchAfterDelay("MyGameOver"));
```

新场景脚本 onStart 里 `SceneManager::getSceneParam("score")` 读 JSON 字符串显示(缺失/坏 JSON 给默认值兜底)。

**7.3 注册进构建列表**:菜单 **File → Build Settings...** → 勾选 `MyFirstLevel` 与 `MyGameOver` → Save(写 `assets/build.novabuild`)。**列表第一项 = Player 双击启动的默认场景**——用 Up/Down 把 `MyFirstLevel` 排第一。

**参照样例**:`NeonGameManager.cpp` 的 `switchScene`(自由协程函数 + 参数——**不要用 lambda + 闭包捕获的协程**,MSVC 工具链已知坑)。

## 第 8 步:打包成独立 exe(约 30 分钟)

**方式 A:编辑器 GUI**——File → **Build Player...** → 选择输出目录 → 产出 `场景名.novapak` + `NovaPlayer.exe` + `NovaGame.dll` 到同一目录(资源缺失会逐条报错并失败)。**注意:打包前必须先构建过 NovaPlayer 与 NovaGame target**(第 1 步全量构建已包含;打包时也会预检提示)。

**方式 B:命令行**(推荐脚本化/CI):

```bash
# 单场景包(demo.novapak + NovaPlayer.exe + NovaGame.dll 到 dist/mygame/)
build/editor/Debug/NovaEditor.exe --headless --build --out dist/mygame

# 发行目录(收集 build.novabuild 全部场景 + 全部引用资产 + 版本/图标/README,
# 输出 dist/mygame/NovaEngine_1.0.0/,双击即玩)
build/editor/Debug/NovaEditor.exe --headless --build --release --out dist/mygame
```

**验证打包产物**:进输出目录双击 `NovaPlayer.exe`(默认加载构建列表第一个场景)→ 走一遍第 0 步的通关流程 → Esc 退出。这个目录整体拷贝到任何 Windows 机器双击即玩(存档在 exe 目录/saves)。

> 打包前建议先跑一次 `--headless --import` 生成资产缓存;引用缺失(删除贴图留断链)时打包会直接失败并列出清单——这是特性不是 bug,按清单补回资产即可。

## 第 9 步(可选):把游戏做得像样一点(半天进阶路线)

- **音效/BGM**:`assets/audio/` 放 wav/ogg;背景音乐 = Create Empty → Add Component → AudioSource,字段 `clipPath`/`loop:true`/`stream:true`/`bus:1`/`playOnAwake:true`/`fadeInSeconds:1`;音效在脚本里 `AudioEngine::play(AudioClip::load(...), ...)`(NeonGameManager::playSfx 范式)。
- **粒子**:收集爆发 = 子对象挂 ParticleSystem{playOnAwake:false, loop:false},命中时 `ps->play()`(NeonCollectible 的 Burst)。
- **存档**:通关时 `SaveGame::save(*scene, 0, "assets/scenes/MyFirstLevel.nova.json", 用时)`,大厅用 `SaveGame::listSlots()` 只读元数据显示最佳纪录(**不要用 load**——它会原地重建场景)。
- **本地记录(排行榜/回放)**:`FileUtil::writeText/readText`("saves/xx" 即存档目录;NeonGameManager 的排行榜/幽灵回放范式)。
- **彩色后期**:Environment 面板 → Color Grading 区选 `assets/luts/cool.cube` + Intensity 0.8(冷色霓虹风);Bloom 阈值 0.8 + 强度 0.6(只让自发光物体泛光)。

## 常见问题

| 现象 | 排查 |
|---|---|
| 双击 exe 白屏/黑屏 | 看 `<项目根>/Logs/` 最新日志;白屏优先怀疑后处理与 LUT 设置 |
| Add Component 里没有我的脚本 | 没构建 NovaGame target / 没 Reload Scripts |
| Play 里角色不动 | CharacterWalk 挂了吗?CharacterController 挂了吗?Console 应有「已挂载到 Player(CharacterController 已连接)」日志 |
| 按钮点击没反应 | UI 布局按 anchor×屏幕+offset 解析,先检查 anchor/offset 数值;Player 窗口固定 1280×720 设计 |
| 打包报「缺失资源」 | 按报错清单补回文件;脚本动态加载的音频可放 `assets/audio/`(发行构建整目录兜底收集) |
| 想改按键 | File → Input Bindings... |

## 文档索引

- [API-REFERENCE.md](API-REFERENCE.md) — 全部脚本 API 签名与示例
- [FEATURES.md](FEATURES.md) — 编辑器操作/快捷键/面板功能手册
- [UNITY-MIGRATION.md](UNITY-MIGRATION.md) — Unity 用户迁移对照表
- [KNOWN-ISSUES.md](KNOWN-ISSUES.md) — 已知问题与修复记录
- [ARCHITECTURE.md](ARCHITECTURE.md) — 架构总纲(模块接口冻结契约)

---

*本文档由内容域 Coder 于 V1.0-14 编写;所有命令 2026-11-25 实机冒烟通过(编辑器 --quit / --headless --import / --headless --build / --headless --build --release / Player 大厅 20s 运行)。*
