# NovaEngine 🚀

一个 C++20 编写的 Mini 3D 游戏引擎 + Unity 风格游戏编辑器。
目标：自己写引擎底层、AI 辅助开发，最终做出能创建、编辑、运行 3D 游戏的完整工具。

## 技术栈

| 项目 | 选型 |
|---|---|
| 语言 | C++20 |
| 图形 API | OpenGL 4.5 |
| 窗口 | GLFW 3.4 |
| UI | Dear ImGui (docking) |
| 数学 | GLM |
| 模型加载 | Assimp 5.4 |
| 序列化 | nlohmann/json |
| 构建 | CMake (FetchContent) + MSVC |
| IDE | VS Code |

## 编辑器功能

- 左侧：Scene Hierarchy（场景层级）
- 中间：3D Scene View（场景视图）
- 右侧：Inspector（属性面板）
- 底部：Console / Asset Browser
- 拖拽模型（.obj / .fbx / .gltf）进入场景
- 物体移动 / 旋转 / 缩放 gizmo（W/E/R 切换，F 聚焦）
- 场景保存与加载（JSON）
- Play 模式运行游戏

## 构建

参见 [CLAUDE.md](CLAUDE.md)。

## 开发流程

AI 辅助多 Agent 协同开发：Architect → Coder ×N → Reviewer → DevOps/Tester，
详见 [docs/AGENTS.md](docs/AGENTS.md) 与架构文档 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。
