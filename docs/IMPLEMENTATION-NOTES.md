# 实现裁定与依赖验证记录

记录于 2026-11-14（接口冻结日）。与 docs/ARCHITECTURE.md 冲突时以本文件为准。

## 依赖版本验证（GitHub API 实测，2026-11-14）

| 依赖 | GIT_TAG | 状态 |
|---|---|---|
| GLFW | 3.4 | ✅ 长期稳定 tag |
| glad2 | v2.0.8 | ✅ |
| GLM | 1.0.3 | ✅ |
| Dear ImGui | v1.93.0-docking | ✅ |
| Assimp | v5.4.3 | ✅ |
| nlohmann/json | v3.12.0 | ✅ 长期稳定 tag |
| stb | 9600522de53973336fa033a289858a4354cbb04c | ✅ commit 存在 |
| ImGuizmo | 1.10 | ✅ |

## 接口冻结裁定（相对架构文档的调整）

1. **GLFWwindow 前向声明**：声明在全局命名空间（非 NovaEngine 内），`nativeHandle()` 返回全局类型指针，可直接传给 ImGui_ImplGlfw（架构文档写在命名空间内会导致类型不一致）。
2. **组件挂载签名**：`GameObject::attachComponent(std::unique_ptr<Component>, uint32_t typeId, bool callAwake = true)`；`Component::componentTypeId<T>()` 为 public（SceneSerializer 反序列化时也需要计算 typeId）。
3. **ModelLoader 辅助函数**：convertAiMesh / extractAiMaterial 为 .cpp 内部自由函数（assimp 类型不进公共头）。
4. **ScriptRegistry 名字裁剪**：注册名含 "::" 时取最后一段（`NE_REGISTER_SCRIPT(NovaGame::SpinningCube)` → 注册名 "SpinningCube"）。
5. **编辑器相机持久化**：Scene 增加公开字段 `editorCameraTarget/Distance/Yaw/Pitch`，SceneSerializer 读写 JSON 的 `editorCamera` 段。
6. **Renderer 渲染队列**：实现于 Renderer.cpp 匿名命名空间（不进头文件）。
7. **nlohmann/json 前置声明**：公共头一律用 `<nlohmann/json_fwd.hpp>`，完整头只在 .cpp 包含。

## 第二轮裁定（2026-11-14，Coder B 交付后）

8. **NE_REGISTER_SCRIPT 宏**：registerScript 返回 void，宏用逗号表达式 `(registerScript(...), true)` 满足 bool 静态变量（原宏赋值 void→bool 编译错误）。
9. **GameObject::addComponent**：typeId hoist 到局部变量再传 attachComponent（避免与 std::move 同语句的求值顺序隐患）。
10. **Material::albedoMapPath**：Material 增加 `std::string albedoMapPath`（贴图资产相对路径，序列化用）；JSON 里 material 段的 "albedoMap" 字符串键不变。MeshRenderer 不再用静态旁表。
11. **demo.nova.json**：SmallCube ID 由 99887766554433221100（超 uint64）改为 9988776655443322110。
12. **Scene::update**：除 Behaviour.enabled 检查外，非激活对象整棵子树跳过 onUpdate（activeInHierarchy 语义）。

## 第三轮裁定（2026-11-14，架构师补遗后）

13. **Scene::start()**：Scene 增加 `void start()`（DFS 广播 onStart，跳过非激活子树与 enabled=false 的 Behaviour）；enterPlayMode 调用它，编辑器不再手写广播。
14. **findScript\<T\>**：Behaviour.h 提供 `template<typename T> T* findScript(GameObject* go)`（dynamic_cast 线性扫描）；脚本共用 Behaviour 的 typeId，getComponent<具体脚本T>() 不保证命中。
15. **material JSON 键名**：保持 schema 原键 "albedoMap"（字符串路径），不采用 "albedoMapPath" 键名（避免 schema 与 demo 场景漂移）。

## 第四轮裁定（2026-11-15，Debugger 排查后）

16. **ImGuizmo 矩阵直传，不转置**：glm 与 ImGuizmo 的 float[16] 同为列主序内存布局，`glm::value_ptr` 直传即可；转置会破坏平移并使 ImGuizmo 的 behind-camera 检测把 gizmo 静默裁掉。ARCHITECTURE §11.7 原「必须转置」结论有误，已修正为直传 + 陷阱说明。
17. **gizmo drawlist 用 `ImGui::GetWindowDrawList()`**：ImGuizmo 的悬停判定依赖 drawlist `_OwnerName` 与 g.HoveredWindow 匹配；`GetForegroundDrawList()` 的 owner 永远不是 SceneView 面板 → 悬停恒假 → 手柄无法拖拽。
18. **诊断日志保留**：GizmoController（显示/操作切换/空间切换）、SceneViewPanel（拾取命中）、EditorApplication（外部拖放分发）、SpinningCube（onStart/前 3 帧旋转）关键路径日志保留，便于后续交互问题排查。

## 环境确认

- VS Community 2026 (MSVC) + CMake 4.4；无 ninja / vcpkg / MinGW → FetchContent + VS 多配置生成器
- Python 3.13.9 可用（glad2 的 configure 期代码生成）
- GitHub SSH 密钥已生成，待用户添加到 GitHub 账户后方可 push
