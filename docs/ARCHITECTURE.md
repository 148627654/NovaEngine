# NovaEngine 架构设计文档 v1.1（含 Coder B 接口裁定补遗）

> 技术栈：C++20 / OpenGL 4.5 / MSVC (VS2026) / CMake 4.4（Visual Studio 多配置生成器）/ FetchContent
> 本文档是唯一接口契约。Coder 按模块并行实现，修改他人模块接口前必须先更新本文档并说明理由。
> 命名约定：类 PascalCase、方法 camelCase、成员 m_ 前缀、命名空间 NovaEngine（引擎）/ NovaEditor（编辑器）/ NovaGame（示例脚本）。注释用中文。
> 坐标约定：右手系，+Y 向上，+Z 为 forward，单位米；UI 角度用度，存储用四元数。

---

## 1. 完整目录树

```
NovaEngine/
├── CMakeLists.txt                        # 顶层：project、C++20 策略、include(Dependencies)、add_subdirectory(engine/samples/editor)
├── CMakePresets.json                     # VS2026 x64 多配置预设（Debug/Release）
├── .gitignore                            # 忽略 build/、.vs/、*.user
├── cmake/
│   ├── Dependencies.cmake                # 全部 FetchContent 声明 + 第三方裁剪选项（见 §9）
│   └── Common.cmake                      # nova_configure_target() 函数：C++20 + MSVC 警告/编码/并行编译选项
├── docs/
│   ├── AGENTS.md                         # 已存在：多 Agent 流程
│   └── ARCHITECTURE.md                   # 本文档
├── assets/                               # 运行时资源（场景内一律用相对项目根路径引用）
│   ├── shaders/
│   │   ├── blinn_phong.vert              # 主模型顶点着色器（uViewProj/uModel，输出世界法线/UV）
│   │   ├── blinn_phong.frag              # Blinn-Phong 光照（uLightDir/uLightColor/uAmbient + 材质 + 可选贴图）
│   │   ├── grid.vert                     # 网格地面顶点着色器（输出世界坐标）
│   │   ├── grid.frag                     # 网格地面片元（按距离淡出、主次线区分）
│   │   ├── outline.vert                  # 选中描边顶点着色器（沿法线膨胀）
│   │   └── outline.frag                  # 选中描边片元（纯色 uColor）
│   ├── models/                           # 手写小 OBJ 测试素材（cube.obj/sphere.obj/plane.obj，Coder B 编写）
│   ├── textures/                         # 用户拖入的贴图目录（导入时自动拷贝至此）
│   └── scenes/
│       └── demo.nova.json                # 默认示例场景（Cube+灯光+相机+旋转脚本）
├── engine/                               # 静态库 NovaEngine
│   ├── CMakeLists.txt                    # add_library(NovaEngine STATIC ...)，PUBLIC 链接 glfw/glad/glm/assimp/json/stb
│   ├── include/NovaEngine/
│   │   ├── NovaEngine.h                  # 总伞头：include 以下全部子模块头
│   │   ├── Core/
│   │   │   ├── Window.h                  # GLFW 窗口封装（前向声明 struct GLFWwindow，公共头不泄漏 GLFW）
│   │   │   ├── Input.h                   # 键盘/鼠标状态查询（按住/按下/释放边沿、滚轮、鼠标位移）
│   │   │   ├── Time.h                    # deltaTime / 总时间 / 帧计数
│   │   │   ├── Logger.h                  # 分级日志（std::format 风格）+ sink 注册（Console 面板用）
│   │   │   └── UUID.h                    # 64 位随机 ID 生成
│   │   ├── Render/
│   │   │   ├── Renderer.h                # 前向渲染器（单例）：beginFrame/submit/drawGrid/drawOutline/endFrame
│   │   │   ├── Shader.h                  # GLSL 编译/链接 + uniform 设置（带位置缓存）
│   │   │   ├── Mesh.h                    # GPU 网格（VAO/VBO/EBO + draw + AABB）
│   │   │   ├── MeshData.h                # CPU 网格数据（Vertex/MeshData/AABB 结构定义）
│   │   │   ├── Texture.h                 # GPU 纹理（RGBA8、mipmap；含白图/棋盘格工厂）
│   │   │   ├── Material.h                # Blinn-Phong 材质结构体（albedo/roughness/metallic/albedoMap/albedoMapPath）
│   │   │   ├── Framebuffer.h             # 离屏 MSAA FBO + resolve 纹理（SceneView 用）
│   │   │   ├── Light.h                   # DirectionalLightData 纯数据结构（传给 Renderer）
│   │   │   └── Primitives.h              # cube/sphere/plane 图元 Mesh 工厂
│   │   ├── Scene/
│   │   │   ├── GameObject.h              # 对象：id/名字/父子层级/组件容器/销毁队列
│   │   │   ├── Component.h               # 组件基类：类型 ID + onAwake/onStart/onUpdate/onSerialize/onDeserialize
│   │   │   ├── Transform.h               # 局部/世界变换 + 脏标记传播 + 欧拉角便捷接口
│   │   │   ├── Scene.h                   # 场景根：对象管理/按 ID 索引/销毁队列/环境色/网格开关/start()
│   │   │   ├── MeshRenderer.h            # 渲染组件：mesh 资产路径 + Material（序列化用）
│   │   │   ├── Camera.h                  # 相机组件：fov/near/far/clearColor/isMain
│   │   │   └── DirectionalLight.h        # 平行光组件：color/intensity（方向取自 Transform.forward）
│   │   ├── Assets/
│   │   │   ├── AssetDatabase.h           # 资产注册表：相对路径→GPU 资产映射、内存缓存、释放
│   │   │   ├── ModelLoader.h             # Assimp 导入 → MeshData 列表 + 材质/贴图路径提取
│   │   │   └── TextureLoader.h           # stb_image 解码为 RGBA8 像素
│   │   ├── Scripting/
│   │   │   ├── Behaviour.h               # 脚本组件基类（enabled + findScript<T> 便捷模板）
│   │   │   └── ScriptRegistry.h          # 脚本名→工厂注册表 + NE_REGISTER_SCRIPT 宏
│   │   └── Serialization/
│   │       └── SceneSerializer.h         # Scene ↔ nlohmann::json 双向 + 文件读写 + ID 重映射
│   └── src/
│       ├── Core/
│       │   ├── Window.cpp                # GLFW 窗口创建/轮询/回调蹦床（user pointer 分发）
│       │   ├── Input.cpp                 # GLFW 键位映射 + 边沿状态计算
│       │   ├── Time.cpp                  # 帧计时实现
│       │   ├── Logger.cpp                # 默认 sink（stdout + OutputDebugString）
│       │   └── UUID.cpp                  # mt19937_64 随机 ID
│       ├── Render/
│       │   ├── Renderer.cpp              # 渲染队列/排序/绘制、glad 初始化、全局 GL 状态
│       │   ├── Shader.cpp                # 文件读取/编译链接/错误日志
│       │   ├── Mesh.cpp                  # 顶点上传与 VAO 布局
│       │   ├── Texture.cpp               # glTexImage2D/mipmap/采样器
│       │   ├── Material.cpp              # 无状态结构体，预留序列化辅助（可为空实现或并入 Renderer）
│       │   ├── Framebuffer.cpp           # MSAA FBO 创建/重建/blit resolve
│       │   └── Primitives.cpp            # 程序化生成 cube/sphere/plane 顶点
│       ├── Scene/
│       │   ├── GameObject.cpp            # 组件容器/层级/销毁实现
│       │   ├── Component.cpp             # 类型 ID 计数器
│       │   ├── Transform.cpp             # 世界矩阵惰性计算与脏传播
│       │   ├── Scene.cpp                 # 对象索引/遍历/销毁队列/update 与 start 广播
│       │   ├── MeshRenderer.cpp          # mesh 路径反序列化时经 AssetDatabase 解析
│       │   ├── Camera.cpp                # 序列化字段实现
│       │   └── DirectionalLight.cpp      # 序列化字段实现
│       ├── Assets/
│       │   ├── AssetDatabase.cpp         # 缓存表 + 项目根路径管理
│       │   ├── ModelLoader.cpp           # Assimp 场景遍历/网格转换/后处理标志（见 §6.3）
│       │   └── TextureLoader.cpp         # 含 #define STB_IMAGE_IMPLEMENTATION（全项目唯一）
│       ├── Scripting/
│       │   ├── Behaviour.cpp             # 构造（设置组件类型 ID）
│       │   └── ScriptRegistry.cpp        # 注册表实现（函数内静态 map，注册名裁剪为尾段）
│       └── Serialization/
│           └── SceneSerializer.cpp       # 序列化/反序列化/文件读写/组件工厂分发
├── editor/                               # 可执行程序 NovaEditor（控制台子系统，保留 stdout 日志）
│   ├── CMakeLists.txt                    # add_executable + 编译 imgui/imguizmo 源码进本 target（见 §9）
│   └── src/
│       ├── main.cpp                      # 入口：Window→Renderer::init→ImGui 初始化→EditorApplication.run()
│       ├── EditorApplication.h/.cpp      # 应用外壳：主循环编排/docking 布局/Play 状态切换/外部拖放分发/文件菜单
│       ├── EditorContext.h/.cpp          # 编辑器全局状态：选中集/Edit-Play 状态/场景指针/快照/脏标记
│       ├── EditorCamera.h/.cpp           # 轨道相机：右键旋转/WASD 平移/滚轮缩放/F 聚焦/屏幕射线
│       ├── Panel.h                       # 面板抽象基类（onGui 纯虚函数）
│       ├── panels/
│       │   ├── SceneViewPanel.h/.cpp     # 视口：FBO 渲染+ImGui::Image+拾取选择+gizmo+拖放接受
│       │   ├── HierarchyPanel.h/.cpp     # 层级树：增删改/重命名/拖拽重父子/右键菜单
│       │   ├── InspectorPanel.h/.cpp     # 属性编辑：Transform/材质/组件增删/脚本字段（序列化往返编辑）
│       │   ├── ConsolePanel.h/.cpp       # 日志显示（Logger sink 实现）+ 清除按钮
│       │   └── AssetBrowserPanel.h/.cpp  # assets/ 文件树导航 + 拖拽源 + 双击加载场景
│       ├── GizmoController.h/.cpp        # ImGuizmo 封装：操作/坐标空间/矩阵写回 Transform
│       └── DragDrop.h/.cpp               # payload 常量 + 文件类型判断 + 导入管线 + 复制进 assets
└── samples/                              # 可选示例：游戏脚本静态库 NovaScripts（OBJECT 库）
    ├── CMakeLists.txt                    # add_library(NovaScripts OBJECT ...)
    ├── SpinningCube.h/.cpp               # 示例脚本：绕 Y 轴匀速旋转立方体（M5 验收用）
    └── OrbitCamera.h/.cpp                # 可选示例：相机环绕脚本（Play 模式演示）
```

---

## 2. 核心类与公开接口

### 2.1 模块边界与依赖关系

七个模块，依赖方向严格单向，无环：

```
                    ┌──────────┐
                    │   core   │  Window/Input/Time/Logger/UUID   （仅依赖 GLFW/GLM/std）
                    └────┬─────┘
              ┌──────────┼──────────────┐
              ▼          ▼              ▼
         ┌────────┐ ┌────────┐   ┌────────────┐
         │ render │ │ scene  │──▶│  render    │  （单向：MeshRenderer 仅持有 shared_ptr<Mesh>）
         └───┬────┘ └───┬────┘   └────────────┘
             │          │
         ┌───▼────┐ ┌───▼──────┐ ┌──────────────┐
         │ assets │ │ scripting │ │serialization │──▶ scene + scripting + assets
         └────────┘ └──────────┘ └──────────────┘
                    （assets → render + core；scripting → scene + core）
        editor（可执行，无公共头）──▶ 上述全部 + imgui + imguizmo
        samples（NovaScripts OBJECT 库）──▶ 上述全部
```

规则：
- render 模块永远不 include scene 头。Renderer 不遍历场景；由 editor 遍历场景调用 submit()（editor 是粘合层）。
- scene 模块只单向 include render 的 Mesh/Material 类型（shared_ptr 引用，无 GL 句柄暴露）。
- scene 的 .cpp 允许包含 Scripting/Behaviour.h（enabled 检查用，受控文本依赖特例，无链接环）；scene 的公共头禁止包含 scripting 头。
- 公共头（engine/include/NovaEngine）不含任何 GL/GLFW include：GL 句柄一律 uint32_t，GLFWwindow 前向声明。glad/GLFW 只在 .cpp 中包含。
- 编辑器内部类（面板、相机、gizmo）无冻结要求，Coder C 自由设计，但必须消费冻结的引擎接口。

### 2.2 接口冻结顺序与 3 Coder 并行分工

冻结 = 写入本文件、评审通过后任何人不得擅自修改（修改须更新本文件并说明）。

| 阶段 | 内容 | 负责人 | 时间 |
|---|---|---|---|
| 冻结批 1（第 0–1 天） | Core 全部头 + Render 全部头 + Scene 全部头 | 架构师执笔，三人评审 | 编码开始前 |
| 冻结批 2（第 1–2 天） | Assets + Scripting + Serialization 头 | 同上 | A/B 实现 core 的同时 |

**Coder A — core + render（引擎底座）**：实现 Core 全部 5 类 + Render 全部 8 类 + assets/shaders 下全部 GLSL。另负责立即创建顶层 CMakeLists.txt + cmake/{Dependencies,Common}.cmake + engine/CMakeLists.txt（A 是首个需要构建验证的人）。交付物：窗口能出画面。不碰 scene/editor 代码。

**Coder B — scene + serialization + scripting + samples**：实现 Scene 模块 7 类 + SceneSerializer + Behaviour/ScriptRegistry + 手写 cube.obj/sphere.obj/plane.obj + demo.nova.json + SpinningCube 示例 + samples/CMakeLists.txt。编译期仅依赖 A 的冻结头（shared_ptr<Mesh> 而已），可与 A 完全并行；链接期才需要 A 的实现。

**Coder C — assets + editor（应用层）**：实现 AssetDatabase/ModelLoader/TextureLoader + editor 全部文件（main.cpp、面板、相机、gizmo、拖放）+ editor/CMakeLists.txt。依赖所有冻结头；链接依赖 A、B 实现。C 的编辑器代码无外部接口，只有消费。

依赖与协作点：
- A 依赖 B/C：无。
- B 依赖 A：仅冻结头（UUID、Mesh/Material）。
- C 依赖 A（Window/Renderer/Framebuffer 实现）+ B（Scene/序列化实现）。C 在 M1 先写 editor 壳（main 循环 + docking 空面板 + 硬编码三角形调用 A 的接口）。
- 集成顺序：每里程碑结束三人合并构建一次（M1 由 A+C 联调；M2 起全员联调）。

### 2.3 Core 接口

```cpp
// engine/include/NovaEngine/Core/Window.h
namespace NovaEngine {
struct GLFWwindow;  // 前向声明，避免公共头泄漏 GLFW

class Window {
public:
    static std::unique_ptr<Window> create(const std::string& title, int width, int height);
    ~Window();
    bool shouldClose() const;
    void pollEvents();                              // 内部 glfwPollEvents + 分发回调
    void swapBuffers();
    void setVsync(bool enabled);
    void getFramebufferSize(int& outWidth, int& outHeight) const;
    GLFWwindow* nativeHandle() const;
    using DropCallback = std::function<void(const std::vector<std::filesystem::path>&)>;
    void setDropCallback(DropCallback cb);          // 外部文件拖入窗口（Explorer 拖拽）
    void setResizeCallback(std::function<void(int, int)> cb);
private:
    Window() = default;
    GLFWwindow* m_handle = nullptr;
    DropCallback m_dropCb;
    std::function<void(int, int)> m_resizeCb;
    // GLFW C 回调通过 glfwSetWindowUserPointer(this) + static 蹦床分发
};
}
```

```cpp
// engine/include/NovaEngine/Core/Input.h
namespace NovaEngine {
// 值域与 GLFW 键码一致（Input.cpp 内 static_assert 校验），编辑器常用键全覆盖：
// A..Z、0..9、F1..F12、Esc、Enter、Tab、Space、Backspace、Delete、方向键、左右 Shift/Ctrl/Alt
enum class Key : int32_t { Unknown = -1, A = 65, B, C, /* ... 见 GLFW 键码 ... */ };
enum class MouseButton : int32_t { Left = 0, Right = 1, Middle = 2 };

class Input {
public:
    static void beginFrame();                       // 每帧一次：快照边沿状态（须在 pollEvents 之后调用）
    static bool isKeyDown(Key key);                 // 按住
    static bool isKeyPressed(Key key);              // 本帧刚按下
    static bool isKeyReleased(Key key);             // 本帧刚松开
    static bool isMouseDown(MouseButton button);
    static bool isMousePressed(MouseButton button);
    static bool isMouseReleased(MouseButton button);
    static glm::vec2 mousePosition();               // 窗口逻辑坐标（左上原点）
    static glm::vec2 mouseDelta();
    static glm::vec2 scrollDelta();
};
}
```

```cpp
// engine/include/NovaEngine/Core/Time.h
namespace NovaEngine {
class Time {
public:
    static void beginFrame();                       // 计算 deltaTime，上限钳制 0.1s
    static float deltaTime();                       // 秒
    static float totalTime();                       // 启动以来秒数
    static uint64_t frameCount();
};
}
```

```cpp
// engine/include/NovaEngine/Core/Logger.h
namespace NovaEngine {
enum class LogLevel { Debug, Info, Warn, Error };

class Logger {
public:
    using Sink = std::function<void(LogLevel, const std::string&)>;
    static void addSink(Sink sink);                 // ConsolePanel 注册
    static void clearSinks();
    template<typename... Args> static void info(std::format_string<Args...> fmt, Args&&... args);
    template<typename... Args> static void warn(std::format_string<Args...> fmt, Args&&... args);
    template<typename... Args> static void error(std::format_string<Args...> fmt, Args&&... args);
private:
    static void log(LogLevel level, const std::string& msg);  // 写 stdout + OutputDebugString + 全部 sink
};
}
```

```cpp
// engine/include/NovaEngine/Core/UUID.h
namespace NovaEngine {
class UUID {
public:
    static constexpr uint64_t kInvalid = 0;         // 保留值：表示「无父级」
    static uint64_t generate();                     // mt19937_64 随机 64 位，保证非 0
};
}
```

### 2.4 Render 接口

```cpp
// engine/include/NovaEngine/Render/MeshData.h
namespace NovaEngine {
struct Vertex { glm::vec3 position; glm::vec3 normal; glm::vec2 uv; glm::vec3 tangent; };
struct AABB {
    glm::vec3 min = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 max = glm::vec3(-std::numeric_limits<float>::max());
};
struct MeshData { std::vector<Vertex> vertices; std::vector<uint32_t> indices; AABB aabb; std::string name; };
}
```

```cpp
// engine/include/NovaEngine/Render/Mesh.h
namespace NovaEngine {
class Mesh {
public:
    static std::shared_ptr<Mesh> create(const MeshData& data);   // 上传 VBO/EBO，构建 VAO
    void draw() const;                       // bindVertexArray + glDrawElements(GL_TRIANGLES)
    const AABB& aabb() const;                // 拾取与聚焦用
private:
    uint32_t m_vao = 0, m_vbo = 0, m_ebo = 0;
    uint32_t m_indexCount = 0;
    AABB m_aabb;
};
}
```

```cpp
// engine/include/NovaEngine/Render/Shader.h
namespace NovaEngine {
class Shader {
public:
    static std::shared_ptr<Shader> createFromFiles(const std::filesystem::path& vsPath,
                                                   const std::filesystem::path& fsPath);  // 失败返回 nullptr 并 Logger::error 编译日志
    ~Shader();
    void bind() const;
    void setInt(const std::string& name, int v);
    void setFloat(const std::string& name, float v);
    void setVec3(const std::string& name, const glm::vec3& v);
    void setVec4(const std::string& name, const glm::vec4& v);
    void setMat4(const std::string& name, const glm::mat4& v);
    uint32_t handle() const;
private:
    uint32_t m_program = 0;
    std::unordered_map<std::string, int32_t> m_uniformCache;   // 值未变不重复 glUniform
};
}
```

```cpp
// engine/include/NovaEngine/Render/Texture.h
namespace NovaEngine {
class Texture {
public:
    static std::shared_ptr<Texture> createWhite();                       // 1x1 白图（默认贴图）
    static std::shared_ptr<Texture> createCheckerboard(int cells = 8,
        glm::vec4 colorA = glm::vec4(1.0f), glm::vec4 colorB = glm::vec4(0.3f, 0.3f, 0.3f, 1.0f));
    static std::shared_ptr<Texture> createFromFile(const std::filesystem::path& path, bool srgb = false);
    void bind(uint32_t slot) const;                                      // glActiveTexture + glBindTexture
    int width() const; int height() const;
    uint32_t handle() const;
private:
    uint32_t m_id = 0; int m_width = 0, m_height = 0;
};
}
```

```cpp
// engine/include/NovaEngine/Render/Material.h
namespace NovaEngine {
struct Material {
    glm::vec3 albedo = glm::vec3(0.8f);
    float roughness = 0.6f;
    float metallic = 0.0f;
    std::shared_ptr<Texture> albedoMap = nullptr;    // nullptr = 纯色
    std::string albedoMapPath;                       // 资产相对路径（序列化用；render 忽略此字符串）
};
}
```

```cpp
// engine/include/NovaEngine/Render/Light.h
namespace NovaEngine {
struct DirectionalLightData {
    glm::vec3 direction = glm::normalize(glm::vec3(0.5f, -1.0f, -0.3f));  // 指向光源
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    glm::vec3 ambient = glm::vec3(0.12f, 0.12f, 0.15f);
};
}
```

```cpp
// engine/include/NovaEngine/Render/Framebuffer.h
namespace NovaEngine {
class Framebuffer {
public:
    bool create(int width, int height, int samples = 4);   // MSAA 彩色+深度模板 + resolve 纹理
    void destroy();
    void resize(int width, int height);                    // 尺寸不变则 no-op；全删重建（简单可靠）
    void bind() const;                                     // 绑定并 glViewport(0,0,w,h)
    void unbind() const;                                   // 恢复默认 FBO
    void resolveMsaa() const;                              // glBlitFramebuffer 到 resolve 纹理（bind 状态下调用）
    uint32_t colorTextureId() const;                       // 供 ImGui::Image 使用
    int width() const; int height() const;
private:
    uint32_t m_fbo = 0, m_msaaColor = 0, m_depthStencil = 0, m_resolveFbo = 0, m_resolveTex = 0;
    int m_width = 0, m_height = 0, m_samples = 4;
};
}
```

```cpp
// engine/include/NovaEngine/Render/Renderer.h
namespace NovaEngine {
struct RenderCamera { glm::mat4 view; glm::mat4 projection; glm::vec3 position; };

class Renderer {
public:
    static void init();       // gladLoadGL(glfwGetProcAddress) + 全局状态（深度测试/背面剔除/UBO）
    static void shutdown();
    static void beginFrame(const RenderCamera& camera, const DirectionalLightData& light,
                           const glm::vec4& clearColor);   // 清屏 + 填充每帧 UBO
    static void submit(std::shared_ptr<Mesh> mesh, const Material& material,
                       const glm::mat4& modelMatrix);       // 入队，不立即绘制
    static void drawGrid();                                 // 内置网格着色器（约 40x40m，主次线）
    static void drawOutline(std::shared_ptr<Mesh> mesh, const glm::mat4& modelMatrix,
                            const glm::vec3& color);        // stencil 两遍描边，立即绘制（见 §3.4）
    static void endFrame();      // 按 (shader, texture) 排序后统一绘制队列并清空
private:
    struct RenderItem { std::shared_ptr<Mesh> mesh; Material material; glm::mat4 model; };
    static std::vector<RenderItem> s_queue;                 // 实现于 Renderer.cpp 的匿名命名空间
};
}
```

```cpp
// engine/include/NovaEngine/Render/Primitives.h
namespace NovaEngine {
class Primitives {
public:
    static std::shared_ptr<Mesh> cube();                    // 1x1x1
    static std::shared_ptr<Mesh> sphere(int segments = 32);
    static std::shared_ptr<Mesh> plane(float size = 10.0f);
};
}
```

着色器 uniform 约定（Coder A 写 GLSL 时遵守，绑定点固定）：
- UBO `CameraBlock`（binding 0）：`mat4 uViewProj; vec4 uCamPos;`
- blinn_phong.frag：`vec3 uLightDir; vec3 uLightColor; vec3 uAmbient; vec3 uAlbedo; float uRoughness; float uMetallic; int uUseAlbedoMap; sampler2D uAlbedoMap;`（unit 0）
- outline：`uViewProj + uColor`；grid：`uViewProj + uCamPos + uFogColor + uLineColor`

### 2.5 Scene 接口

```cpp
// engine/include/NovaEngine/Scene/Component.h
namespace NovaEngine {
class GameObject;
class Transform;

class Component {
public:
    explicit Component(GameObject* owner);
    virtual ~Component() = default;
    GameObject* gameObject() const;
    Transform* transform() const;               // owner->getComponent<Transform>() 便捷
    uint32_t typeId() const;
    // 生命周期（默认空实现）
    virtual void onAwake() {}                   // 挂载后立即调用（attach 时）
    virtual void onStart() {}                   // 进入 Play 时调用一次
    virtual void onUpdate(float dt) {}          // Play 每帧调用
    // 序列化扩展字段（公共字段由 SceneSerializer 处理，子类只写自己独有的）
    virtual void onSerialize(nlohmann::json& out) const {}
    virtual void onDeserialize(const nlohmann::json& in) {}
protected:
    template<typename T> static uint32_t componentTypeId() {
        static uint32_t id = s_nextTypeId.fetch_add(1); return id;
    }
private:
    GameObject* m_owner = nullptr;
    uint32_t m_typeId = 0;
    inline static std::atomic<uint32_t> s_nextTypeId{1};
    friend class GameObject;
};
}
```

```cpp
// engine/include/NovaEngine/Scene/GameObject.h
namespace NovaEngine {
class GameObject {
public:
    GameObject(uint64_t id, const std::string& name);
    ~GameObject();
    uint64_t id() const;
    const std::string& name() const;
    void setName(const std::string& name);
    bool activeSelf() const;
    void setActiveSelf(bool active);
    bool activeInHierarchy() const;             // 自身 && 所有祖先 activeSelf
    // 层级
    GameObject* parent() const;
    void setParent(GameObject* newParent, bool keepWorldTransform = true);
    const std::vector<std::unique_ptr<GameObject>>& children() const;
    // 组件
    template<typename T, typename... Args> T* addComponent(Args&&... args);   // 同类型已存在则 assert
    void attachComponent(std::unique_ptr<Component> comp, uint32_t typeId, bool callAwake = true);
    template<typename T> T* getComponent();                                  // 按 typeId 线性扫描
    template<typename T> bool hasComponent();
    template<typename T> void removeComponent();
    template<typename T> std::vector<T*> getComponents();                     // 含派生类
    Transform* transform() const;
    // 其它
    Scene* scene() const;
    void destroy();                              // 标记销毁（含子树），Scene 帧末真正删除
private:
    uint64_t m_id = 0;
    std::string m_name;
    bool m_activeSelf = true;
    GameObject* m_parent = nullptr;
    std::vector<std::unique_ptr<GameObject>> m_children;
    std::vector<std::unique_ptr<Component>> m_components;
    Scene* m_scene = nullptr;
    friend class Scene;
};
}
```

addComponent 实现规范（防 UB，见补遗 #2）：
```cpp
template<typename T, typename... Args> T* addComponent(Args&&... args) {
    static_assert(std::is_base_of_v<Component, T>);
    std::unique_ptr<Component> comp = std::make_unique<T>(this, std::forward<Args>(args)...);
    const uint32_t typeId = comp->typeId();      // 必须在 move 之前读取
    T* raw = static_cast<T*>(comp.get());
    attachComponent(std::move(comp), typeId, true);
    return raw;
}
```

```cpp
// engine/include/NovaEngine/Scene/Transform.h
namespace NovaEngine {
class Transform : public Component {
public:
    // 局部 TRS（相对父级）
    const glm::vec3& localPosition() const;  void setLocalPosition(const glm::vec3& v);
    const glm::quat& localRotation() const;  void setLocalRotation(const glm::quat& q);
    const glm::vec3& localScale() const;     void setLocalScale(const glm::vec3& v);
    glm::vec3 localEulerAngles() const;      void setLocalEulerAngles(const glm::vec3& eulerDeg);
    const glm::mat4& localMatrix() const;
    // 世界（惰性计算 + 脏标记）
    const glm::mat4& worldMatrix() const;
    glm::vec3 worldPosition() const;
    glm::quat worldRotation() const;
    // 世界方向（forward = +Z，right = +X，up = +Y）
    glm::vec3 right() const; glm::vec3 up() const; glm::vec3 forward() const;
    // 便捷操作（脚本常用）
    void translate(const glm::vec3& worldDelta);
    void rotate(const glm::vec3& eulerDeg);   // 绕自身轴累计旋转
    void lookAt(const glm::vec3& worldTarget);
private:
    glm::vec3 m_position{0.0f};
    glm::quat m_rotation = glm::identity<glm::quat>();
    glm::vec3 m_scale{1.0f};
    mutable glm::mat4 m_world{1.0f};
    mutable bool m_worldDirty = true;         // setParent 时由 GameObject 递归置脏
    friend class GameObject;
};
}
```

```cpp
// engine/include/NovaEngine/Scene/Scene.h
namespace NovaEngine {
class Scene {
public:
    explicit Scene(const std::string& name = "Untitled");
    const std::string& name() const;
    void setName(const std::string& name);
    // 对象管理
    GameObject* createGameObject(const std::string& name = "GameObject",
                                 GameObject* parent = nullptr);   // 自动附带 Transform
    GameObject* instantiate(uint64_t id, const std::string& name); // 反序列化用：不自动加 Transform
    void destroyGameObject(uint64_t id);        // 入销毁队列
    void flushDestroyQueue();                    // Edit/Play 每帧都调用
    GameObject* findGameObject(uint64_t id) const;
    std::vector<GameObject*> findGameObjectsByName(const std::string& name) const;
    std::vector<GameObject*> rootObjects() const;
    void forEachObject(const std::function<void(GameObject*)>& fn) const;  // 深度优先
    template<typename T> std::vector<T*> findComponents() const;           // 遍历所有 GO 过滤 typeId
    void start();                                // 进入 Play：深度优先广播 onStart（跳过非激活子树与 enabled=false 的 Behaviour）
    void update(float dt);                       // Play 每帧：flush + 广播 onUpdate（同样跳过非激活子树与 disabled Behaviour）
    // 环境（SceneSerializer 直接读写）
    glm::vec3 skyColor = glm::vec3(0.18f, 0.22f, 0.35f);
    glm::vec3 ambientColor = glm::vec3(0.15f, 0.15f, 0.18f);
    bool showGrid = true;
private:
    std::string m_name;
    std::vector<std::unique_ptr<GameObject>> m_rootObjects;
    std::unordered_map<uint64_t, GameObject*> m_objectById;   // O(1) 查找
    std::vector<uint64_t> m_destroyQueue;                      // 避免 update 中迭代器失效
};
}
```

```cpp
// engine/include/NovaEngine/Scene/MeshRenderer.h
namespace NovaEngine {
class MeshRenderer : public Component {
public:
    std::shared_ptr<Mesh> mesh = nullptr;        // 运行时 GPU 网格
    std::string meshPath;                        // 资产相对路径（如 "assets/models/cube.obj"，序列化用）
    Material material;
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;   // 经 AssetDatabase::getMesh 解析
};
}

// engine/include/NovaEngine/Scene/Camera.h
class Camera : public Component {
public:
    float fov = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    glm::vec4 clearColor = glm::vec4(0.18f, 0.22f, 0.35f, 1.0f);
    bool isMain = false;                         // Play 时优先使用
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

// engine/include/NovaEngine/Scene/DirectionalLight.h
class DirectionalLight : public Component {
public:
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
    // 世界方向 = transform()->forward()
};
```

### 2.6 Assets 接口

```cpp
// engine/include/NovaEngine/Assets/AssetDatabase.h
namespace NovaEngine {
class AssetDatabase {
public:
    static AssetDatabase& instance();
    static void setProjectRoot(const std::filesystem::path& root);   // 编辑器启动时调用一次
    static std::filesystem::path projectRoot();
    std::shared_ptr<Mesh> getMesh(const std::filesystem::path& relativePath);    // 单网格（缓存未命中自动加载）
    std::vector<std::shared_ptr<Mesh>> loadModel(const std::filesystem::path& relativePath); // 多子网格模型
    std::shared_ptr<Texture> getTexture(const std::filesystem::path& relativePath, bool srgb = false);
    void clearCache();       // 菜单 Reload Assets（修改文件后手动刷新，M5 前不做 mtime 自动重载）
    void releaseUnused();
private:
    std::unordered_map<std::string, std::weak_ptr<Mesh>> m_meshCache;      // key = canonical 绝对路径
    std::unordered_map<std::string, std::weak_ptr<Texture>> m_textureCache;
};
}
```

```cpp
// engine/include/NovaEngine/Assets/ModelLoader.h
namespace NovaEngine {
struct ModelImportResult {
    std::vector<MeshData> meshes;                       // 每个 aiMesh 一个
    std::vector<Material> materials;                    // 与 meshes 一一对应（第 i 个子网格用第 i 个材质）
    std::vector<std::filesystem::path> albedoMapPaths;  // 可为空路径
};
class ModelLoader {
public:
    static ModelImportResult load(const std::filesystem::path& absolutePath);  // 失败时 meshes 为空 + Logger::error
private:
    static MeshData convertAiMesh(const aiMesh* src);
    static void extractAiMaterial(const aiMaterial* src, Material& out, std::filesystem::path& outAlbedoPath);
};
}

// engine/include/NovaEngine/Assets/TextureLoader.h
namespace NovaEngine {
class TextureLoader {
public:
    // 解码为 RGBA8（stb 自动翻转 false：配合 Assimp FlipUVs，避免双翻转）；失败返回 false
    static bool loadRgba8(const std::filesystem::path& absolutePath,
                          int& outWidth, int& outHeight, std::vector<uint8_t>& outPixels);
};
}
```

### 2.7 Scripting 接口

```cpp
// engine/include/NovaEngine/Scripting/Behaviour.h
namespace NovaEngine {
class Behaviour : public Component {
public:
    explicit Behaviour(GameObject* owner);
    bool enabled = true;
    // onStart/onUpdate 继承自 Component，子类直接 override（见 §8 示例）
};

// 脚本查找便捷模板（脚本共用 Behaviour::typeId，getComponent<具体脚本T>() 不保证命中）
template<typename T> T* findScript(GameObject* go) {
    static_assert(std::is_base_of_v<Behaviour, T>);
    for (auto* c : go->getComponents<Behaviour>())
        if (auto* s = dynamic_cast<T*>(c)) return s;
    return nullptr;
}
}

// engine/include/NovaEngine/Scripting/ScriptRegistry.h
namespace NovaEngine {
class ScriptRegistry {
public:
    using ScriptFactory = std::function<std::unique_ptr<Behaviour>(GameObject*)>;
    static void registerScript(const std::string& name, ScriptFactory factory);
    // 注册名规范化：保留最后一个 "::" 之后的尾段（"NovaGame::SpinningCube" → "SpinningCube"）
    static std::unique_ptr<Behaviour> create(const std::string& name, GameObject* owner);  // 未知返回 nullptr
    static bool exists(const std::string& name);
    static std::vector<std::string> allNames();     // Inspector「Add Component」下拉列表用
private:
    static std::unordered_map<std::string, ScriptFactory>& factories();   // 函数内静态，避免初始化顺序问题
};
}

// 脚本 .cpp 文件末尾放置（注册宏）：
#define NE_REGISTER_SCRIPT(ClassName) \
    static const bool ClassName##_s_registered = \
        (::NovaEngine::ScriptRegistry::registerScript(#ClassName, \
            [](::NovaEngine::GameObject* go) { return std::make_unique<ClassName>(go); }), true)
```

### 2.8 Serialization 接口

```cpp
// engine/include/NovaEngine/Serialization/SceneSerializer.h
namespace NovaEngine {
class SceneSerializer {
public:
    static std::string serialize(const Scene& scene);                 // → JSON 字符串（Play 快照也用它）
    static std::string serializeGameObject(GameObject& object);       // 复制/剪贴板用
    static void deserialize(Scene& target, const std::string& json);  // 清空并按 JSON 重建 target（对象身份不变）
    static bool saveToFile(const Scene& scene, const std::filesystem::path& path);
    static bool loadFromFile(Scene& target, const std::filesystem::path& path);
    // 复制对象：递归换新 ID（编辑器 Duplicate 用）
    static void remapGameObjectIds(nlohmann::json& objectJson);
private:
    static std::unique_ptr<Component> createComponentFromJson(GameObject* owner, const nlohmann::json& j);
    // 内置组件按名字分发：Transform/MeshRenderer/Camera/DirectionalLight；其余走 ScriptRegistry
};
}
```

### 2.9 Editor 骨架接口（Coder C 自由实现，仅给骨架）

```cpp
// editor/src/EditorApplication.h
namespace NovaEditor {
class EditorApplication {
public:
    int run();
private:
    void init();                                   // ImGui/docking/面板/默认场景/字体
    void shutdown();
    void update(float dt);                         // 见 §3.1 伪代码
    void render();
    void handleExternalDrop(const std::vector<std::filesystem::path>& paths);
    void enterPlayMode(); void exitPlayMode();
    void newScene(); void openScene(const std::filesystem::path& p); void saveScene(); void saveSceneAs();
    std::unique_ptr<NovaEngine::Window> m_window;
    std::unique_ptr<NovaEngine::Scene> m_scene;
    EditorContext m_context;
    EditorCamera m_camera;
    std::vector<std::unique_ptr<Panel>> m_panels;
    GizmoController m_gizmo;
    std::vector<std::filesystem::path> m_pendingDrops;   // GLFW 拖放回调写入，update 中消费
};
}

// editor/src/EditorContext.h
class EditorContext {
public:
    enum class EditorState { Edit, Play };
    EditorState state = EditorState::Edit;
    bool paused = false;                            // Play 内暂停
    NovaEngine::Scene* scene = nullptr;
    std::unordered_set<uint64_t> selectedIds;
    NovaEngine::GameObject* activeObject();         // 恰好单选时返回，否则 nullptr
    void setSelected(uint64_t id);
    void toggleSelected(uint64_t id);               // Ctrl 加选
    void clearSelection();
    bool isSelected(uint64_t id) const;
    std::filesystem::path currentScenePath;         // 空 = 未保存
    bool sceneDirty = false;
    std::string playSnapshot;                       // 进入 Play 的 JSON 快照
    // 面板每帧上报自身屏幕区域（外部拖放目标判定用）
    ImVec2 viewportScreenMin, viewportScreenMax;    // SceneView 区域
    ImVec2 assetBrowserScreenMin, assetBrowserScreenMax;
};

// editor/src/EditorCamera.h
class EditorCamera {
public:
    void update(float dt, const glm::vec2& viewportPos, const glm::vec2& viewportSize);
    void focus(const glm::vec3& target, float distance);   // F 聚焦
    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float aspect) const;
    glm::vec3 position() const;
    std::pair<glm::vec3, glm::vec3> screenPointToRay(const glm::vec2& mousePos,
        const glm::vec2& viewportPos, const glm::vec2& viewportSize) const;  // (origin, direction)
    float fov = 60.0f, nearPlane = 0.1f, farPlane = 1000.0f;
    float distance = 8.0f, yaw = 0.0f, pitch = -0.35f;
    glm::vec3 target = glm::vec3(0.0f, 1.0f, 0.0f);
private:
    // 输入：右键拖拽旋转、中键/WASD 平移、滚轮缩放（滚轮速度按 distance 缩放）
};

// editor/src/Panel.h
class Panel {
public:
    explicit Panel(const std::string& name);
    virtual ~Panel() = default;
    virtual void onGui(EditorContext& ctx, EditorCamera& camera) = 0;
    const std::string& name() const;
    bool open = true;
protected:
    std::string m_name;
};

// editor/src/GizmoController.h
class GizmoController {
public:
    enum class Operation { Translate, Rotate, Scale };
    enum class Space { Local, World };
    void update(EditorContext& ctx, const glm::vec2& viewportPos, const glm::vec2& viewportSize);
    bool isUsing() const;      // gizmo 正在拖拽（抑制相机与选择）
    bool isOver() const;       // 鼠标悬停 gizmo（抑制点击选择）
    void cycleOperation();     // W/E/R
    void toggleSpace();
    Operation operation() const; Space space() const;
private:
    Operation m_operation = Operation::Translate;
    Space m_space = Space::World;
    bool m_using = false, m_over = false;
};

// editor/src/DragDrop.h
namespace NovaEditor::DragDrop {
inline constexpr const char* kAssetPayload     = "NOVA_ASSET_PATH";   // 字符串：绝对路径
inline constexpr const char* kHierarchyPayload = "NOVA_GO_ID";        // uint64（ImGui ID）
bool isModelFile(const std::filesystem::path& p);     // .obj .fbx .gltf .glb .dae
bool isTextureFile(const std::filesystem::path& p);   // .png .jpg .jpeg .bmp .tga
bool isSceneFile(const std::filesystem::path& p);     // .nova.json
std::filesystem::path copyIntoAssets(const std::filesystem::path& sourcePath);  // 返回项目内相对路径
NovaEngine::GameObject* importModelToScene(NovaEngine::Scene& scene,
    const std::filesystem::path& sourcePath, const glm::vec3& spawnPosition);
NovaEngine::GameObject* duplicateGameObject(NovaEngine::Scene& scene, NovaEngine::GameObject* source);
}
```

---

## 3. 渲染架构

### 3.1 主循环流程（伪代码）

```
main()（editor/src/main.cpp）:
  1. Window::create("NovaEditor", 1600, 900)
  2. Renderer::init()                          // gladLoadGL(glfwGetProcAddress)，必须在窗口上下文创建后
  3. AssetDatabase::setProjectRoot(项目根)
  4. ImGui::CreateContext()
     io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // ViewportsEnable 默认关（§11）
     ImGui::StyleColorsDark(); 加载中文字体（C:/Windows/Fonts/msyh.ttc 与默认字体合并，失败则用默认）
  5. ImGui_ImplGlfw_InitForOpenGL(window, true); ImGui_ImplOpenGL3_Init("#version 450")
  6. EditorApplication.init()：五面板 + 加载 assets/scenes/demo.nova.json（不存在则建空场景）
  loop:
     window->pollEvents()                       // GLFW 事件 → Input 原始状态 + 拖放回调队列
     Time::beginFrame(); Input::beginFrame()
     app.update(Time::deltaTime())
     app.render()
     window->swapBuffers()

app.update(dt):
  ImGui_ImplGlfw_NewFrame(); ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
  ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());   // 首帧 DockBuilder 摆五面板（§6.5）
  if (state == Play) { if (!paused) scene->update(dt); } else { scene->flushDestroyQueue(); }
  主菜单栏（File: New/Open/Save/Save As；Edit: Play(Ctrl+P)/Pause/Stop；View: Reload Assets）
  for (panel : m_panels) panel->onGui(m_context, m_camera);    // SceneView 在其 onGui 内完成整帧 3D 渲染
  handleExternalDrop(m_pendingDrops); m_pendingDrops.clear();
  if (选中集含已销毁对象) 清理选中集
  ImGui::Render();

app.render():
  glViewport(0, 0, 窗口 framebuffer 尺寸)
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
```

### 3.2 SceneView 渲染到 ImGui 窗口（Framebuffer + ImGui::Image）

SceneViewPanel 持有一个 Framebuffer（MSAA 4x + resolve 纹理）。onGui 流程：

```
ImGui::Begin("Scene")
  vpPos  = 内容区左上角（GetWindowPos + GetCursorScreenPos）
  vpSize = ImGui::GetContentRegionAvail()
  if (vpSize.x > 0 && vpSize.y > 0 && vpSize != m_fbo 当前尺寸):
      m_fbo.resize((int)vpSize.x, (int)vpSize.y)      // 内部全删重建
  if (vpSize 有效):
      m_fbo.bind()
        Renderer::beginFrame(camera, light, clearColor)   // camera 见 §7；clearColor = scene.skyColor
        if (scene->showGrid) Renderer::drawGrid();
        for (go : scene->forEachObject):
            if (go->activeInHierarchy() && mr = go->getComponent<MeshRenderer>() && mr->mesh)
                Renderer::submit(mr->mesh, mr->material, go->transform()->worldMatrix())
        for (id : ctx.selectedIds):                       // 选中高亮（§3.4）
            if (go && mr) Renderer::drawOutline(mr->mesh, go->transform()->worldMatrix(), 橙色)
        Renderer::endFrame()
        m_fbo.resolveMsaa()
      m_fbo.unbind()
      ImGui::Image((ImTextureID)(intptr_t)m_fbo.colorTextureId(), vpSize,
                   ImVec2(0, 1), ImVec2(1, 0))            // UV 翻转：FBO 纹理相对 ImGui 是上下颠倒的
      bool hovered = ImGui::IsItemHovered()
      // 交互层（顺序重要）
      if (state == Edit && 单选) gizmo.update(ctx, vpPos, vpSize)   // ImGuizmo 绘制进前景 drawlist，覆盖在 Image 之上
      if (state == Edit && !gizmo.isUsing()) m_camera.update(dt, vpPos, vpSize)
      if (hovered && 左键点击 && !gizmo.isOver()) 射线拾取 → 选择（§6.1）
      if (hovered && Input::isKeyPressed(F)) 聚焦选中（§6.2）
      if (hovered && AcceptDragDropPayload(DragDrop::kAssetPayload)) importModelToScene(落点)
      ctx.viewportScreenMin/Max = 本窗口屏幕区域   // 上报给外部拖放判定
ImGui::End()
```

要点：
- resize 处理：只在内容区尺寸变化且 > 0 时重建 FBO（窗口最小化时尺寸为 0，必须跳过渲染，否则 GL 报错）。
- Play 模式：SceneView 不再处理编辑器相机输入，用场景主相机渲染（§7）。
- ImGuizmo 的 gizmo 绘制必须在 ImGui::Image 之后调用（drawlist 追加顺序决定遮挡关系）。

### 3.3 每帧渲染顺序（FBO 内部）

1. 清屏（clearColor = scene.skyColor，纯色天空；深度/模板一并清除）
2. 网格地面（drawGrid，不写模板）
3. 全部不透明物体（endFrame 内按 shader/texture 排序减少状态切换；不做视锥剔除——mini 引擎）
4. 选中物体描边（stencil 两遍，逐个立即绘制）
5. MSAA resolve → ImGui::Image 显示
6. gizmo 与 ImGui UI（在主 FBO 之上，ImGui drawlist）

### 3.4 选中物体高亮方案（stencil 描边）

drawOutline 内部两遍：
```
第一遍：glEnable(GL_STENCIL_TEST); glStencilFunc(GL_ALWAYS, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE); glStencilMask(0xFF);
        正常着色器绘制 mesh（深度照写）
第二遍：glStencilFunc(GL_NOTEQUAL, 1, 0xFF); glStencilMask(0x00);
        glDisable(GL_DEPTH_TEST); 绑定 outline 着色器（顶点沿法线膨胀约 4% 模型尺寸）绘制 mesh（纯橙色）
恢复：  glEnable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST); glStencilMask(0xFF)
```
效果：物体本体照常渲染，外圈出现橙色描边（被遮挡部分也可见，Unity 风格）。FBO 的深度渲染缓冲须为 GL_DEPTH24_STENCIL8（Framebuffer::create 已保证）。

---

## 4. 场景模型

### 4.1 GameObject/Component 生命周期

```
创建：
  createGameObject(name, parent) → new GameObject(UUID::generate(), name) → 自动 addComponent<Transform>()
  → 插入 parent 或根列表 → 注册进 m_objectById（ID 冲突则重新生成，概率可忽略）
  addComponent<T>() → new T(this) → attachComponent（设 m_typeId，调用 onAwake）
销毁：
  go->destroy() → 子树整体标记，入 Scene::m_destroyQueue
  Scene::flushDestroyQueue()（Edit/Play 每帧调用）→ 从父级 children 移除、从 m_objectById 删除、释放内存
  （用队列延迟删除，避免 update 遍历中迭代器失效）
反序列化（loadFromFile）：
  instantiate(id, name)（不自动加 Transform）→ 逐个 attachComponent(callAwake=false) → onDeserialize
  → 全部对象挂根后按 parent 字段 setParent(keepWorldTransform=false)（直接用文件里的局部值）
  → 最后统一补调 onAwake（保证 onAwake 时字段已就绪、父级已存在）
```

### 4.2 父子层级与局部/世界变换同步

- Transform 存局部 TRS（position/quat/scale）+ 世界矩阵缓存（m_worldDirty）。
- getWorldMatrix() 惰性计算：parent ? parent->worldMatrix() * localMatrix() : localMatrix()。
- 任何局部修改 → 置自己与全部后代 dirty（setParent 同样递归置脏）。
- setParent(newParent, keepWorldTransform=true)：keepWorld 时先把世界矩阵取出来，改父后反解局部 TRS（glm::decompose），否则保持局部值。
- 根对象 parent == nullptr。

### 4.3 64 位唯一 ID 生成

- UUID::generate()：静态 std::mt19937_64，种子 = std::random_device 异或 steady_clock 时间戳；返回非 0 值。
- 0 保留为「无父级」（序列化中 parent: 0）。
- Scene 在注册 m_objectById 时查重（unordered_set），冲突重生成；加载场景时 ID 原样保留，与现有对象冲突才重新生成（罕见）。
- ID 全局唯一、可持久化，选中集/快照/序列化全部以 ID 引用对象。

### 4.4 组件查找方式

- Component::componentTypeId<T>()：函数内 static 计数器（C++11 起线程安全），首次使用时分配，运行期稳定（不持久化，持久化用名字）。
- getComponent<T>()：对 m_components 线性扫描 typeId（mini 引擎单对象组件数 < 20，无性能问题）。
- Scene::findComponents<T>()：深度优先遍历 + typeId 过滤（几百对象规模完全够用；后续优化方向：per-type 索引，不引入）。
- 序列化/脚本按「类型名字符串」查找：内置组件 if-else 分发，脚本走 ScriptRegistry。
- 脚本组件统一使用 Behaviour::typeId；查找具体脚本用 findScript<T>()（dynamic_cast）或 getComponents<Behaviour>()。

---

## 5. JSON 序列化 Schema（nlohmann/json）

约定：版本字段 `version: 1`；对象平铺列表 + parent 引 ID（0 = 根）；rotation 为四元数 [x,y,z,w]；组件数组第一项必须是 Transform；资产路径为相对项目根；未知组件类型 → Logger::warn 并跳过该组件。

```json
{
  "version": 1,
  "name": "DemoScene",
  "environment": {
    "skyColor": [0.18, 0.22, 0.35],
    "ambientColor": [0.15, 0.15, 0.18],
    "showGrid": true
  },
  "editorCamera": { "target": [0.0, 1.0, 0.0], "distance": 8.0, "yaw": 0.6, "pitch": -0.35 },
  "objects": [
    {
      "id": 16840732146653812735,
      "name": "Main Camera",
      "active": true,
      "parent": 0,
      "components": [
        { "type": "Transform", "position": [0.0, 3.0, 8.0], "rotation": [0.0, 0.0, 0.0, 1.0], "scale": [1.0, 1.0, 1.0] },
        { "type": "Camera", "fov": 60.0, "nearPlane": 0.1, "farPlane": 1000.0, "isMain": true }
      ]
    },
    {
      "id": 9723810445972830153,
      "name": "Sun",
      "active": true,
      "parent": 0,
      "components": [
        { "type": "Transform", "position": [0.0, 5.0, 0.0], "rotation": [-0.3536, 0.3536, -0.1464, 0.8536], "scale": [1.0, 1.0, 1.0] },
        { "type": "DirectionalLight", "color": [1.0, 0.95, 0.85], "intensity": 1.2 }
      ]
    },
    {
      "id": 12345678901234567890,
      "name": "Cube",
      "active": true,
      "parent": 0,
      "components": [
        { "type": "Transform", "position": [0.0, 1.0, 0.0], "rotation": [0.0, 0.0, 0.0, 1.0], "scale": [1.0, 1.0, 1.0] },
        {
          "type": "MeshRenderer",
          "mesh": "assets/models/cube.obj",
          "material": { "albedo": [0.9, 0.35, 0.2], "roughness": 0.5, "metallic": 0.0, "albedoMap": "" }
        },
        { "type": "SpinningCube", "speed": 30.0 }
      ]
    },
    {
      "id": 9988776655443322110,
      "name": "SmallCube",
      "active": true,
      "parent": 12345678901234567890,
      "components": [
        { "type": "Transform", "position": [1.5, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0, 1.0], "scale": [0.4, 0.4, 0.4] },
        {
          "type": "MeshRenderer",
          "mesh": "assets/models/cube.obj",
          "material": { "albedo": [1.0, 1.0, 1.0], "roughness": 0.4, "metallic": 0.1, "albedoMap": "assets/textures/checker.png" }
        }
      ]
    }
  ]
}
```

往返规则：
- 序列化：forEachObject 深度优先输出；每个组件对象先写公共字段（type 等），再调 onSerialize(out) 合并扩展字段（脚本字段如 "speed"）。
- 反序列化：先建全部对象（instantiate），再逐组件 createComponentFromJson + onDeserialize + 补 onAwake，最后统一 setParent 重建层级。
- Play 快照复用同一 serialize/deserialize（见 §7），保证「快照 = 存档」同一条代码路径。
- 编辑器复制对象：serializeGameObject → remapGameObjectIds（递归换新 ID）→ deserialize 进同场景（parent 一并重映射）。
- 示例 ID 必须 < 2^64-1（uint64 上限 18446744073709551615），否则 nlohmann 按 double 解析会舍入。

---

## 6. 编辑器交互设计

### 6.1 选择逻辑

- 射线拾取：SceneView hover 且左键点击（无拖拽，位移阈值 3px）且 !gizmo.isOver()：
  1. EditorCamera::screenPointToRay(mouse, vpPos, vpSize) 得 (origin, dir)。
  2. 遍历所有带 MeshRenderer 的 GO：世界 AABB = mesh->aabb() 的 8 角点经 worldMatrix 变换后的包围盒，射线-AABB slab 测试，取 t 最小者。
  3. Ctrl 按住 → toggleSelected（加选/减选）；否则单选 setSelected；未命中 → clearSelection。
- 已知局限：AABB 过粗（如大平面会挡住后方物体）。M3 验收不阻塞；若体感差，M5 追加按三角形精确拾取（CPU 遍历索引，mini 规模可接受）。
- Hierarchy 点击行 → setSelected；点击空白 → clearSelection。Del 删除选中（含子树）；Ctrl+D 复制选中。

### 6.2 聚焦逻辑（F）

- 有选中：取选中物体世界 AABB 中心 c 与半径 r → camera.focus(c, max(r * 3.0f, 0.5f))。
- 无选中：focus(原点, 8)。focus 只改 target/distance，不动 pitch/yaw，无插值（mini 简化）。

### 6.3 Gizmo 方案：推荐 ImGuizmo（明确结论）

推荐理由：
1. 功能完备且免费（MIT）：平移/旋转/缩放、局部/世界空间、屏幕空间手柄、遮挡判定（IsOver/IsUsing）、snap 支持，全部现成。
2. 与 ImGui docking 无缝：直接往 ImGui drawlist 绘制，天然叠在 ImGui::Image 的 viewport 之上，无第二渲染层。
3. 自研成本高：手柄射线命中、平面交点、多轴高亮、拖拽状态机等交互细节约 1–2 周且易出 bug；ImGuizmo 单头文件 + 一个 .cpp，集成半天。
4. 风险可控：只需处理矩阵直接传递（见下，无需转置）与矩阵分解回写。

集成要点（GizmoController::update）：
```
单选且 Edit 态时：
  ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList())   // 必须用当前 SceneView 窗口 drawlist（见下方陷阱）
  ImGuizmo::SetRect(vpPos.x, vpPos.y, vpSize.x, vpSize.y)
  glm::mat4 view = cam.viewMatrix(), proj = cam.projectionMatrix(aspect)
  glm::mat4 world = trans.worldMatrix()
  ImGuizmo::Manipulate(glm::value_ptr(view),          // 直接传 glm::value_ptr，无需转置：
                       glm::value_ptr(proj),          // ImGuizmo 的 float[16] 与 glm 同为列主序内存布局
                       operation, space, glm::value_ptr(world))
  if (ImGuizmo::IsUsing()):
      glm::decompose(world, scale, rot, pos, skew, persp)   // Manipulate 已写回同一矩阵
      若对象有父级：glm::inverse(parentWorld) * world 反解局部
  m_using = ImGuizmo::IsUsing(); m_over = ImGuizmo::IsOver();
```
- W/E/R 切换 Operation（cycleOperation），X 键切换 Local/World（toggleSpace）。
- gizmo 拖拽期间（isUsing）跳过相机输入与点击选择。
- 陷阱：不要用 ImGui::GetForegroundDrawList() 传给 SetDrawlist——ImGuizmo 的悬停判定
  IsHoveringWindow 依赖 drawlist 的 _OwnerName 与 g.HoveredWindow 匹配，前台 drawlist 的
  owner 永远不是当前面板 → 判定恒假 → gizmo 画出来也无法交互。

### 6.4 拖拽导入全流程

ImGui drag-drop payload 设计：
- 窗口内拖拽（AssetBrowser → SceneView）用 ImGui payload `NOVA_ASSET_PATH`，内容为 UTF-8 绝对路径字符串（SetDragDropPayload 字符串长度含 \0）。
- 层级面板内部拖拽用 `NOVA_GO_ID`，内容为 uint64（ImGui ID）。
- 外部文件（Windows Explorer）：ImGui 不支持 OS 文件拖放，用 glfwSetDropCallback → Window::setDropCallback → EditorApplication::m_pendingDrops 队列；update 末尾 handleExternalDrop：按鼠标坐标落在 ctx.viewportScreenMin/Max 还是 assetBrowserScreenMin/Max 判定目标（各面板每帧上报自己的屏幕区域）→ 落在 SceneView = 导入场景，落在 AssetBrowser = 仅拷贝进 assets。

导入管线（DragDrop::importModelToScene）：
1. isModelFile 校验扩展名（.obj/.fbx/.gltf/.glb/.dae）。
2. 若源文件在项目 assets/ 之外 → copyIntoAssets 复制到 assets/models/（重名自动加 "_1" 后缀），保证场景可移植；否则直接用原路径。
3. AssetDatabase::loadModel（ModelLoader::load 用以下 Assimp 后处理标志）：
   `aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals | aiProcess_FlipUVs`
   说明：FlipUVs 把 OBJ/FBX 的 UV 翻成 GL 约定；因此 stb_image 加载纹理时翻转标志一律为 false，避免「双翻转」经典 bug。glTF 经 Assimp 内部处理后在 FlipUVs 下也一致，M2 用实物验证。
4. 生成根 GO（文件名）+ 每个子网格一个子 GO（各挂 MeshRenderer，MeshRenderer.meshPath 指向源文件；albedoMapPath 经 AssetDatabase::getTexture 解析；没有贴图则纯色 albedo）。
5. 落点：射线与 y=0 平面交点（无交点则相机 target 前方 3m）→ 根 GO 放在该点 → 自动聚焦新物体。

资产缓存策略（M1–M5）：
- 纯内存缓存：AssetDatabase 内 unordered_map<canonical 绝对路径, weak_ptr>，同文件只导入一次；releaseUnused 回收无引用资产。
- 不监听 mtime（改文件后点 View → Reload Assets 手动 clearCache）。
- 不做磁盘二进制缓存（.novaasset），列 M5 之后可选优化。
- 场景文件只存相对路径，不内嵌网格数据。

### 6.5 面板布局与分工

- 首帧用 DockBuilder：DockSpaceOverViewport → SplitNode 左（Hierarchy）→ 右（Inspector）→ 底部（Tab: Console + Asset Browser）→ 中央留 Scene。
- HierarchyPanel：树形展示（Indent + 展开箭头）、F2 重命名、拖拽重父子（kHierarchyPayload，drop 到目标行 = setParent，drop 到空白 = 挂根）、右键菜单（Create Empty/Cube/Plane/Directional Light/Camera、Duplicate、Delete）、单击选中。
- InspectorPanel：Transform 数值编辑（DragFloat3/DragEuler，度）；MeshRenderer 的 mesh 路径与材质编辑（ColorEdit3/Roughness 滑块/贴图路径）；组件 enable 勾选、移除按钮；Add Component 下拉 = [Camera, DirectionalLight, MeshRenderer] + ScriptRegistry::allNames()；脚本字段编辑：调 onSerialize(json) → 逐键 ImGui 编辑 → onDeserialize(json)（最小「伪反射」，无需反射系统）。
- ConsolePanel：构造时 Logger::addSink，实现为 deque 环形缓冲（上限 500 条），Info/Warn/Error 三色，Clear 按钮。
- AssetBrowserPanel：std::filesystem 遍历 assets/，面包屑导航 + 双击进目录/双击 .nova.json 加载场景；模型与贴图文件为拖拽源（BeginDragDropSource + kAssetPayload）。

---

## 7. Play 模式设计

### 7.1 状态机

```
Edit ──Ctrl+P / Play 按钮──▶ Play
Play ──Stop 按钮──────────▶ Edit
Play 内可切换 paused（暂停时 scene->update 传入 dt=0，即 onUpdate 停摆）

enterPlayMode():
  ctx.playSnapshot = SceneSerializer::serialize(*scene)   // 快照 = JSON 字符串（内存）
  ctx.state = Play; ctx.paused = false
  scene->start()                                          // 引擎内广播 onStart（深度优先，跳过非激活子树与 disabled）
exitPlayMode():
  SceneSerializer::deserialize(*scene, ctx.playSnapshot)  // 同一 Scene 对象原地重建
  ctx.state = Edit; 选中集按 ID 恢复（ID 未变，天然保留）
```

### 7.2 快照与恢复策略

- 选 JSON 字符串快照而非深拷贝：复用序列化器的同一条代码路径（快照/存档/复制三合一），实现量最小、天然覆盖层级/组件/材质/脚本字段；反序列化到同一 Scene 对象，外部持有的 Scene* 不失效。
- 已知限制（写入文档）：脚本组件的 C++ 成员状态（运行时中间变量）不会保留——恢复的是进入 Play 前的字段值。这是 C++ 内嵌脚本方案的固有取舍，示例脚本按此约束设计。
- Play 中禁止结构修改：创建/删除对象、改父子、增删组件在 Play 态被面板禁用（Inspector/Hierarchy 判断 ctx.state），允许改 Transform 与材质数值（live edit）。
- 快照不落盘；Ctrl+S 保存的是当前编辑态场景（与快照互不干扰）。

### 7.3 渲染与脚本生命周期

- Play 相机：scene->findComponents<Camera>() 中 isMain 优先，无则第一个 Camera，再无则回退编辑器相机（SceneView 渲染与交互都切换；gizmo 禁用、编辑器相机输入禁用）。
- 灯光：场景第一个 DirectionalLight → DirectionalLightData（direction = transform()->forward()），无则用默认方向。
- 每帧顺序：scene->update(dt)（广播 onUpdate）→ SceneView 渲染 → UI。脚本移动物体当帧可见。
- onStart/onUpdate 顺序即场景深度优先顺序（与序列化顺序一致），文档化，不做依赖排序。

---

## 8. 脚本组件设计（C++ 内嵌脚本）

### 8.1 方案对比与结论

- 虚函数接口（推荐）：class Behaviour : Component，子类 override onStart/onUpdate/onSerialize/onDeserialize，配合 NE_REGISTER_SCRIPT 注册表。类型安全、可继承复用、序列化字段自然、示例最短。这正是 Unity MonoBehaviour 的形态。
- 回调注册（不推荐）：向 GameObject 注册 std::function 回调。更灵活但无类型、字段序列化要靠全局表、调试体验差，对「旋转立方体」示例没有收益。
- 结论：虚函数 + 工厂注册表。脚本是静态库 NovaScripts（OBJECT 库）编译进 NovaEditor——「引擎内 C++ 脚本」的最简单可行形态；不引入 DLL 热重载（列 M5 之后可选方向）。

### 8.2 完整示例（samples/SpinningCube）

```cpp
// samples/SpinningCube.h
#pragma once
#include <NovaEngine/Scripting/Behaviour.h>

namespace NovaGame {
class SpinningCube : public NovaEngine::Behaviour {
public:
    explicit SpinningCube(NovaEngine::GameObject* owner);
    float m_speed = 30.0f;                          // 度/秒，Inspector 可调
    void onStart() override;                        // 记录初始欧拉角（可选）
    void onUpdate(float dt) override;               // 绕自身 Y 轴旋转
    void onSerialize(nlohmann::json& out) const override;    // 写 "speed"
    void onDeserialize(const nlohmann::json& in) override;  // 读 "speed"
};
}

// samples/SpinningCube.cpp
#include "SpinningCube.h"
#include <NovaEngine/Scene/Transform.h>

namespace NovaGame {
SpinningCube::SpinningCube(NovaEngine::GameObject* owner) : Behaviour(owner) {}

void SpinningCube::onStart() {}

void SpinningCube::onUpdate(float dt) {
    if (!enabled) return;
    transform()->rotate(glm::vec3(0.0f, m_speed * dt, 0.0f));   // dt 秒 * 度/秒
}

void SpinningCube::onSerialize(nlohmann::json& out) const { out["speed"] = m_speed; }
void SpinningCube::onDeserialize(const nlohmann::json& in) { m_speed = in.value("speed", 30.0f); }
}

NE_REGISTER_SCRIPT(NovaGame::SpinningCube)   // 静态注册，见 §9/§11 的链接裁剪陷阱
```

用法：Hierarchy 右键 → Add Component → 下拉出现 "SpinningCube" → Inspector 里 speed 数值可调 → Play 时旋转，Stop 恢复。

---

## 9. CMake 构建方案

### 9.1 依赖版本（已核实，2026-11）

| 依赖 | 仓库 | GIT_TAG | 说明 |
|---|---|---|---|
| GLFW | https://github.com/glfw/glfw.git | `3.4` | 需求锁定 3.4（最新 3.5.1 不用） |
| glad2 | https://github.com/Dav1dde/glad.git | `v2.0.8` | 最新 release |
| GLM | https://github.com/g-truc/glm.git | `1.0.3` | 2025-12-31 发布 |
| Dear ImGui | https://github.com/ocornut/imgui.git | `v1.93.0-docking` | 必须是 docking 分支 tag |
| Assimp | https://github.com/assimp/assimp.git | `v5.4.3` | 需求锁定 5.4 系列（最新 5.4.x） |
| nlohmann/json | https://github.com/nlohmann/json.git | `v3.12.0` | 2025-04-11 发布 |
| stb | https://github.com/nothings/stb.git | `9600522de53973336fa033a289858a4354cbb04c` | master 最新 commit（stb 无 tag，必须 pin commit） |
| ImGuizmo | https://github.com/CedricGuillemet/ImGuizmo.git | `1.10` | 2026-05 发布，与 ImGui v1.93 docking 同期兼容 |

### 9.2 cmake/Dependencies.cmake（核心内容）

```cmake
include(FetchContent)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")  # 全项目 /MD 一致，防 CRT 混用

# GLFW
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

# glad2（生成 gl:core=4.5 加载器；glad_add_library 需要 Python3，见 §11）
FetchContent_Declare(glad GIT_REPOSITORY https://github.com/Dav1dde/glad.git GIT_TAG v2.0.8 GIT_SHALLOW TRUE)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# GLM（纯头文件）
FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 1.0.3 GIT_SHALLOW TRUE)

# Dear ImGui（docking）
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.93.0-docking GIT_SHALLOW TRUE)

# Assimp 5.4（重度裁剪：只留 OBJ/FBX/glTF/Collada 导入器，编译时间砍掉大半）
FetchContent_Declare(assimp GIT_REPOSITORY https://github.com/assimp/assimp.git GIT_TAG v5.4.3 GIT_SHALLOW TRUE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_COLLADA_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)   # FBX 需要 zlib（用 Assimp 自带）

# nlohmann/json
FetchContent_Declare(nlohmann_json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.12.0 GIT_SHALLOW TRUE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

# stb（纯头文件，接口库）
FetchContent_Declare(stb GIT_REPOSITORY https://github.com/nothings/stb.git
                     GIT_TAG 9600522de53973336fa033a289858a4354cbb04c GIT_SHALLOW TRUE)

# ImGuizmo
FetchContent_Declare(imguizmo GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git GIT_TAG 1.10 GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(glfw glm imgui assimp nlohmann_json stb imguizmo glad)
glad_add_library(glad_gl_core_45 STATIC REPRODUCIBLE API gl:core=4.5)   # 生成目标 glad_gl_core_45
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})
```

### 9.3 cmake/Common.cmake

```cmake
function(nova_configure_target t)
  target_compile_features(${t} PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(${t} PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8 /MP /EHsc)
    target_compile_definitions(${t} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN GLFW_INCLUDE_NONE _CRT_SECURE_NO_WARNINGS)
  endif()
endfunction()
```
注意：编译选项只加在本项目 target 上（第三方 target 用各自默认，避免 assimp 被 /W4 轰出警告墙）。NovaEngine 必须套用（含 /utf-8——引擎 .cpp 里有中文日志字面量）。

### 9.4 各 target 划分

- **NovaEngine**（STATIC，engine/CMakeLists.txt）：全部 engine/src；`target_include_directories PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include`；`target_link_libraries PUBLIC glfw glad_gl_core_45 glm::glm assimp nlohmann_json::nlohmann_json stb`（PUBLIC 让 editor 自动继承）。不安装、无 dllexport。
- **NovaScripts**（OBJECT，samples/CMakeLists.txt）：SpinningCube.cpp、OrbitCamera.cpp；PUBLIC 链接 NovaEngine。用 OBJECT 库而非 STATIC：MSVC 链接器会丢弃「无引用」的静态库 .obj，导致 NE_REGISTER_SCRIPT 的静态初始化从未执行、脚本注册失败；OBJECT 库的 .obj 直接并入 NovaEditor，保证注册代码存活（替代方案 /WHOLEARCHIVE:，见 §11）。
- **NovaEditor**（可执行，editor/CMakeLists.txt）：editor/src 全部 + imgui 四个核心 .cpp + imgui_impl_glfw.cpp + imgui_impl_opengl3.cpp + ImGuizmo.cpp（直接把源文件编进本 target，绕开 imgui/imguizmo 自身 CMake 的联动问题）；`target_include_directories PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imguizmo_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/samples`；`target_compile_definitions PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD IMGUI_DEFINE_MATH_OPERATORS`；链接 NovaEngine、`$<TARGET_OBJECTS:NovaScripts>`；`set_target_properties(NovaEditor PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")`（运行目录 = 项目根，相对路径 assets/... 直接可用，与 CLAUDE.md 的 `build/editor/Debug/NovaEditor.exe` 运行方式兼容）。

### 9.5 顶层 CMakeLists 与 Presets

```cmake
cmake_minimum_required(VERSION 3.24)          # CMake 4.4 要求 >= 3.5，3.24 稳妥
project(NovaEngine VERSION 0.1.0 LANGUAGES CXX)
include(cmake/Dependencies.cmake)
include(cmake/Common.cmake)
add_subdirectory(engine)
add_subdirectory(samples)
add_subdirectory(editor)
```

```json
// CMakePresets.json
{
  "version": 6,
  "configurePresets": [
    { "name": "vs2026", "generator": "Visual Studio 18 2026", "architecture": "x64",
      "binaryDir": "${sourceDir}/build" }
  ],
  "buildPresets": [
    { "name": "debug",   "configurePreset": "vs2026", "configuration": "Debug" },
    { "name": "release", "configurePreset": "vs2026", "configuration": "Release" }
  ]
}
```
（generator 名字以 `cmake --help` 实际列表为准；VS2026 内部版本号 18。）

### 9.6 引擎导出头文件组织

- 公共头全部在 engine/include/NovaEngine/{Core,Render,Scene,Assets,Scripting,Serialization}/，编辑器与 samples 一律 `#include <NovaEngine/...>`。
- NovaEngine.h 总伞头 include 全部子模块。
- 铁律：公共头不含 GL/GLFW include（GL 句柄 uint32_t；GLFWwindow 前向声明），保证编译隔离与编译速度。

---

## 10. 里程碑 M1–M5（验收标准）

每个里程碑结束：三人合并 → 全量构建通过 → 冒烟测试 → git 提交推送（按 AGENTS.md）。

**M1 窗口 + 三角形 + 相机（A 主力，C 辅助，约 3–4 天）**
- `cmake -S . -B build` 配置成功（FetchContent 拉齐全部依赖，一次成功）；`cmake --build build --config Debug` 编译通过。
- NovaEditor.exe 启动：1600x900 窗口、OpenGL 4.5 上下文、glad 加载成功。
- 屏幕中央渲染一个彩色立方体/三角形（A 提供 Renderer 接口，C 的 main.cpp 硬编码调用）。
- ImGui docking 启动：五个面板区（左/中/右/底）就位，可拖动。
- 编辑器相机：右键旋转、WASD 平移、滚轮缩放。
- Console 面板打印引擎初始化日志；退出无崩溃、无 GL 报错。

**M2 模型渲染 + 层级面板（B 主力，约 3–4 天）**
- Assimp 加载 assets/models/*.obj 显示，Blinn-Phong 光照 + 网格地面 + 纯色天空正确。
- DirectionalLight 组件生效（方向随 Transform）。
- Hierarchy 面板：创建空物体/立方体、F2 重命名、Del 删除、拖拽重父子、展开折叠、单击选中。
- 多子网格模型（.fbx/.gltf）渲染正确；带贴图模型显示（棋盘格纹理验证 UV 无翻转问题）。
- SceneView resize（拖 dock、缩放窗口）不崩溃（FBO 重建）。

**M3 gizmo + Inspector（C 主力，A/B 支援，约 3–4 天）**
- 点击选择（射线-AABB）、选中橙色描边高亮、点击空白取消、Ctrl 加选。
- W/E/R gizmo 移动/旋转/缩放（Local/World 切换），拖拽期间相机与选择正确抑制。
- F 聚焦选中物体。
- Inspector：Transform 数值编辑（位置/欧拉角/缩放）、材质编辑（颜色/粗糙度/贴图路径）、组件 enable/移除。
- Add Component 下拉（内置组件 + 注册脚本）可挂 SpinningCube 并调 speed。

**M4 序列化 + 资源浏览器 + 拖拽导入（B+C 主力，约 3–4 天）**
- Ctrl+S 保存 / File-Open 加载 .nova.json：层级、组件参数、材质、脚本字段、父子关系往返无损。
- Asset Browser 浏览 assets/，双击 .nova.json 加载场景。
- Explorer 拖 .obj/.fbx/.gltf 到 SceneView：自动拷贝进 assets/models/ → 导入 → 生成 GO → 聚焦；拖贴图到 Asset Browser → 拷贝进 assets/textures/。
- Asset Browser 拖模型到 SceneView 等效；同一模型重复拖入不重复导入（缓存生效）。
- 编辑器相机位置随场景保存/恢复。

**M5 Play 模式 + Console + 打磨（全员，约 3–4 天）**
- Play/Stop（Ctrl+P）：进入前快照、Stop 后完整恢复（层级/位置/参数/选中集）。
- SpinningCube 在 Play 中旋转；场景多实例互不影响；Inspector 可 live 调整 speed。
- Play 切换场景主相机（isMain），gizmo 与编辑器相机锁定；Pause 生效。
- Console 完整：Info/Warn/Error 分色、上限 500 条、Clear 按钮；引擎各模块错误路径有日志。
- 打磨：窗口最小化恢复、空场景全操作、连拖 20 个模型不卡死、Release 构建通过。

---

## 11. 风险与注意事项

1. **Assimp 编译耗时**：全量 Assimp 是首次构建最慢项（5–15 分钟）。对策：§9.2 的导入器裁剪（只留 OBJ/FBX/glTF/Collada）+ /MP 并行编译 + 不删 build/_deps（改 GIT_TAG 才需清）。预期裁剪后 2–4 分钟，仅首次。不要在顶层全局改编译选项（用 nova_configure_target 只作用于自家 target，避免 assimp 在 /W4 下报错墙）。
2. **ImGui docking 集成细节**：必须用 `-docking` 分支 tag（master 无 docking）；ImGuizmo 1.10 与 imgui v1.93.0-docking 同期、兼容，升级须成对；imgui_impl_opengl3.cpp 用 glad 时定义 `IMGUI_IMPL_OPENGL_LOADER_GLAD`；全局定义 `GLFW_INCLUDE_NONE` 防止 glfw3.h 拉系统 gl.h 与 glad 冲突；ViewportsEnable（多视口拖出）默认关闭——Windows 多 DPI 下有缩放问题，M5 后再评估。
3. **MSVC/FetchContent 常见坑**：CMake 4.4 拒绝 cmake_minimum_required < 3.5；VS 多配置生成器下 `CMAKE_BUILD_TYPE` 无效，输出路径/条件一律用 `$<CONFIG>` 生成器表达式；CRT 必须全项目一致（CMAKE_MSVC_RUNTIME_LIBRARY 设 /MD）；中文路径资产（Windows 11 中文环境）用 /utf-8 + std::filesystem::path + std::ifstream(path) 重载，禁止 char* 路径直传 C API（必要时 .u8string() 转 fopen_s）；FetchContent 首次拉取需网络，失败重跑 configure 即可（已拉取的 _deps 会保留）。
4. **glad2 集成方式**：`glad_add_library(glad_gl_core_45 STATIC REPRODUCIBLE API gl:core=4.5)` 在 configure 期用 Python3 生成源码，机器必须装 Python 3（无则 configure 报错）。备选方案 B（写入文档）：从 glad.dav1d.de 网页生成一次 4.5 core 的 glad.c/glad.h/khrplatform.h 提交到 engine/vendor/glad 并改成普通静态库。头文件包含顺序：.cpp 中先 `<glad/gl.h>` 后 `<GLFW/glfw3.h>`。
5. **stb_image**：`STB_IMAGE_IMPLEMENTATION` 必须且只能在 engine/src/Assets/TextureLoader.cpp 定义一次（多处定义 = LNK2005）；加载纹理一律 flip_vertically_on_load = false，与 Assimp 的 aiProcess_FlipUVs 配套，否则贴图上下颠倒（双翻转经典 bug）。
6. **脚本静态注册被链接器丢弃**：STATIC 库中只有静态初始化器引用符号的 .obj 会被 MSVC 链接器整体丢弃 → 脚本「查无此类型」。对策：NovaScripts 用 OBJECT 库 + `$<TARGET_OBJECTS:NovaScripts>` 并入 NovaEditor（首选），或 `target_link_options(NovaEditor PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:NovaScripts>")`（备选）。
7. **glm 与 ImGuizmo 的矩阵传递**：ImGuizmo 的 float[16] 与 glm/OpenGL 同为列主序内存布局，直接传 `glm::value_ptr(m)` 即可（ImGuizmo 内部按行向量约定计算，双重转置恰好抵消；显式转置反而会破坏平移并触发 "behind camera" 检测把 gizmo 静默裁掉）。拖拽结束时 Manipulate 已把新矩阵写回同一指针，`glm::decompose` 分解后再换算局部 TRS。
8. **ImGui::Image 显示 FBO**：MSAA resolve 完成前不能把颜色纹理给 ImGui；Image 的 UV 必须传 (0,1)-(1,0)（FBO 纹理上下颠倒）；viewport 尺寸为 0（最小化/折叠）时跳过整个 FBO 渲染路径。
9. **中文字体**：ImGui 默认字体无 CJK 字形，Console 里的中文日志会显示为问号。启动时 `io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", 16, nullptr, io.Fonts->GetGlyphRangesChineseFull())` 并合并进默认字体；文件不存在则回退默认字体。编辑器 UI 文案建议英文（Unity 风格），避免字体负担。
10. **场景内存模型**：父子树用 unique_ptr 持有 + 父裸指针，无循环引用；销毁走队列延迟，严禁在 forEachObject/update 回调内直接 delete；选中集在 flushDestroyQueue 后做有效性检查。
11. **编辑器控制台**：NovaEditor 保持控制台子系统（不设 WIN32_EXECUTABLE），stdout 日志在 VS 调试与双击运行时都可见；后续要隐藏再切 WIN32 + OutputDebugString。
12. **MSAA 兼容**：OpenGL 4.5 core 下 GL_TEXTURE_2D_MULTISAMPLE + glBlitFramebuffer 全驱动可用；创建后必须 glCheckFramebufferStatus == GL_FRAMEBUFFER_COMPLETE，失败 Logger::error 并降级无 MSAA 路径（集成显卡安全网）。
13. **addComponent 实参求值顺序**：`attachComponent(std::move(comp), comp->m_typeId)` 是 UB（C++17 实参求值顺序未指定）。必须按 §2.5 的规范实现：先读 typeId 再 move。
14. **nlohmann 解析大整数**：JSON 里 uint64 ID 必须 < 2^64-1，否则按 double 解析舍入导致 ID 错乱（§5 示例已修正）。

---

## 附录 A：依赖版本核实来源

- ImGui docking tags（v1.93.0-docking 为最新，2026-11 核实）：https://github.com/ocornut/imgui/releases
- GLM 1.0.3（2025-12-31）：https://github.com/g-truc/glm/releases
- nlohmann/json v3.12.0（2025-04-11）：https://json.nlohmann.me/home/releases/v3.12.0/
- ImGuizmo 1.10（2026-05）：https://github.com/CedricGuillemet/ImGuizmo/releases/tag/1.10
- glad v2.0.8、assimp v5.4.3（5.4 系列最新）、stb commit 9600522、GLFW 3.4：GitHub API（api.github.com，2026-11-14 直接查询核实）

## 附录 B：Coder B 接口问题裁定记录（v1.1 修订，已批准执行）

1. NE_REGISTER_SCRIPT 宏改为逗号表达式（registerScript 保持 void）；注册名裁剪为尾段。
2. addComponent 先读 typeId 再 move（防 UB）；attachComponent 签名改为 (comp, typeId, callAwake)。
3. Material 增加 albedoMapPath 字段；删除 MeshRenderer 静态旁表。
4. 脚本共用 Behaviour::typeId；新增 findScript<T>() 便捷模板；按脚本类型区分 typeId 列为后续优化。
5. 示例 SmallCube ID 改为 9988776655443322110（合法 uint64）。
6. Scene.cpp 允许包含 Scripting/Behaviour.h（受控特例）；新增 Scene::start()，EditorApplication::enterPlayMode 调用之。
7. CMake 骨架分工：顶层 + cmake/ + engine/ 归 A，samples/ 归 B，editor/ 归 C；NovaEngine 必须套 nova_configure_target。

### Critical Files for Implementation

- C:/Users/guoyu/Desktop/NovaEngine/cmake/Dependencies.cmake
- C:/Users/guoyu/Desktop/NovaEngine/engine/include/NovaEngine/Render/Renderer.h
- C:/Users/guoyu/Desktop/NovaEngine/engine/include/NovaEngine/Scene/Scene.h
- C:/Users/guoyu/Desktop/NovaEngine/engine/include/NovaEngine/Serialization/SceneSerializer.h
- C:/Users/guoyu/Desktop/NovaEngine/editor/src/SceneViewPanel.cpp

---

# V0.3 增补（2026-11-15 冻结，11 项功能 + 4 波实施计划）

## 12. 新第三方依赖（V0.3-1/2）

### 12.1 Jolt Physics v5.7.0
- 仓库根**无 CMakeLists.txt**（v5 起 Samples 移出）→ 不能 MakeAvailable，用 glad 同款模式：`FetchContent_Populate` + `add_subdirectory(${jolt_SOURCE_DIR}/Build)`；target 名 `Jolt`（alias `Jolt::Jolt`）。
- **必须覆盖的选项**（违反项目规则/引发崩溃）：`USE_STATIC_MSVC_RUNTIME_LIBRARY OFF`（默认 /MT 违反全项目 /MD 铁律，漏设 LNK2038）、`INTERPROCEDURAL_OPTIMIZATION OFF`、`FLOATING_POINT_EXCEPTIONS_ENABLED OFF`（Debug FP 异常中断）、`ENABLE_ALL_WARNINGS OFF`、`ENABLE_INSTALL OFF`、`ENABLE_OBJECT_STREAM OFF`、`JPH_USE_DX12/VK/MTL OFF`；`DEBUG_RENDERER_IN_DEBUG_AND_RELEASE ON`（碰撞体线框提取）。
- 公共头隔离：`engine/include/NovaEngine/Physics/*.h` 一律不 include Jolt 头（pimpl Impl 进 .cpp）。

### 12.2 miniaudio 0.11.26
- 有正式 tag（pin 0.11.26）；单头文件无 CMakeLists → stb 同款：Populate + `add_library(miniaudio INTERFACE)` + include dirs。
- `#define MINIAUDIO_IMPLEMENTATION` 唯一定义于 `engine/src/Audio/AudioEngine.cpp`（stb_image 纪律）；wav/mp3/flac/ogg 解码内置；设备线程为引擎唯一合法非主线程。

### 12.3 Dependencies.cmake 增补
```cmake
FetchContent_Declare(jolt GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git GIT_TAG v5.7.0 GIT_SHALLOW TRUE)
FetchContent_GetProperties(jolt)
if(NOT jolt_POPULATED) FetchContent_Populate(jolt) endif()
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
add_subdirectory(${jolt_SOURCE_DIR}/Build ${jolt_BINARY_DIR})

FetchContent_Declare(miniaudio GIT_REPOSITORY https://github.com/mackron/miniaudio.git GIT_TAG 0.11.26 GIT_SHALLOW TRUE)
FetchContent_GetProperties(miniaudio)
if(NOT miniaudio_POPULATED) FetchContent_Populate(miniaudio) endif()
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${miniaudio_SOURCE_DIR})
```
`engine/CMakeLists.txt`：`target_link_libraries(NovaEngine PUBLIC ... Jolt miniaudio)`。

## 13. 模块增量与组件注册表（波 0 改造）

新模块：Core/Events.h、Scripting/Coroutine.h、Physics/（PhysicsTypes/PhysicsWorld）、Audio/（AudioEngine/AudioClip）、UI/（UIRenderer/FontAtlas）、Render/PostProcess.h、Assets/MaterialAsset.h、Scene/ComponentRegistry.h、Scene/SceneManager.h、Scene/（Rigidbody/Collider×4/AudioListener/AudioSource/Canvas/Image/Text/Button/PrefabInstance）、Serialization/PrefabSerializer.h。全部无环；UI 模块零 ImGui 依赖。

**ComponentRegistry**（波 0 第 1 天，一次解决四波组件注册冲突）：
```cpp
class ComponentRegistry {
public:
    using Factory = std::function<std::unique_ptr<Component>(GameObject*)>;
    static void registerBuiltin(const std::string& typeName, Factory factory);  // 幂等
    static std::unique_ptr<Component> create(const std::string& typeName, GameObject* owner);
    static bool exists(const std::string& typeName);
    static std::vector<std::string> builtinNames();   // Inspector Add Component 下拉
};
#define NE_REGISTER_COMPONENT(ClassName) /* 逗号表达式 registerBuiltin(#ClassName, factory) */
```
SceneSerializer 组件分发改查 ComponentRegistry（脚本仍走 ScriptRegistry）；Transform/MeshRenderer/Camera/DirectionalLight 迁移注册。

## 14. 逐项功能设计（详见各波次任务书，此处为冻结契约摘要）

- **14.1 物理（Jolt，波 2）**：PhysicsWorld 为 **Scene 级服务**（Scene 持 unique_ptr，前向声明）；固定步进 60Hz 累加器最多 3 步/帧，`Scene::update` 第一步（flush 后、广播前）；Rigidbody{mass,useGravity,isKinematic,damping×2,addForce/addImpulse/velocity}；Collider 基类{isTrigger,center} + Box/Sphere/Capsule/MeshCollider；仅 Collider=static body；触发器回调=事件系统 TriggerEvent；动态刚体直改 Transform 会被物理覆盖（文档化）；Play 快照经 deserialize 天然还原；Renderer::drawLines + debug_line shader + 「Show Colliders」开关。
- **14.2 音频（miniaudio，波 2）**：AudioEngine 引擎级单例（编辑器启动即 init，Play 只 stopAll）；AudioClip 全量解码 PCM f32（AssetStream/pak 兼容）；AudioListener 挂主相机；AudioSource{clipPath,volume,loop,spatialBlend,minDistance,maxDistance,playOnAwake} 位置每帧跟随。
- **14.3 运行时 UI（波 2）**：Canvas{uiScale} 根组件；Image{sprite,color,slice9,anchor,offset,size}；Text{text,fontSize,color,fontPath,anchor,offset}（stb_truetype 按需烘焙，默认 msyh.ttc）；Button（含 Image 字段+onClick/hovered/pressedThisFrame）；UIRenderer 单动态 VBO 批处理（每 Canvas 1 draw call）+ processInput（letterbox 坐标换算）；渲染顺序：HDR→post→UI→显示；Edit 预览与 Play 同路径，无 Canvas 零开销。
- **14.4 PBR（波 1）**：**blinn_phong.frag 整体重写**（不新建 shader——drawOutline/透明/网格共用路径）；Cook-Torrance GGX + Fresnel-Schlick；Material 扩展 normalMap/metallicMap/roughnessMap/emissionMap(+paths)、emissionColor/emissionIntensity、materialAsset 双轨（前向声明）；texture unit：0 albedo/1 shadow/2 normal/3 metallic/4 roughness/5 emission；uGamma 开关（无后处理=1/2.2；HDR 模式 uGamma<=0 走直通分支——pow(x,0)≡1 全白陷阱，shader 内显式分支而非数值 0；grid/skybox 同步）；法线贴图强制 meta 线性加载；旧 JSON 数值字段原样保留。
- **14.5 材质库（波 1）**：.novamat JSON（与场景 material 段同构）；MaterialAsset{load/save/material/path}；AssetDatabase::getMaterialAsset/reloadMaterialAsset；MeshRenderer 双轨（materialAssetPath 非空=整块引用）；AssetBrowser 创建/双击编辑模态；Inspector 拖放赋值+Detach。
- **14.6 协程（波 2）**：/std:c++20 即启用（无 /await，CMake 零改动）；最小 Coroutine 任务类型（promise resumeMode：NextFrame/AfterSeconds/EndOfFrame/AfterCoroutine/Condition）；Behaviour::startCoroutine/stopAllCoroutines（std::list 保地址稳定）；帧内顺序见 §15；done 协程帧末统一 destroy。
- **14.7 Prefab（波 4）**：.novaprefab 树形 JSON{root{...children[]}}；PrefabSerializer{serializeSubtree/instantiateSubtree(outInstanceToSource)/instantiateSubtreeKeepingIds/loadAsset/saveAsset}；PrefabInstance 组件{prefabPath,instanceToSource}（场景内实例照常全量写字段）；override=动态比对源值加粗；Revert=字段回源；Apply=逐字段合并+嵌套烘焙+刷新全部实例；UndoRedoManager 加文件恢复通道 pushSnapshotWithFiles/undoFull/redoFull（旧方法保留）。
- **14.8 事件（波 2）**：Events{subscribe/publish(同步)/post(延迟)/unsubscribe/flushDeferred}；type_index key；publish 拷贝快照防重入；flush swap 模式；Behaviour::subscribeEvent 析构自动清理。
- **14.9 场景管理（波 3）**：SceneManager{init/registerScene/buildList/loadScene(入队帧末)/flushPendingLoad/activeScene/setActiveScene/dontDestroyOnLoad}；Build Settings 列表=assets/build.novabuild JSON；LoadScene=DDOL 子树 serializeSubtree(保留 ID)→原地 deserialize→挂回→stopAll→start→post 事件；exitPlayMode 快照恢复照常；GameObject 加 dontDestroyOnLoad 标志（序列化字段）；BuildSettingsPanel 第 8 面板；Packager 读清单收全部场景。
- **14.10 后处理（波 3）**：PostProcess{init/resize/hdrSupported/apply}；PostProcessSettings{enabled,bloomThreshold,bloomIntensity,bloomLevels,exposure} 进 Scene 环境字段 + EnvironmentPanel 折叠区；Framebuffer::create 加 hdrColor 参数（RGBA16F + resolve）；SceneRenderer::renderSceneToOutput（HDR FBO→resolve→brightpass→blur ping-pong×3→combine(ACES+gamma)→输出 LDR→UI）；pp.enabled=false 时 RGBA8 直通（A/B 干净对比）；8bit 降级不崩溃；shader：post.vert + post_brightpass/blur/combine.frag。
- **Play 帧内顺序（§15 定案）**：pollEvents→Scene::update{flush→物理step→onUpdate 广播→协程 advance(false)→Events flush→协程 advance(true)}→SceneManager::flushPendingLoad→AudioEngine::setListener→renderSceneToOutput→显示。Edit 态仅 flushDestroyQueue。
- **Schema（§16）**：version 3→4 一次 bump（写 4 读 1/2/3/4）；material 段新字段、12 个新组件类型名（ComponentRegistry 分发）、dontDestroyOnLoad、environment.postProcess、PrefabInstance 字段；旧文件默认值兜底。.novamat/.novaprefab/.novabuild 为新资产类型（各自 version:1）。

## 17. 冻结头变更总清单（随波冻结）

1. Render/Material.h：PBR 贴图槽+emission+materialAsset 双轨（波 1）
2. Render/Framebuffer.h：create +hdrColor（波 3）
3. Render/Renderer.h：drawLines（波 2）
4. Scene/Scene.h：m_physics（unique_ptr 前向声明）+postProcess 字段+physics()（波 2/3）
5. Scene/GameObject.h：attachComponent 两参重载+dontDestroyOnLoad（波 0/3）
6. Scripting/Behaviour.h：startCoroutine/stopAllCoroutines/advanceCoroutines+subscribeEvent+私有成员（波 2）
7. Scene/SceneRenderer.h：renderSceneToOutput+OutlineItem（波 3）
8. editor UndoRedoManager.h：pushSnapshotWithFiles/undoFull/redoFull（波 4）
9. Assets/AssetDatabase.h：+getAudioClip/getMaterialAsset/reloadMaterialAsset（波 1/2）
10. 新增头：Events/Coroutine/Physics×2/Audio×2/UI×3/PostProcess/MaterialAsset/SceneManager/PrefabSerializer/PrefabInstance/ComponentRegistry + 组件头×12；NovaEngine.h 伞头同步

**GLSL**：改 blinn_phong.frag（重写）、grid/skybox（+uGamma）；增 ui.vert/.frag、post×4、debug_line×2。**CMake**：Dependencies.cmake +jolt/miniaudio；engine 链接 Jolt miniaudio；协程零改动。

## 18. 分波实施计划

| 波 | 内容 | 线 |
|---|---|---|
| 波 0（1 天） | ComponentRegistry+分发迁移+attachComponent 重载+SerializationInternals 抽取；验收=全量回归 | A 引擎 + C Inspector 下拉 |
| 波 1（~1 周） | PBR（M）→ 材质库（M，严格先后） | A=frag 重写+uniform；B=序列化双轨；C=Inspector+AssetBrowser |
| 波 2（~1.5 周） | 物理（L）/UI（L）/音频+协程+事件（M×3），三线并行；冲突化解=架构师第 1 天布 Scene.cpp/Behaviour.h 骨架 | A=物理；B=UI；C=音频→协程→事件 |
| 波 3（~1 周） | 后处理（L）+场景管理（M） | A=PostProcess+renderSceneToOutput；C=SceneManager+BuildSettings；B=DDOL 标志 |
| 波 4（~1 周） | Prefab（L）+全量回归+schema v4 终验+Release | B=PrefabSerializer；C=PrefabManager+override UI+Undo 文件通道 |

依赖序：PBR 先于后处理；材质库紧随 PBR；触发器用事件系统（波 2 内先事件后物理联调）；Prefab 依赖波 0 注册表。

## 19. 风险

1. Jolt 无根 CMakeLists：Populate+add_subdirectory(Build)；USE_STATIC_MSVC_RUNTIME_LIBRARY OFF 漏设=LNK2038；首次构建 +2~3 分钟
2. 协程：/std:c++20 足够；final_suspend=suspend_always+手动 destroy；WaitForCoroutine 依赖 list 地址稳定（不可换 vector）
3. 后处理浮点 FBO：老集显降级 8bit 是验收项；MSAA RGBA16F blit 需 GL4.5（满足）
4. LoadScene 一律延迟到 flushPendingLoad（防广播中迭代器失效）
5. UI 零 ImGui 依赖：评审 grep engine/include/NovaEngine/UI 校验
6. 动态刚体直改 Transform 被覆盖（文档化）；miniaudio 单实现点

## 20. 波 2 落地终态（2026-11-15，物理/音频/UI/协程/事件 三线合并验收通过）

**执行结果**：74 第一方源文件强制重编译零错误零第一方警告；NovaEditor/NovaPlayer 两 exe 冒烟全绿（守卫 15 条验证通过、demo v3 打开、Player 场景加载+旋转正常）。Reviewer 12 项清单全过，2 P1 + 6 P2 全部修复闭环；DevOps 另定位修复 2 处运行期启动缺陷（栈回溯实证）。

**§13 增补（序列化分发终态）**：SceneSerializer::componentTypeName 已改为**按 typeId 反查 ComponentRegistry**（遍历 builtinNames 比对 typeIdForName）优先于 scriptTypeName——覆盖全部 11 个新内置组件，不再误报「脚本组件未注册」；内置+脚本同名双注册在反序列化时 Logger::warn 防静默替换（P1-2 修复）。组件分发契约自波 0 起不变。

**§14.3 增补（UI 输入时序分侧契约）**：`UIRenderer::processInput` 的调用时序为——Player 主循环在 `scene.update` **之前**调用（脚本 onUpdate 读到本帧按钮状态）；编辑器 SceneView 为渲染期调用（脚本侧按钮状态滞后一帧，已知取舍，调用点注释文档化）。波 2 的编辑器 UI 渲染位于场景 FBO 内、resolveMsaa 前（MSAA 覆盖 UI，波 2 无后处理故正确）；**波 3 迁移**：renderSceneToOutput 落地后两处调用点（SceneViewPanel Play/Edit 路径）迁至 PostProcess::apply 之后、输出 LDR FBO 内，Player 侧换 renderSceneToOutput 统一（调用点已留注释）。

**§14.3 增补（FontAtlas 清晰度，2026-11-16）**：①字形参考像素高 64→128px（kRefPx），UIRenderer 排版缩放改为 fontSize/128——小字号经 mip 降采样不再发虚，大字号（≤128px）走 level 0；代价是图集容量（1024² 约 60 个 CJK，满图集仍走既有 warn+缺字回退）。②纹理 MIN 过滤改 GL_LINEAR_MIPMAP_LINEAR，新增接口 `FontAtlas::refreshMipmaps()`（冻结头增量，既有 glyph/textureId 签名不变）——UIRenderer 每 Canvas 预烘焙 pass 后调用重建 mip 链（多图集下遍历全部图集）。③内置 5x7 降级字体 kFontScale 8→16 对齐新参考高。④**kRefPx 唯一出处 = FontAtlas::kRefPx（FontAtlas.h 内 static constexpr）**——曾因 FontAtlas.cpp/UIRenderer.cpp 各维护一份 64/128 不一致导致全部文字 2 倍大（形似乱码），已收敛，新增代码不得再复制该常量。⑤**默认字体优先级链（用户要求更好看的字体）**：NotoSansSC-VF.ttf → Deng.ttf（等线）→ msyh.ttc → 内置 5x7；首个加载成功者生效并打 info 日志「[FontAtlas] 默认字体: …」；.ttf/.ttc 统一走 stbtt_GetFontOffsetForIndex(data,0)（普通 ttf 恒返回 0）。⑥**v1.1 每文本独立字体（用户要求新罗马等指定字体）**：Text.fontPath 非空时走独立图集（路径为键的注册表，新增 `glyph(cp,fontPath)` / `textureId(fontPath)` 增量接口）；指定字体加载失败自动回退默认图集（warn 一次，中文显示为 ?）。批处理按纹理分桶天然兼容多图集（画家顺序跨贴图排序为既有已知取舍）。⑦**HiDPI 适配（用户 150% 显示器文字发虚的根治）**：Window::create 增 GLFW_SCALE_TO_MONITOR——窗口按逻辑尺寸创建、帧缓冲按显示器缩放（150% = 1.5 倍物理分辨率）；编辑器 ImGui 字体按 glfwGetWindowContentScale 放大烘焙（16×1.5=24px 物理）；UIRenderer 新增 setDpiScale()（uiScale × 内容缩放——fontSize 等逻辑尺寸映射到物理像素）；编辑器 SceneView 每帧同步 displayContentScale 并设置 UI 倍率，FBO 出图倍率另用 ImGui FramebufferScale；播放器同理以内容缩放设置 UI 倍率。注意：SCALE_TO_MONITOR 下 glfwGetWindowSize 与帧缓冲同倍放大，比值恒 1——UI 倍率必须取 glfwGetWindowContentScale 而非 fbW/winW。

**§14.6 增补（协程回收实现注记）**：帧末回收采用 FIFO 游标 + 「本 pass 已销毁目标」集合 + 链式唤醒（新 waitFor 指向已销毁/正在销毁目标时立即续推）；病态自旋上限 kMaxChainResumes=64（销毁帧 + warn）。节点地址复用误判的最坏后果是提前放行一次（无崩溃，安全降级）。跨 Behaviour 的 WaitForCoroutine 悬垂为冻结设计取舍（同 Behaviour 内互等完全安全）。

**§14.1 增补（Jolt 初始化顺序，DevOps 修复）**：`RegisterDefaultAllocator/Factory/RegisterTypes` 必须先于任何 Jolt 对象构造——PhysicsWorld.cpp 的 Impl 成员区以 **JoltInitGuard RAII 声明在最前**（先注册分配器；析构逆序在 teardownAll 之后注销，引用计数无泄漏）。此前分配器注册晚于 pairFilter 等成员构造，启动即 JPH::Allocate 空指针崩溃。

**§12 增补（已批准的物理头补全并入冻结签名）**：PhysicsWorld.h 注册通道 8 方法（register/unregisterRigidbody/Collider + addForce/addImpulse/velocity(const Rigidbody*)/setVelocity）；Rigidbody.h/Collider.h 显式 `~X() override` 与 `onAwake() override` 声明；AudioSource.h 私有 AudioHandle m_handle + 显式析构；Button.h 三个非序列化运行期输入状态字段（m_hovered/m_pressed/m_pressedEdge）；Scene::start() 头部调用 `m_physics->syncFromTransforms()`。

**UI 模块零 ImGui 依赖**已复核（grep 仅注释命中）；NovaPlayer 直接继承 UI 模块 → 打包独立性成立。

**遗留**（详见 KNOWN-ISSUES.md「波 2 遗留 backlog」）：跨 Behaviour 协程等待悬垂（设计取舍）、编辑器 UI 输入一帧滞后、Text.fontPath v1 不生效、物理近似裁定 5 项、Release 构建留待波 4 全量回归。

## 21. 波 3 落地终态（2026-11-15，后处理/场景管理/DDOL/PrefabSerializer 合并验收通过）

**执行结果**：四 target 全量构建零错误零第一方警告；postProcess 开启场景实跑 16F FBO 链（无降级 warn）、默认直通路径实跑通过；4 个 post shader 首编译零错误；编辑器守卫 15 条通过。Reviewer 12 项清单全过，3 P1 + 3 P2 全部修复闭环。

**§14.10 增补（renderSceneToOutput 最终冻结签名，三轮裁定定稿）**：

```cpp
static uint32_t renderSceneToOutput(const Scene& scene, const RenderCamera& fallbackCamera,
                                    Framebuffer& hdrFbo, Framebuffer& outLdrFbo,
                                    const PostProcessSettings& pp,
                                    const std::vector<OutlineItem>* outlines = nullptr,
                                    const std::function<void()>& afterSceneDraw = {});   // 引用形态，空默认
```

管线内顺序：hdrFbo 绑定 → 天空 → 阴影 → grid → opaque → transparent → outlines → **afterSceneDraw**（Show Colliders 线框注入点，hdrFbo 内、resolve 前；`if (afterSceneDraw)` 判定）→ resolveMsaa → [pp.enabled && hdrSupported：PostProcess::apply 到 outLdrFbo；否则 blit 直通] → **UIRenderer::render 画进 outLdrFbo**（§14.3 迁移目标达成：原 SceneViewPanel 两处直接 UI render 已删）→ 返回 outLdrFbo 纹理。

**uGamma 统一传递（§14.4 增补）**：Renderer 新增 `static setGammaMode(bool)` + `static linearOutput()` getter；s_linearOutput 粘性由两个公开入口每帧覆盖（renderScene 入口重置 false=旧路径零变化；renderSceneToOutput 按 pp.enabled && hdrSupported 设置）；**blinn_phong/grid/skybox 三处全部经 linearOutput() 驱动**（HDR 线性输出 uGamma=0 直通分支 / 旧路径 1/2.2），无硬编码旁路（评审 P1-1 修复）。

**§14.10 增补（PostProcess 永不黑屏契约）**：apply 早退统一判定 `!enabled || s_disabled || !s_initialized || !hdrSupported()` → blitPassthrough 纯 blit（不依赖 shader 与 bloom FBO 完整性）；blitPassthrough 保存/恢复入口 READ FBO（评审 P1-2/P2-3 修复）。bloom 链 = brightpass（阈值软膝+2x2 降采样）→ L0 9-tap 高斯 H/V ping-pong → stride-2 降采样级 ×2 → combine（scene+bloom×intensity → Narkowicz ACES → exposure → gamma）；bloomLevels 1..3 选链末级（单 uBloom 契约下多级经链式降采样折叠，设计裁定）。

**§14.9 增补（已批准补全）**：SceneManager::saveBuildSettings（BuildSettingsPanel 持久化）；编辑器侧 SceneManager::init/shutdown（setProjectRoot 后 / shutdown 序）。BuildSettingsPanel 合并式 rescan（首开+显式按钮触发；既有顺序/勾选保留、新文件按名追加）——手动排序持久（评审 P2-1 修复）。**合波纪律**：编辑器新增源文件必须同步改 editor/CMakeLists.txt 显式源清单（BuildSettingsPanel.cpp LNK2019 教训）。

**§14.7 增补（树格式 DDOL 键）**：PrefabSerializer 树形 JSON 节点追加「仅 true 写」的 `"dontDestroyOnLoad"` 键（与场景平铺格式同键名同语义）；instantiateNode 以 safeGet 缺省 false 读回——DDOL 经树格式迁移多次 LoadScene 持续存活（评审 P1-3 修复）。

**§16 增补**：environment.postProcess{enabled,bloomThreshold,bloomIntensity,bloomLevels,exposure} 与对象节点 "dontDestroyOnLoad" 已落地序列化（读写全默认兜底，旧 JSON 往返无损，version 4 不变）。

**最终验收待补两项**（不阻塞合波，转入 ROADMAP §5.1）：① 双 FBO resize/格式切换分支运行期验证（手动拉伸窗口+切换 pp.enabled）；② LoadScene 端到端脚本触发（DDOL 存活 + 退出 Play 快照恢复）。

**遗留**（详见 KNOWN-ISSUES.md「波 3 遗留 backlog」）：build.novabuild 缺失良性 warn、bloom 单方向降采样近似、resize 在 disabled 时仍建纹理、Release 构建留待波 4。

**波 4 后修复（2026-11-15，用户报障）**：Scene 视图 Edit 模式被场景相机劫持——renderSceneToOutput 内部 collectCamera 优先场景 Main Camera，编辑器相机被忽略；demo 的 Main Camera 又恰为恒等旋转（forward=+Z 背对原点）→ 视图只有天空、cube/网格全部不可见。修复：renderSceneToOutput 增 `bool useSceneCamera = true` 尾参（Edit 传 false 强制 fallback 编辑器相机；Play/Player 默认 true 场景相机语义不变）；demo.nova.json 的 Main Camera 旋转改 180° 偏航（0,1,0,0）朝向原点。验证：编辑器截图像素采样——修复前场景区纯天空蓝、修复后地面+天空+中心橙色描边 cube 齐全。

## 22. 波 4 落地终态 + V0.3 收官（2026-11-15，Prefab 完整化 + 全量回归）

**执行结果**：Debug 全量构建零错误零第一方警告（NovaEditor.exe 重链接）；**Release 全量构建零错误**（Jolt 首次 Release 编译，Release 双 exe 产出）；编辑器守卫 16 条验证通过；双 exe 冒烟通过；v3 旧场景加载参数不变（schema 回归通过）。Reviewer 12 项清单全过，2 P1 + 4 P2 全部修复闭环。

**§14.7 落地终态（Prefab 全链路）**：

- **PrefabSerializer**（冻结五方法，波 3 落地）：serializeSubtree（树形 JSON 无 parent）/instantiateSubtree（ID 重生成+映射累积）/instantiateSubtreeKeepingIds（DDOL 用）/loadAsset（AssetStream）/saveAsset（磁盘直写）。树格式含 "dontDestroyOnLoad" 键（P1-3 修复）。
- **PrefabInstance**（注册表第 16 项）：prefabPath + instanceToSource（sourceIds [[instId,srcId],...] 按 instId 排序确定性输出）。
- **override 动态比对**：Inspector FieldLabel 金黄高亮 + `▶ ` 前缀近似加粗（Transform/MeshRenderer/Camera/DirectionalLight/脚本字段直连；其余组件经 Prefab 区 override 列表兜底）。**比对用 componentFullJson**（type + 公共字段 + onSerialize，与 writeComponentsJson 同构）——未改不亮（P2-1 修复）。
- **Apply**：serializeSubtree → 点分路径差异键 setJsonPath 写回源 → 嵌套展开烘焙（整体替换+remapIds 回源 ID 空间+删嵌套 PrefabInstance+递归）→ saveAsset+clearCache → 刷新全场景同路径实例（override 保留实例值）→ pushSnapshotWithFiles（改写前内容）+ 置脏。**P1-2 修复**：未映射分支按 instId 源树查重 + saveAsset 后 syncNewObjectIds 补映射。**P2-3 修复**：空映射 warn+中止。v1 取舍文档化：源中删除对象不传播。
- **Revert**：**P1-1 修复**——applyComponentJson 助手（镜像 SerializationInternals 公共字段语义：Transform setLocalPosition/Rotation/Scale、Behaviour enabled 显式写回+onDeserialize；**升级须双处同步，源码注释互指**）→「改位置后 Revert」真实生效；revertComponent/revertAll 递归整树按自身映射溯源。
- **Undo 文件通道（§17 冻结落地）**：pushSnapshotWithFiles/undoFull/redoFull——pushSnapshot 委托新方法旧语义零变化；undoFull 先写回 files 再 deserialize；redoFull 的 .novaprefab 由 rewriteAssetFromScene 按场景重序列化（FileRestore 单内容的 v1 取舍）；clear 双栈同步（含 baseline 压空文件条目，P2-2）。
- **接线**：Hierarchy 右键 Create Prefab（assets/prefabs/ 去重）；DragDrop importPrefabToScene（copyIntoAssets 路由 + instantiateSubtree + 挂 PrefabInstance）；SceneViewPanel 拖放分支；AssetBrowser [PF] 前缀+双击提示；EditorApplication undoFull/redoFull 消费 + Reload Assets 清 PrefabManager 缓存；Packager 递归收集 .novaprefab（深度 8 防环）。
- **合波纪律（§17 增补）**：editor/CMakeLists.txt 显式源清单已补 BuildSettingsPanel.cpp 与 PrefabManager.cpp。

**V0.3 总账**：立项 11 项（骨骼动画 V0.3-13 按用户裁定顺延 V0.5）代码交付全部完成——波 1（PBR+材质库）/波 2（物理+音频+UI+协程+事件）/波 3（后处理+场景管理）/波 4（Prefab）。**最终人工验收清单**（GUI 交互项，不阻塞）：Prefab 全链路（拖入→改值→Apply→同步→嵌套烘焙→Revert→undoFull）、删源不崩、双 FBO resize、LoadScene 端到端、画面总验收（PBR+阴影+天空+Bloom/Tonemap）、物理/音频/UI 各项目测、V0.2 全量回归。详见 ROADMAP §5.1。
7. 波间热点文件（Scene.cpp/EditorApplication.cpp/SceneViewPanel.cpp/InspectorPanel.cpp）按「骨架先行」纪律执行
## 23. 文件日志系统（2026-11-16，验收期用户需求）

- **动机**：Logger 原仅 stdout/OutputDebugString/Console 面板——双击运行（无控制台）出 bug 无处可查；物理验收排障全程依赖 stdout 重定向，效率低且用户侧不可用。
- **接口（冻结头 Logger.h）**：`initFileLog(dir, appName)` / `shutdown()` / `installCrashHandlers()`。实现为第四个 Sink（与 ConsolePanel 同通道）：<dir>/<appName>_<时间戳>.log，逐行 fflush 落盘（崩溃不丢），行格式 `YYYY-MM-DD HH:MM:SS.mmm [级别] 内容`；文件关闭后 sink 静默 no-op，不影响其它 sink。
- **接线**：编辑器 main——setProjectRoot 后 `initFileLog(项目根/Logs, "NovaEditor")`；播放器 main——exeDir/Logs + "NovaPlayer"。两 exe 启动即 `installCrashHandlers()`（terminate 与 SEH 过滤器把原因/异常码写入日志后交还系统默认处理）。编辑器退出走 EditorApplication::shutdown 尾部、播放器 main 尾部调用 `Logger::shutdown()`。
- **约定**：Logs/ 已入 .gitignore；文档已同步 FEATURES §15。

---

# V0.5 波 1 契约（2026-11-17 冻结）

> 范围：补 V0.2-9（InputMap，路线图遗留）+ V0.5-11（手柄输入）+ V0.5-12（存档系统）+ V0.5-13（UI 系统增强）+ V0.5-14（性能优化包）+ V0.5-2（命令行批处理，整合者）。
> 分工：c1 InputMap+手柄 / c2 存档 / c3 UI 增强 / c4 性能优化包四线并行；编辑器侧全部（含 CLI）归整合者。
> 本节号承接 V0.3 增补（§12–§23），新增 §24 一章；每节含：文件落点、公共签名（头文件级）、序列化键名、验收要点。

## 24.0 总则：所有权分区、跨目录特例与冻结头总清单

**文件所有权严格分区（并行安全前提）**：

| 线 | 目录 | 新增/修改文件 |
|---|---|---|
| c1 | engine/include/NovaEngine/Core/、engine/src/Core/ | 新增 InputMap.h/.cpp；修改 Input.h/.cpp（仅追加手柄段，既有键鼠签名零变更） |
| c2 | engine/include/NovaEngine/SaveGame/、engine/src/SaveGame/ | 新增 SaveGame.h/.cpp（新模块目录） |
| c3 | engine/include/NovaEngine/UI/、engine/src/UI/ | 新增 Slider.h/.cpp、ScrollView.h/.cpp；修改 UIRenderer.h/.cpp |
| c3 特例 | engine/include/NovaEngine/Scene/、engine/src/Scene/ | 仅修改 Image.h/.cpp、Text.h/.cpp（fill/wrap/hAlign 追加字段）——既有 UI 组件落在 Scene/ 的波 1 唯一跨目录特例；Scene/ 其余文件一律不动 |
| c4 | engine/include/NovaEngine/Render/、engine/src/Render/ | 新增 Frustum.h/.cpp；修改 Mesh.h/.cpp、Renderer.h/.cpp、Material.h；新增 assets/shaders/blinn_phong_instanced.vert（复用 blinn_phong.frag） |
| c4 特例 | engine/include/NovaEngine/Scene/、engine/src/Scene/ | 仅修改 SceneRenderer.h/.cpp（渲染遍历接入剔除/实例化）——遍历点所在文件的跨目录特例 |
| 整合者 | editor/、player/、assets/input/ | §24.5 清单 |

**合波纪律**：
1. 上表之外的文件（Scene.cpp/SceneSerializer.cpp/Scene.h/SerializationInternals.h/samples/ 等）任何 Coder 不得触碰；新组件接入序列化零改动 SceneSerializer（ComponentRegistry 按名分发 + NE_REGISTER_COMPONENT 自注册）。
2. `engine/include/NovaEngine/NovaEngine.h` 伞头由整合者合波时统一追加 4 行 include（Core/InputMap.h、SaveGame/SaveGame.h、UI/Slider.h、UI/ScrollView.h）——避免四线同时改同一行。
3. 无新第三方库、Dependencies.cmake 零改动；engine/CMakeLists.txt 为 GLOB_RECURSE（新 .cpp 自动收录）；editor/CMakeLists.txt 显式清单由整合者补 InputBindingsPanel.cpp。
4. 场景 JSON schema version 保持 4（纯增量）：新组件按 type 名分发、旧版读取 warn 跳过；Image/Text 新字段缺省兜底。旧文件往返无损。
5. 内置组件数 16 → 18（+Slider/+ScrollView）；编辑器守卫与序列化回归清单同步更新为 18 条（整合者）。
6. 新资产类型两个（version 均从 1 起）：assets/input/*.novainput（输入绑定）、saves/slot<N>.novasave（存档）。

**冻结头变更总清单（总表；签名见各子节）**：
1. Core/Input.h：+GamepadButton/GamepadAxis 枚举、+Input 手柄查询 9 方法、+kMaxGamepads（追加式，旧签名不变）
2. Core/InputMap.h：新增（全静态类 InputMap + InputBinding/AxisBinding）
3. SaveGame/SaveGame.h：新增（SaveGame + SaveSlotInfo）
4. UI/Slider.h、UI/ScrollView.h：新增组件
5. Scene/Image.h：+ImageFillType 枚举、+fillType/fillAmount/fillClockwise（追加式）
6. Scene/Text.h：+TextHAlign 枚举、+wrap/wrapWidth/hAlign（追加式）
7. UI/UIRenderer.h：+UIHitResult、+hitTest/isPointerOverUI/lastPointerConsumed（追加式）
8. Render/Frustum.h：新增
9. Render/Mesh.h：+drawInstanced（追加式）
10. Render/Renderer.h：+submitInstanced/setFrustumCullingEnabled/frustumCullingEnabled/lastCulledCount/lastInstancedDrawCalls/lastInstancedInstanceCount（追加式）
11. Render/Material.h：+operator==（实例化分组键，追加式）
12. Scene/SceneRenderer.h：签名不变（内部遍历改造）
13. assets/shaders/blinn_phong_instanced.vert：新增

---

## 24.1 InputMap + 手柄（c1，V0.2-9 补课 + V0.5-11）

**文件落点**：`engine/include/NovaEngine/Core/InputMap.h`（新增）、`engine/src/Core/InputMap.cpp`（新增）、`engine/include/NovaEngine/Core/Input.h`（追加手柄段）、`engine/src/Core/Input.cpp`（手柄轮询实现）。

### 24.1.1 Input.h 手柄段（追加，既有键鼠接口零变更）

```cpp
// engine/include/NovaEngine/Core/Input.h（追加部分）
namespace NovaEngine {
// 手柄枚举值域与 GLFW_GAMEPAD_BUTTON_* / GLFW_GAMEPAD_AXIS_* 一致
//（Input.cpp 内 static_assert 校验，与 Key/MouseButton 同纪律）
enum class GamepadButton : int32_t {
    A = 0, B, X, Y, LeftBumper, RightBumper, Back, Start, Guide,
    LeftThumb, RightThumb, DPadUp, DPadRight, DPadDown, DPadLeft, Count
};
enum class GamepadAxis : int32_t {
    LeftX = 0, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count
};

class Input {
    // ...既有键鼠成员保持不变...
    static constexpr int kMaxGamepads = 4;
    static bool isGamepadConnected(int j = 0);            // j = 手柄序号
    static int connectedGamepadCount();
    static const char* gamepadName(int j = 0);            // 未连接 nullptr
    static bool isGamepadButtonDown(GamepadButton b, int j = 0);
    static bool isGamepadButtonPressed(GamepadButton b, int j = 0);   // 本帧边沿
    static bool isGamepadButtonReleased(GamepadButton b, int j = 0);
    static float gamepadAxis(GamepadAxis a, int j = 0);   // -1..1，死区已应用
    static void setGamepadDeadzone(float d);              // 默认 0.15，钳制 [0,1)
};
}
```

**Input.cpp 实现要点**：
- beginFrame 内（放在 `glfwGetCurrentContext()` 判定之外——手柄查询不依赖窗口）轮询 `GLFW_JOYSTICK_1..GLFW_JOYSTICK_4`：`glfwJoystickIsGamepad` 为真 → `glfwGetGamepadState` 映射到边沿快照数组（g_gpButtonDown/Prev[4][15]、g_gpAxes[4][6]）；连接/断开状态变化各打一次 info/warn 日志（含设备名）。
- 断开时全部状态清零（防按键粘连）；`glfwJoystickPresent` 但非 gamepad 映射设备（第三方手柄）→ 连接可见（name 有效）、按钮/轴恒 0、warn 一次。
- 轴死区在 gamepadAxis 读取时应用（|v| < deadzone → 0）。

### 24.1.2 InputMap.h（新增，全静态）

```cpp
// engine/include/NovaEngine/Core/InputMap.h
namespace NovaEngine {

// 动作绑定条目（键鼠/手柄源；JSON 可序列化）
struct InputBinding {
    Key key = Key::Unknown;                    // 键盘源（Unknown = 不参与）
    MouseButton mouseButton = MouseButton::Left;
    bool useMouseButton = false;               // true = 鼠标源参与
    GamepadButton gamepadButton = GamepadButton::A;
    bool useGamepadButton = false;             // true = 手柄源参与
    int gamepadIndex = 0;                      // 手柄序号
    bool ctrl = false, shift = false, alt = false;   // 修饰键（按住才生效；手柄源忽略修饰）
};

// 轴绑定条目：键盘正负键 + 可选手柄轴
struct AxisBinding {
    Key negativeKey = Key::Unknown;            // Unknown = 无
    Key positiveKey = Key::Unknown;
    GamepadAxis gamepadAxis = GamepadAxis::LeftX;
    bool useGamepadAxis = false;
    float gamepadScale = 1.0f;                 // 乘数（负 = 反向）
    bool ctrl = false, shift = false, alt = false;
};

class InputMap {
public:
    static void beginFrame();     // 每帧一次，紧跟 Input::beginFrame 之后（editor/player 主循环接线，见 §24.5）

    // 配置（.novainput JSON；失败 = Logger::warn + 保留旧表；首启无文件 = 内置默认绑定）
    static bool load(const std::filesystem::path& path);
    static bool save(const std::filesystem::path& path);
    static void clear();          // 清空全部动作/轴（换项目时由编辑器调用）

    // 脚本查询（Play 中 onUpdate 内调用；语义见下）
    static bool hasAction(const std::string& name);
    static bool getAction(const std::string& name);        // 按住（任意源）
    static bool getActionDown(const std::string& name);    // 本帧按下边沿
    static bool getActionUp(const std::string& name);      // 本帧松开边沿
    static float getAxis(const std::string& name);         // -1..1；缺失 warn 一次返回 0

    // 绑定管理（编辑器 InputBindingsPanel 用；改动即时生效）
    static const std::vector<std::string>& actionNames();
    static const std::vector<std::string>& axisNames();
    static void addAction(const std::string& name);        // 重名 warn + 忽略
    static void removeAction(const std::string& name);
    static void addAxis(const std::string& name);          // 重名 warn + 忽略
    static void removeAxis(const std::string& name);
    static void setActionBindings(const std::string& name, const std::vector<InputBinding>& b);
    static void setAxisBindings(const std::string& name, const std::vector<AxisBinding>& b);
    static const std::vector<InputBinding>& actionBindings(const std::string& name);
    static const std::vector<AxisBinding>& axisBindings(const std::string& name);

    // 键名 ↔ 枚举（绑定面板下拉与 JSON 读写共用）
    static std::string keyName(Key k);
    static Key keyFromName(const std::string& name);       // 未知返回 Key::Unknown
    static std::string gamepadButtonName(GamepadButton b);
    static GamepadButton gamepadButtonFromName(const std::string& name);
    static std::string gamepadAxisName(GamepadAxis a);
    static GamepadAxis gamepadAxisFromName(const std::string& name);
};

}
```

**语义裁定（冻结）**：
- 动作边沿：任一绑定（键盘源 = key 或 useMouseButton；手柄源 = useGamepadButton）满足「本帧按下 + 修饰键满足」→ getActionDown 为真；getActionUp 对称；getAction = 任一绑定按住且修饰满足。
- 轴值：全部绑定各自求值（键盘 = (正键?1:0)-(负键?1:0)；手柄 = gamepadAxis × gamepadScale，经 Input 全局死区后），取绝对值最大者（并列取绑定列表中靠前者）；修饰键不满足的绑定按 0 计。
- 缺失动作/轴：getAction* 返回 false、getAxis 返回 0，同名只 warn 一次（内部集合记录）。
- 内置默认绑定（load 失败/未调用时生效，保证「无手柄回退键鼠」开箱可用）：
  - 轴 Horizontal：A/D + LeftX；轴 Vertical：S/W + LeftY(scale=-1)
  - 动作 Jump：Space + 手柄 A；Submit：Enter；Cancel：Escape；Pause：P
- load 语义 = 整表替换（内部先 clear 再按 JSON 建表）。手柄热插拔无需 InputMap 感知（Input 每帧轮询）。

### 24.1.3 序列化键名（.novainput，version 1）

```json
{
  "version": 1,
  "actions": {
    "Jump": [
      { "key": "Space" },
      { "gamepadButton": "A" },
      { "key": "F", "ctrl": true }
    ]
  },
  "axes": {
    "Horizontal": [
      { "negativeKey": "A", "positiveKey": "D" },
      { "gamepadAxis": "LeftX", "gamepadScale": 1.0 }
    ],
    "Vertical": [
      { "negativeKey": "S", "positiveKey": "W" },
      { "gamepadAxis": "LeftY", "gamepadScale": -1.0 }
    ]
  }
}
```

- key/gamepadButton 键名 = 枚举成员名字符串（"A".."Z"、"Num0".."Num9"、"F1".."F12"、"Space"、"Escape"、"Enter"、"Tab"、"Backspace"、"Insert"、"Delete"、"Right"/"Left"/"Down"/"Up"、"PageUp"/"PageDown"/"Home"/"End"、"LeftShift"/"LeftControl"/"LeftAlt"/"RightShift"/"RightControl"/"RightAlt"）；mouseButton 键名 = "Left"/"Right"/"Middle"（出现即启用鼠标源）。
- 缺省字段：ctrl/shift/alt=false、gamepadIndex=0、gamepadScale=1.0、negativeKey/positiveKey 缺省=无。
- 每条 binding 至少含 key、mouseButton 或 gamepadButton 之一（多源并存 = 任一按下即触发）；未知键名 → warn + 跳过该条；未知动作/轴名 → warn + 忽略该条目；非法 JSON → load 返回 false。
- 读取经 AssetStream（pak 内配置可读）；save 写盘（编辑器侧用）。

### 24.1.4 验收要点（c1）

1. engine 全量编译零错误零警告；枚举 ↔ GLFW static_assert 通过。
2. 无手柄：脚本 `getAxis("Horizontal")` 按 A/D 返回 ±1、`getActionDown("Jump")` 按 Space 边沿触发。
3. 热插拔：插 Xbox 手柄 → info 日志（含名字）；左摇杆驱动 Horizontal/Vertical（死区生效、scale 方向正确）；A 键触发 Jump；拔出 → warn 日志、状态清零无粘连、键鼠回退即时生效；反复插拔不崩溃。
4. 绑定窗口（整合者）改 WASD→方向键即时生效并持久化（.novainput 往返无损）。
5. 缺失动作 getAxis/getActionDown 各 warn 一次、不崩溃；手写畸形 .novainput 加载失败保留旧绑定。
6. 第三方无映射手柄：连接可见、输入全 0、warn 一次（不崩溃）。
7. samples/ 本波不动（V0.2-9「示例脚本迁移」验收项顺延至后续波，风险清单注明）。

---

## 24.2 SaveGame 存档系统（c2，V0.5-12）

**文件落点**：`engine/include/NovaEngine/SaveGame/SaveGame.h`（新增模块目录）、`engine/src/SaveGame/SaveGame.cpp`。

```cpp
// engine/include/NovaEngine/SaveGame/SaveGame.h
namespace NovaEngine {

// 槽元数据（编辑器槽列表/读档返回值）
struct SaveSlotInfo {
    int slot = 0;
    bool valid = false;                 // 文件存在且 version 校验通过
    std::string scenePath;              // 存档时的场景资产相对路径（可为空）
    double timestampSeconds = 0.0;      // 存档时刻（system_clock）
    float playTimeSeconds = 0.0f;       // 调用方提供的游戏内计时
};

class SaveGame {
public:
    static constexpr int kCurrentSaveVersion = 1;
    // 目录：默认 <AssetDatabase::projectRoot()>/saves（编辑器=项目根/saves、Player=exe 目录/saves）
    static void setSaveDirectory(const std::filesystem::path& dir);   // 可覆盖（如 %APPDATA%）
    static std::filesystem::path saveDirectory();
    static std::filesystem::path slotPath(int slot);   // <dir>/slot<slot>.novasave

    // 存档（Play 中调用）：复用 SceneSerializer::serialize 产出场景 JSON（与 Play 快照同一代码路径）。
    // 原子写：先写 slot<N>.novasave.tmp 再 rename 覆盖；失败 Logger::error 返回 false
    static bool save(const Scene& scene, int slot, const std::string& scenePath,
                     float playTimeSeconds = 0.0f);
    // 读档（Play 中调用）：SceneSerializer::deserialize 原地重建同一 Scene 对象
    //（对象 ID 保持 → 外部 Scene*/选中集/按 ID 引用全部有效；脚本持有的裸 GameObject*
    // 需重取——与 LoadScene 同约束）。失败（文件缺失/损坏/未来版本）返回 valid=false，
    // 场景保持不变，不崩溃
    static SaveSlotInfo load(Scene& target, int slot);
    // 槽列表（编辑器面板用）：目录扫描 + 浅校验（JSON 合法 + version ≤ 当前 + snapshot 为场景对象（含 objects 数组））
    static std::vector<SaveSlotInfo> listSlots();     // 按 slot 升序
    static bool deleteSlot(int slot);                 // 失败 Logger::warn
    static bool slotExists(int slot);
};

}
```

**实现要点**：
- 依赖方向：SaveGame → Serialization + Scene + Assets（projectRoot）+ Core；不引入任何新头依赖。
- .novasave 结构见键名节；snapshot 节点直接内嵌 `SceneSerializer::serialize(scene)` 的 JSON（dump 成对象保存，load 时 dump 回字符串交给 deserialize）。
- 目录不存在时 save 自动 create_directories；写盘走 std::ofstream path 重载（中文路径安全）。
- version 校验：文件 version > kCurrentSaveVersion → Logger::warn「存档来自更新版本」+ valid=false（未来存档拒绝）；version 缺失按 0 处理（尽力加载 + warn）。
- 浅校验（2026-11-17 Review 修复）：snapshot 必须含 "objects" 数组键（真存档必为 SceneSerializer 产物），缺键按损坏处理——防止 {"snapshot":{}} 等非场景 JSON 经 deserialize 把场景清空为合法空场景。

### 24.2.1 序列化键名（.novasave，version 1）

```json
{
  "version": 1,
  "scene": "assets/scenes/demo.nova.json",
  "timestamp": 1732000000.0,
  "playTimeSeconds": 123.4,
  "snapshot": { ...SceneSerializer 场景 JSON（version 4，键名同 §5/§16）... }
}
```

### 24.2.2 验收要点（c2）

1. Play 中 Save(slot 1) 生成 saves/slot1.novasave；改 Transform/脚本字段/active 后 Load(slot 1) 恢复原状态；对象 ID 不变（选中集/按 ID 引用有效）。
2. save→load→save 二次往返字段无损；多槽（1/2/3）互不干扰。
3. 手改半截 JSON：listSlots 标 valid=false、load 不崩溃且场景保持不变、error 日志可查。
4. version=2 伪造文件：load 拒绝 + warn（未来版本保护）。
5. 退出 Play 快照恢复不受存档影响（存档只读场景、不落 Play 快照）。
6. 只读目录 save 失败：返回 false + error 日志，不崩溃。

---

## 24.3 UI 控件集增强（c3，V0.5-13）

**文件落点**：
- 新增 `engine/include/NovaEngine/UI/Slider.h` + `engine/src/UI/Slider.cpp`
- 新增 `engine/include/NovaEngine/UI/ScrollView.h` + `engine/src/UI/ScrollView.cpp`
- 修改 `engine/include/NovaEngine/Scene/Image.h` + `engine/src/Scene/Image.cpp`（fill 三字段，追加式）
- 修改 `engine/include/NovaEngine/Scene/Text.h` + `engine/src/Scene/Text.cpp`（wrap/wrapWidth/hAlign，追加式）
- 修改 `engine/include/NovaEngine/UI/UIRenderer.h` + `engine/src/UI/UIRenderer.cpp`（命中/冒泡/裁剪/fill/wrap 渲染）
- 两个新组件 .cpp 末尾 `NE_REGISTER_COMPONENT(Slider)` / `NE_REGISTER_COMPONENT(ScrollView)`（内置组件 16→18，Inspector Add Component 经 ComponentRegistry::builtinNames 自动出现——InspectorPanel 已消费该列表，无需改该路径）

### 24.3.1 Image fill 类型（Scene/Image.h 追加）

```cpp
enum class ImageFillType { None, Horizontal, Vertical, Radial90, Radial180, Radial360 };

// Image 类追加三个字段（旧场景缺省 = None/1.0/true，行为零变化）
ImageFillType fillType = ImageFillType::None;
float fillAmount = 1.0f;        // 0..1，越界钳制；None 时忽略
bool fillClockwise = true;      // Radial 方向
```

渲染语义（UIRenderer::emitImage 扩展）：fill = UV 裁切（保持贴图比例，不等比拉伸）——Horizontal 裁右（u∈[0,fill]）、Vertical 裁上（v∈[0,fill]，自底填充）、Radial 自顶部顺时针扇形（Radial90 顶点在右上角，180/360 按象限网格近似）；纯色白块同样裁 UV。9-slice 与 fill 共存：先 fill 后 slice（fillAmount 极小导致尺寸 < 边距和时退化单 quad——沿用现有退化规则）。

### 24.3.2 Text 换行与水平对齐（Scene/Text.h 追加）

```cpp
enum class TextHAlign { Left, Center, Right };

// Text 类追加三个字段（缺省 = false/0/Left，旧行为逐像素一致）
bool wrap = false;              // 按 wrapWidth 折行（按码点断行，CJK 安全；保留 '\n'）
float wrapWidth = 0.0f;         // 逻辑像素；<=0 时取当前 Canvas 屏幕宽
TextHAlign hAlign = TextHAlign::Left;
```

排版语义：wrap=true 时逐行测量（码点 advance 累加超宽即断）；hAlign 对每行相对 wrapWidth（wrap=true）或相对文本块最宽行（wrap=false）做左/中/右对齐（anchor 仍为排版原点）。按码点断行不存在超宽单字。

### 24.3.3 Slider 组件（UI/Slider.h 新增）

```cpp
// engine/include/NovaEngine/UI/Slider.h
namespace NovaEngine {
// 横向滑条（V0.5-13 v1：横向；纵向列后续）。轨道 = size 矩形，手柄沿 X 滑动
class Slider : public Component {
public:
    explicit Slider(GameObject* owner);

    // 视觉
    std::string backgroundSpritePath;   // 空 = 纯色背景
    std::string fillSpritePath;         // 空 = 纯色填充
    std::string handleSpritePath;       // 空 = 纯色手柄
    glm::vec4 backgroundColor{0.15f, 0.15f, 0.15f, 1.0f};
    glm::vec4 fillColor{0.9f, 0.9f, 0.9f, 1.0f};
    glm::vec4 handleColor{1.0f};
    float fillThickness = 6.0f;         // 轨道/填充条厚度（逻辑像素，垂直居中）
    glm::vec2 handleSize{16.0f, 24.0f}; // 手柄尺寸
    glm::vec2 anchor{0.5f}, offset{0.0f}, size{200.0f, 30.0f};   // 布局（与 Button 同公式）

    // 语义
    float minValue = 0.0f, maxValue = 1.0f;
    float value = 0.5f;                 // 序列化；钳制 [min,max]
    bool wholeNumbers = false;          // true = 值取整
    std::function<void(float)> onValueChanged;   // 脚本回调：拖拽中值变化即触发（连续）

    bool dragging() const;              // 运行期查询

    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;

    // ---- 运行期输入状态（非序列化，同 Button::m_* 裁定）----
    bool m_dragging = false;            // processInput 维护
};
}
```

行为：processInput 冒泡链命中 Slider 时，左键按下即开始拖拽（按下瞬间手柄跳到指针 X）；拖拽中 value 随指针位置线性映射并触发 onValueChanged（连续）；松开结束。value 写回由 UIRenderer 直接改组件字段（Inspector live edit 语义一致）。

### 24.3.4 ScrollView 组件（UI/ScrollView.h 新增）

```cpp
// engine/include/NovaEngine/UI/ScrollView.h
namespace NovaEngine {
// 滚动容器（V0.5-13 v1）：内容 = 直接子物体（GameObject 子树）的 UI 组件；
// 可视区 = anchor/offset/size 矩形；渲染期 scissor 裁剪；滚轮滚动
class ScrollView : public Component {
public:
    explicit ScrollView(GameObject* owner);

    glm::vec2 anchor{0.5f}, offset{0.0f}, size{300.0f, 200.0f};   // 可视区
    bool scrollHorizontal = false;
    bool scrollVertical = true;
    float scrollSensitivity = 20.0f;     // 滚轮每格像素（Input::scrollDelta 消费）
    glm::vec2 scrollOffset{0.0f};        // 像素（正 = 内容向右/下移）；序列化（位置记忆）
    // 可视区外框（验收补强，2026-11-17）：背景填充 + 描边——无外框的纯裁剪窗口
    // 在场景里不可见，验收者找不到滚动区域；旧文件缺字段走默认值。
    glm::vec4 bgColor{0.10f, 0.11f, 0.13f, 0.85f};
    glm::vec4 borderColor{0.42f, 0.45f, 0.52f, 1.0f};
    float borderThickness = 1.5f;        // 逻辑像素

    glm::vec2 contentSize() const;       // 内容包围盒（渲染期重算：子物体 UI rect 并集；脚本查询）

    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};
}
```

行为：UIRenderer 渲染该 Canvas 时，先绘制可视区外框（bgColor 背景填充 + borderColor 四边描边，发射序先于子树 = 内容绘制其上；enter 回调发射、节点 ctx 裁剪），ScrollView 子树的 quad 在 scissor（可视区矩形，嵌套 ScrollView 取交集）内裁剪绘制；滚动指示条（右侧/底部细条，显示内容比例与位置，**可拖动**——按下条带拖拽改 scrollOffset，渲染/命中共用 scrollBarRect 同一公式）。processInput：滚轮（Input::scrollDelta × scrollSensitivity）滚动指针下冒泡链最近的 ScrollView。**滚动方向（2026-11-17 修正）**：滚轮向下（delta<0）→ scrollOffset 增加 → 内容上移（看更下面）；钳制按内容原位边界 vs 可视区：上界 = 内容底对齐视口底，下界 = 内容顶对齐视口顶（≤0，内容未贴顶时允许回拉）。指针在 ScrollView 上时滚轮一律消费（含已到边界）——编辑器据此抑制相机缩放。子物体仍按普通 UI 布局（v1 不做内容自动重排——内容布局由创作者摆放，滚动仅平移视口）。

### 24.3.5 命中、冒泡与「Play 中 UI 优先于场景拾取」（UI/UIRenderer.h 追加）

```cpp
// engine/include/NovaEngine/UI/UIRenderer.h（追加部分）
namespace NovaEngine {
// 指针命中结果（最上层 UI 元素；Image/Text 也参与命中 = 不透明覆盖语义）
struct UIHitResult {
    GameObject* gameObject = nullptr;   // 命中组件所在 GO
    Component* component = nullptr;     // 命中的 UI 组件（Image/Text/Button/Slider/ScrollView）
};

class UIRenderer {
    // ...既有成员不变...
    // 最上层命中：全部 UI 元素 rect（与 render 同一布局公式）中 DFS 画家序最后命中者
    static bool hitTest(const Scene& scene, const glm::vec2& mouse, UIHitResult& out);
    // 任一 UI 元素覆盖该点（「UI 优先于场景拾取」的引擎侧判定入口）
    static bool isPointerOverUI(const Scene& scene, const glm::vec2& mouse);
    // 最近一次 processInput 是否有交互组件消费（onClick/拖拽/滚动）——调用方据此抑制
    // 场景拾取/其它指针行为（编辑器 SceneView Play 分支接线见 §24.5）
    static bool lastPointerConsumed();
};
}
```

**processInput 冒泡裁定（冻结，替换现有「遍历全部 Button 各自命中」逻辑）**：
1. 命中 winner = hitTest（最上层 UI 元素；指针未落在任何 UI 上 → 全部交互状态复位，lastPointerConsumed=false）。
2. 从 winner GO 起沿 GameObject 父链向上，找最近的交互组件（Button/Slider/ScrollView）：找到即交付本帧输入（Button hover/press/onClick 边沿；Slider 拖拽；ScrollView 滚轮），交付结果决定 consumed（Button 按下/点击边沿、Slider 拖拽中、ScrollView 本帧滚轮生效均算消费）。
3. 首个交互组件未消费（如悬停无操作）→ 继续向父链更上层找下一个交互组件（事件冒泡）；已消费 → 停。
4. 仅冒泡链上的交互组件更新 m_hovered/m_pressed/m_dragging 等状态，链外组件一律复位（旧实现多按钮重叠各自置位的缺陷一并修正）。
5. 时序契约不变（§14.3）：Player 在 scene.update 前调用、编辑器 SceneView 渲染期调用；Edit 态不调 processInput → UI 预览不挡场景拾取（只在 Play 中「UI 优先」）。
6. ScrollView 滚轮在 processInput 内读 Input::scrollDelta()（UI → Core 依赖合法），既有签名不变。
7. **Edit 态滚轮例外（验收补强，2026-11-17）**：`UIRenderer::processWheelInput(scene, mouse)`——命中/冒泡与 processInput 同一实现，但仅投递 ScrollView 滚动（Button/Slider 不参与，保持「预览不挡拾取」）。消费返回 true，编辑器据此跳过本帧相机缩放（EditorCamera::update 第四参 uiConsumedWheel）。Play 行为不变。
8. **HiDPI 布局裁定（2026-11-17 修复）**：布局空间 = 逻辑像素（`s_screenSize = 物理 FBO / dpiScale`），dpi 全局乘数只在顶点发射期统一乘（pushQuad/pushTri 唯一出口）；命中输入（processInput/processWheelInput/hitTest）入口把物理 FBO 坐标 ÷ dpiScale 转回逻辑。此前 anchor×物理尺寸 与 offset×dpi 混用，150% 显示器上整个 UI 放大 1.5 倍溢出视口（V05UITest 验收暴露）。

### 24.3.6 序列化键名（场景 JSON，version 4 不变）

- Slider：`"type":"Slider", "value":0.5, "minValue":0.0, "maxValue":1.0, "wholeNumbers":false, "backgroundColor":[r,g,b,a], "fillColor":[...], "handleColor":[...], "fillThickness":6.0, "handleSize":[16.0,24.0], "anchor":[0.5,0.5], "offset":[0.0,0.0], "size":[200.0,30.0], "backgroundSpritePath":"", "fillSpritePath":"", "handleSpritePath":""`
- ScrollView：`"type":"ScrollView", "anchor":[...], "offset":[...], "size":[...], "scrollHorizontal":false, "scrollVertical":true, "scrollSensitivity":20.0, "scrollOffset":[0.0,0.0], "bgColor":[r,g,b,a], "borderColor":[r,g,b,a], "borderThickness":1.5`（验收补强字段，缺省 = 默认外框，读旧文件零警告）
- Image 追加：`"fillType":"None"|"Horizontal"|"Vertical"|"Radial90"|"Radial180"|"Radial360", "fillAmount":1.0, "fillClockwise":true`（缺省 = 旧行为；未知字符串 warn 回退 None）
- Text 追加：`"wrap":false, "wrapWidth":0.0, "hAlign":"Left"|"Center"|"Right"`（缺省 = 旧行为；未知字符串 warn 回退 Left）

### 24.3.7 验收要点（c3）

1. 设置菜单（Slider+Button+ScrollView 同屏）在 16:9 面板/1920x1080/1280x720 三档下布局正确（目测）。
2. Play 中点击 UI 不穿透触发场景选择（isPointerOverUI/lastPointerConsumed 接线后：覆盖按钮/图片区域点击，场景侧无任何拾取/交互发生）；点击 UI 外区域行为与波 4 一致。
3. fill 血条：脚本改 fillAmount 当帧更新；Horizontal/Vertical/Radial 三型目测正确。
4. Text：中文折行正确、Left/Center/Right 对齐正确、\n 保留；wrap=false 旧文本渲染逐像素不变（回归）。
5. Slider：拖拽连续触发 onValueChanged、松手停；wholeNumbers 取整；序列化往返无损；Edit 预览与 Play 渲染一致。
6. ScrollView：滚轮滚动、内容 scissor 裁剪（超出可视区不可见）、scrollOffset 序列化往返、嵌套 ScrollView 不越界。
7. 冒泡：子 Image 覆盖父 Button → 父 Button 仍收到点击（透明冒泡）；子 Button 覆盖父 Button → 仅子触发（消费阻断）。
8. 内置组件数 18：Inspector Add Component 下拉出现 Slider/ScrollView；旧场景（含 Image/Text 旧字段）打开零警告变化。

---

## 24.4 性能优化包（c4，V0.5-14）

**文件落点**：
- 新增 `engine/include/NovaEngine/Render/Frustum.h` + `engine/src/Render/Frustum.cpp`（纯数学，仅 GLM）
- 修改 `engine/include/NovaEngine/Render/Mesh.h` + `engine/src/Render/Mesh.cpp`（drawInstanced）
- 修改 `engine/include/NovaEngine/Render/Renderer.h` + `engine/src/Render/Renderer.cpp`（submitInstanced/剔除开关/统计）
- 修改 `engine/include/NovaEngine/Render/Material.h`（operator==）
- 修改 `engine/include/NovaEngine/Scene/SceneRenderer.h` + `engine/src/Scene/SceneRenderer.cpp`（遍历接入剔除 + 实例化分组）
- 新增 `assets/shaders/blinn_phong_instanced.vert`（复制 blinn_phong.vert + 实例矩阵属性；复用 blinn_phong.frag；Packager 收 assets/shaders 全目录自动进 pak）

### 24.4.1 Frustum（新增）

```cpp
// engine/include/NovaEngine/Render/Frustum.h
namespace NovaEngine {
// 视锥剔除（V0.5-14）：Gribb-Hartmann 从 viewProj 提取 6 平面，法线朝锥体外
class Frustum {
public:
    static Frustum fromViewProj(const glm::mat4& viewProj);
    bool intersects(const AABB& worldAabb) const;   // true = 部分或完全在内（可渲染）
private:
    glm::vec4 m_planes[6];                          // 内部归一化
};
}
```

### 24.4.2 剔除接入（SceneRenderer 内部遍历）

- renderSceneInternal 的 forEachObject 遍历中：对每个带 MeshRenderer 的 GO 先算世界 AABB（局部 AABB 的 8 角点经 worldMatrix 变换取包围盒——含非均匀缩放精确性），`frustum.intersects` 为 false → 跳过 submit 与阴影收集（计数 +1）。
- frustum 由 renderCam（view × projection）构造；网格地面/天空/描边（drawOutline 由编辑器单独提交）不受剔除影响；阴影 pass 项随可见集自然剔除。
- 开关：`Renderer::setFrustumCullingEnabled(bool)`（默认 true）；关闭后遍历行为与波 4 基线逐帧一致（回归 A/B）。
- 正交相机（V0.5-10 未来）：Gribb-Hartmann 对正交投影同样成立（w 行平面退化为平行面），无需特判。

### 24.4.3 静态网格实例化

**Material.h 追加**：`bool operator==(const Material&) const = default;`（逐字段比较，含贴图指针/资产指针/blendMode——分组键用）。

**Mesh.h 追加**：
```cpp
// 实例化绘制（V0.5-14）：懒建 per-Mesh instanced VAO（复用 VBO/EBO + 实例矩阵 VBO，
// attrib location 4..7 mat4、divisor 1）；每次调用上传矩阵后 glDrawElementsInstanced。
// models 为空 no-op；GL 细节全部封装于 Mesh.cpp（公共头无泄漏）
void drawInstanced(const std::vector<glm::mat4>& models) const;
```

**Renderer.h 追加**：
```cpp
static void submitInstanced(std::shared_ptr<Mesh> mesh, const Material& material,
                            const std::vector<glm::mat4>& modelMatrices);  // 入队实例化批
static void setFrustumCullingEnabled(bool enabled);   // 默认 true
static bool frustumCullingEnabled();
static uint32_t lastCulledCount();                // 上一帧剔除项数
static uint32_t lastInstancedDrawCalls();         // 上一帧实例化 draw call 数
static uint32_t lastInstancedInstanceCount();     // 上一帧实例化累计实例数
```

**SceneRenderer 遍历分组（冻结算法）**：
1. 遍历收集 (mesh, material, world) 三元组（剔除后）；
2. 按 (mesh 指针, material) 分组（material 用 operator==）；
3. 每组 ≥2 个实例 → `Renderer::submitInstanced`（批上限 512 个矩阵，超出拆批）；恰 1 个 → 普通 `Renderer::submit`；
4. 仅 opaque 段实例化：blendMode==AlphaBlend 的项一律走普通 submit（透明段保持视距排序语义，禁实例化）；
5. 阴影 pass 不实例化（保持现状，单独计数）。

**Renderer endFrame 顺序（冻结）**：opaque 普通项（现有排序）→ 实例化批 → transparent（现有排序）。实例化绘制经 s_blinnPhongInstanced shader（blinn_phong_instanced.vert + 复用 blinn_phong.frag，uniform 绑定与 setBlinnPhongUniforms 完全一致 + 实例矩阵属性）；深度 LEQUAL 同普通 opaque；统计 s_tris += indexCount/3 × 实例数、s_drawCalls += 1/批。

**统计接线**：beginFrame 清零，endFrame 结算到 s_lastCulledCount/s_lastInstanced*；StatsPanel（整合者）新增三行显示；Render 段开销约束 < 0.5ms（Release，统计层验证）。

### 24.4.4 验收要点（c4）

1. 2000 个同 mesh+材质立方体场景（统计层）：剔除+实例化后 draw calls < 20、提交数 ≈ 可见数；Release 60 FPS（对照 ROADMAP V0.5-14 的 30→60FPS 目标）。
2. 剔除无漏绘：相机缓慢旋转一周目测边缘无物体闪出/闪没；背对场景时 lastSubmittedCount 显著下降、lastCulledCount 与之对应。
3. 开关回归：setFrustumCullingEnabled(false) 后行为与波 4 基线一致（A/B）。
4. 透明物体：交叉半透明平面排序无变化（实例化禁用验证：全透明场景 lastInstancedDrawCalls==0）。
5. 阴影：剔除后阴影轮廓与可见集一致（无幽灵阴影）。
6. 非均匀缩放/旋转物体剔除无漏绘（8 角点 AABB 精确性）；缩放出视锥的物体被剔除。
7. 描边/网格/天空不受影响；Demo 场景帧统计无回退。
8. 统计：StatsPanel 显示 Culled/Instanced draws/Instances 三行且数值与直觉一致。

---

## 24.5 命令行批处理 + 编辑器接线清单（整合者，V0.5-2 + 波 1 全部编辑器侧）

### 24.5.1 CLI 契约（editor/src/main.cpp）

```
NovaEditor.exe                                   # 无参数 = 原 GUI 模式，行为零变化
NovaEditor.exe --headless [--import] [--build --scene <path> --out <dir>] [--quit]
```

| 参数 | 语义 |
|---|---|
| `--headless` | 无窗口批处理模式：Window::create 后立即 glfwHideWindow（保留 GL 上下文——Renderer::init 依赖；不初始化 ImGui/面板/EditorApplication），任务完成即退出（等价隐含 --quit） |
| `--import` | 全量导入：扫描 assets/ 下全部模型（.obj/.fbx/.gltf/.glb/.dae）与贴图（.png/.jpg/.jpeg/.bmp/.tga/.hdr），逐资产 AssetDatabase::importModel / getTexture 生成二进制缓存；日志汇总「成功 N / 失败 M」；M>0 → 退出码 1 |
| `--build` | 打包：复用 Packager::buildPlayer(scene, outDir)（无 UI 依赖，直接调用）；`--scene` 缺省 "assets/scenes/demo.nova.json"；`--out` 缺省 `<项目根>/dist` |
| `--quit` | 交互模式启动后立即退出（CI 冒烟用） |

- 退出码：0 成功；1 任务失败（导入有失败/打包失败）；2 参数错误（未知参数/缺值）——stderr + Logs 文件双通道有错误日志。
- `--import/--build` 不带 `--headless` 时：先执行任务再进入 GUI（与 Unity -batchmode 行为对齐；v1 允许）。
- 可复现性：Packager 已保证 pak 内文件 map 排序确定性（同输入两次 build 产物逐字节一致——验收项）。
- 风险备案：headless 依赖隐藏窗口 GL 上下文（无 GPU 的 CI 环境可能创建失败 → 退出码 2 + error 日志）；纯 CPU 导入 API（engine/Assets 新接口）如需支持列后续波，届时 Architect 再裁定。

### 24.5.2 编辑器接线清单（整合者逐项核对）

1. **SceneViewPanel.cpp（Play UI 拦截）**：Play 分支保留 processInput 调用（hovered 时）；新增——`UIRenderer::lastPointerConsumed()` 为真时跳过其余任何指针处理并加注释守护「Play 中 UI 优先于场景拾取」（当前 Play 无场景拾取，此接线为 V0.5-10 2D 与未来拾取预留 + 验收守护）；Edit 分支不调 hitTest（UI 预览不挡拾取）。
2. **EditorApplication.cpp 菜单**：
   - File → 「Save Game」（Play 态可用）：弹槽选择（1–8 简版模态，显示 listSlots 元数据）→ `SaveGame::save(*m_scene, slot, m_context.currentScenePath)`。
   - File → 「Load Game」（Play 态可用）：列 listSlots（valid 标记）→ `SaveGame::load(*m_scene, slot)`。
   - File → 「Input Bindings...」（Edit 态可用）：打开 InputBindingsPanel。
   - run()：`Input::beginFrame()` 后追加 `InputMap::beginFrame()`。
   - init()：`InputMap::load(项目根/assets/input/default.novainput)`（失败 warn 不阻断——内置默认绑定兜底）。
3. **main.cpp**：§24.5.1 CLI 解析与 headless 分支（headless 不触碰 EditorApplication；GUI 模式零变化）。
4. **Packager.cpp**：资源收集加一条——`assets/input/` 全目录 *.novainput 进 pak（Player 内 InputMap::load 读 pak 经 AssetStream）。
5. **editor/CMakeLists.txt**：显式源清单补 `src/panels/InputBindingsPanel.cpp`（若 CLI 拆独立文件则一并补）。
6. **InspectorPanel.cpp 新控件编辑**（复用 UIWidgets.h 模式，改动走脏标记漏斗 + Undo）：
   - Image：+Fill Type 下拉 / Fill Amount 滑条(0–1) / Fill Clockwise 勾选；
   - Text：+Wrap 勾选 / Wrap Width 数值 / HAlign 下拉；
   - Slider：value/min/max/wholeNumbers/三色/thickness/handleSize/size/anchor/offset/三 sprite 路径；
   - ScrollView：size/anchor/offset/scroll 双开关/sensitivity。
7. **InputBindingsPanel.cpp（简版，View 菜单 + dock 底部 Tab）**：
   - 顶部：配置路径（assets/input/default.novainput）+ Reload/Save 按钮；
   - 动作列表（增删）+ 选中动作的绑定列表（每行：源类型下拉 Key/MouseButton/GamepadButton + 键位下拉（keyName 全表）+ 修饰键勾选 + 删除；「Listen」录制模式 v1 省略）；
   - 轴列表（增删）+ 选中轴的绑定列表（negativeKey/positiveKey/gamepadAxis/gamepadScale）；
   - 修改即时调 InputMap::setActionBindings/setAxisBindings；Save 调 InputMap::save（目录不存在自动创建）。
8. **player/src/main.cpp**：`Input::beginFrame()` 后追加 `InputMap::beginFrame()`；场景加载后 `InputMap::load("assets/input/default.novainput")`（pak 内读取，失败内置默认）。
9. **NovaEngine.h 伞头**：追加 `#include <NovaEngine/Core/InputMap.h>`、`<NovaEngine/SaveGame/SaveGame.h>`、`<NovaEngine/UI/Slider.h>`、`<NovaEngine/UI/ScrollView.h>` 四行（合波时统一做，防四线冲突）。
10. **StatsPanel.cpp**：新增 Culled / Instanced draws / Instances 三行（Renderer::lastCulledCount/lastInstancedDrawCalls/lastInstancedInstanceCount）。
11. **守卫与回归**：内置组件数 16→18，编辑器守卫与序列化回归清单同步（Slider/ScrollView 各一条）。

### 24.5.3 波 1 整体验收（合波后）

- 四线合并构建：Debug 全量零错误零第一方警告；两 exe 冒烟（编辑器守卫 18 条 + Player 场景加载）。
- CLI：`NovaEditor.exe --headless --import --build --quit` 全链路退出码 0；删坏一个资产后重跑退出码 1；两次 build 产物可复现。
- 全量回归 V0.2/V0.3 验收清单（尤其：Play 快照恢复、序列化往返、UI 批渲染、后处理双路径）。
- Release 构建通过；统计层数字随波记录（draw calls/帧时间回归证据）。

---

## 24.6 波 1 风险（给 DevOps/Reviewer）

1. 跨目录特例（§24.0 表）：c3 改 Scene/Image.h+Text.h、c4 改 Scene/SceneRenderer.cpp——合并时按上表 git diff 核对，越界即回退。
2. InputMap::beginFrame 接线遗漏 → getActionDown 恒 false：验收清单首项即查（editor run() + player main 两处）。
3. 实例化 VAO 懒建 + 每帧矩阵上传：验证 2000 实例下 GL 状态无泄漏（绑定恢复）；透明段禁实例化的判定点 = blendMode。
4. blinn_phong_instanced.vert 与 blinn_phong.vert 需同 uniform 约定（setBlinnPhongUniforms 共用）——shader 改一处另一处同步（评审对照）。
5. headless 隐藏窗口：CI 无 GPU 环境风险（§24.5.1 备案）；--import 依赖现有 GPU 上传路径（importModel 创建 GPU Mesh）。
6. 存档目录 Player 下 = exe 目录/saves：只读介质写失败只 warn/error 不崩溃（契约已含）；后续 setSaveDirectory 到 %APPDATA%。
7. samples/ 本波不动：V0.2-9 的「示例脚本迁移到 InputMap」验收项顺延；波 1 验收以引擎 API + 绑定面板为准。
8. schema version 保持 4 的取舍：新组件/新字段对旧读取器 = warn 跳过/缺省兜底；若波内出现破坏性变更必须即时回报 Architect 重议 version。
9. UIRenderer 命中公式必须与 render 共用同一布局 helper（hitTest 与绘制 rect 不一致 = 点击漂移）——评审重点。

---

# V0.5 波 2 契约（2026-11-18 冻结）

> 范围：V0.5-3（LOD）/ #4（IBL）/ #5（SSAO）/ #6（粒子）/ #7（脚本 DLL 热重载）/ #8（地形）/ #9（动画控制器）/ #10（2D 模式）/ #15（网络预研 spike，独立分支，代码不进 main）。
> **范围修正（架构师裁定，2026-11-18）**：①ROADMAP §6 标注 #9「已有：V0.3-13 骨骼动画模块」与代码实况不符——勘察确认引擎内无任何骨骼动画代码（V0.3-13 于 V0.3 波次整体顺延，§22 可证）。本节 #9 契约 = V0.3-13 全量范围（蒙皮+单动画播放）+ 状态机，#9 是「新模块」而非「升级」。②#15 保留「1 周可运行 spike 原型」（用户裁定 2026-11-18）：与 2a/2b 解耦，独立分支 spike/network-v1，代码不进 main，交付与理由见 §25.9。
> 分工：2a 轮（渲染四件套）c1 LOD / c2 IBL / c3 SSAO / c4 粒子 + c0 渲染整合者；2b 轮 c5 热重载 / c6 地形 / c7 动画 / c8 2D；两轮编辑器侧收尾归 c0；并行轨 c9 网络 spike（独立分支，与 2a/2b 解耦）。
> 本节号承接 §24，新增 §25 一章；每节含：文件落点、公共签名（头文件级）、序列化键名、shader、验收要点、风险与回滚。

## 25.0 总则：两轮交付、骨架日纪律与冻结头总清单

**交付顺序（2a/2b 拆分裁定）**：波 2 拆两轮——
- **2a（约 1 周）**：#3/#4/#5/#6 渲染四件套。四项全部动渲染管线共享文件（SceneRenderer/Renderer/PostProcess/Framebuffer/blinn_phong.frag），冲突最集中，骨架日一次性化解。
- **2b（约 1.5 周）**：#7/#8/#9/#10。
- **并行轨（约 1 周，与 2a 同时起）**：#15 网络 spike（c9），独立分支 spike/network-v1，与 2a/2b 零文件冲突，不占两轮人力。
- 拆轮收益：2a 合波先回归一次（剔除/实例化/后处理双路径全量回归），2b 合波风险减半；2a 交付的 blinn_phong.frag（IBL 版）是 2b 蒙皮 shader 复用的前置。

**文件所有权严格分区（并行安全前提）——「骨架日」模式**：

| 轮 | Coder | 独占文件 | 共享文件接触 |
|---|---|---|---|
| 2a | c0 渲染整合者 | 无（唯一合法触碰共享文件者） | SceneRenderer.h/.cpp、Renderer.h/.cpp、PostProcess.h/.cpp、Framebuffer.h/.cpp、Scene.h、SceneSerializer.cpp（仅环境段）、ComponentRegistry.cpp、NovaEngine.h、blinn_phong.frag、post_combine.frag、EnvironmentPanel.cpp、InspectorPanel.cpp、EditorApplication.cpp、editor/CMakeLists.txt |
| 2a | c1 LOD | engine/include\|src/Scene/LODGroup.h/.cpp（新增） | 无 |
| 2a | c2 IBL | engine/include\|src/Render/IBL.h/.cpp（新增）、assets/shaders/ibl_irradiance_conv.frag、ibl_prefilter.frag、ibl_brdf_lut.frag（可选 ibl_irradiance.comp/ibl_prefilter.comp） | 无 |
| 2a | c3 SSAO | engine/include\|src/Render/SSAO.h/.cpp（新增）、assets/shaders/ssao.frag | 无 |
| 2a | c4 粒子 | engine/include\|src/Particles/ParticleSystem.h/.cpp、ParticleRenderer.h/.cpp（新目录）、assets/shaders/particle.vert/.frag、editor/src/panels/ParticleWidgets.h | 无 |
| 2b | c5 热重载 | engine/include\|src/Scripting/ScriptRuntime.h/.cpp（新增）、ScriptRegistry.h/.cpp、samples/CMakeLists.txt、samples/*.h/.cpp、player/src/main.cpp、player/CMakeLists.txt、editor/src/Packager.cpp | 无 |
| 2b | c6 地形 | engine/include\|src/Terrain/Terrain.h/.cpp、TerrainRenderer.h/.cpp（新目录）、assets/shaders/terrain.vert/.frag、engine/include\|src/Physics/PhysicsWorld.h/.cpp、editor/src/panels/SceneViewPanel.cpp、editor/src/panels/TerrainPanel.h/.cpp（新增） | 无 |
| 2b | c7 动画 | engine/include\|src/Animation/（新目录全部）、engine/src/Assets/ModelLoader.cpp、engine/include\|src/Render/Mesh.cpp、assets/shaders/blinn_phong_skinned.vert、editor/src/panels/AnimatorPanel.h/.cpp（新增） | MeshData.h 蒙皮段与 ModelLoader.h 声明由 c0 骨架日布（接口本节冻结） |
| 2b | c8 2D | engine/include\|src/Scene/SpriteRenderer.h/.cpp、BoxCollider2D.h/.cpp、engine/include\|src/Physics/Physics2D.h/.cpp（新模块）、engine/src/Scene/Camera.cpp、assets/shaders/sprite.vert | Camera.h 投影段由 c0 骨架日布（接口本节冻结） |
| 并行轨 | c9 网络 spike | spike/network-v1 分支上：engine/include\|src/Network/（新目录，自由） | 无（main 零接触；仅结论文档由主会话写回 §25.9） |

**共享文件窗口纪律（唯一合法修改窗口 = 骨架日 + 合波日）**：
1. **骨架日（2a 第 0 天，c0 执行）**：c0 一次性完成 2a+2b 全部共享文件修改——创建全部冻结头声明（接口本节已冻结，仅声明）、SceneRenderer 遍历扩展点（见下）、Renderer 粒子/环境钩子、Framebuffer MRT、PostProcess AO 钩子、Scene.h 环境字段、SceneSerializer 环境段、ComponentRegistry.cpp link-guard 追加 7 行、blinn_phong.frag 的 IBL 采样与 GBuffer 输出段、post_combine.frag 的 AO 合成段、MeshData.h 蒙皮段、Camera.h 投影段、ModelLoader.h 声明。骨架日结束后**共享文件冻结**，任何 Coder 不得再触碰（越界改动 = 合波回退，§24.0 同纪律）。
2. 各 Coder 只写自己独占文件（上表）；实现经骨架日钩子接入，不再改遍历代码。
3. **合波日（c0）**：2a 与 2b 各一次收尾——NovaEngine.h 伞头 include、editor/CMakeLists.txt 显式清单（TerrainPanel/AnimatorPanel .cpp）、InspectorPanel/EnvironmentPanel/EditorApplication 接线、守卫与序列化回归清单更新、双 exe 冒烟、SceneViewPanel 的 SpriteRenderer 拾取分支。
4. 新组件接入序列化零改动 SceneSerializer（ComponentRegistry 按名分发 + NE_REGISTER_COMPONENT 自注册；组件 .cpp 末尾放宏）。
5. 场景 JSON schema version 保持 4（纯增量，读 1/2/3/4）：新组件缺失 = 无该组件、新字段缺省兜底、未知组件 warn 跳过；旧文件往返无损。
6. **内置组件数 18 → 25**（+LODGroup/ParticleSystem/Terrain/SpriteRenderer/BoxCollider2D/SkeletalMeshRenderer/Animator）；编辑器守卫与序列化回归清单同步更新为 25 条（c0 合波）。
7. 无新第三方库、Dependencies.cmake 零改动；engine/CMakeLists.txt 为 GLOB_RECURSE 自动收录新 .cpp；samples/CMakeLists.txt 归 c5 独占重写（OBJECT→SHARED）。stb 仓库内含 stb_image_write.h（地形 16bit PNG 写回用，不引入新库）。
8. 新资产类型一个：assets/terrains/*.png（16bit 灰度高度图，见 §25.6）；IBL 卷积缓存走既有 assets/.novacache（BinaryCache，非新资产类型）。

**SceneRenderer 遍历扩展点（骨架日布，冻结语义）**——渲染遍历是 7 项功能的交汇点，按「钩子函数 + 空实现默认」冻结，各 Coder 实现钩子本体：

```
收集阶段（renderSceneInternal 内，实例化分组前）：
  对每个带 MeshRenderer 的 GO：
    mesh = LODGroup::resolveMeshFor(go, renderCam.position)   // 无 LODGroup/disabled/空档 → 返回 mr->mesh
    （剔除/材质双轨/实例化分组/阴影收集逻辑不变，仅 mesh 来源替换）
  遍历结束后、阴影 pass 前：
    TerrainRenderer::collectAndSubmit(scene, renderCam, frustum, shadowItems)  // 空实现默认
    ParticleRenderer::collectAndSubmit(scene, renderCam)                       // 空实现默认
  蒙皮网格走既有 submit（Mesh 含蒙皮顶点布局；骨骼矩阵 UBO 由 Mesh 持有，Animation 模块每帧更新）
```

**冻结头变更总清单（总表；签名见各子节）**：
1. Render/MeshData.h：+SkinnedVertex/SkeletonBone/Skeleton/AnimKey/AnimChannel/AnimationClip 结构、MeshData +skinnedVertices/skeleton/clips（非蒙皮路径零变化，ROADMAP §10-C2 兼容裁定）
2. Render/Mesh.h：+setBoneMatrices/isSkinned（蒙皮 UBO 懒建，binding 1，上限 64）
3. Render/Renderer.h：+setEnvironment（IBL 纹理绑定）/+lastParticleCount/lastParticleDrawCalls（追加式）
4. Render/Framebuffer.h：create +normalDepth 参数；+normalTextureId()/depthLinearTextureId()
5. Render/PostProcess.h：PostProcessSettings +ssaoEnabled/ssaoRadius/ssaoStrength/ssaoSampleCount（追加式，缺省=关）；PostProcess +setAmbientOcclusionTexture（追加式）
6. Scene/Scene.h：+iblIntensity（环境段 environment.ibl）
7. Scene/Camera.h：+ProjectionMode 枚举/+orthoSize（追加式，缺省 Perspective 旧行为零变化）
8. Assets/ModelLoader.h：+extractSkeleton/extractAnimations 声明（骨架日布，c7 实现）
9. Scripting/ScriptRegistry.h：+unregisterScript/removeAllScripts（追加式）
10. Scripting/ScriptRuntime.h：新增
11. 新增头：Scene/LODGroup.h、Render/IBL.h、Render/SSAO.h、Particles/ParticleSystem.h、Particles/ParticleRenderer.h、Terrain/Terrain.h、Terrain/TerrainRenderer.h、Animation/{Skeleton,AnimationClip,SkeletalMeshRenderer,AnimatorController,Animator}.h、Scene/SpriteRenderer.h、Scene/BoxCollider2D.h、Physics/Physics2D.h；NovaEngine.h 伞头同步（c0 合波）
12. GLSL 新增：ibl_irradiance_conv.frag、ibl_prefilter.frag、ibl_brdf_lut.frag、（可选 ibl_irradiance.comp/ibl_prefilter.comp）、ssao.frag、particle.vert/.frag、terrain.vert/.frag、blinn_phong_skinned.vert、sprite.vert；修改：blinn_phong.frag（IBL 间接光 + GBuffer 法线/线性深度输出）、post_combine.frag（AO 合成）——两处修改均由 c0 骨架日完成
13. CMake：samples/CMakeLists.txt 重写（OBJECT→SHARED NovaGame.dll）；editor/CMakeLists.txt +WINDOWS_EXPORT_ALL_SYMBOLS +/WHOLEARCHIVE +NOVA_GAME_DLL_PATH/NOVA_BUILD_CONFIG 编译定义 + 面板源清单（c0/c5）；player/CMakeLists.txt +WINDOWS_EXPORT_ALL_SYMBOLS（c5）

**骨架日落地裁定（2026-11-18，c0，提交 890f475）**——对 §25 冻结签名的 4 处补充（不改任何已冻结签名，全部追加式；各 Coder 以本块 + §25 原文为准）：
1. Renderer.h 追加 `static void setGbufferPassEnabled(bool)`：§25.3 的 uGbufferPass 开关需要携带通道（§25.0 冻结清单未列）。SceneRenderer 每帧按 (hdr && ssaoEnabled) 设置；renderScene 旧路径恒 false。
2. AnimatorParameter/AnimatorTransition/AnimatorState 三结构体落点 = `Animation/AnimatorController.h`（§25.7 代码块列于 Animator.h 内，与 §25.0 文件清单要求的 AnimatorController.h 合并裁定）；Animator.h include AnimatorController.h——名字/签名逐字不变，c7 include 任一均可取到。
3. blinn_phong.frag 追加 `uniform mat4 uView`（契约未列）：GBuffer 段换算视图空间法线/线性深度所需（CameraBlock 仅 uViewProj）；由 Renderer.cpp 的 setBlinnPhongUniforms 每项设置；blinn_phong.vert 未动。
4. SSAO 触发条件 = 契约「ssaoEnabled && hdr」追加附件存在性检查（normalTextureId()/depthLinearTextureId() != 0，即 §25.3「附件创建失败 → AO 跳过」降级路径）。注意：编辑器 SceneViewPanel（c6 文件）与 Player main（c5 文件）的 hdrFbo.create 尚未传 normalDepth=true，接通前 SSAO 实际走降级分支（接通归合波日）。

---

## 25.1 LOD 系统（c1，V0.5-3）

**文件落点**：`engine/include/NovaEngine/Scene/LODGroup.h` + `engine/src/Scene/LODGroup.cpp`（新增，独占）。接入点 = SceneRenderer 收集阶段的 resolveMeshFor 调用（c0 骨架日布）。

```cpp
// engine/include/NovaEngine/Scene/LODGroup.h
namespace NovaEngine {
// LOD 档位描述（V0.5-3）
struct LODLevel {
    std::string meshPath;               // 资产相对路径；反序列化经 AssetDatabase 解析
    float distance = 0.0f;              // 相机距离 >= distance 时该档可用（升序；LOD0 = 0）
    std::shared_ptr<Mesh> mesh = nullptr;   // 运行时（不序列化）
};

// LOD 组件：挂在带 MeshRenderer 的 GO 上，渲染遍历按相机距离选 mesh。
// 与 MeshRenderer 的关系：enabled 且 lods 非空时，遍历收集用 resolveMeshFor 结果
// 替代 mr->mesh（阴影/剔除/实例化分组全部使用同一替代结果）；
// 无 LODGroup / disabled / lods 空 → 原 MeshRenderer.mesh 路径零变化。
class LODGroup : public Component {
public:
    explicit LODGroup(GameObject* owner);
    bool enabled = true;
    std::vector<LODLevel> lods;         // v1 上限 3 档（0/1/2；距离升序）
    bool crossFade = false;             // v1 预留（dither 过渡本波不实现；序列化保留字段）

    // 冻结入口（渲染遍历扩展点调用；SceneRenderer 不再有 LOD 分支逻辑）：
    // 按相机世界位置与 GO 世界位置距离选档；无可用档返回 nullptr（调用方回退 mr->mesh）
    static std::shared_ptr<Mesh> resolveMeshFor(GameObject* go, const glm::vec3& cameraPos);

    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};
}
```

**行为**：距离判定每帧每 GO 一次 O(lods)；Edit/Play 一致生效；Inspector 编辑 lods 列表（mesh 路径 + distance，接线 c0 合波）；mesh 路径加载失败 warn + 该档跳过。

**序列化键名（场景 JSON，version 4 不变）**：`{"type":"LODGroup","enabled":true,"crossFade":false,"lods":[{"mesh":"assets/models/rock_lod0.obj","distance":0.0},{"mesh":"assets/models/rock_lod1.obj","distance":15.0},{"mesh":"assets/models/rock_lod2.obj","distance":30.0}]}`（lods 非数组/空 → resolveMeshFor 返回 nullptr 等效无 LOD；未知键忽略）。

**验收要点（c1）**：
1. 2000 物体场景（统计层）：相机拉远 lastTriangleCount 显著下降、拉近回升；三档切换目测正确。
2. 切换无闪烁帧（无 cross-fade v1：整档边界切换；目测无 z-fighting/闪面）。
3. LODGroup 随场景序列化往返无损（路径/距离/顺序）；旧文件（无 LODGroup）打开零警告。
4. 每对象组切换开销 < 0.1ms（1000 组场景统计层帧时间回归）。
5. LOD 替代 mesh 参与阴影/剔除/实例化（无幽灵阴影、剔除无漏绘）。

**风险：低。回滚**：骨架日钩子返回 nullptr = 旧路径；删除 LODGroup.h/.cpp + 钩子一行即全回滚。**shader：无**（切 mesh 而已）。

---

## 25.2 IBL 环境光照（c2，V0.5-4）

**文件落点**：`engine/include/NovaEngine/Render/IBL.h` + `engine/src/Render/IBL.cpp`（新增，独占）+ 3 个卷积 shader + 可选 2 个 compute。blinn_phong.frag 的 IBL 采样段与 SceneRenderer 天空段绑定由 c0 骨架日布（接口本节冻结）。

```cpp
// engine/include/NovaEngine/Render/IBL.h
namespace NovaEngine {
// IBL 环境（卷积产物）：irradiance 32² + prefiltered 128² mip 链（5 级）+ BRDF LUT 512²
class IblEnvironment {
public:
    void bind(uint32_t irradianceUnit, uint32_t prefilterUnit, uint32_t brdfLutUnit) const;
    uint32_t irradianceId() const; uint32_t prefilterId() const; uint32_t brdfLutId() const;
};

class IBL {
public:
    static void init(); static void shutdown();   // 惰性初始化（幂等）；shader 失败 → 全部入口安全 no-op
    static bool available();
    // 烘焙：等距柱状 HDRI 绝对路径 → 环境。fragment 卷积默认；compute（4.5 core 直接可用，
    // glad gl:core=4.5 提供 glDispatchCompute，无扩展名；IBL 模块内自行编译 compute program，
    // 不改 Shader 类）编译/执行失败自动回退 fragment；fragment 也失败 → 无 IBL（不黑屏）。
    static std::shared_ptr<IblEnvironment> bake(const std::filesystem::path& equirectPath);
    // 磁盘缓存：key = 源文件 hash + 参数 hash；assets/.novacache/ibl/（复用 BinaryCache 读写器）
    static std::shared_ptr<IblEnvironment> loadCached(const std::filesystem::path& equirectPath);
    static void saveCache(const std::filesystem::path& equirectPath, const IblEnvironment& env);
    static bool cacheExists(const std::filesystem::path& equirectPath);
    static uint32_t brdfLutTextureId();   // 全局一张（与具体 HDRI 无关；首用生成/缓存）
};
}
```

**渲染接线（骨架日，c0）**：SceneRenderer 天空段——skyMode==Cubemap 且 skyCubemapPath 非空时：skyCube 加载后 `IBL::loadCached(同源文件)` → `Renderer::setEnvironment(irrId, prefId, lutId, scene.iblIntensity)`；否则 setEnvironment(0,0,0,0)。**天空盒与 IBL 同源（v1 无独立 IBL 源路径）**；无 HDRI（渐变天空/加载失败）→ 无环境绑定，blinn_phong.frag 的 IBL 段贡献 0（程序化回退 = 既有 ambientColor，不黑屏）。

**shader**：
- ibl_brdf_lut.frag：GGX BRDF 积分 LUT（512² 一次性，与场景无关）。
- ibl_irradiance_conv.frag / ibl_prefilter.frag：渲染到 cubemap 6 面（equirect_to_cube 同模式；fragment 路径）；可选 compute 变体 ibl_irradiance.comp/ibl_prefilter.comp（imageStore 直写面）。
- blinn_phong.frag（c0 改，追加式）：`samplerCube uIrrMap(unit 6)/uPrefMap(unit 7)/sampler2D uBrdfLut(unit 8)/float uIblIntensity`；间接光 = irradiance 漫反射 + prefiltered 镜面（LOD = roughness × mip 数）+ BRDF LUT 补偿；uIblIntensity=0 或未绑定（1×1 黑 cubemap 兜底）→ 间接光 0，与 2a 前逐像素一致（回滚路径）。

**序列化键名**：environment 段追加 `"ibl":{"iblIntensity":1.0}`（Scene.h 字段；旧文件缺省 1.0——无 HDRI 时本就无 IBL，有 cubemap 天空的旧场景打开后 IBL 自动生效为增强，用户可调 0 关闭）。

**验收要点（c2）**：
1. 导入 .hdr 设为天空盒（现有路径）：金属球（roughness 0）出现环境反射图像，随 skyRotation 旋转正确。
2. 粗糙度 0→1：间接高光由锐利变柔和（mip 链正确，目测）。
3. 无 HDRI（渐变天空）：场景亮度与 2a 基线一致（IBL 段为 0，回归 A/B）。
4. 卷积缓存：首次烘焙后二次启动 < 1s（日志打烘焙耗时）；删除 assets/.novacache/ibl 后重建正确。
5. 统计层开销 < 1ms（绑定+采样；烘焙不计入帧）。
6. iblIntensity 序列化往返；EnvironmentPanel 加 IBL 强度滑条（c0 合波）。

**风险：中**（compute 驱动差异；一次性烘焙耗时）。**回滚**：uIblIntensity=0 逐像素回退；conv shader 失败 → 无 IBL 环境；烘焙为启动成本可缓存。

---

## 25.3 SSAO（c3，V0.5-5）

**文件落点**：`engine/include/NovaEngine/Render/SSAO.h` + `engine/src/Render/SSAO.cpp`（新增，独占）+ assets/shaders/ssao.frag。PostProcess.cpp 的 AO 合成段、Framebuffer MRT、blinn_phong.frag 的 GBuffer 输出段由 c0 骨架日布（接口本节冻结）。

```cpp
// engine/include/NovaEngine/Render/SSAO.h
namespace NovaEngine {
// 屏幕空间 AO（V0.5-5）：输入 = 场景法线纹理（RGBA16F）+ 线性深度（RGBA16F，view-space -z）；
// 半球采样 → 半分辨率 AO → 复用 post.vert+post_blur.frag 高斯模糊（H/V 各一次）
// → 供 PostProcess combine 合成。参数取自 PostProcessSettings（环境段，见下）。
class SSAO {
public:
    static void init(); static void shutdown();
    static void resize(int w, int h);    // 半分辨率 AO 纹理重建
    static uint32_t render(uint32_t normalTex, uint32_t linearDepthTex, const PostProcessSettings& s);
};
}
```

**PostProcess.h 追加（c0，追加式）**：
```cpp
struct PostProcessSettings {
    // ...既有字段不变...
    bool ssaoEnabled = false;      // 追加：环境段缺省 false（旧文件零变化）
    float ssaoRadius = 0.5f;       // 采样半径（米，view-space）
    float ssaoStrength = 1.0f;     // 遮蔽强度
    int ssaoSampleCount = 16;      // 半球采样数（8/16/32）
};
// PostProcess 追加：
static void setAmbientOcclusionTexture(uint32_t tex);   // 0 = 无 AO（combine 跳过合成）
```

**数据流（骨架日管线，c0）**：Framebuffer::create 加 `bool normalDepth=false` 参数——hdr 模式且 ssaoEnabled 时 MRT：附件 0 颜色（RGBA16F）+ 附件 1 法线（RGBA16F）+ 附件 2 线性深度（RGBA16F），resolve 各附件（+normalTextureId()/depthLinearTextureId()）；blinn_phong.frag 加 `layout(location=1) out vec3 oNormal; layout(location=2) out float oLinearDepth;`（uniform uGbufferPass 开关，off 时零变化）；renderSceneToOutput 内 resolveMsaa 后、PostProcess::apply 前：`if (pp.ssaoEnabled && hdr) SSAO::render(...) → PostProcess::setAmbientOcclusionTexture(aoTex); else setAmbientOcclusionTexture(0)`；post_combine.frag 乘 AO（0 = 乘 1）。**降级**：ssaoEnabled 但非 hdr 路径（RGBA8 直通）或附件创建失败 → AO 跳过 + warn 一次（「永不黑屏」同纪律）。

**序列化键名**：environment.postProcess 段追加 `"ssaoEnabled":false,"ssaoRadius":0.5,"ssaoStrength":1.0,"ssaoSampleCount":16`（缺省兜底，旧文件零警告）。

**验收要点（c3）**：
1. 墙角/物体接触处出现柔和暗部；开关 A/B 目测明显（EnvironmentPanel 加 SSAO 折叠区，c0 合波）。
2. 1080p 开销 < 2ms（统计层：ssaoEnabled on/off 帧时间差）。
3. 模糊后无闪烁/噪点跳动（静止画面连续 120 帧目测）。
4. radius/strength 参数调节即时生效；随场景序列化往返。
5. 降级路径：RGBA8 直通（pp.enabled=false）时 SSAO 自动跳过不崩溃、不黑屏。

**风险：中**（MSAA 多附件 resolve 驱动差异、老集显）。**回滚**：ssaoEnabled=false = 逐像素旧路径；附件创建失败自动关闭。

---

## 25.4 粒子系统 v1（c4，V0.5-6）

**文件落点**：`engine/include/NovaEngine/Particles/ParticleSystem.h`、`ParticleRenderer.h` + `engine/src/Particles/*.cpp`（新目录，独占）+ assets/shaders/particle.vert/.frag + editor/src/panels/ParticleWidgets.h。SceneRenderer 粒子钩子（c0 骨架日布）、InspectorPanel 接线（c0 合波）。

```cpp
// engine/include/NovaEngine/Particles/ParticleSystem.h
namespace NovaEngine {
// 粒子系统组件（V0.5-6 v1）：CPU 模拟粒子池 + GPU 实例化 billboard quad 单 draw call。
// Edit 态渲染「稳态快照」（按 emissionRate×lifetime 预填、时间冻结——参数变更当帧生效，
// 满足「参数编辑即时预览」）；Play 态 onUpdate 全速推进；退出 Play 快照恢复自动清零。
class ParticleSystem : public Component {
public:
    explicit ParticleSystem(GameObject* owner);

    float emissionRate = 30.0f;      // 个/秒
    float lifetime = 1.0f;           // 秒（±20% 随机）
    float initialSpeed = 2.0f;       // m/s
    float gravity = 0.0f;            // 世界 -Y 加速度（m/s²）
    glm::vec4 startColor{1.0f};
    glm::vec4 endColor{1.0f, 1.0f, 1.0f, 0.0f};   // 寿命内线性插值
    float startSize = 0.2f;
    float endSize = 0.05f;
    std::string texturePath;         // 纹理条（flipbook）贴图；空 = 白块
    int tileX = 1, tileY = 1;        // 纹理条网格（寿命内按序播放）
    int maxParticles = 1000;
    bool loop = true;                // false = 播完停止发射
    bool playOnAwake = true;
    bool billboard = true;           // v1 固定 true（相机面向 quad）
    bool blendAdditive = false;      // true = 加性混合（火花/火焰）

    void play(); void stop(); void clear();          // 运行期控制（Play 中脚本可用）
    bool isPlaying() const;
    uint32_t aliveCount() const;

    void onUpdate(float dt) override;                // Play 推进（发射/积分/死亡回收）
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};
}

// engine/include/NovaEngine/Particles/ParticleRenderer.h
namespace NovaEngine {
// 粒子渲染（V0.5-6）：遍历收集全部启用的 ParticleSystem → 合并实例数据一次 VBO 上传
// → 一次 glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, count)。
// 透明段绘制、不写深度；绘制顺序 = 全部透明几何之后（粒子最后，保证可见）。
// 骨架日钩子：SceneRenderer 在 opaque 收集后调用（初始空实现由 c4 填充）。
class ParticleRenderer {
public:
    static void init(); static void shutdown();
    static void collectAndSubmit(const Scene& scene, const RenderCamera& camera);
};
}
```

**shader**：particle.vert（实例属性：位置/颜色/大小/纹理条索引；相机右/上向量展开 billboard quad）+ particle.frag（贴图采样 × 颜色；blendAdditive 加性）。

**序列化键名**：`{"type":"ParticleSystem","emissionRate":30.0,"lifetime":1.0,"initialSpeed":2.0,"gravity":0.0,"startColor":[1,1,1,1],"endColor":[1,1,1,0],"startSize":0.2,"endSize":0.05,"texturePath":"","tileX":1,"tileY":1,"maxParticles":1000,"loop":true,"playOnAwake":true,"billboard":true,"blendAdditive":false}`（缺省兜底；未知键忽略）。

**验收要点（c4）**：
1. 火焰粒子（纹理条 4×4）：Play 中发射→消亡循环正确、UV 动画正确、加性混合火光感正确。
2. 1000 粒子：单 draw call（统计层 lastDrawCalls 增量 = 1、lastParticleCount 一致）、< 2ms（Release 统计层）。
3. 参数编辑即时预览（Edit 态稳态：改 emissionRate/颜色 → SceneView 当帧变化）。
4. 序列化往返无损；退出 Play 粒子清零（快照恢复，重进 Play 重新发射）。
5. loop=false：发射停止后粒子自然消亡；play/stop/clear 脚本接口生效。

**风险：低-中。回滚**：新模块新 shader 独立；钩子空实现 = 零影响；删除 Particles 目录 + 钩子一行即全回滚。

---

## 25.5 脚本 DLL 热重载（c5，V0.5-7）

**文件落点**：`engine/include/NovaEngine/Scripting/ScriptRuntime.h` + `engine/src/Scripting/ScriptRuntime.cpp`（新增）、ScriptRegistry.h/.cpp（追加）、samples/CMakeLists.txt（重写）、samples/*（验证性零改动）、player/src/main.cpp、player/CMakeLists.txt、editor/src/Packager.cpp（独占）。编辑器触发接线归 c0 合波。

```cpp
// engine/include/NovaEngine/Scripting/ScriptRegistry.h（追加）
class ScriptRegistry {
    // ...既有签名不变...
    static void unregisterScript(const std::string& name);   // 追加：单个移除（不存在静默）
    static void removeAllScripts();                          // 追加：DLL 卸载前清空（防旧工厂悬垂）
};

// engine/include/NovaEngine/Scripting/ScriptRuntime.h（新增）
namespace NovaEngine {
// 脚本 DLL 运行时（V0.5-7）：NovaScripts OBJECT 库 → NovaGame.dll（SHARED）。
class ScriptRuntime {
public:
    static void setDllPath(const std::filesystem::path& path);   // 启动时注入（编译期路径）
    static bool load();                                          // LoadLibrary；失败 false + Logger::error
    static void unload();                                        // FreeLibrary（正常路径走 reload）
    static bool isLoaded();
    static const std::string& lastError();

    // 重载核心：快照脚本字段（复用 onSerialize）→ 逐脚本组件替换实例（旧 vtable 实例
    // 不可在 FreeLibrary 后存活）→ 卸载旧 DLL → 加载新 DLL（静态注册器自动重新填充
    // ScriptRegistry）→ 按「对象 ID + 组件下标」原位恢复字段（onDeserialize）。
    // 任一步失败（加载失败/工厂缺失）→ 保留旧 DLL 继续运行，返回 false + error 日志。
    static bool reload(Scene& scene);

    // 字段快照（内存 JSON，不落盘）：每脚本组件 {type, enabled, fields（onSerialize 产物）}
    static std::string snapshotScriptFields(const Scene& scene);
};
}
```

**CMake 与符号裁定（本功能核心技术风险，冻结）**：
- samples/CMakeLists.txt 重写：`add_library(NovaGame SHARED SpinningCube.cpp OrbitCamera.cpp SceneSwitcher.cpp MoveWithInput.cpp)` + target_link_libraries PRIVATE NovaEngine；输出 samples/<Config>/NovaGame.dll。
- **NE_REGISTER_SCRIPT 宏不动**：DLL 加载时静态初始化器自动执行注册，与静态链接行为一致（脚本代码零改动）。
- **符号方向**：引擎代码静态链入 exe；DLL 内脚本类引用引擎符号（vtable/RTTI）——exe 必须导出全部符号：editor/CMakeLists.txt 与 player/CMakeLists.txt 各加 `set_target_properties(<t> PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)`，editor 追加 `target_link_options(NovaEditor PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:NovaEngine>")`（player 已有 /WHOLEARCHIVE）。
- **对象指针跨 DLL**：脚本持有的 GameObject*/Transform* 均为 exe 内存，引擎内存不卸载 → 指针稳定（复杂度评估已确认）；脚本静态变量重置 = 文档化取舍。
- **卸载时序铁律**：先替换全部脚本组件实例，再 FreeLibrary；反序列化在新 DLL 加载后进行（新工厂创建）。
- 编辑器编译期注入：`NOVA_GAME_DLL_PATH="${CMAKE_BINARY_DIR}/samples/$<CONFIG>/NovaGame.dll"`、`NOVA_BUILD_CONFIG="$<CONFIG>"`。

**2b 落地裁定（2026-11-19，c5 提交 e2fdcc6，经主会话确认）**——§25.5 冻结方案的 3 处机制替换（契约意图达成，机制落地调整）：
1. **导出机制替换**：WINDOWS_EXPORT_ALL_SYMBOLS 不扫描静态库成员（CMake 文档限制，实测 objects.txt 仅含 exe 自身 .obj）——改为从 NovaEngine.lib 生成去重 .def（editor/player CMakeLists 内嵌 Python 单遍解析，<10s，按 .lib 时间戳缓存；ENABLE_EXPORTS + /DEF + /WHOLEARCHIVE）。「导出全部引擎符号」意图达成，机制替换。
2. **双 DLL 形态**：DLL 导入表按模块名解析，单 NovaGame.dll 无法同时服务两个 exe——编辑器形态 NovaGame（--target NovaGame，热重载对象）+ Player 形态 NovaGamePlayer（Packager 拷贝），OUTPUT_NAME 均 NovaGame.dll 分居 samples/ 与 player/<Config>/。
3. **整场景 JSON 重建替代逐组件替换**：冻结 GameObject 接口无「按指针移除组件」（removeComponent<T> 按 typeId 匹配，反序列化挂载的脚本组件 typeId=0 不可命中）——reload = 全场景 JSON 快照 → 清旧工厂 → 中间重建（旧实例在 FreeLibrary 前安全虚析构）→ FreeLibrary → LoadLibrary 新 DLL → 再重建按「对象 ID+组件下标」恢复字段。副作用：reload 后全场景 GO/组件指针作废、内置组件 onAwake 重跑。
4. 工厂缺失裁定：新 DLL 加载成功但脚本类型被删 → warn + 丢弃该组件、返回 true（契约字面「工厂缺失保留旧 DLL」会把用户有意删除的脚本永久复活，未采纳）。

**编辑器触发流程（契约）**：
1. 菜单「Scripts → Reload Scripts」+ Ctrl+Shift+R（Edit 态可用；Play 态灰显——Play 快照含旧类型）。
2. 编辑器 spawn `cmake --build <NOVA_PROJECT_ROOT>/build --config <NOVA_BUILD_CONFIG> --target NovaGame` 并等待退出；失败 → 编译输出进 Console，**旧 DLL 保持运行**（不调用 reload）。
3. 成功 → `ScriptRuntime::reload(*m_scene)` → Console 打结果日志（重载脚本数/字段恢复数）。
4. mtime 自动检测（用户裁定 2026-11-18：**默认开**）：每 1s 轮询 NovaGame.dll mtime；Edit 态下变化即触发与手动相同的流程（编译失败 → Console 报错、旧 DLL 保留运行）；Play 态跳过检测（退出 Play 后若 DLL 已变化则触发一次）；菜单「Scripts → Auto Reload」勾选可关（EditorApplication 接线归 c0 合波）。

**Player**：启动时 `ScriptRuntime::setDllPath(exe 目录/NovaGame.dll)` + load()；失败 → 脚本组件反序列化 warn 跳过、场景其余正常；Packager 拷贝 NovaGame.dll 进发布目录（c5 改 Packager.cpp）。

**验收要点（c5）**：
1. 改 SpinningCube.cpp 重编译 → 编辑器自动检测（mtime，默认开）或 Ctrl+Shift+R 手动触发，热重载成功；场景对象脚本字段（speed=30 手改值）保留、无需重启；新代码行为当帧生效；Auto Reload 菜单关闭时仅手动可用。
2. 故意写错编译失败 → 构建输出进 Console、旧 DLL 保留运行、场景不崩。
3. 连续 10 次热重载：无内存增长/崩溃/泄漏痕迹（日志）。
4. 热重载后 Ctrl+P Play 正常（新工厂创建 + 快照恢复）；退出 Play 正常。
5. 热重载后 Ctrl+S 保存/重开场景脚本字段往返无损（反序列化走新注册表）。
6. Player：发布目录含 NovaGame.dll，双击运行脚本行为与编辑器一致。
7. 无 DLL（未编译）启动：编辑器 warn 一次、场景加载正常、Add Component 脚本列表为空（不崩）。
8. 回归：samples 四脚本 DLL 形态行为与静态链接一致（SpinningCube/OrbitCamera/SceneSwitcher/MoveWithInput）。

**风险：高（全波最高；DLL 边界 + 链接器 + RTTI）**。**回滚**：CMake 变量 NOVA_SCRIPTS_DLL 双形态开关（samples/CMakeLists.txt 保留 OBJECT 分支）——回滚 = 关开关恢复 $<TARGET_OBJECTS:NovaScripts>；ScriptRuntime 未加载时全部入口 no-op。**开工前先做半天 PoC**：最小 DLL（单脚本）加载/重载/字段恢复验证链接器方案，通过后再全量改造。

---

## 25.6 地形 v1（c6，V0.5-8）

**文件落点**：`engine/include/NovaEngine/Terrain/Terrain.h`、`TerrainRenderer.h` + `engine/src/Terrain/*.cpp`（新目录，独占）+ assets/shaders/terrain.vert/.frag + PhysicsWorld.h/.cpp（追加）+ editor/src/panels/TerrainPanel.h/.cpp + SceneViewPanel.cpp（笔刷，独占）。SceneRenderer 地形钩子（c0 骨架日布）。

```cpp
// engine/include/NovaEngine/Terrain/Terrain.h
namespace NovaEngine {
// 地形组件（V0.5-8 v1）：512² 高度图 + 4 层材质 splat 混合 + Jolt heightfield 碰撞。
// 高度图资产 = 16bit 灰度 PNG（assets/terrains/*.png；stb 读 ushort 通道）；
// 笔刷改内存高度 → 保存写回 PNG（16bit，stb_image_write——stb 仓库内含，不引入新库）。
class Terrain : public Component {
public:
    explicit Terrain(GameObject* owner);

    std::string heightmapPath;             // 16bit 灰度 PNG；空 = 平面 0 高度
    std::string splatmapPath;              // RGBA 权重图（4 层）；可空
    glm::vec2 worldSize{100.0f, 100.0f};   // 世界尺寸（米；XZ 平面）
    float heightScale = 10.0f;             // 0..1 高度值 → 米
    struct TerrainLayer {                  // 固定 4 层（v1）；splat R/G/B/A 对应 0..3
        std::string albedoPath;            // 空 = 白
        std::string normalPath;            // 空 = 无
        float tileSize = 10.0f;            // 贴图平铺（米/次）
    };
    std::array<TerrainLayer, 4> layers;
    bool collisionEnabled = true;          // Jolt HeightFieldShape 静态体

    // 笔刷 API（编辑器 Edit 态；TerrainPanel/SceneViewPanel 用）：
    bool raycast(const glm::vec3& origin, const glm::vec3& dir, glm::vec3& outHit) const;
    enum class BrushMode { Raise, Lower, Flatten, Smooth };
    void applyBrush(const glm::vec3& worldCenter, float radius, float strength, BrushMode mode);
    void rebuildDirtyRegion();             // 帧末调用：顶点/法线区域重算 + GPU 上传 + 碰撞重建（每帧最多一次）
    float heightAt(float x, float z) const;
    bool saveHeightmap() const;            // 写回 PNG（失败 Logger::error）
    uint32_t resolution() const;           // 512（自高度图）

    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};
}

// engine/include/NovaEngine/Terrain/TerrainRenderer.h
namespace NovaEngine {
class TerrainRenderer {
public:
    static void init(); static void shutdown();
    // 骨架日钩子（空实现默认）：懒建 GPU 网格（高度图→顶点/法线/索引 512²）→ 专用
    // terrain shader submit；castShadows 时地形进 shadowItems
    static void collectAndSubmit(const Scene& scene, const RenderCamera& camera,
                                 const Frustum& frustum, std::vector<ShadowItem>& shadowItems);
};
}

// engine/include/NovaEngine/Physics/PhysicsWorld.h（追加）
void registerTerrainHeightfield(class Terrain* terrain);      // 创建/重建 Jolt HeightFieldShape（静态 body）
void unregisterTerrainHeightfield(class Terrain* terrain);
```

**shader**：terrain.vert（高度图 R16 位移 + 4 层 splat 权重传递）+ terrain.frag（4 层 albedo/normal 按权重混合 + 自含 GGX 光照（与 blinn_phong.frag 同公式，注释互指）+ 阴影采样同 unit 约定）。

**序列化键名**：`{"type":"Terrain","heightmapPath":"assets/terrains/h1.png","splatmapPath":"","worldSize":[100.0,100.0],"heightScale":10.0,"collisionEnabled":true,"layers":[{"albedo":"assets/textures/grass.png","normal":"","tileSize":10.0},{"albedo":"","normal":"","tileSize":10.0},{"albedo":"","normal":"","tileSize":10.0},{"albedo":"","normal":"","tileSize":10.0}]}`（缺省兜底）。

**笔刷接线（SceneViewPanel，c6）**：工具栏「Terrain Brush」按钮（Edit 态，场景含 Terrain 时可用）；左键拖拽在 Terrain 上刷（raycast 命中）；TerrainPanel（dock 底部）：笔刷模式下拉（抬升/压低/平整/平滑）、半径/强度滑条、层材质编辑、Save Heightmap 按钮。

**验收要点（c6）**：
1. 512×512 高度图地形渲染正确（起伏目测 + 4 层 splat 混合过渡自然）；中等画质 Release 60 FPS（统计层）。
2. 笔刷四模式即时生效；Save 后重开场景高度一致（16bit PNG 往返）。
3. 刚体球在起伏地形上滚落（碰撞正确、无穿透）；Show Colliders 显示 heightfield 轮廓。
4. 序列化往返无损；无 heightmap 文件 → 平面地形不崩溃（warn）。
5. 非地形场景零回退（钩子空实现路径，统计层回归）。

**风险：中**（16bit PNG 精度 ±1/65535 可接受；重建耗时——节流每帧一次，笔刷期间 < 30ms/次）。**回滚**：删除 Terrain 目录 + 钩子一行 + PhysicsWorld 追加段。

---

## 25.7 动画控制器（c7，V0.5-9 = V0.3-13 全量 + 状态机）

**范围修正（冻结）**：V0.3-13 未落地（勘察确认无任何动画代码）。本项分两阶段一次交付：**9.1 蒙皮基础**（V0.3-13 范围：蒙皮顶点/骨架/动画通道提取 + 蒙皮着色路径 + 单动画播放）**9.2 状态机**（V0.5-9 范围：状态/参数/过渡/混合 + 列表式面板）。

**文件落点**：`engine/include/NovaEngine/Animation/`（新目录：Skeleton.h/AnimationClip.h/SkeletalMeshRenderer.h/AnimatorController.h/Animator.h 等）+ `engine/src/Animation/*.cpp` + assets/shaders/blinn_phong_skinned.vert + ModelLoader.cpp（骨骼提取实现）+ Mesh.cpp（蒙皮布局）。MeshData.h 蒙皮段与 ModelLoader.h 声明由 c0 骨架日布（接口本节冻结）。

```cpp
// engine/include/NovaEngine/Render/MeshData.h（追加，冻结接口变更——骨架日 c0 布）
namespace NovaEngine {
// 蒙皮顶点（V0.5-9）：独立布局，不并入 Vertex（非蒙皮路径零变化，ROADMAP §10-C2 兼容裁定）
struct SkinnedVertex {
    glm::vec3 position; glm::vec3 normal; glm::vec2 uv; glm::vec3 tangent;
    glm::ivec4 boneIds{0}; glm::vec4 boneWeights{0.0f};   // 权重和归一化由导入器保证
};
struct SkeletonBone { std::string name; int parent = -1; glm::mat4 inverseBind{1.0f}; };
struct Skeleton { std::vector<SkeletonBone> bones; };
struct AnimKey { float time = 0.0f; glm::vec3 position{0.0f}; glm::quat rotation{1,0,0,0}; glm::vec3 scale{1.0f}; };
struct AnimChannel { std::string boneName; std::vector<AnimKey> keys; };
struct AnimationClip { std::string name; float duration = 0.0f; float ticksPerSecond = 1.0f; std::vector<AnimChannel> channels; };
// MeshData 追加（非蒙皮网格留空，旧代码路径逐字节不变）：
std::vector<SkinnedVertex> skinnedVertices;
Skeleton skeleton;
std::vector<AnimationClip> clips;
}

// engine/include/NovaEngine/Animation/SkeletalMeshRenderer.h
namespace NovaEngine {
class SkeletalMeshRenderer : public Component {
public:
    explicit SkeletalMeshRenderer(GameObject* owner);
    std::shared_ptr<Mesh> mesh = nullptr;      // 蒙皮布局 GPU 网格
    std::string meshPath;                      // 资产相对路径
    Material material;
    Skeleton skeleton;                         // 反序列化自 mesh 缓存
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};
}

// engine/include/NovaEngine/Animation/Animator.h
namespace NovaEngine {
// 动画状态机组件（V0.5-9）：参数驱动状态过渡 + 过渡混合；v1 列表式配置（内联 JSON，无独立资产）。
struct AnimatorParameter {
    enum class Type { Float, Bool, Trigger } type = Type::Float;
    std::string name; float value = 0.0f;   // Trigger：运行期置位标记（当帧消费，不序列化）
};
struct AnimatorTransition {
    int from = 0, to = 0;
    float duration = 0.25f;                  // 过渡混合时长（秒）
    struct Condition { std::string parameter; enum class Op { Greater, Less, Equals, BoolTrue } op = Op::Greater; float value = 0.0f; };
    std::vector<Condition> conditions;       // 全部满足才触发（v1 无 Exit Time）
};
struct AnimatorState { std::string name; std::string clip; float speed = 1.0f; bool loop = true; };

class Animator : public Component {
public:
    explicit Animator(GameObject* owner);
    std::vector<AnimatorState> states;
    int defaultState = 0;
    std::vector<AnimatorTransition> transitions;
    std::vector<AnimatorParameter> parameters;

    void setFloat(const std::string& name, float v);    // 脚本 API（Play 中；对齐 Unity 心智）
    void setBool(const std::string& name, bool v);
    void setTrigger(const std::string& name);
    float getFloat(const std::string& name) const;
    const std::string& currentStateName() const;
    void play(const std::string& stateName);   // 直接跳转（无混合）

    void onUpdate(float dt) override;          // 过渡判定/参数消费/混合推进/骨骼矩阵上传
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};
}

// engine/include/NovaEngine/Render/Mesh.h（追加）
void setBoneMatrices(const std::vector<glm::mat4>& mats);   // 懒建 UBO（binding 1，上限 64）；空 = 不绑定
bool isSkinned() const;

// engine/include/NovaEngine/Assets/ModelLoader.h（追加声明，骨架日 c0 布）
static bool extractSkeleton(const aiScene* src, const aiMesh* skinMesh, Skeleton& out);
static bool extractAnimations(const aiScene* src, std::vector<AnimationClip>& out);   // aiNodeAnim/aiMeshAnim
```

> 落地裁定（890f475）：AnimatorParameter/AnimatorTransition/AnimatorState 三结构体实际落点 `Animation/AnimatorController.h`（Animator.h include 之，签名不变）——见 §25.0 骨架日落地裁定 2。

**行为（9.1）**：动画采样（最近关键帧插值 TRS）→ 局部矩阵 → 全局矩阵 × inverseBind → 骨骼矩阵数组 → Mesh::setBoneMatrices → blinn_phong_skinned.vert 蒙皮变换，**复用 blinn_phong.frag（2a 后含 IBL——蒙皮角色自动获得 IBL 间接光）**；单动画播放 = Animator 未配置状态机时的退化形态（1 状态）。**行为（9.2）**：每帧检查当前状态出边过渡条件 → 触发后按 duration 线性混合两状态骨骼矩阵（权重渐变）→ 过渡完切换；Trigger 当帧消费；无 SMR → Animator no-op warn 一次。面板：AnimatorPanel 列表式（状态列表/参数列表/过渡列表，无节点图——ROADMAP 裁定）。

**序列化键名**：
- SkeletalMeshRenderer：`{"type":"SkeletalMeshRenderer","mesh":"assets/models/char.glb","material":{...同 MeshRenderer material 段...}}`
- Animator：`{"type":"Animator","defaultState":0,"states":[{"name":"Idle","clip":"idle","speed":1.0,"loop":true},{"name":"Walk","clip":"walk","speed":1.0,"loop":true}],"transitions":[{"from":0,"to":1,"duration":0.25,"conditions":[{"parameter":"Speed","op":"Greater","value":0.5}]}],"parameters":[{"name":"Speed","type":"Float","value":0.0},{"name":"Running","type":"Bool","value":false}]}`
- 畸形兜底：states 空 → no-op；clip 名找不到 → warn 保持 T-pose；参数未知 → warn 忽略。

**验收要点（c7）**：
1. 导入 Mixamo 角色 FBX/glTF：蒙皮显示正确、无顶点脱离；walk 循环播放流畅。
2. walk→run→idle 按 Speed 参数自动过渡且混合平滑（过渡期两动画权重渐变，目测无跳变/滑步）；过渡时长 0.25s 生效。
3. 状态机配置随场景序列化往返无损；示例角色走跑跳组合正确（整合者搭示例场景）。
4. 骨骼 UBO 更新开销 < 0.2ms/角色（统计层）。
5. 非骨骼模型路径不受影响（回归：普通模型/材质/阴影/后处理零变化）；蒙皮角色参与阴影与实例化分组（单实例走普通 submit）。

**风险：高**（冻结接口变更 MeshData + ModelLoader + 蒙皮 shader；Assimp 各格式骨骼数据差异）。**回滚**：skinnedVertices 为空时全部新路径不触发（MeshData 追加字段对旧代码零影响）；blinn_phong_skinned.vert 独立新文件；提取失败 → 无蒙皮数据 → SkeletalMeshRenderer 显示静态 T-pose 不崩溃。

---

## 25.8 2D 模式（c8，V0.5-10）

**文件落点**：`engine/include/NovaEngine/Scene/SpriteRenderer.h`、`BoxCollider2D.h` + 对应 .cpp、`engine/include/NovaEngine/Physics/Physics2D.h` + `engine/src/Physics/Physics2D.cpp`（新模块）、Camera.cpp（投影实现）、assets/shaders/sprite.vert（新，复用 ui.frag）。Camera.h 投影段、SceneRenderer 的正交投影与 Sprite 收集分支由 c0 骨架日布；SceneViewPanel 的 sprite 拾取分支由 c0 合波补。

```cpp
// engine/include/NovaEngine/Scene/Camera.h（追加，骨架日 c0 布）
enum class ProjectionMode { Perspective, Orthographic };
// Camera 类追加：
ProjectionMode projectionMode = ProjectionMode::Perspective;   // 缺省 = 旧行为零变化
float orthoSize = 5.0f;                                        // 正交半高（米；Unity 语义）

// engine/include/NovaEngine/Scene/SpriteRenderer.h
namespace NovaEngine {
// 2D 精灵渲染（V0.5-10）：世界空间 quad（尺寸 = 贴图像素 / pixelsPerUnit；2D 平面 = 世界 XY）。
// 渲染：透明段（AlphaBlend 固定），按 (sortingLayer, orderInLayer, 视距) 排序——层越大越后绘。
class SpriteRenderer : public Component {
public:
    explicit SpriteRenderer(GameObject* owner);
    std::string texturePath;               // 空 = 白块
    std::shared_ptr<Texture> texture = nullptr;
    glm::vec4 color{1.0f};
    bool flipX = false, flipY = false;
    int sortingLayer = 0;                  // v1 无命名层表
    int orderInLayer = 0;
    float pixelsPerUnit = 100.0f;
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};
}

// engine/include/NovaEngine/Scene/BoxCollider2D.h
namespace NovaEngine {
// 2D AABB 碰撞（V0.5-10 v1）：重叠查询 + 事件回调；v1 无刚体动力学（移动由脚本/Transform 驱动）。
// 独立于 Jolt（不注册进 PhysicsWorld；2D 与 3D 物理共存不互扰）。
class BoxCollider2D : public Component {
public:
    explicit BoxCollider2D(GameObject* owner);
    glm::vec2 center{0.0f}; glm::vec2 size{1.0f};   // 局部（2D 平面 = 世界 XY）
    bool isTrigger = false;
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};
}

// engine/include/NovaEngine/Physics/Physics2D.h（新模块）
namespace NovaEngine {
// 2D 碰撞求解（V0.5-10 v1）：每帧（Play）对全部 BoxCollider2D 做 AABB 重叠对检测
//（O(n²) 上限 500 组件——mini 引擎规模可接受，验收 < 0.5ms）→ 事件回调：
// OnCollision2DEnter/Stay/Exit、OnTrigger2DEnter/Stay/Exit（复用 Events 类型化事件；
// Enter/Exit 边沿由内部状态表维护）。
struct Collision2DEvent { GameObject* other = nullptr; glm::vec2 overlap{0.0f}; };
class Physics2D {
public:
    static void step(Scene& scene);   // Scene::update 内、onUpdate 广播后调用（固定点，契约）
};
}
```

**行为**：正交相机投影：SceneRenderer::assembleFromCamera 按 projectionMode 分支（骨架日）；2D 与 3D 共存：同场景可同时有透视/正交相机，isMain 决定 Play 渲染相机；SpriteRenderer 进 SceneRenderer 收集（sprite.vert 世界变换 quad + 复用 ui.frag）。**2D 模板**：新建场景菜单加「2D Scene」模板（整合者）：正交相机（isMain）+ 2-3 个 sprite + BoxCollider2D 示例，assets/scenes/2DTemplate.nova.json。

**序列化键名**：
- SpriteRenderer：`{"type":"SpriteRenderer","texturePath":"assets/textures/hero.png","color":[1,1,1,1],"flipX":false,"flipY":false,"sortingLayer":0,"orderInLayer":0,"pixelsPerUnit":100.0}`
- BoxCollider2D：`{"type":"BoxCollider2D","center":[0.0,0.0],"size":[1.0,1.0],"isTrigger":false}`
- Camera 追加键：`"projectionMode":"Perspective"|"Orthographic","orthoSize":5.0`（缺省兜底，旧文件零变化）

**验收要点（c8）**：
1. 正交场景中 sprite 正确渲染（尺寸/PPU 正确、flip 生效）；排序层生效（大 layer 后绘覆盖，目测）。
2. 2D AABB 碰撞回调正确：两 sprite 重叠触发 OnCollision2DEnter、分离触发 Exit（脚本日志验证）；isTrigger 事件独立。
3. 2D 模板场景：New 2D Scene 开箱可见（相机+精灵）；Play 可交互。
4. 2D 与 3D 场景共存：透视/正交相机切换 isMain 渲染互不干扰；2D 组件不进 Jolt（无 3D 物理副作用）。
5. 旧文件（Camera 无投影字段）打开 = 透视，零警告；序列化往返无损；500 个 BoxCollider2D 检测 < 0.5ms（统计层）。

**风险：低-中。回滚**：ProjectionMode 缺省 Perspective 零变化；新组件/新模块独立删除。

---

## 25.9 网络多人预研 spike（c9，V0.5-15，独立分支不进 main）

**裁定（用户，2026-11-18）：保留「1 周 spike 原型」，与 2a/2b 解耦。** 单独 Coder c9、独立分支 spike/network-v1（独立 worktree 开发，与 main 并行零文件冲突）；**分支代码不进 main**（spike 完成后，结论文档由主会话写入本小节「落地结论」并合入 main）。

**目标**：确定性帧同步（锁步）最小原型——局域网两台机器同步移动方块。方案：输入帧同步——每帧收集本地输入 → 按帧号经网络同步 → 双方在同一帧号应用输入、各自推进模拟（引擎已有固定步进 60Hz 物理与单线程主循环，直接利用）。网络层用 WinSock2（ws2_32，Windows 平台自带，不引入新第三方库）。

**文件落点（仅 spike 分支）**：`engine/include/NovaEngine/Network/` + `engine/src/Network/`（NetworkManager/NetPeer 等，自由设计）+ 主循环帧同步入口 + 示例场景/脚本。main 侧唯一落点 = 本小节「落地结论」（spike 完成后由主会话写入）。

**交付物（约 1 周，与 2a 同时起）**：
1. 可运行原型：两台机器局域网运行同一场景，一方移动方块，另一方同步，60 秒无明显偏离。
2. 结论文档（写入本小节「落地结论」）：锁步可行性结论、成本拆解（联网层/协议/两机联调/编辑器支持）、V1.0+ 是否纳入的明确建议。
3. ROADMAP §6 #15 行标注「已决策（见 ARCHITECTURE §25.9 落地结论）」。

**验收要点（c9，spike 分支内）**：
1. 双机局域网同步移动方块 60 秒位置一致（真机人工验证；单机双进程 127.0.0.1 先行验证）。
2. 断开/延迟注入下不崩溃，结论中记录处理策略。
3. 结论文档给出明确 V1.0 建议与成本拆解；结论合入 main 后 ROADMAP #15 行有标注。

**风险：中**（Jolt 跨机浮点确定性未验证——spike 用「确定性积分移动方块」绕开物理同步，结论中记录 Jolt 确定性评估；WinSock2 代码全部隔离在 Network 目录）。**回滚**：分支不合并即 main 零影响；结论文档仅一段文字，可随时删。

**落地结论（spike 完成，c9 分支 b2d93f9，2026-11-18）**：
- **可行性：成立。** 锁步与引擎天然契合（物理固定 60Hz 步进 + 单线程主循环）；非阻塞 WinSock2 网络层 ~1100 行即可用；单机双进程实测 60 秒 3482 帧**双方日志逐字节一致零偏离**；80ms 延迟注入（超 3 帧缓冲）仍逐帧精确同步（以慢机为准停等，设计行为）；断开/强杀不崩溃。
- **Jolt 确定性评估**：固定步进是必要非充分条件——Jolt 用 8 线程 JobSystemThreadPool 多线程求解器，官方仅承诺「同二进制+同平台」确定性，本引擎**未实测验证**；引擎侧另有帧率依赖逻辑/容器遍历顺序/脚本非确定性等不确定源。跨机物理锁步需双机万帧比对实测 + 必要时改单线程求解器（性能代价）——最大不确定项 2-6 周。
- **成本拆解**：联网层产品化（重连/超时/多玩家/NAT）1-2 周；协议完善（显式小端序列化/版本协商）1-2 周；真机联调与缓冲调参 1 周；编辑器支持（网络面板/Play 联机入口）2-3 周；物理确定性验证改造 2-6 周——合计约 8-14 周（1.5-2 人月）。
- **V1.0+ 建议**：① V1.0 **不纳入**跨机 Jolt 物理锁步（确定性未验证，风险高）；② V1.0 **可选纳入**「局域网双人锁步（确定性积分移动、无物理同步）」——约 2-3 周，可直接收编 spike 代码；③ 全物理联机留 V1.1+，先在「锁步+确定性验证」与「服务器权威状态同步」之间二选一再决策。
- **分支处置**：spike/network-v1 保留不合并（用户裁定）。真机双机验证步骤见 c9 报告（同一局域网两机：主机 `--net host`，从机 `--net join --net-ip <主机IP>`，HUD 双方位置一致；定量验收 `--net-automove --net-duration 60` 后比对双方 Logs/NetSync_*.log 逐字节一致）。

---

## 25.10 波 2 通用验收与回归（合波后，整合者 c0）

- **合波构建**：Debug 全量零错误零第一方警告；Release 构建通过；双 exe 冒烟（编辑器守卫 25 条 + Player 场景加载）。
- **波 1 零回归（重点）**：2000 立方体剔除+实例化（draw calls < 20、Release 60FPS）；UI 三分辨率布局；存档往返；CLI 退出码；手柄/InputMap。
- **后处理双路径回归**：pp.enabled A/B、RGBA8 直通；SSAO/IBL 关闭时 = 2a 前逐像素基线（A/B 对比）。
- **schema v4 回归**：demo.nova.json（旧文件）打开参数不变；全部新组件序列化往返；未知组件 warn 跳过。
- **统计层数字随波记录**（draw calls/帧时间/tris）作为验收证据（§12.7 纪律）。
- 编辑器守卫与序列化回归清单 18 → 25 条（内置组件数）。
- samples 四脚本 DLL 形态行为与静态链接一致（回归）。
- 2a 合波先跑一次全量回归，2b 合波再跑一次；两次均通过才推送（AGENTS.md 流程）。

---

# V1.0 波 1 契约（2026-11-20 冻结）

> 范围：V1.0-9（测试框架+CI）/ #1（Profiler 完整版）/ #6（纹理压缩与批量导入）/ #7（资产引用完整性）/ #8（音频完善）。
> 分工：cT（V1.0_Coder_Tooling，#9）/ cR（V1.0_Coder_Runtime，#1）/ cG（V1.0_Coder_Rendering，#6）/ cA（V1.0_Coder_AssetsAudio，#7+#8）四线并行；共享文件窗口 = 骨架日 + 合波日。V1.0 无专职 c0，整合职责由主会话执行（V0.5 波 2 合波「主会话执行」同款先例）。
> 本节号承接 §25，新增 §26–§28 三章（对应 V1.0 三个波）；每节含：文件落点、公共签名（头文件级）、序列化键名、验收要点、风险与回滚。

## 26.0 总则：三波总览、所有权分区、冻结头总清单与第三方库裁定

**三波总览（依赖铁律）**：

| 波 | 功能（编号） | 线 | 依赖排序理由 |
|---|---|---|---|
| 波 1（约 1.5 周） | #9 / #1 / #6 / #7 / #8 | cT / cR / cG / cA×2 | #9 测试先行（回归防护覆盖后续全部波，历史事故点转测试）；#1 内存统计是 #13 前置；#6 压缩产物供 #1 内存面板展示（同波骨架日定接口）；#7 打包预检给 #11 让路；#8 为 #10 提供 BGM/总线 |
| 波 2（约 2 周） | #2 / #3 / #4 / #5 / #11 | cR×2 / cG×2 / cT | #2 异步加载为架构级变化（ROADMAP §10-D），接口先冻结再实现；#3 与 #2 同域（cR 域内顺序：#3 先 M 级速赢 → #2 L 级）；#4 改后处理链、#5 改材质提交路径——渲染共享文件最密集，骨架日一次性化解；#11 依赖 #7（预检，波 1 已交付）与 build.novabuild 多场景收集（静态扫描，不等 #2 实现完成） |
| 波 3（约 1.5–2 周） | #13 / #12 / #10 / #14 | cR / cT / cC×2 | #13 审计 #1（内存统计）与 #2（场景切换）的产物；#10 依赖 #1/#2/#3/#8/#11 全部合波后开工；#14 最后（以实际代码为准） |

**波 1 文件所有权严格分区（并行安全前提）**：

| 线 | 目录/文件 | 说明 |
|---|---|---|
| cT | tests/、cmake/、.github/workflows/ | tests/CMakeLists.txt 重写；Dependencies.cmake 增补 Catch2 + bc7enc_rdo（裁定见下） |
| cR | engine/include\|src/Profiling/（新目录全部）、engine/src/Scene/Scene.cpp（update 测量段）、editor/src/panels/ProfilerPanel.h/.cpp（新增） | Scene.cpp 波 1 内 cR 独占（其余线不碰） |
| cG | engine/include\|src/Assets/{TextureLoader,TextureMeta,BinaryCache,AssetDatabase}.h/.cpp、engine/include\|src/Render/Texture.h/.cpp、editor/src/panels/AssetBrowserPanel.cpp（导入设置压缩下拉） | 纹理压缩全链路 |
| cA | engine/include\|src/Assets/AssetReferenceScanner.h/.cpp（新增）、engine/include\|src/Audio/*、engine/include\|src/Scene/AudioSource.h/.cpp、editor/src/Packager.cpp（打包预检）、editor/src/panels/{ReferenceCheckerPanel,MixerPanel}.h/.cpp（新增） | AudioSource 为 Scene/ 目录波 1 唯一跨目录特例；其余 Scene/ 文件一律不动 |
| 主会话（骨架日/合波日） | 见下方清单 | 唯一合法触碰共享文件者 |

**骨架日清单（波 1 第 0 天，主会话执行，一次性完成）**：
1. editor/CMakeLists.txt：`src/*.cpp` + `src/panels/*.cpp` 显式清单 → `file(GLOB_RECURSE ...)` 收敛为自动收集（imgui/imguizmo 源保持显式）——终结三起 LNK2019 类事故（PrefabManager/BuildSettingsPanel/InputBindingsPanel 均已备案）；新面板 .cpp 从此自动收录。
2. Render/Mesh.h 的 create 与 Primitives：GL 就绪守卫（glad 未加载 → 返回 nullptr + Logger::warn）——运行期行为零变化（运行期必先 Renderer::init）；历史遗留 PlayCycleTest 无 GL 崩溃（KNOWN-ISSUES 2026-11-18 段）就此闭环。
3. Scene/SceneRenderer.h：GPU timer 查询包裹宏 `NE_GPU_SCOPE(name)` 定义 + renderSceneToOutput 内 6 处 pass 包裹（shadow/sky/opaque/transparent/post/ui；空实现默认 = 关闭时零开销）。GL 不支持计时查询 → 段恒 0（Profiler 面板显示 "n/a"）。
4. Assets/AssetDatabase.h：`static MemorySnapshot memorySnapshot();` 声明（MemorySnapshot 定义于 Profiling/Profiler.h，assets→profiling include 合法）；Render/Texture.h：`sizeBytes()` 声明（cG 实现）。
5. NovaEngine.h 伞头追加 include（Profiling/Profiler.h、Assets/AssetReferenceScanner.h）。
6. 骨架日结束后共享文件冻结，任何 Coder 不得再触碰（越界改动 = 合波回退，§24.0/§25.0 同纪律）。

**合波日清单（主会话）**：面板 dock 接线（ProfilerPanel/ReferenceCheckerPanel/MixerPanel）+ 菜单接线（Window→Profiler、Assets→Reference Checker、Audio→Mixer）、AssetBrowser 右键入口（Reference Checker）、EditorApplication::run 与 player/src/main.cpp 的 Profiler beginFrame/endFrame + pushSection 接线、编辑器守卫与序列化回归清单核对（内置组件数 25 不变——波 1 无新组件）、NovaTests 跑绿、双 exe 冒烟。

**V1.0 冻结接口变更总表（三波合计；签名见各子节，均为「追加式」或「新入口」，既有签名零变更）**：

1. Render/Mesh.h + Primitives：GL 就绪守卫（波 1 骨架日；运行期零变化）
2. Scene/SceneRenderer.h：NE_GPU_SCOPE 宏包裹（波 1 骨架日，空实现默认）；+renderScenesToOutput 新入口（波 2 骨架日；renderSceneToOutput/renderScene 保留转发）
3. Render/Texture.h：+sizeBytes()（波 1，cG 追加式）
4. Assets/AssetDatabase.h：+memorySnapshot()（波 1 骨架日声明、cG 实现、波 3 cR 收口校准）
5. Assets/BinaryCache.h：+loadTextureCompressed/saveTextureCompressed（波 1，追加式 payloadType 3）
6. Assets/TextureMeta.h：TextureImportSettings +compression 字段（波 1，追加式缺省 Auto）
7. Audio/AudioEngine.h：+AudioBus/+setBusVolume/busVolume/playStreaming/setBus/fadeIn/fadeOut/loadSettings/saveSettings（波 1，追加式）
8. Scene/AudioSource.h：+bus/stream/fadeInSeconds/fadeOutSeconds（波 1，追加式缺省零值）
9. Scene/SceneManager.h：+loadSceneAsync/unloadScene/loadedScenes/updateAll/setSceneParam/getSceneParam/clearSceneParams + SceneUnloaded 事件（波 2，追加式）
10. Assets/AsyncLoader.h：新增（波 2）
11. Physics/PhysicsWorld.h：+raycastAll/overlapSphere/overlapBox/setLayerName/layerName/setCollisionEnabled/collisionEnabled（波 2，追加式）；PhysicsTypes.h：RaycastHit +hitObject（追加式）
12. Scene/Collider.h：+layer/friction/restitution（波 2，追加式缺省兜底）
13. Scene/CharacterController.h：新增组件（波 2，内置组件数 25→26）
14. Scene/Scene.h：+physicsCollisionMatrix/physicsLayerNames（波 2，environment.physics 段）
15. Render/PostProcess.h：PostProcessSettings +lutPath/lutIntensity/dofEnabled/dofFocusDistance/dofAperture/vignetteEnabled/vignetteIntensity/grainEnabled/grainAmount（波 2，追加式缺省全关）；PostProcess +setLut（追加式）
16. Render/Material.h：+shaderOverride/shaderGraphPath（波 2 骨架日；operator== 扩展含 shaderGraphPath）
17. 新增头：Profiling/Profiler.h、Assets/AssetReferenceScanner.h（波 1）；Render/ColorGrading.h、ShaderGraph/{ShaderGraphAsset,ShaderGraphCodeGen}.h（波 2）
18. GLSL：改 post_combine.frag（LUT/vignette/grain 段，波 2 骨架日）；新 post_dof.frag；Shader Graph 生成 shader 复用 blinn_phong.vert + 引擎 uniform 契约
19. CMake：editor/CMakeLists.txt src/ 改 GLOB_RECURSE（波 1 骨架日）；Dependencies.cmake +catch2 v3.16.0 +bc7enc_rdo（波 1，cT）；tests/CMakeLists.txt 重写（NovaTests + ctest）；project VERSION 0.1.0 → 1.0.0 + cmake/version.rc.in + editor/player RC 接入（波 2 骨架日/合波，cT）
20. 序列化 schema：场景 JSON version 保持 4（三波全部纯增量——新字段缺省兜底、旧文件往返无损、未知组件 warn 跳过）；新资产类型 assets/shadergraphs/*.novashadergraph（v1）、assets/audio/audio-settings.novaaudio（v1）；.novameta +compression 字段（增量）；build.novabuild 保持 v1；BinaryCache 新增压缩 payloadType（版本不 bump，缓存键扩展）；.cube 为数据文件非资产类型
21. Packager：buildPlayer 开头加引用预检（波 1，cA）；+buildRelease 新入口（波 2，cT；buildPlayer 保留语义不变）

**通用纪律**：新组件接入序列化零改动 SceneSerializer（ComponentRegistry 按名分发 + NE_REGISTER_COMPONENT 自注册，组件 .cpp 末尾放宏）；engine/CMakeLists.txt 为 GLOB_RECURSE（新 .cpp 自动收录）；editor/CMakeLists.txt GLOB 化后新面板自动收录（改 CMake 后需重 configure）；无新组件波次不更新守卫数；渲染改动必须附统计层数字（ROADMAP §12-7 纪律）。

**第三方库裁定（V1.0 共 2 个新依赖，均已批准）**：
1. **Catch2 v3.16.0（V1.0-9，批准）**：BSL-1.0；FetchContent 一条命令，Catch2WithMain target + catch_discover_tests 接 ctest。自研手写断言宏成本 1–2 天，但参数化/分区/报告器/IDE 集成/CI 集成全部缺失且需长期维护——集成成本远低于自研。接入：Dependencies.cmake `FetchContent_Declare(catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.16.0 GIT_SHALLOW TRUE)` 并入 MakeAvailable 列表（tag 2026-09 发布，DevOps 配置期核实）。
2. **bc7enc_rdo（V1.0-6，批准）**：MIT/Unlicense 双许可，单源文件家族覆盖 BC1–BC7 编码（含 RDO）；仓库无 tag → pin master commit（stb 同款纪律，DevOps 核实写入）。否决项：自研 BC7（64 模式搜索空间，正确实现 >2 人周且质量难达标）、DirectXTex（DX/DDK 依赖重）、stb_dxt（无 BC7）、NVTT（体积大）。接入：Populate 成静态库，**仅编 bc7enc.cpp + rgbcx.cpp**（ISPC 加速器与 lodepng 不编）。
3. 其余 12 项全部自研或消费既有库能力：miniaudio 内置 fade（ma_sound_set_fade_in_milliseconds）与文件流式播放（ma_sound_init_from_file）、Jolt CharacterVirtual（§27.2）均属既有依赖的能力消费，非新库。

## 26.1 测试框架 + CI（cT，V1.0-9）

**文件落点**：tests/CMakeLists.txt（重写）、tests/*.cpp（PlayCycleTest.cpp 迁入）、cmake/Dependencies.cmake（Catch2 段）、.github/workflows/ci.yml（新增）。

**tests/CMakeLists.txt 目标形态**：

```cmake
add_executable(NovaTests
    TestMain.cpp TestScene.cpp TestSerialization.cpp TestAssets.cpp TestMath.cpp
    TestInput.cpp TestSaveGame.cpp TestSceneManager.cpp TestMemory.cpp)   # 或 GLOB tests/*.cpp（排除实现头）
target_link_libraries(NovaTests PRIVATE NovaEngine Catch2::Catch2WithMain)
catch_discover_tests(NovaTests)   # ctest 注册（本地: ctest --test-dir build -C Debug）
```

- PlayCycleTest 原用例 catch 化迁入 TestScene.cpp；原 PlayCycleTest target 删除（「用后即删」注释兑现）。
- **无 GL 上下文纪律**：测试进程不 init GL——Mesh 上传路径靠 §26.0 骨架日 GL 守卫跳过；场景往返测试用含 MeshRenderer 的场景 JSON（onDeserialize 的 GPU 上传自动降级，材质/meshPath 字段语义照常验证）。
- **测试 harness 必须 try/catch 防弹窗**（2026-11-19 c7 教训备案：deserialize 类型不符字段抛 type_error 未捕获 → 测试进程 abort 弹窗挂死）。

**覆盖清单（对照 KNOWN-ISSUES 历史事故点逐条转测试）**：
1. 序列化往返：ComponentRegistry 全部内置组件类型逐一（当前 25 项）——「meshPath 字段名」事故（V05Wave2Demo 全白）转成正反用例；未知组件 warn 跳过；version 1/2/3/4 旧文件读取兜底。
2. 场景操作：createGameObject 父指针（2026-11-16 层级拍平事故）、setParent/重父子、destroy 队列、Play 快照往返（原 PlayCycleTest）。
3. 资产缓存：BinaryCache 载荷往返 + 损坏文件删除重导 + 缓存键稳定性；meta serialize 稳定文本。
4. 数学：Frustum AABB 相交（含边界）、退化用例。
5. InputMap 默认绑定往返；SaveGame 原子写与浅校验；SceneManager 入队/切换/DDOL 迁移（无 GL 可跑）。
6. 协程基础推进/销毁回收；事件 swap-flush。
7. 崩溃防御族：畸形 JSON（无 Transform）、nlohmann 类型不符字段（safeGet 路径）。

**CI（.github/workflows/ci.yml）**：push/PR 触发 windows-latest；`cmake -S . -B build`（VS2022 生成器，Python 已预装）→ Debug 全量构建 → `ctest -C Debug --output-on-failure` → Release 全量构建 → 失败时归档 Logs/。仓库 git@github.com:148627654/NovaEngine.git。**注意**：CI 机器无 GPU——NovaTests 不得依赖 GL（纪律见上）；编辑器/Player 完整冒烟由 DevOps 本机执行（CI 只跑 NovaTests）。

**验收要点（cT，照抄细化 ROADMAP §7-9）**：
1. CI 全绿（构建+测试）：push 后 workflow 完成、Debug/Release 双构建零错误、NovaTests 全部通过。
2. 序列化往返测试覆盖全部组件类型（25 项逐一）；破坏性改动被 CI 拦截（故意破坏一处字段名 → 对应用例红）。
3. 本地 ctest 全绿；历史事故点用例 ≥ 6 族可对照 KNOWN-ISSUES 事故条目。
4. 不破坏本地构建（DevOps 双配置验证）。

**风险：低。回滚**：删 tests/ 与 workflow；Dependencies.cmake 删 catch2 段。**序列化**：无。

## 26.2 Profiler 完整版（cR，V1.0-1）

**文件落点**：engine/include/NovaEngine/Profiling/Profiler.h + engine/src/Profiling/Profiler.cpp（新目录，独占）；editor/src/panels/ProfilerPanel.h/.cpp（新增）；Scene.cpp 测量段（波 1 独占）；SceneRenderer GPU 包裹（骨架日）。

```cpp
// engine/include/NovaEngine/Profiling/Profiler.h
namespace NovaEngine {

// 内存快照（V1.0-1；V1.0-6 填贴图压缩字节、V1.0-13 收口校准）
struct MemorySnapshot {
    size_t meshVertexBytes = 0;     // 顶点+索引字节（MeshData 上传时累计）
    size_t textureBytes = 0;        // GPU 贴图占用估算：未压缩 = w*h*4；压缩 = 块数据字节（Texture::sizeBytes 汇总）
    size_t audioBytes = 0;          // AudioClip 解码 PCM 字节（stream=true 的流式源不计入）
    size_t cubemapBytes = 0;
    size_t materialAssetCount = 0;
    size_t objectCount = 0, componentCount = 0;   // 活动场景统计
    size_t loadedSceneCount = 0;    // V1.0-2 落地后生效（波 1 恒 1）
};

// 帧记录（环形缓冲）
struct FrameRecord {
    uint64_t frameNumber = 0;
    float frameMs = 0.0f;
    float sectionMs[kMaxSections];      // 定长数组（见下）；未上报 = -1
    float gpuPassMs[kMaxGpuPasses];     // shadow/sky/opaque/transparent/post/ui；延迟 3 帧回读
    MemorySnapshot memory;
};

class Profiler {
public:
    static constexpr uint32_t kCapacity = 600;   // 10 秒 @60FPS（sizeof(FrameRecord) 实现时自测并写入汇报）
    static constexpr int kMaxSections = 64;
    static constexpr int kMaxGpuPasses = 6;
    static void beginFrame();        // 帧首（Stats::beginFrame 之后）
    static void endFrame();          // 帧末（swapBuffers 前）
    static void pushSection(const char* name, float ms);   // 命名分段；同名累加；名字表帧末定型
    static const FrameRecord* frame(uint32_t index);       // 0 = 最旧；越界 nullptr
    static uint32_t frameCount();
    static uint32_t capacity();
    static void clear();
    static MemorySnapshot memorySnapshot();   // 转发 AssetDatabase::memorySnapshot + 活动场景统计
    static float lastOverheadMs();   // 自身开销自测（验收 <1ms 证据）
};
}
```

- **CPU 火焰图**：两档——模块档（update/physics/render/ui 由主循环 pushSection）与脚本档（Scene::update 广播循环内按脚本类型名累计 onUpdate 耗时，`pushSection("script:" + 类名, ms)`；kMaxSections=64 上限，超出合并为 "script:其他"）。宏 `NE_PROFILE_SCOPE(name)` 供引擎内部随手插桩。
- **GPU timer**：GL_TIME_ELAPSED query 环形（延迟 3 帧回读防管线停顿）；每帧最多 6 段（骨架日包裹点）。
- **ProfilerPanel**：帧时间线（600 帧环形折线，点击定位某帧）+ 该帧 CPU 段条形图（火焰图简版：横向堆叠条）+ GPU 段 + 内存分类表；F8 是 Stats，Profiler 走 Window 菜单 + 工具栏按钮（接线合波日）。
- **接线**：EditorApplication::run 与 player/main.cpp 的 beginFrame/endFrame + pushSection 调用点（合波日，两处）。

**序列化**：无（纯运行时数据，不进场景 JSON）。

**验收要点（cR，照抄细化 ROADMAP §7-1）**：
1. 录制 10 秒帧数据定位到最慢脚本函数：demo 场景挂 5 个不同耗时脚本，Profiler 面板可见脚本名分段耗时排序，与代码逻辑一致。
2. GPU 各 pass 耗时可见（shadow/opaque/post 数值随场景复杂度变化；不支持计时查询的机器段显示 n/a 不崩溃）。
3. 内存分类统计与实际一致：载入 10 张 1024² 贴图 → textureBytes ≈ 40MB（未压缩）或压缩后显著下降（V1.0-6 联动）；二次切换场景后数字回落。
4. Profiler 自身开销 < 1ms（lastOverheadMs 自测 + Stats 帧时间 A/B）。
5. 统计层（F8）与 Profiler 共存无冲突。

**风险：低-中。回滚**：删 Profiling/ 目录 + 面板 + 骨架日钩子还原（钩子空实现默认即旧行为）。**shader**：无。

## 26.3 纹理压缩与批量导入（cG，V1.0-6）

**文件落点**：engine/include|src/Assets/{TextureLoader,TextureMeta,BinaryCache,AssetDatabase}.h/.cpp、engine/include|src/Render/Texture.h/.cpp（sizeBytes）、editor/src/panels/AssetBrowserPanel.cpp（导入设置模态压缩下拉）；cmake/Dependencies.cmake 由 cT 加 bc7enc 段（§26.0）。

**压缩格式裁定**：BC7（RGBA）/ BC1（RGB）/ BC5（RG 法线）。自动策略：meta compression=Auto 时——法线贴图（.novameta colorSpace=Linear 且用途为法线）→ BC5；贴图含有效 alpha 通道 → BC7；其余 → BC1。GPU 能力查询（GL_TEXTURE_COMPRESSED 下查 GL_COMPRESSED_RGBA_BPTC_UNORM / GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM / GL_COMPRESSED_RGBA_S3TC_DXT1_EXT）：不支持所选 → 逐级回退（BC7→BC1→未压缩 RGBA8），warn 一次。能力标志进缓存键（GPU 能力变化 → 键变自动重导）。

**接口（追加式）**：

```cpp
// Assets/TextureMeta.h 追加
enum class TextureCompression { Auto, None, BC1, BC7, BC5 };
struct TextureImportSettings {
    // ...既有字段不变...
    TextureCompression compression = TextureCompression::Auto;   // 追加（.novameta 文本增量，旧 meta 读缺省 Auto）
};

// Assets/BinaryCache.h 追加（压缩载荷 payloadType 3）
// [format:u8][w:u32][h:u32][mipCount:u8][blockBytes:u64][块数据]
// format: 0=BC7 1=BC1 2=BC5；mip 链由编码器逐级生成（压缩纹理不可 glGenerateMipmap）
static bool loadTextureCompressed(const std::string& key, int& outFormat, int& outW, int& outH,
                                  std::vector<uint8_t>& outBlocks);
static bool saveTextureCompressed(const std::string& key, int format, int w, int h,
                                  const std::vector<uint8_t>& blocks);
// 缓存键 = makeKey(源字节, meta 文本 + ";comp=<format>;caps=<能力标志>")（makeKey extraText 语义不变）

// Render/Texture.h 追加
size_t sizeBytes() const;   // 压缩 = 块字节；未压缩 = w*h*4
```

- 编码时机：AssetDatabase::getTexture 导入路径——解码 RGBA8 →（Auto 且 GPU 支持）→ bc7enc 编码（BC7 高质模式）→ 压缩缓存落盘 → glCompressedTexImage2D 上传（含 mip 链）；meta compression=None 或 GPU 不支持 → 既有 RGBA8 路径零变化。
- **不做**：不产出 .dds 资产文件（压缩产物 = BinaryCache 缓存，延续「二进制缓存仅是解码产物缓存」原则 ROADMAP §1.3-3）；不做运行时压缩（全部离线）。
- 批量导入：CLI --import 全量导入即全量压缩重缓存（改 meta compression 后缓存键变 → 自动重导，无需新 CLI 参数）。
- Profiler 联动：textureBytes 上报压缩后字节（「纹理内存统计」= Profiler 面板内存分类 + 导入日志逐贴图 before/after 字节）。
- AssetBrowser 导入设置模态加 compression 下拉（Auto/None/BC1/BC7/BC5 + 说明）。

**序列化键名**：.novameta 追加 `"compression": "auto"|"none"|"bc1"|"bc7"|"bc5"`（旧文件缺省 auto，零警告）。场景 JSON 不变。

**验收要点（cG，照抄细化 ROADMAP §7-6）**：
1. 压缩后 GPU 内存显著下降（Profiler 可见）：10 张 1024² 带 alpha 贴图 textureBytes 从 ≈40MB 降至 ≈13MB（BC7）。
2. 画质损失可接受：压缩前后同屏 A/B 目测（BC7 高质模式）；法线贴图 BC5 后光照无明显变差。
3. 批量导入时间可接受：CLI --import 全量（demo 全部资产）耗时记录；二次导入命中缓存 <2s。
4. 压缩格式不支持时自动回退：强制 None / 模拟不支持（能力标志注入）→ RGBA8 路径照常，warn 一次。
5. 二进制缓存兼容：旧 RGBA8 缓存照常读取（payloadType 0 不动）；损坏压缩缓存删除重导。

**风险：中。回滚**：压缩路径默认 Auto 且回退链完整——最坏情况 = 未压缩旧行为；删 bc7enc 段 + 压缩分支即全回滚。**shader**：无。**新库**：bc7enc_rdo（已批准，§26.0）。

## 26.4 资产引用完整性（cA，V1.0-7）

**文件落点**：engine/include/NovaEngine/Assets/AssetReferenceScanner.h + engine/src/Assets/AssetReferenceScanner.cpp（新模块，独占）；editor/src/Packager.cpp（预检，波 1 内 cA 独占）；editor/src/panels/ReferenceCheckerPanel.h/.cpp（新增）。

```cpp
// engine/include/NovaEngine/Assets/AssetReferenceScanner.h
namespace NovaEngine {
struct AssetReference {
    std::string assetPath;    // 项目相对路径（被引用的资产）
    std::string kind;         // "mesh"/"texture"/"materialAsset"/"cubemap"/"audioClip"/"prefab"/"scene"/"font"/"terrainHeightmap"/"lut"/"shaderGraph"
    std::string fromFile;     // 引用方文件（场景/材质/预制体/build 列表路径）
};

class AssetReferenceScanner {
public:
    static std::vector<AssetReference> scanScene(const std::filesystem::path& scenePath);
    static std::vector<AssetReference> scanMaterial(const std::filesystem::path& matPath);
    static std::vector<AssetReference> scanPrefab(const std::filesystem::path& prefabPath);  // 递归嵌套，深度上限 8 防环（§22 既有纪律）
    static std::vector<AssetReference> scanProject();   // assets/ 全量：场景+.novamat+.novaprefab+build.novabuild
    // 移动资产：磁盘移动 + 全项目引用重写（场景 JSON/.novamat/.novaprefab/build.novabuild）
    // 重写保序无损（nlohmann::json 读改写，键序 std::map 稳定；浮点精度既有「无损重排」备案）；
    // 返回被重写的引用数；移动失败/目标已存在 → -1 + Logger::error
    static int moveAsset(const std::filesystem::path& from, const std::filesystem::path& to);
    // 存在性判定经 AssetStream（pak 命中优先、磁盘回退——与既有资产读取语义一致）
};
}
```

- **覆盖形态清单（cA 先出清单再实现，V1.0_Coder_AssetsAudio 工作流程要求）**：场景 JSON 路径字段全集——MeshRenderer.mesh、Material{albedoMap/normalMap/metallicMap/roughnessMap/emissionMap}Path、materialAssetPath、skyCubemapPath、AudioSource.clipPath、PrefabInstance.prefabPath、Terrain.heightmapPath、Text.fontPath；.novamat 贴图路径；.novaprefab 递归全部路径；build.novabuild 场景条目。**V1.0-4/5 新路径（lutPath/shaderGraphPath）波 2 合波日由主会话按本节 kind 约定补两行扫描（契约预留 kind 常量，实现留扩展位）**。
- **打包前完整性检查（并入 V0.5-1）**：Packager::buildPlayer 开头调用 scanScene(场景)+逐引用存在性检查 → 任一缺失：逐条 Logger::error（含引用方文件）→ 返回 false 不产出 pak——「打包遇缺失引用直接失败并列出清单」。
- ReferenceCheckerPanel：扫描按钮（全项目）+ 缺失列表（kind/缺失路径/引用方，双击打开引用方文件定位）+ Move Asset 对话框（移动/重命名 + 全项目重链）+ 修复后复扫。

**序列化**：无新 schema（只读扫描 + 重写时保持原 JSON 结构；重写不 bump version）。

**验收要点（cA，照抄细化 ROADMAP §7-7）**：
1. 删除贴图后扫描器列出所有引用缺失场景（含 .novamat/.novaprefab 内部引用，逐条 kind 正确）。
2. 移动资产后引用自动更新：移动一张贴图（改名+换目录）→ 全部引用方 JSON 路径更新 → 场景打开无 warn、画面不变。
3. 打包遇缺失引用直接失败并列出清单：删一个被引用贴图 → buildPlayer 返回 false、日志逐条、不产出 pak。
4. 扫描/重写不损坏文件：全项目往返扫描零误报（所有存在的引用零告警）。

**风险：低-中。回滚**：预检钩子删一行；扫描器独立模块。**注意**：重写引用方文件是「编辑资产文件」——必须在扫描通过且用户确认后执行（面板交互：先扫后修），不静默改文件。

## 26.5 音频完善（cA，V1.0-8）

**文件落点**：engine/include|src/Audio/AudioEngine.h/.cpp（追加式）、engine/include|src/Scene/AudioSource.h/.cpp（追加字段）、editor/src/panels/MixerPanel.h/.cpp（新增）。AudioSource 为 Scene/ 目录波 1 唯一跨目录特例（§26.0 表）。

```cpp
// engine/include/NovaEngine/Audio/AudioEngine.h 追加（既有签名零变更）
enum class AudioBus : uint8_t { Master = 0, Music, Sfx, Ui, Count };   // 4 总线固定
// 总线音量：Master 与既有 setMasterVolume 同源（同一变量，两入口等价）
static void setBusVolume(AudioBus bus, float v);
static float busVolume(AudioBus bus);
// 流式播放：miniaudio ma_sound_init_from_file（文件流式解码，不整载 PCM）;
// relPath 经 AssetStream（pak 兼容）；失败返回 0
static AudioHandle playStreaming(const std::filesystem::path& relPath, AudioBus bus,
                                 float volume, bool loop, float spatialBlend,
                                 float minDistance, float maxDistance);
static void setBus(AudioHandle h, AudioBus bus);           // 播放中改总线
static void fadeIn(AudioHandle h, float seconds);          // miniaudio 内置 ma_sound_set_fade_in_milliseconds
static void fadeOut(AudioHandle h, float seconds);         // （库能力消费，非新依赖）
// 音频设置持久化（assets/audio/audio-settings.novaaudio，version 1）：
//   { "version":1, "master":1.0, "buses":{ "music":1.0, "sfx":1.0, "ui":1.0 } }
// 缺失/损坏 → 内置默认 + warn，不崩溃
static bool loadSettings(const std::filesystem::path& relPath);
static bool saveSettings(const std::filesystem::path& relPath);
```

```cpp
// engine/include/NovaEngine/Scene/AudioSource.h 追加字段（序列化缺省兜底）
int bus = 0;                 // AudioBus 枚举值（0=Master）
bool stream = false;         // true = 流式播放（长 BGM）；false = 既有整载 PCM 路径
float fadeInSeconds = 0.0f, fadeOutSeconds = 0.0f;   // 0 = 无淡变
```

- 播放路径：AudioSource::Play——stream=false 走既有 play(clip)；stream=true 走 playStreaming（clipPath 直接给路径，不预解码 AudioClip、不进 m_audioClipCache）；onAwake/析构 stop 语义不变（流式句柄同池管理）。fadeOutSeconds>0 时 Stop 前先 fadeOut（延迟 stop 的实现自选，验收只认「无爆音」）。
- 接线：AudioEngine::init 后编辑器 main 与 player main 调用 loadSettings（缺省路径 assets/audio/audio-settings.novaaudio；缺失自动生成默认文件——与 .novameta 同纪律；两处接线归合波日）。
- **与 V1.0-12 编辑器设置的边界（冻结裁定）**：音频设置 = 引擎级（Player 可读，随项目 assets/ 走、随 pak 打包）；编辑器偏好（吸附值/主题/快捷键/最近文件）= 编辑器级（%APPDATA%/NovaEngine/editor-settings.json，波 3）。两者不合并。
- MixerPanel：Master/Music/Sfx/Ui 四滑条 + Save/Reload；调节实时生效（setBusVolume 即时应用于活动句柄）。

**序列化键名**：AudioSource 组件段追加 `"bus":0,"stream":false,"fadeInSeconds":0.0,"fadeOutSeconds":0.0`（旧文件缺省零值，场景 JSON version 4 不变）。新资产类型：assets/audio/audio-settings.novaaudio（version 1）。

**验收要点（cA，照抄细化 ROADMAP §7-8）**：
1. 音量总线调节实时生效：MixerPanel 拉 SFX 滑条 → 场景中音效即时变音量；Master 与 setMasterVolume 行为一致。
2. 长 BGM 流式播放内存平稳：≥3 分钟音频，stream=true 时 Profiler audioBytes 不含该 BGM PCM（对比 stream=false 的整载峰值），播放无卡顿。
3. 淡入淡出无爆音：fadeIn/fadeOut 2s 循环听测；快速进出 Play 无爆音（既有验收项回归）。
4. 设置保存：改总线音量 → Save → 重启编辑器 → 音量保持（audio-settings.novaaudio 往返）。

**风险：低。回滚**：追加式接口，删面板与字段即回滚。**注意**：stream=true 的 AudioClip 不再进 m_audioClipCache（避免双路径缓存语义混乱）。

## 26.6 波 1 整体验收（合波后）

- 合波构建：Debug 全量零错误零第一方警告；Release 构建通过；双 exe 冒烟（编辑器守卫 25 条 + Player 场景加载）。
- NovaTests 全绿（本地 ctest）；CI workflow 文件本地语法校验（首次 push 后验证 runner 侧）。
- Profiler 面板：demo 录制 10s 帧数据、脚本分段可见；内存分类数字合理。
- 纹理压缩：Profiler textureBytes 压缩前后对比数字；CLI --import 退出码 0 且二次 <2s。
- 引用扫描 + 打包预检：删贴图 → 扫描命中 + buildPlayer 失败清单。
- 混音器：总线滑条实时生效；音频设置往返。
- 全量回归 V0.2/V0.3/V0.5 验收清单（重点：Play 快照恢复、序列化 v4 往返、热重载 DLL、后处理双路径）。
- 统计层数字随波记录（ROADMAP §12-7 纪律）。

## 26.7 波 1 风险（给 DevOps/Reviewer）

1. editor/CMakeLists.txt GLOB 化：imgui/imguizmo 源保持显式——GLOB 只收 src/，若误收 imgui 源会双定义（评审核对）；GLOB 无 CONFIGURE_DEPENDS（保持简单，新文件需重 configure——DevOps 知悉）。
2. 压缩缓存键含能力标志：getTexture 每次导入查 GL 压缩能力——缓存键稳定性测试必含（§26.3 验收 5）；BC7 编码耗时（1024² 约 0.5–2s）首次导入可接受，DevOps 记录数字。
3. Profiler 环形缓冲：kCapacity=600 时 sizeof(FrameRecord) ≈ 330B → 总量 ≈ 200KB，可接受；cR 实现时用 sizeof 自测写入汇报（若 >5MB 回报 Architect 重议）。
4. GPU timer 延迟回读：query 对象复用环形，未就绪跳过（不阻塞）；不支持扩展 → 段恒 0 且面板标 n/a。
5. AudioSource.stream 与热重载/序列化共存：追加字段缺省零值 → 旧场景行为零变化（回归点）。
6. 打包预检的路径归一：扫描器 kind/路径与 Packager 收集清单必须同口径（相对项目根、正斜杠、无 ".."）——评审对照。
7. Catch2/bc7enc FetchContent 在无网环境配置失败——既有全部依赖同纪律（首次配置需网），无新增风险。
8. NovaTests 链接 NovaEngine 含 Jolt：测试进程无 GL 但 PhysicsWorld 构造起 job 线程——测试退出须完整析构 Scene，防悬挂线程挂死 ctest。

---

# V1.0 波 2 契约（2026-11-20 冻结）

> 范围：V1.0-2（多场景工作流）/ #3（物理增强）/ #4（色彩分级与后期完善）/ #5（可视化着色器编辑器）/ #11（发布流水线）。
> 分工：cR（#2+#3，域内顺序：#3 先 M 级速赢 → #2 L 级架构）/ cG（#4+#5，域内顺序：#4 先 → #5）/ cT（#11）；渲染共享文件（SceneRenderer/Material/SceneSerializer/post_combine.frag）最密集，骨架日一次性化解。
> 本节号承接 §26，新增 §27 一章。

## 27.0 总则：所有权分区、骨架日纪律与冻结头变更

**文件所有权严格分区（并行安全前提）**：

| 线 | 独占文件 | 共享文件接触 |
|---|---|---|
| cR | engine/include\|src/Assets/AsyncLoader.h/.cpp（新）、Scene/SceneManager.h/.cpp、Scene/CharacterController.h/.cpp（新）、Physics/PhysicsWorld.h/.cpp、Physics/PhysicsTypes.h、Scene/Collider.h/.cpp、editor/src/panels/PhysicsLayersPanel.h/.cpp（新）、editor/src/panels/PhysicsWidgets.h | 无（SceneSerializer 环境段/ComponentRegistry/SceneRenderer 多场景入口由主会话骨架日布） |
| cG | engine/include\|src/Render/PostProcess.h/.cpp、Render/ColorGrading.h/.cpp（新）、ShaderGraph/（新目录全部）、assets/shaders/post_dof.frag（新）、editor/src/panels/EnvironmentPanel.cpp（Color Grading 折叠区）、editor/src/panels/ShaderGraphPanel.h/.cpp（新） | 无（Material.h 字段/post_combine.frag LUT 段/SceneSerializer 环境字段由主会话骨架日布） |
| cT | cmake/（version.rc.in 新）、顶层 CMakeLists.txt（project VERSION）、player/CMakeLists.txt、player/src/main.cpp、editor/src/Packager.cpp（buildRelease）、editor/res/app.ico（新）、.github/workflows/ci.yml（release 打包 job 扩展） | 无 |
| 主会话（骨架日/合波日） | 见清单 | SceneRenderer.h/.cpp、Material.h、Scene.h（environment.physics）、SceneSerializer.cpp（environment.physics + postProcess 新字段）、ComponentRegistry.cpp、NovaEngine.h、post_combine.frag、editor/src/panels/InspectorPanel.cpp、editor/src/EditorApplication.cpp |

**骨架日清单（波 2 第 0 天，主会话执行）**：
1. SceneRenderer.h：`renderScenesToOutput(...)` 新入口声明（§27.1 签名）+ 实现 = renderSceneToOutput 的多场景包装（旧入口保留转发，单场景行为零变化）。
2. Material.h：+shaderOverride（shared_ptr<Shader> 前向声明）+shaderGraphPath（§27.4）；operator== 扩展含 shaderGraphPath（实例化分组键）。
3. Scene.h：+physicsCollisionMatrix/physicsLayerNames 字段声明；SceneSerializer.cpp：environment.physics 段读写 + environment.postProcess 新字段（§27.3）读写——两处一次完成。
4. ComponentRegistry.cpp：CharacterController link-guard +1（内置组件 25→26）。
5. post_combine.frag：LUT 采样/渐晕/颗粒段（§27.3 位置裁定：ACES 后、gamma 前）。
6. NovaEngine.h 伞头：+Assets/AsyncLoader.h、Scene/CharacterController.h、Render/ColorGrading.h、ShaderGraph/ShaderGraphAsset.h。
7. 顶层 CMakeLists.txt：project VERSION 0.1.0 → 1.0.0。
8. 骨架日结束后共享文件冻结（同纪律）。

**合波日清单（主会话）**：面板 dock 接线（ShaderGraphPanel/PhysicsLayersPanel）、EnvironmentPanel Color Grading 区接线、InspectorPanel 接线（CharacterController 区路由 PhysicsWidgets、材质区 shaderGraphPath 拖放+Detach）、EditorApplication Play 循环与 player main 的主循环接线（§27.1 清单：updateAll/flushPendingLoad/flushPendingResults）、AssetBrowser 右键 New→Shader Graph、守卫与序列化回归清单 25→26 条、NovaTests 跑绿（+波 2 新用例）、双 exe 冒烟。

**波 2 冻结头变更总清单**：
1. Scene/SceneManager.h：+loadSceneAsync/unloadScene/loadedScenes/updateAll/setSceneParam/getSceneParam/clearSceneParams + SceneUnloaded 事件（追加式）
2. Assets/AsyncLoader.h：新增（§27.1 签名）
3. Scene/SceneRenderer.h：+renderScenesToOutput（新入口；renderSceneToOutput/renderScene 保留）
4. Physics/PhysicsWorld.h：+raycastAll/overlapSphere/overlapBox/setLayerName/layerName/setCollisionEnabled/collisionEnabled（追加式）；PhysicsTypes.h：RaycastHit +hitObject（追加式）
5. Scene/Collider.h：+layer/friction/restitution（追加式缺省兜底）
6. Scene/CharacterController.h：新增组件（注册表 +1）
7. Scene/Scene.h：+physicsCollisionMatrix/physicsLayerNames（environment.physics 段）
8. Render/PostProcess.h：PostProcessSettings +lutPath/lutIntensity/dofEnabled/dofFocusDistance/dofAperture/vignetteEnabled/vignetteIntensity/grainEnabled/grainAmount（追加式缺省全关）；PostProcess +setLut（追加式）
9. Render/Material.h：+shaderOverride/shaderGraphPath（追加式；operator== 扩展）
10. 新增头：Render/ColorGrading.h、ShaderGraph/{ShaderGraphAsset,ShaderGraphCodeGen}.h
11. GLSL：改 post_combine.frag（LUT/vignette/grain）；新 post_dof.frag；Shader Graph 生成 shader 复用 blinn_phong.vert + 引擎 uniform 契约
12. CMake：project VERSION 1.0.0；cmake/version.rc.in；player/CMakeLists.txt +RC；editor/CMakeLists.txt +RC（GLOB 已覆盖源）
13. 序列化：场景 JSON version 4 不变（environment.physics/environment.postProcess 新字段/Collider 新字段/CharacterController 新组件/material 段 shaderGraphPath 全部纯增量）；新资产类型 assets/shadergraphs/*.novashadergraph（version 1）；.cube 为数据文件（非资产类型，Packager 收集归 cT）

## 27.1 多场景工作流（cR，V1.0-2）

**架构裁定（ROADMAP §10-D 落地）**：唯一新线程 = 资产加载线程。铁律：线程只做 CPU 侧（JSON 解析、MeshData/贴图解码、Assimp）；GPU 上传（Mesh::create/Texture 创建/材质资产解析挂 GPU）一律回主线程 flush 执行；物理、渲染、脚本、UI 保持主线程。

```cpp
// engine/include/NovaEngine/Assets/AsyncLoader.h
namespace NovaEngine {
// 后台资产加载（V1.0-2）：单工作线程 + 任务队列（std::thread + mutex + condition_variable）
class AsyncLoader {
public:
    static void init();       // 引擎启动（Renderer::init 之后）；线程启动即阻塞等待任务
    static void shutdown();   // 停止 + join（丢弃未完成任务；须先于 Renderer::shutdown）
    // 入队场景加载：线程内 = 读 JSON + 全部资产 CPU 解码（MeshData/贴图像素/材质资产）
    // → 结果封装成主线程执行体（GPU 上传 + 场景挂载）入结果队列
    static bool loadSceneAsync(const std::string& name, bool additive);  // 经 SceneManager 解析路径；查无 → false
    static void flushPendingResults();   // 主线程帧末（SceneManager::flushPendingLoad 之前）：依次执行结果体
    static float progress();             // 0..1（完成/总数；无任务 = 1）
    static uint32_t generation();        // 取消语义：每次 reset/加载目标变更递增，旧代际结果作废
    static void reset();                 // 清队列 + generation++（退出 Play/LoadScene 时调用）
};
}
```

```cpp
// engine/include/NovaEngine/Scene/SceneManager.h 追加（追加式，既有语义零变化）
static bool loadSceneAsync(const std::string& name, bool additive = false);   // 入队异步（帧末生效）；失败同 loadScene
static bool unloadScene(const std::string& name);   // 仅 additive 场景可卸载；activeScene 卸载 = error + false
static const std::vector<Scene*>& loadedScenes();   // activeScene 在前（渲染/update 顺序）
static void updateAll(float dt);                    // 全部已加载场景 update（activeScene 先；替代单 activeScene::update 调用点）
// 场景间参数：loadScene/loadSceneAsync 调用瞬间快照参数表；新场景脚本 onStart 期间可读
static void setSceneParam(const std::string& key, const std::string& jsonValue);
static std::string getSceneParam(const std::string& key);    // 无 → 空串
static void clearSceneParams();
struct SceneUnloaded { std::string name; };        // 卸载完成事件（Events::post 延迟投递）
```

- **每 Scene 独立 PhysicsWorld 已成立**（Scene 级服务）：additive 场景自带物理世界，updateAll 内各自 step（帧内顺序：主场景先）。
- **渲染合并**：renderScenesToOutput——环境（天空/IBL/雾/后处理参数）与相机取 scenes[0]（主场景）；对象遍历合并全部场景（剔除/阴影/透明排序/实例化分组跨场景统一收集）；additive 场景不画自己的天空。
- **帧内顺序扩展（§15 修订）**：pollEvents → SceneManager::updateAll{flush+物理+onUpdate+协程+事件} → AsyncLoader::flushPendingResults → SceneManager::flushPendingLoad → AudioEngine::setListener → renderScenesToOutput → 显示。两处主循环（EditorApplication Play 循环 / player main）按此重排（接线归 cT，清单由本节提供）。
- **取消语义**：loadScene 同步路径与 loadSceneAsync 共用代际——任一新的加载请求先 reset()（旧结果作废不挂载）。
- **退出 Play**：全部 additive 场景卸载 → 编辑场景快照还原（既有语义）；Play 中 LoadScene 的 DDOL 迁移（§14.9）对 additive 场景同样生效（DDOL 子树随主场景迁移）。
- **内存回落**：unloadScene 内 Scene 析构后显式调用 AssetDatabase::releaseUnused()（波 3 增强审计）。
- **编辑器侧 v1 最小化**：Build Settings 不变（additive 由脚本 API 驱动）；可选 Play 工具栏已加载场景列表（合波日一行，可选）。多场景打包收集见 §27.5。

**序列化**：无新场景 schema；build.novabuild 不变（additive 是运行时概念）。

**验收要点（cR，照抄细化 ROADMAP §7-2）**：
1. 大厅+关卡 additive 共存：Play 中脚本 loadSceneAsync("Level", true) → 大厅保持渲染，关卡场景对象异步出现（进度条 = progress() 驱动的 UI 示例）。
2. 切换无全黑帧：异步加载期间主场景持续渲染（无长停顿；帧时间尖峰记录于统计层）。
3. 后台加载不卡主线程：加载期与空载期主线程帧时间对比（统计层数字）。
4. 卸载后内存回落：unloadScene 后 memorySnapshot（Profiler）回落到加载前基线（±10%）。
5. 场景间参数传递正确：加载前 setSceneParam("playerHp","100") → 关卡脚本 onStart 读 getSceneParam 拿到 100。
6. 取消与异常：加载中触发新 LoadScene → 旧结果不挂载、无崩溃；加载线程内异常（坏 JSON/坏模型）不传播（error 日志 + 该场景加载失败标记）。
7. 退出 Play 一切还原（PlayCycleTest 回归）。

**风险：高（本波最高）。回滚**：unload 全部新方法 + 主循环重排还原（同步 loadScene 路径即旧行为）。**审重点**：代际取消、线程异常隔离、GPU 上传顺序、与热重载 DLL 共存（加载线程不触碰 ScriptRegistry/DLL）。

## 27.2 物理增强（cR，V1.0-3）

**选型裁定**：CharacterController 封装 **Jolt CharacterVirtual**（库内现成：楼梯步进/坡度限制/地面检测内置）——不自研运动学胶囊（库能力消费，非新依赖）。

```cpp
// engine/include/NovaEngine/Scene/CharacterController.h（新增组件）
namespace NovaEngine {
class CharacterController : public Component {
public:
    explicit CharacterController(GameObject* owner);
    float radius = 0.4f, height = 1.8f;          // 胶囊
    float stepHeight = 0.35f;                    // 楼梯步进高度
    float slopeLimitDeg = 45.0f;                 // 坡度上限（超过 = 阻挡）
    float mass = 70.0f, gravityScale = 1.0f;
    bool isGrounded() const;                     // Play 中有效
    void move(const glm::vec3& displacement);    // Play 中；Edit no-op（与物理组件同纪律）
    glm::vec3 velocity() const;
    void setVelocity(const glm::vec3& v);
    void onAwake() override; void onUpdate(float dt) override;
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
    // 注：与 Rigidbody 互斥（同 GO 挂两者 = 启动 warn + CharacterController 优先）
};
}
```

```cpp
// engine/include/NovaEngine/Physics/PhysicsWorld.h 追加
struct RaycastFilter { uint32_t layerMask = 0xFFFFFFFF; GameObject* ignore = nullptr; };
bool raycastAll(const glm::vec3& origin, const glm::vec3& dir, float maxDist,
                const RaycastFilter& filter, std::vector<RaycastHit>& outHits);   // 全部命中按距离升序
bool overlapSphere(const glm::vec3& center, float radius, const RaycastFilter& filter,
                   std::vector<GameObject*>& outObjects);
bool overlapBox(const glm::vec3& center, const glm::vec3& halfExtents, const RaycastFilter& filter,
                std::vector<GameObject*>& outObjects);
// 碰撞层（矩阵对称）
static constexpr int kMaxLayers = 16;
void setLayerName(int layer, const std::string& name);
const std::string& layerName(int layer);
void setCollisionEnabled(int a, int b, bool enabled);
bool collisionEnabled(int a, int b) const;

// PhysicsTypes.h：RaycastHit 追加
GameObject* hitObject = nullptr;   // 命中对象（触发器/静态体所属 GO）
```

```cpp
// engine/include/NovaEngine/Scene/Collider.h 追加字段
int layer = 0;                       // 碰撞层（0 默认）
float friction = 0.5f, restitution = 0.0f;   // 物理材质 v1 = Collider 内联（不发明 .novaphysmat 资产——务实裁定）
```

- 层矩阵语义：Collider.layer 决定归属；矩阵决定层间是否碰撞（Jolt ObjectLayer/CollisionGroup 映射，pimpl 内实现）；矩阵配置 = 场景级（environment.physics 段）。
- 物理材质：Jolt Material 映射（摩擦组合规则 v1 取平均，注释文档化）。
- 层矩阵编辑 UI：PhysicsLayersPanel（16 层命名 + 16×16 对称矩阵勾选 + 恢复默认；写 environment.physics）。

**序列化键名（场景 JSON version 4 不变）**：
- environment.physics：`"physics":{"layerNames":["Default","Enemy",...16],"collisionMatrix":[[true,false,...],...16×16]}`（缺省 = 全通矩阵 + 默认层名，旧文件零变化）。
- Collider 组件段：`"layer":0,"friction":0.5,"restitution":0.0`（缺省兜底）。
- 新组件：`{"type":"CharacterController","radius":0.4,"height":1.8,"stepHeight":0.35,"slopeLimitDeg":45.0,"mass":70.0,"gravityScale":1.0}`。

**验收要点（cR，照抄细化 ROADMAP §7-3）**：
1. 角色控制器上下楼梯/斜坡不卡住：台阶高 0.3m（<stepHeight）可上、0.5m 阻挡；20°/30° 斜坡可走、60° 阻挡；斜坡行走速度不衰减为 0。
2. 射线查询返回完整命中信息：raycastAll 命中 3 个堆叠物 → 距离升序、每项 point/normal/distance/hitObject 正确；ignore 过滤生效。
3. 层过滤生效：Enemy 层与 Default 层矩阵关断 → 两层刚体互相穿过；打开恢复碰撞。
4. 摩擦/弹性参数效果可见：同斜坡同初速 friction 0.1 vs 1.0 滑行距离差异目测；restitution 0.8 弹跳 vs 0 落地不弹。
5. 30/60/144 FPS 下固定步进行为一致（既有物理验收口径回归）。
6. CharacterController 随场景序列化往返；退出 Play 快照还原（位置还原）。

**风险：中。回滚**：全部追加式——删组件注册与新方法即回滚；矩阵缺省全通 = 旧行为。**审重点**：Jolt CharacterVirtual 与固定步进累加器集成（每物理步调用）、与既有 Rigidbody 注册互斥断言。

## 27.3 色彩分级与后期完善（cG，V1.0-4）

**文件落点**：engine/include|src/Render/ColorGrading.h/.cpp（新：.cube 解析 + LUT 纹理）、PostProcess.h/.cpp（追加）、assets/shaders/post_dof.frag（新）、EnvironmentPanel.cpp（Color Grading 折叠区）；post_combine.frag LUT 段骨架日布。

```cpp
// engine/include/NovaEngine/Render/ColorGrading.h
namespace NovaEngine {
// .cube LUT 解析（V1.0-4）：3D 优先（LUT_3D_SIZE N；DOMAIN_MIN/MAX）；1D 仅解析降级
// 数据经 AssetStream（pak 兼容）；失败 → nullptr 语义（loadLut 返回 false）+ Logger::warn
class ColorGrading {
public:
    static bool loadLut(const std::filesystem::path& relPath);   // 上传 GL_TEXTURE_3D（RGBA16F 优先，降级 RGBA8）
    static void unloadLut();                                     // 清纹理（路径为空/加载失败时 PostProcess 调用）
    static uint32_t lutTexture();                                // 0 = 无 LUT
};
}
```

```cpp
// engine/include/NovaEngine/Render/PostProcess.h 追加
struct PostProcessSettings {
    // ...既有字段不变...
    std::string lutPath;            // .cube 相对路径（空 = 关）
    float lutIntensity = 1.0f;      // 0..1 混合
    bool dofEnabled = false;        // 简版 DOF（可选）
    float dofFocusDistance = 5.0f, dofAperture = 0.05f;
    bool vignetteEnabled = false;   float vignetteIntensity = 0.4f;
    bool grainEnabled = false;      float grainAmount = 0.05f;   // 时间抖动胶片颗粒
};
class PostProcess {
    // ...既有不变...
    static bool setLut(const std::filesystem::path& relPath);   // 设置变更时调用（内部缓存纹理 id；失败 warn + 关闭 LUT）
};
```

- **链顺序裁定（冻结）**：brightpass → bloom → **DOF（scene 线性 HDR：CoC 计算 → 2-pass 可变半径盒式模糊（半分辨率）→ 合成）** → combine{ACES → **LUT（线性 LDR，intensity 混合）→ vignette → grain** → gamma}。LUT 在 ACES 后、gamma 前（Unity 同款位置）。
- DOF 简版：CoC = f(深度, focusDistance, aperture)；2-pass 半分辨率盒式（半径量化 8 档）；开销目标 <1ms @1080p。
- post_dof.frag：单 shader 双 pass（uHorizontal 开关）。
- post_combine.frag 增段：uLut（sampler3D unit 8）+ uLutIntensity/uVignette/uGrain（uniform 追加式；无 LUT 时走直通分支——永不黑屏契约保持）。
- EnvironmentPanel：Color Grading 折叠区（LUT 路径拖放 + intensity 滑条；DOF 开关+焦点/光圈；vignette/grain 开关+强度）——全部随场景序列化（environment.postProcess 段）。
- .cube 为数据文件（无 .novameta）；**Packager 收集 .cube 归 cT（§27.5）**（扩展名识别即可）。

**序列化键名（environment.postProcess 段追加，version 4 不变）**：`"lutPath":"","lutIntensity":1.0,"dofEnabled":false,"dofFocusDistance":5.0,"dofAperture":0.05,"vignetteEnabled":false,"vignetteIntensity":0.4,"grainEnabled":false,"grainAmount":0.05`（旧文件缺省全关零变化）。

**验收要点（cG，照抄细化 ROADMAP §7-4）**：
1. LUT 切换氛围对比明显：内置 2 个示例 .cube（暖色/冷色，assets/luts/ 验收物料）+ 用户 .cube 加载生效；intensity 0→1 平滑过渡。
2. 后期设置序列化往返：改 LUT/DOF/渐晕 → Ctrl+S → 重开 → 参数不变（环境面板与 JSON 一致）。
3. 链上各环节开销可在 Profiler 查看：post 段 GPU 耗时随 LUT/DOF 开关变化（§26.2 骨架日钩子联动）。
4. 降级路径：无 LUT/加载失败/8bit FBO → 直通分支画面正常不崩溃（永不黑屏契约）。
5. DOF：焦点远近景模糊对比目测明显；开关 A/B 无残留。

**风险：中。回滚**：字段缺省全关 = 旧行为；删 shader 段与面板即回滚。**审重点**：LUT 采样在 16F 线性域（HDR）与 RGBA8 直通路径的一致性（直通路径 LUT 同样生效）。

## 27.4 可视化着色器编辑器（cG，V1.0-5）

**文件落点**：engine/include|src/ShaderGraph/（新目录：ShaderGraphAsset.h/.cpp、ShaderGraphCodeGen.h/.cpp）、editor/src/panels/ShaderGraphPanel.h/.cpp（新）；Material.h 字段骨架日布。

**资产格式（.novashadergraph，version 1，存 assets/shadergraphs/）**：

```json
{ "version": 1,
  "nodes": [ { "id": "n1", "type": "TextureSample", "pos": [80, 120],
               "params": { "path": "assets/textures/checker.png" } },
             { "id": "master", "type": "Master", "pos": [640, 200], "params": {} } ],
  "edges": [ { "from": ["n1", "rgba"], "to": ["master", "albedo"] } ] }
```

**节点集 v1（约 16 个，超出即明确提示不支持）**：输入类 UV/Time/Color/Scalar/Vector2/Vector3/TextureSample（+path 参数）；运算类 Add/Sub/Mul/Div/Lerp/Clamp/OneMinus/Normalize/Dot；主输出 Master 槽 albedo/roughness/metallic/normal/emission。端口带类型（float/vec2/vec3/vec4/sampler2D），连线类型不匹配 = 拒绝 + 面板提示（不静默）。

```cpp
// engine/include/NovaEngine/ShaderGraph/ShaderGraphAsset.h
namespace NovaEngine {
// 图资产（V1.0-5）：JSON 资产 + 节点/边模型 + 端口类型校验
class ShaderGraphAsset {
public:
    static std::shared_ptr<ShaderGraphAsset> load(const std::filesystem::path& relPath);  // AssetStream；失败 nullptr
    static bool save(const std::filesystem::path& relPath, const ShaderGraphAsset& graph);
    // 校验：环/类型不匹配/缺失输入 → 错误列表（明确错误文本，面板直接展示）
    std::vector<std::string> validateGraph() const;
};
}

// engine/include/NovaEngine/ShaderGraph/ShaderGraphCodeGen.h
namespace NovaEngine {
// 代码生成（V1.0-5）：生成 GLSL 片元源码（含 #line + 节点注释映射——编译错误可定位到节点）
// 顶点部分复用 blinn_phong.vert（不生成 vs）；uniform 契约与 blinn_phong.frag 完全一致
//（CameraBlock/uLightDir/uLightColor/uAmbient/uView/材质槽 uniform/sampler2D unit 0-5——
//  PBR 光照函数模板内嵌生成器，与 blinn_phong.frag 同步维护、源码注释互指）
class ShaderGraphCodeGen {
public:
    // 生成成功返回 true；失败时 outErrors 为节点级错误文本（行号经 #line 映射回节点 id）
    static bool generate(const ShaderGraphAsset& graph, std::string& outFragmentSource,
                         std::vector<std::string>& outErrors);
};
}
```

```cpp
// engine/include/NovaEngine/Render/Material.h 追加（骨架日布）
std::shared_ptr<Shader> shaderOverride = nullptr;   // 运行时生成 shader（前向声明；非空 = 提交走该 shader）
std::string shaderGraphPath;                        // .novashadergraph 相对路径（序列化用）
// operator== 扩展含 shaderGraphPath（实例化分组键——生成 shader 材质不参与实例化分组，见下）
```

- **提交路径裁定（冻结）**：SceneRenderer 收集阶段——material.shaderOverride 非空 → 该 RenderItem 标记「自定义 shader」独立队列（不实例化分组、不混入共享材质队列；排序仍按 opaque/transparent）；uniform 设置复用 setBlinnPhongUniforms 同契约（生成器保证同名同型）。shaderGraphPath 非空但资产缺失 → warn + 回退 blinn_phong（不崩溃）。
- **编译错误定位**：Shader::createFromSource 失败 → 解析 GL 编译 log 行号 → #line 映射表反查节点 id → 面板高亮 + 显示完整 log（映射失败兜底）。
- **ShaderGraphPanel**：画布（ImGui 自绘：节点框 + bezier 连线 + 拖拽）+ 节点面板（分类列表拖入画布）+ 选中节点参数区 + 编译按钮/状态区（错误节点高亮）；保存/加载 .novashadergraph；AssetBrowser 右键 New→Shader Graph（合波日）。
- **Inspector 接线（合波日）**：材质区 shaderGraphPath 拖放（AssetBrowser 源）+ Detach；编译失败状态展示。

**序列化键名**：场景 JSON material 段追加 `"shaderGraphPath":""`（缺省空 = 旧行为）。新资产类型：assets/shadergraphs/*.novashadergraph（version 1）。

**验收要点（cG，照抄细化 ROADMAP §7-5）**：
1. 用节点连出「颜色×纹理+发光」shader 并赋给材质生效：Mul(Color, TextureSample)→albedo + Color→emission，Play/Edit 画面符合预期。
2. 生成 shader 编译错误可定位到节点：故意制造非法表达式 → 面板高亮对应节点 + 明确错误文本。
3. 保存/加载图资产：.novashadergraph 往返无损（节点/边/参数逐项一致）；删除图资产 → 场景加载 warn + 回退默认 shader 不崩溃。
4. 不支持的特性明确提示而非静默错误：未实现节点/循环依赖/端口类型不匹配全部面板级提示。
5. 打包：引用 .novashadergraph 的场景打包后 Player 渲染一致（Packager 收集 .novashadergraph 归 cT，§27.5）。

**风险：中-高。回滚**：shaderOverride 空 = 旧路径全回滚；删面板与资产类即可。**审重点**：生成 GLSL 的 uniform 布局与 setBlinnPhongUniforms 严格一致（错位 = 花屏，评审对照）；#line 映射表在 shader 编译失败（无 program）时仍可用（映射表编译前构建）。

## 27.5 发布流水线（cT，V1.0-11）

**文件落点**：cmake/version.rc.in（新）、editor/res/app.ico（新）、顶层 CMakeLists.txt（VERSION 1.0.0，骨架日）、editor/CMakeLists.txt + player/CMakeLists.txt（RC 接入）、player/src/main.cpp（默认启动场景）、editor/src/Packager.cpp（buildRelease + 新资产收集）、.github/workflows/ci.yml（release 打包 job）。

**版本信息注入（Windows RC）**：cmake/version.rc.in 模板——FileVersion/ProductVersion（CMake project VERSION 注入 1.0.0）、FileDescription/ProductName/CompanyName/LegalCopyright（NOVA_PRODUCT/NOVA_COMPANY 编译定义，默认 "NovaEngine"/"Nova"）、Icon（editor/res/app.ico）。两个 exe 各生成 RC 对象。exe 属性验收 = 资源管理器右键 → 详细信息。

```cpp
// editor/src/Packager.h 追加（buildPlayer 保留，语义不变）
namespace NovaEditor {
// 发布构建（V1.0-11）：buildPlayer 的发行级扩展
// 收集 = build.novabuild 全部场景 + 全场景引用资产（AssetReferenceScanner 预检照常）+
// 新资产形态（.novashadergraph/.cube/16bit 高度图）+ shaders + 字体 + 脚本 DLL
// → 单 pak（v1 单 pak 多场景）→ 输出目录结构：
//   <out>/NovaEngine_<版本>/NovaPlayer.exe + NovaEngine.pak + README.txt（生成：运行说明/最低配置）
//   + docs/（拷贝）+ steam/（depotbuild 目录占位 + steam_appid.txt 占位 + STEAM-说明.txt）
// 版本元数据写入 README（NOVA_PRODUCT/NOVA_VERSION）。
// 可复现：RC 元数据固定（无时间戳注入）+ pak 内 std::map 确定性（既有）→ 两次 buildRelease 产物字节一致。
static bool buildRelease(const std::filesystem::path& outputDir);
}
```

- **默认启动场景**：player main 解析顺序——--scene 显式 > pak 内 build.novabuild 第一个场景 > error + 退出码非 0（SceneManager::init 已读 build.novabuild，Player 端消费既有能力）。
- **多场景 pak 收集**：§27.1 的异步加载与本节无耦合（收集 = 静态扫描 build.novabuild，与运行加载机制无关）——#11 不阻塞于 #2 实现，只依赖 build.novabuild（已有）。
- CI 扩展：ci.yml 增加 release 打包 job（Release 构建后调 buildRelease 至 artifact，可选上传）。

**验收要点（cT，照抄细化 ROADMAP §7-11）**：
1. exe 属性含正确版本/图标：两 exe 详细信息 = 1.0.0.0 / NovaEngine / 公司名 / 图标可见。
2. 发行目录双击即玩：干净目录跑 NovaPlayer.exe → 默认场景加载成功（pak 旁挂）。
3. 打包产物可复现：连续两次 buildRelease 产物字节一致（含 RC 元数据）。
4. 目录结构符合 Steam 上传要求：steam/ 子目录含 depot 结构说明（人工对照 Steamworks 文档清单）。
5. 多场景收集：含大厅+关卡的 build 列表打包后 Player 可经 --scene 切换任意场景。

**风险：中。回滚**：buildRelease 是新入口，buildPlayer 旧路径保留即全回滚。**审重点**：RC 注入不破坏 /WHOLEARCHIVE/DEF 导出链（RC 对象与链接选项互不干扰——DevOps 双配置验证）；可复现性（排除构建路径/时间戳污染）。

## 27.6 波 2 整体验收（合波后）

- 合波构建：Debug 全量零错误零第一方警告；Release 构建通过；双 exe 冒烟（编辑器守卫 26 条 + Player 场景加载）。
- 波 1 零回归（重点）：NovaTests 全绿；Profiler 面板正常；纹理压缩缓存命中；引用预检；混音器。
- 多场景端到端：大厅+关卡 additive 共存（异步进度条）、卸载内存回落、参数传递。
- 物理增强目测：楼梯步进/斜坡/层过滤/摩擦弹性。
- 色彩分级 A/B：LUT 氛围对比、DOF/渐晕/颗粒开关无残留。
- Shader Graph 端到端：连图 → 编译 → 赋材质 → 渲染；错误定位高亮。
- buildRelease 产物：双 exe 属性（版本/图标）+ 发行目录双击即玩 + 可复现。
- 统计层数字随波记录；schema v4 回归（旧文件打开参数不变）。

## 27.7 波 2 风险（给 DevOps/Reviewer）

1. **多场景渲染合并**（本波最高风险）：跨场景剔除/实例化分组/阴影收集的合并遍历——评审对照 renderScenesToOutput 与 renderSceneToOutput 的单场景等价性（scenes=[单场景] 时行为逐像素一致）。
2. 加载线程与 GL 上传顺序：flushPendingResults 必须在 flushPendingLoad 之前（帧序修订）；乱序 = 场景挂载引用未上传资产（评审核对 §15 修订版）。
3. 生成 shader uniform 契约：与 setBlinnPhongUniforms 逐名核对（错位 = 花屏）。
4. LUT 直通路径：RGBA8 降级路径下 LUT 段同样生效且不崩（永不黑屏契约扩展）。
5. RC 与导出链：RC 对象接入后 /DEF 导出/WHOLEARCHIVE 双 exe 链接验证。
6. CharacterVirtual 与固定步进：步进频率/地面探测在 30/60/144 FPS 三档一致性。
7. 主循环重排回归：帧内顺序修订后协程/事件/物理的既有帧序断言（§15）与 Profiler 钩子（波 1）全部重验。
8. schema 增量回归：environment.physics/environment.postProcess/Collider 新字段/CharacterController 全部往返 + 旧文件零警告。

---

# V1.0 波 3 契约（2026-11-20 冻结）

> 范围：V1.0-13（内存与泄漏审计）/ #12（编辑器细节完善）/ #10（示例游戏项目）/ #14（文档化与迁移指南）。
> 分工：cR（#13）/ cT（#12）/ cC（V1.0_Coder_Content，#10+#14）；波 3 无新引擎冻结头（全部追加式已在前两波布完），无骨架日（轻量：仅合波日回归 + V1.0 收官验收）。
> 本节号承接 §27，新增 §28 一章。

## 28.0 总则：所有权分区与收官纪律

**文件所有权严格分区**：

| 线 | 目录/文件 |
|---|---|
| cR | engine/src/Assets/AssetDatabase.cpp（releaseUnused 收口/memorySnapshot 校准）、engine/src/Scene/SceneManager.cpp（卸载钩子增强——波 2 已建 unload 路径，cR 延续）、tests/（内存审计用例） |
| cT | editor/src/{EditorApplication,EditorContext,EditorSettings,SettingsPanel,SceneViewPanel,main}.h/.cpp（EditorSettings/SettingsPanel 新增） |
| cC | samples/、assets/scenes/、assets/audio/、assets/models/、assets/luts/、docs/（FEATURES/KNOWN-ISSUES + 新文档三件——契约明确授权新增 md） |
| 主会话 | docs/ROADMAP.md（波次状态行全程维护）+ 合波日 |

- 波 3 内 editor/src/EditorApplication.cpp 与 SceneViewPanel.cpp 为 cT 独占（cC 不碰编辑器代码）；docs/ROADMAP.md 归主会话（cC 只更新 FEATURES/KNOWN-ISSUES/新文档）。
- 内置组件数 26 不变；schema 无变更；无新第三方库。

## 28.1 内存与泄漏审计（cR，V1.0-13）

**文件落点**：engine/src/Assets/AssetDatabase.cpp、engine/src/Scene/SceneManager.cpp、tests/（新用例）、KNOWN-ISSUES.md（审计步骤文档，随 cC 协作）。

- **releaseUnused 自动化收口**：调用点——unloadScene 后（波 2 已接）、退出 Play 后、LoadScene 同步路径后；增强 releaseUnused 清理 m_materialCache 旁表（KNOWN-ISSUES 遗留 P3-7：m_materialCache 无 releaseUnused——本波修复）。
- **泄漏检测自动化（NovaTests 新用例，全部无 GL）**：①100 次 LoadScene 同步切换 + unload 循环 → memorySnapshot 回到基线（±5%，三次平均防分配器抖动）；②Play 100 次循环（Scene::start/update×N/退出——harness 直驱，无编辑器 GUI）→ 基线对比；③热重载：ScriptRuntime reload×10 → ScriptRegistry 名称集不变 + memorySnapshot 稳定。
- **脚本卸载路径验证（配合 V0.5-7）**：reload 后旧 DLL 引用计数为零（FreeLibrary 语义）——测试 + 日志。
- **VS 诊断**：手动步骤文档化（写入 KNOWN-ISSUES「V1.0-13 人工审计步骤」：CRT 泄漏报告/快照对比命令）——「VS 诊断无未释放资产」验收 = 该步骤人工执行 + 自动基线测试双重证据。

**验收要点（cR，照抄细化 ROADMAP §7-13）**：
1. 100 次场景切换内存回到基线（±5%，Profiler memorySnapshot 证据）。
2. Play 100 次无增长（headless 循环，基线对比）。
3. VS 诊断无未释放资产（人工步骤 + 自动基线双证据）。
4. 热重载路径无泄漏（reload×10 注册表/内存稳定）。

**风险：中。回滚**：审计代码全部测试侧/日志侧，releaseUnused 增强是行为改进（可独立回退）。**审重点**：内存基线断言抖动——本地与 CI 差异 → 容差 5%（CI 放宽 10%）+ 三次平均 + 失败重跑一次。

## 28.2 编辑器细节完善（cT，V1.0-12）

**文件落点**：editor/src/EditorSettings.h/.cpp（新）、editor/src/panels/SettingsPanel.h/.cpp（新）、EditorApplication.h/.cpp（最近文件/设置加载）、SceneViewPanel.cpp（三角形拾取）、main.cpp（ViewportsEnable 按设置）。

- **多视口（ImGui ViewportsEnable）评估**：开启前必须回归——gizmo 拖拽（drawlist owner 语义变化，KNOWN-ISSUES #11 同族）、拖放（SceneView/AssetBrowser payload）、HiDPI 150%、面板拖出/拖回。**评估失败 → 保持关闭 + KNOWN-ISSUES 备案理由（允许的交付形态，不硬开）**。
- **最近文件**：EditorContext +recentFiles（≤10，文件存在性过滤）→ File→Open Recent 菜单。
- **编辑器设置持久化**：%APPDATA%/NovaEngine/editor-settings.json（version 1）——`{"version":1,"snap":{"move":0.5,"rotate":15.0,"scale":0.5},"theme":"dark","keybindings":{...},"recentFiles":[...],"multiViewport":false}`。吸附值并入既有 Snap Settings 面板（读写换存储）；主题 v1 = 2–3 套预置 + ImGuiStyle 自定义；快捷键 v1 = 静态映射表（核心快捷键 Ctrl+Z/Ctrl+S/Ctrl+P 等保留不可移除，自定义项冲突检测）。
- **三角形级拾取（替代 AABB 粗测，KNOWN-ISSUES #11 闭环）**：SceneViewPanel 拾取路径替换——保留 AABB 粗筛（先剔除）→ 逐三角形 Möller–Trumbore（CPU，世界空间变换缓存）→ 最近交点；护栏：单网格 >200k 三角形跳过精确测试（AABB 结果降级 + warn 一次，防大模型卡顿）；gizmo/选择行为不变（hit 语义变化仅「大平面不再遮挡后方」）。

**验收要点（cT，照抄细化 ROADMAP §7-12）**：
1. 面板可拖出主窗口（或评估失败 → 备案回退，二选一，理由写入 KNOWN-ISSUES）。
2. 重启编辑器设置保留：改吸附值/主题/快捷键 → 重启 → 全部保持。
3. 最近文件列表可用：打开过 3 个场景 → File→Open Recent 列出且可点击加载；被删除文件过滤不崩溃。
4. 大平面不再遮挡后方物体拾取：demo 地面 + 地面后小物体 → 点击小物体直接选中（#11 闭环）；gizmo 真拖真看（2026-11-17 教训：交互验收必须真拖）。
5. 多视口开启后（若开）：gizmo 拖拽/拖放/字体三处回归通过。

**风险：中。回滚**：设置文件独立、拾取函数内部替换——回退 AABB 路径即旧行为。**审重点**：拾取性能（2000 物体场景拾取帧耗时统计层回归）、多视口 GL 上下文切换（imgui_impl_opengl3 原生支持，但 gizmo/拖放/字体三处必测）。

## 28.3 示例游戏项目（cC，V1.0-10）

**文件落点**：assets/scenes/、assets/audio/、assets/models/、assets/luts/、samples/NovaSampleGame/（脚本组，进 NovaGame.dll，遵循热重载约定）。

**流程**：1 页设计稿（玩法/关卡/用到的引擎功能清单）→ Architect 确认 → 搭场景。**Architect 裁定方向建议（cC 设计稿需覆盖）**：第一人称探索 1–2 关——功能清单：多场景（菜单→关卡→结算，§27.1）+ CharacterController 控制器（§27.2）+ 地形 + IBL/色彩分级风格化 + 粒子 + 音频总线（BGM 流式 + SFX，§26.5）+ 运行时 UI（血条/交互提示）+ 存档（关卡进度）+ 手柄（可选）。2D vs 3D 由设计稿结论定（V1.0_Coder_Content 工作流程）。

**硬性约束（Architect 重申）**：
- 只用引擎自带功能；发现缺口报 Architect 决策，**不得写引擎侧特例代码**。
- 资产体积控制：模型 <20MB/个、音频用压缩格式（ogg/mp3）、仓库总增量 <200MB（无 git LFS 基础设施；超限 → Architect 裁减）。
- 打包用 §27.5 buildRelease；通关冒烟与 DevOps 协作（半自动：Player --scene 启动 + 脚本自检目标可达性——v1 以人工通关为主）。

**验收要点（cC，照抄细化 ROADMAP §7-10）**：
1. 游戏可打包独立运行：buildRelease 产物在干净目录双击通关（含存档/读档）。
2. 通关流程完整：菜单→关卡→目标→结算→回菜单（1–2 关闭环）。
3. 每次发版前通关冒烟（与 DevOps 协作，流程写入文档）。
4. 代码作为最佳实践范本（脚本注释/结构，Reviewer 按 CLAUDE.md 风格审）。

**风险：中。回滚**：纯内容资产 + samples 脚本，删目录即回滚。**审重点**：脚本不得绕过引擎公开 API（grep 引擎私有头引用）；场景 schema 用 v4 标准形态（不得手写未来字段）。

## 28.4 文档化与迁移指南（cC，V1.0-14）

**新文档三件（本契约明确授权新增 md——Architect 角色约束「不新增 md」的例外条款）**：
1. docs/API-REFERENCE.md：脚本 API 手册（对照 engine/include 逐条核实，含签名/示例/序列化字段）。
2. docs/TUTORIAL.md：新手教程（从零到打包一个游戏，每步可执行，目标「新用户 1 天完成小游戏打包」）。
3. docs/UNITY-MIGRATION.md：Unity→Nova 迁移对照表（GameObject/Transform/Input/Time/Physics/UI/SceneManager/Coroutine 高频 API 对照）。

**既有文档更新**：FEATURES.md（V1.0 功能章节，按既有「V0.x 波 N 新增」结构追加）、KNOWN-ISSUES.md（P2 backlog 状态更新 + 三角形拾取 #11 闭环标注 + V1.0-13 审计步骤）。

**验收要点（cC，照抄细化 ROADMAP §7-14）**：
1. 新用户按教程 1 天完成一个小游戏打包（人工验证路径，教程每步可执行）。
2. Unity 用户按对照表完成 API 迁移（对照表覆盖高频 API 族）。
3. 文档与代码行为一致（以实际代码为准，抽查 10 处 API 签名比对）。

**风险：低。回滚**：删文档。**注意**：文档以波 3 合波后的最终代码为准再校对一轮（防漂移）。

## 28.5 波 3 整体验收 + V1.0 收官（合波后）

- 合波构建：Debug/Release 全绿零第一方警告；双 exe 冒烟（守卫 26 条）；NovaTests 全绿（含波 3 内存用例）；CI 全绿。
- 内存审计三组数字随波记录：100 次场景切换基线、Play 100 次、热重载 10 次。
- 编辑器细节：拾取 #11 闭环 + 设置持久化 + 最近文件 + 多视口结论。
- 示例游戏：buildRelease 打包 + 通关冒烟。
- **V1.0 收官 = ROADMAP §7.1 五项**：①发行验收（干净 Win11 机器双击 exe 通关示例游戏，含存档/读档）；②性能验收（示例游戏 1080p 60FPS，中端集显可玩）；③稳定性（8 小时编辑器马拉松：连续编辑/Play/热重载/打包无崩溃、内存无泄漏）；④CI 全绿 + 文档与代码同步；⑤全量回归 V0.2/V0.3/V0.5 验收清单。
- git commit/push（DevOps，提交信息 Co-Authored-By 结尾）。

## 28.6 波 3 风险（给 DevOps/Reviewer）

1. 三角形拾取性能：2000 物体场景拾取帧耗时回归（统计层）；>200k 三角形降级护栏生效。
2. 多视口：若开启——ImGui 后端多视口 GL 上下文切换下 gizmo/拖放/字体三处回归必测；若关闭——备案理由。
3. 内存基线断言抖动：容差 5%（CI 10%）+ 三次平均 + 失败重跑一次——不稳定的断言比没有断言更坏，保持必跑。
4. 示例游戏资产体积：无 git LFS——模型 <20MB/个、音频压缩、总增量 <200MB（超限 → Architect 裁减）。
5. 文档与代码漂移：cC 波 3 期间其他线若出修复补丁——文档以合波后最终代码为准再校对一轮。
6. 8 小时马拉松的自动化辅助：建议与 DevOps 编排 headless 压力脚本（Play 进出/热重载循环）辅助人工马拉松。
