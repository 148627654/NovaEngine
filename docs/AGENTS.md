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

## V2.0 角色拓展（展望，对应 ROADMAP §8）

V2.0 引入两个新领域（贴图/皮肤创作链、机器人仿真），现有四角色之外按需启用专项角色（实现上仍用 `general-purpose`/`Plan` agent + 专项任务书，不新增 agent 类型）：

| 角色 | 职责 | 对应工作 |
|---|---|---|
| 🔬 Format Researcher（格式研究员） | 调研格式规范（URDF XML schema / SDFormat / STEP AP242 / MJCF），产出「格式 → 引擎映射」设计文档与样例语料 | ROADMAP §8.2-1/2 导入器设计输入 |
| 🤖 Domain Expert（领域专家） | 机器人学与工业仿真领域知识：关节/传感器语义、行业验收惯例（与 Gazebo/RViz 数值对照）、URDF 模型甄选 | B 线全部波次的方案评审 |
| 🧪 Simulation QA（仿真验证） | 仿真一致性验证：确定性复跑、与真值/参考实现的数值比对、传感器数据校验 | §8.2-4/5 验收、headless 确定性测试 |
| 🎨 Content Pipeline QA（内容管线验证） | 贴图/皮肤/Decal 内容链路的像素级验收（沿用现有帧缓冲读回方法） | §8.1 全部波次验收 |

使用约定：先由 Architect 在波次计划里指定本波启用的角色与任务书；Format Researcher 的产出必须归档进 docs/ 供 Coder 实现引用；不启用则按现有四角色流程执行。
