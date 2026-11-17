# NovaEngine 🚀

> 一个 C++20 编写的 Mini 3D 游戏引擎 + Unity 风格游戏编辑器。
> 自己写引擎底层、AI 辅助开发,做出能创建、编辑、运行、打包 3D 游戏的完整工具。

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![OpenGL](https://img.shields.io/badge/OpenGL-4.5-green)
![Platform](https://img.shields.io/badge/Platform-Windows%20x64-lightgrey)
![Version](https://img.shields.io/badge/Version-V0.3-orange)

![编辑器截图](screenshots/editor.png)

## ✨ 特性

**编辑器(Unity 风格)**

- 五面板布局:Scene Hierarchy / 3D Scene View / Inspector / Console / Asset Browser,另有 Environment(环境)、Build Settings、材质库等面板
- 场景编辑:点选 / Ctrl 加选 / W·E·R·X gizmo(位移·旋转·缩放·Local/World 切换)/ 吸附 / 多选批量编辑 / F 聚焦
- 拖拽 `.obj` / `.fbx` / `.gltf` 模型进场景,重复导入走二进制缓存秒进
- Play 模式(Ctrl+P):快照进出、游戏分辨率与 Render Scale、运行时 UI 输入
- Undo/Redo 全链路(场景编辑 + 资产文件级撤销)
- 中文路径资产、文件日志 + 崩溃兜底

**渲染**

- PBR 材质(金属度/粗糙度工作流)+ 方向光阴影 + 天空盒(渐变/HDRI)+ 透明混合
- 后处理链:Bloom + ACES 色调映射(HDR 16F 中间缓冲,老显卡自动降级直通)
- HiDPI 适配(150% 缩放显示器文字清晰不发虚)

**游戏系统**

- 物理:Jolt Physics——刚体、盒/球/胶囊/网格碰撞体、触发器、kinematic、固定步进
- 音频:miniaudio——3D 空间音效(近大远小)、多音源混音、循环
- 运行时 UI:Canvas / Button / Text / Image,锚点布局,支持中文字体与每文本独立字体
- 脚本:C++ 脚本组件,字段可序列化、Inspector 可调(示例:SpinningCube / OrbitCamera / SceneSwitcher)
- 协程 + 事件系统
- Prefab:创建 → 拖放实例化 → 字段 override 高亮 → Apply 全实例同步 → Revert
- 材质库:`.novamat` 共享材质资产,改一处、处处同步
- 场景管理:Build Settings、运行时 LoadScene、DDOL 对象跨场景存活

**发布**

- 一键打包独立 exe:NovaPlayer + `.novapak` 单文件资源包,目标机无需开发环境
- 无头回归测试(Play 循环 / 序列化往返)

## 🚀 快速开始

**环境要求**:Windows 10/11 · Visual Studio 2026(MSVC)· CMake 4.4+。第三方依赖全部经 CMake FetchContent 自动拉取,无需手动安装。

```bash
# 配置(首次编译 Assimp 需数分钟,属正常现象)
cmake -S . -B build

# 编译编辑器(或 cmake --build build 全量)
cmake --build build --config Debug --target NovaEditor

# 运行编辑器
build/editor/Debug/NovaEditor.exe
```

其余产物:

| Target | 产物 | 说明 |
|---|---|---|
| NovaPlayer | `build/player/Debug/NovaPlayer.exe` | 打包运行时播放器(`--scene/--pak/--width/--height` 参数) |
| PlayCycleTest | `build/tests/Debug/PlayCycleTest.exe` | 无头回归测试 |

Release 构建:`cmake --build build --config Release`。

## 📖 使用

**编辑器**

- `Ctrl+N` / `Ctrl+O` / `Ctrl+S` 新建 / 打开 / 保存场景(`.nova.json`,JSON 文本可读)
- 场景视口右键创建物体(Cube/Sphere/Plane/Camera/Light/UI 等),拖拽成父子层级
- `W` 位移 / `E` 旋转 / `R` 缩放 / `X` 切换 Local/World;按住 Ctrl 拖动 = 吸附
- 右键拖拽旋转视角、中键拖拽平移、滚轮缩放;按住右键 + WASD 飞行(Shift 加速)
- `Ctrl+P` Play / Stop,`F10` 单步推进
- `File → Build Player...` 打包独立 exe
- 脚本:挂 `SpinningCube`(可调 speed)、`SceneSwitcher`(Play 中 K 键切 Second 场景、L 切回 demo,验证 DDOL)

**播放器**

```bash
build/player/Debug/NovaPlayer.exe --scene assets/scenes/demo.nova.json
# 打包产物:exe 同目录放 .novapak 自动挂载,双击即玩
```

**示例场景**:`demo`(PBR+阴影+UI)· `UITest`(按钮/文本)· `PhysicsTest`(刚体下落)· `Second`(切场景验证)

## 📁 项目结构

```
engine/    引擎核心(渲染 / 物理 / 音频 / UI / 脚本 / 序列化 / 资产)
editor/    Unity 风格编辑器(ImGui 面板)
player/    打包运行时播放器(无编辑器依赖)
samples/   示例脚本(编译进编辑器与播放器)
assets/    示例场景与资产(models / scenes / audio)
tests/     无头回归测试
docs/      架构 / 功能 / 路线图文档
```

## 🗺 路线图

- ✅ M1–M5:窗口+渲染+五面板 → 模型渲染+层级 → 编辑交互 → 序列化 → Play 模式
- ✅ V0.2:阴影 / 天空盒 / Undo-Redo / 统计面板 / 透明渲染 / 打包
- ✅ V0.3:PBR 材质库 / 物理 / 音频 / 运行时 UI / 协程事件 / 后处理 / 场景管理 / Prefab
- ⏳ V0.5「发布能力与规模内容」:LOD / IBL / SSAO / 粒子 / 地形 / 动画状态机 / 2D / 热重载
- ⏳ V1.0「商业独立游戏发布级」:Profiler / 多场景流式 / Shader Graph / CI / 示例游戏
- 🔭 V2.0「领域扩展」(展望):皮肤系统 / Decal 贴花 / 贴图热重载 · URDF/STEP 机器人仿真(关节/传感器/headless 加速)

详见 [docs/ROADMAP.md](docs/ROADMAP.md)。

## 📚 文档

| 文档 | 内容 |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 架构总纲(所有模块接口以此为准) |
| [docs/FEATURES.md](docs/FEATURES.md) | 功能使用手册(快捷键 / 面板 / 工作流) |
| [docs/ROADMAP.md](docs/ROADMAP.md) | 版本路线图与验收清单 |
| [docs/KNOWN-ISSUES.md](docs/KNOWN-ISSUES.md) | 已知问题与修复记录 |
| [docs/AGENTS.md](docs/AGENTS.md) | 多 Agent 开发流程 |
| [CLAUDE.md](CLAUDE.md) | 构建与开发约定 |

## 🤝 贡献

AI 辅助多 Agent 协同开发:Architect(方案)→ Coder(按模块并行)→ Reviewer(审查)→ DevOps(编译验证),流程见 [docs/AGENTS.md](docs/AGENTS.md)。

代码约定:类 `PascalCase`、方法 `camelCase`、成员 `m_` 前缀;注释用中文;不引入新第三方库需 Architect 同意。

## 📄 许可证

尚未选择许可证(私有仓库)。

## 💖 致谢

站在这些开源项目的肩膀上:

[GLFW](https://github.com/glfw/glfw) · [Dear ImGui](https://github.com/ocornut/imgui) · [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) · [GLM](https://github.com/g-truc/glm) · [Assimp](https://github.com/assimp/assimp) · [Jolt Physics](https://github.com/jrouwe/JoltPhysics) · [miniaudio](https://github.com/mackron/miniaudio) · [stb](https://github.com/nothings/stb) · [nlohmann/json](https://github.com/nlohmann/json)
