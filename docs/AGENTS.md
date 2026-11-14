# 多 Agent 协同开发流程

NovaEngine 采用 AI 辅助多 Agent 协同开发，角色映射到 Claude Code 内置 agent：

| 角色 | Agent 类型 | 职责 |
|---|---|---|
| 🏗️ Architect | `Plan` | 架构设计、模块接口定义、里程碑规划 → docs/ARCHITECTURE.md |
| 💻 Coder | `general-purpose` ×N | 按模块并行实现，只能实现 ARCHITECTURE.md 中定义的接口 |
| 🔍 Reviewer | `general-purpose` | 审查全部改动，输出 bug 清单与设计问题 |
| 🔧 DevOps/Tester | `general-purpose` | CMake 配置 + 编译 + 运行冒烟测试，修复构建问题 |

## 流程

1. Architect 产出架构文档（目录结构、模块接口、里程碑）
2. 基础模块先行（窗口 / 渲染 / 场景核心），上层模块并行
3. Reviewer 审查 → Coder 修复
4. DevOps 编译验证 → 修复 → git 提交
5. 每个里程碑结束推送到 GitHub

## 规则

- Coder 修改他人模块接口前必须先更新 ARCHITECTURE.md
- 不引入技术栈之外的第三方库
- C++ 代码遵循 CLAUDE.md 中的代码风格
