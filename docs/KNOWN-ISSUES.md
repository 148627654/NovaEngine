# 已知问题与后续里程碑（2026-11-14）

## 当前状态

M1（窗口+渲染+五面板）+ M2（模型渲染+层级）已达成：Debug 构建 0 错误 0 警告，冒烟测试通过。
M3–M5 的功能代码已全部实现，待人工交互验收。

## P2 打磨清单（来自 Reviewer 审查报告）

1. 网格地面雾色硬编码（Renderer.cpp kGridFogColor），与 scene.skyColor 不联动
2. Window 析构未调 glfwTerminate（单窗口无影响）
3. 内置图元（builtin:cube/sphere/plane）无缓存，每次整场景重建重传 GPU 网格
4. TextureLoader 用 ANSI 路径（stb），中文路径贴图加载失败
5. 运行依赖工作目录 = 项目根（VS 调试已配置；双击 exe 直接运行会缺 shader/资产）
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
