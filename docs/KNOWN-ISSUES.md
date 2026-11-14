# 已知问题与后续里程碑（2026-11-14）

## 当前状态

M1（窗口+渲染+五面板）+ M2（模型渲染+层级）已达成：Debug 构建 0 错误 0 警告，冒烟测试通过。
M3–M5 的功能代码已全部实现，待人工交互验收。

## P2 打磨清单（来自 Reviewer 审查报告）

1. 网格地面雾色硬编码（Renderer.cpp kGridFogColor），与 scene.skyColor 不联动
2. Window 析构未调 glfwTerminate（单窗口无影响）
3. 内置图元（builtin:cube/sphere/plane）无缓存，每次整场景重建重传 GPU 网格
4. TextureLoader 用 ANSI 路径（stb），中文路径贴图加载失败
5. ~~运行依赖工作目录 = 项目根（双击 exe 缺 shader/资产）~~ **已修复（cc0e865）**：NOVA_PROJECT_ROOT 编译期注入 + 启动切工作目录 + SetConsoleOutputCP
6. 模型首次导入双解析（loadModel 不含材质信息）
7. Duplicate/移除脚本组件走整场景 JSON 重建（O(n²) 多选复制、脚本状态清零）
8. Hierarchy F2 重命名未在 Play 态禁用
9. 脚本类型名反查依赖 MSVC typeid().name() 格式
10. 畸形 JSON（对象无 Transform）会导致编辑器解引用 nullptr
11. Input 滚轮注册依赖首帧当前上下文（多窗口脆弱）
12. Gizmo 用前景 drawlist，与面板重叠时画到面板之上

## DevOps 遗留建议

- GameObject::setParent 防御分支可能双归属（GameObject.cpp:91-94，正常流程不触发）
- MSVC 14.51 下整数→指针 static_cast 一律用 reinterpret_cast

## M3–M5 人工验收要点

- **M3**：点击选择/橙色描边/Ctrl 加选、W/E/R gizmo（Local/World 切换）、F 聚焦、Inspector 数值编辑、Add Component 挂 SpinningCube 调 speed
- **M4**：Ctrl+S 保存/打开 .nova.json 往返无损、Asset Browser 浏览、Explorer 拖 .obj/.fbx/.gltf 导入、重复拖入走缓存
- **M5**：Play/Stop（Ctrl+P）快照恢复、SpinningCube 旋转、Play 相机切换、Console 三色 500 条、Release 构建

## 接口改进候选（后续版本）

- GameObject::removeComponent(Component*)
- AssetDatabase::loadModel 返回材质信息
- Component::enabled 提升到基类
- drawGrid 颜色参数化

## 2026-11-15 功能排查修复记录（Debugger Agent）

- **gizmo 双 bug 已修复**：①ImGuizmo 矩阵直传（不转置）——glm 与 ImGuizmo 同为列主序内存布局，原转置破坏平移并使 gizmo 被静默裁剪（ARCHITECTURE §11.7 原结论有误，已修正）；②gizmo drawlist 改 ImGui::GetWindowDrawList()——前台 drawlist 的 _OwnerName 不匹配导致悬停判定恒假、手柄无法拖拽。验证：自动化点击 + 截屏像素确认 gizmo 三色轴真实绘制，W/E/R/X 切换正常。
- **Explorer 拖拽导入**：链路代码原本完整（回调已接线），端到端 WM_DROPFILES 验证通过。若"没反应"：确认鼠标落在场景视口/资产面板区域内、且不在 Play 态（均有 warn 日志）。
- **Play 旋转**：链路原本正确（每帧 scene->update(dt)），运行时验证 SpinningCube 持续旋转。旧构建可能造成误判；关键路径已加诊断日志。

## 新增缺口（2026-11-15，Docs Writer 通读代码发现）

1. Ctrl+D 复制是浅拷贝：serializeGameObject 不含子树，复制不跟随子物体（Delete 含子树，语义不对称）
2. Play 中 Inspector 仅禁用 Add/Remove 按钮：Name/Active/mesh 路径/贴图路径输入框仍可编辑
3. sceneDirty 只写不读：无标题 *、无未保存确认
4. Play 中 live edit 置脏后 Stop 不还原脏标记（功能无损，标记不准）
5. Add Component 无同类型去重：可重复挂 Camera/MeshRenderer/同名脚本成「僵尸组件」
6. 脚本类名与内置组件名冲突会被序列化误判
7. 外部拖贴图到 SceneView 在 Play 中未禁止（与内部拖贴图路径不一致）
8. Pause 整帧跳过 scene->update()（含 flush；Play 禁结构修改故无实际影响）
9. 场景环境参数（skyColor/ambientColor/showGrid）无编辑器 UI
10. 脚本 C++ 运行时成员（如 OrbitCamera 的 m_angle）不随快照恢复（规格 §7.2 已知）
11. 拾取 AABB 粗测：大平面遮挡后方物体（规格 §6.1 已知）
12. Hierarchy Ctrl+单击是 toggle（可减选），SceneView Ctrl+单击未命中时保留选择——两处 Ctrl 语义不一致
