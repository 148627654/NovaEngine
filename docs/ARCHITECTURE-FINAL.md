# 收官批次设计摘要（2026-11-15 冻结）

范围：透明排序（V0.2-8）、中文路径+P2 修复包（V0.2-14）、环境面板（V0.2-5）、多选（V0.2-15）、吸附（V0.2-12）、游戏分辨率（V0.2-13）、Play 增强（V0.3-10 子集）、.novameta（V0.3-5）、二进制缓存（V0.3-7）、打包 exe（V0.5-1）。不引入新第三方库；schema 一次性 bump 到 version 3（写 3、读 1/2/3）。

## 全局枢纽：AssetStream

- `Assets/AssetStream.h`：mountPak / pakMounted / exists / readAllBytes / readAllText（磁盘走 ifstream path 重载——中文安全；pak 挂载后按相对路径查内存 entry 表）
- 改造点：Shader / TextureLoader / ModelLoader / Cubemap / SceneSerializer 读路径全部收敛到 AssetStream；pak 未挂载时行为与现状逐字节等价

## 关键裁定

1. **中文路径**：`std::ifstream(path)` + `stbi_load_from_memory`（零转换代码，同时服务 pak）
2. **透明队列**：endFrame = stable_partition 分 opaque/transparent 两段；transparent 按 viewDepth 降序（后→前）、GL_BLEND + glDepthMask(false)；drawShadowPass 排除透明物体；blinn_phong.frag +uAlpha
3. **材质序列化**：material 段 + "alpha"（默认 1.0）、"blendMode"（"Opaque"/"AlphaBlend"）
4. **内置图元缓存**：故意泄漏的函数内静态（跳过静态析构，防 GL 上下文时序问题）
5. **导入单解析**：AssetDatabase::importModel 返回网格+材质+贴图路径（Assimp 一次）；loadModel 兼容转发
6. **环境面板**：新 EnvironmentPanel（sky/ambient/showGrid/fogColor + 天空全组），dockRight 与 Inspector 并排 Tab；Inspector 删除 Environment 替身区；fogColor 缺省随 skyColor 联动；drawGrid 雾色参数化
7. **多选**：Inspector 逐轴公共值（不等显示 "(…)"，拖动写绝对值到全部）；gizmo 多选 pivot=世界 AABB 并集中心，delta 应用 D*W 写回（相对位置不变，非均匀缩放 shear 为 v1 已知限制）；结束瞬间单次 markSceneDirty（单步 Undo）
8. **吸附**：ImGuizmo 原生 snap（Translate 0.5m / Rotate 15° / Scale 0.5）；`有效 = snapSettings.enabled || Ctrl 按住`；入口 = Inspector 无选中时 Snap Settings 折叠区（EditorContext 运行期，不序列化）
9. **游戏分辨率**：Play 态 FBO 按目标分辨率 × renderScale 重建，面板内等比 letterbox 居中（单 ImGui::Image）；相机 aspect 用目标分辨率；预设 Free/1920x1080/1280x720/Custom；renderScale 与 MSAA 正交
10. **Play 增强**：Pause 中 F10/菜单 Step 推进一帧（dt=Time::deltaTime）；进入 Play 未保存 → 非阻塞 3 秒 toast；Play 相机 Follow Selected（偏移保持、直接写场景相机 Transform，快照自动还原）
11. **打包**：NovaPlayer 独立 target（NovaEngine + NovaScripts + player/main.cpp ~150 行，无 ImGui——引擎本就无 ImGui 依赖，解耦≈0）；SceneRenderer::renderScene 抽取共用渲染；.novapak 无压缩（zlib 不复用——assimp 内嵌实现不导出）；v1 pak 装原始源文件（不含缓存产物）；缺失资源打包即报错；发布目录 = exe + pak
12. **二进制缓存**：assets/.novacache、FNV-1a 64 内容 hash（贴图拼 meta 文本）、magic "NVBI" + version + payloadSize 校验、损坏即删即重导；格式：Header 48B + MeshData/TextureRGBA8/TextureRGBFloat 三种载荷
13. **.novameta**：JSON 伴随文件（type sRGB/Linear/NormalMap + wrap + filter + mipmaps）；缺失自动生成默认落盘；AssetBrowser 右键编辑；改 meta → 贴图缓存 hash 失效
14. **TextureWrap/TextureFilter 枚举**：定义在 Render/Texture.h（render 侧），TextureMeta 引用之（assets→render 合法方向）——对架构师原方案的修正

## 两波实施

- **Wave 1**（透明/中文路径/环境面板/多选/吸附/分辨率/Play 增强）：A=引擎线（AssetStream、SceneRenderer、Renderer 两段队列、材质/贴图/着色器/加载器/序列化 v3、图元缓存、glfwTerminate）、C=编辑器线（EnvironmentPanel、Inspector 大改、Gizmo 多选+snap、SceneView 分辨率/letterbox/follow/toast、Hierarchy Ctrl 语义、EditorContext/Application 扩展、DragDrop 单解析）
- **Wave 2**（.novameta/二进制缓存/打包）：冻结 TextureMeta/BinaryCache/PakArchive + player/ 工程；A=引擎+Player、C=AssetBrowser meta 编辑 + Packager + Build Player 菜单

## 风险

打包若超期按阶梯裁剪：单场景 --scene（已是）→ 降级「exe + assets 文件夹复制」（AssetStream 磁盘路径天然兼容）→ 无压缩（已裁定）。renderScale 2.0 + MSAA4 显存 ~130MB 文档注明。多选非均匀缩放 shear 为已知限制。
