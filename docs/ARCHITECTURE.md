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