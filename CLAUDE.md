# NovaEngine

Mini 3D 游戏引擎 + Unity 风格编辑器。技术栈：C++20 / OpenGL 4.5 / GLFW / Dear ImGui / GLM / Assimp。

## 架构与开发流程

- 架构文档：docs/ARCHITECTURE.md（Architect agent 产出，所有模块接口以它为准）
- 多 Agent 流程：Architect(Plan) → Coder(general-purpose) 按模块并行 → Reviewer 审查 → DevOps 编译验证
- Coder 修改他人模块接口前必须先更新 ARCHITECTURE.md 并说明理由

## 构建（Windows）

- 工具链：Visual Studio Community 2026 (MSVC) + CMake 4.4
- 机器上无 ninja / vcpkg / MinGW，第三方依赖一律用 CMake FetchContent
- 配置：`cmake -S . -B build`
- 编译：`cmake --build build --config Debug`
- 运行：`build/editor/Debug/NovaEditor.exe`

## 约束

- 只用 C++20 标准库 + 技术栈中列出的依赖；引入新第三方库需 Architect 同意
- 务实优先：不要 ECS、不要反射、不要多线程渲染、不要过度抽象
- 代码风格：类 PascalCase、方法 camelCase、成员 m_ 前缀；注释用中文
- 远程仓库：git@github.com:148627654/NovaEngine.git
