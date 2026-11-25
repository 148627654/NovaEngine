# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

# NovaEngine

Mini 3D 游戏引擎 + Unity 风格编辑器。技术栈：C++20 / OpenGL 4.5 / GLFW / Dear ImGui / GLM / Assimp / Jolt / miniaudio / nlohmann-json。

## 架构与开发流程

- 架构文档：docs/ARCHITECTURE.md（Architect agent 产出，所有模块接口以它为准；各波次契约在文末 §27/§28）
- 多 Agent 流程：Architect(Plan) → 域 Coder 并行 → Reviewer 审查 → DevOps 编译验证 → 合波 → 用户人工验收。Agent 定义在 .claude/agents/V1.0_*.md（v1-architect / 5 域 Coder / v1-reviewer / v1-devops / 3 游戏 Agent），按 ARCHITECTURE 波次契约派发
- Coder 修改他人模块接口前必须先更新 ARCHITECTURE.md 并说明理由
- 路线图与验收：docs/ROADMAP.md（波次状态行随开发维护）；已知问题与修复方向备案：docs/KNOWN-ISSUES.md
- 用户是非技术背景：验收一律通过现成 demo 场景目测 + 简单中文说明，不要让用户跑命令或读代码

## 构建（Windows）

- 工具链：Visual Studio Community 2026 (MSVC) + CMake 4.4
- 机器上无 ninja / vcpkg / MinGW，第三方依赖一律用 CMake FetchContent（构建期 glad 生成器需要 Python + jinja2）
- 配置：`cmake -S . -B build`
- 编译：`cmake --build build --config Debug`（按 target 增量：NovaEditor / NovaPlayer / NovaTests / NovaGame；内存审计用例 ③④ 需要测试脚本 DLL，构建用 `--target NovaTests NovaTestScripts`）
- 运行编辑器：`build/editor/Debug/NovaEditor.exe`；播放器冒烟：`build/player/Debug/NovaPlayer.exe --scene assets/scenes/NeonLobby.nova.json`
- 编辑器 CLI：`--headless --import`（批量导入）、`--quit`（无头冒烟）、`--build --release`（出发布目录）
- 测试：`build/tests/Debug/NovaTests.exe`（Catch2，支持按名称过滤；CI 等价 `ctest --test-dir build -C Debug`）
- 坑：运行中的 exe 会锁住产物（LNK1168）→ 重建前 `taskkill /F /IM NovaEditor.exe /IM NovaPlayer.exe /IM NovaTests.exe`

## 代码结构要点

- engine/（引擎核心，静态链入 exe）· editor/（ImGui 编辑器）· player/（打包运行时）· samples/（示例脚本）· tests/（Catch2 无头回归）
- samples/ 双形态编译：NovaGame.dll（编辑器热重载）+ NovaGamePlayer（打包发布），同一份源、各链各的 exe 导入库；脚本命名空间 NovaGame、`NE_REGISTER_SCRIPT` 注册、字段可序列化；热重载后静态单例需自愈（onUpdate 里 `s_instance != this` 时重新注册）
- 跨 DLL 组件 typeId：FNV-1a 哈希 `typeid(T).name()`
- 场景文件 .nova.json 为 JSON 文本，**schema version 4**（assets/scenes/V05Wave2Demo.nova.json 是权威模板；V10 场景是旧 v3，勿照抄）
- 冻结面：内置组件数 26、schema、第三方库集合——变更需 Architect 裁定
- 打包：Player + .novapak；示例游戏《霓虹收集者》场景 NeonLobby / NeonLevel1，打包验收产物在 dist_game/

## 引擎语义陷阱（详见 docs/KNOWN-ISSUES.md）

- MSVC Debug 下带捕获的 lambda 协程闭包复制损坏（0xCC 未初始化）→ 用自由协程函数或参数传递，不要用捕获 lambda 写协程
- UI 子节点锚点相对**屏幕**解析，非 Unity 式相对父 Rect → 按钮标签用同偏移的兄弟节点
- 未激活 GameObject 不跑 onStart → 需要脚本在 onStart 里自行隐藏的 GO，场景中必须 active=true
- 物理 CharacterVirtual 每步 writeBackCharacters 覆盖 Transform → 复活/传送必须走 CharacterController::setPosition（物理传送）
- 后处理设置默认值契约「缺省全关」（如 lutIntensity 默认 0.0；违背会触发每帧 LUT 加载/卸载循环）
- 窗口最大化后鼠标映射失准（GLFW_SCALE_TO_MONITOR 双缩放）→ Player 窗口固定 1280×720（GLFW_RESIZABLE=FALSE）

## 约束

- 只用 C++20 标准库 + 技术栈中列出的依赖；引入新第三方库需 Architect 同意
- 务实优先：不要 ECS、不要反射、不要多线程渲染、不要过度抽象
- 代码风格：类 PascalCase、方法 camelCase、成员 m_ 前缀；注释用中文
- 文档原地更新，不新增 md（ARCHITECTURE 契约明确授权的除外）
- 远程仓库：git@github.com:148627654/NovaEngine.git
