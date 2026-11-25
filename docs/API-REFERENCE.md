# NovaEngine 脚本 API 手册

> 快照时间 2026-11-25(V1.0 波 3,#14 文档化)。**本文档以 engine/include 实际代码为准逐条核实**;发现本文与代码不符时,以代码为准。
> 面向脚本开发者(C++20):所有脚本组件继承 `Behaviour`,经 `NE_REGISTER_SCRIPT` 注册,挂到场景对象上由 Play 模式驱动。
> 本手册覆盖脚本侧高频 API;引擎内部模块(渲染管线细节/编辑器接口)见 [ARCHITECTURE.md](ARCHITECTURE.md)。操作类功能见 [FEATURES.md](FEATURES.md),新手从零到打包见 [TUTORIAL.md](TUTORIAL.md),Unity 迁移见 [UNITY-MIGRATION.md](UNITY-MIGRATION.md)。

## 目录

1. [头文件与命名空间](#1-头文件与命名空间)
2. [脚本组件基础:Behaviour 与 Component](#2-脚本组件基础behaviour-与-component)
3. [GameObject 与场景对象查找](#3-gameobject-与场景对象查找)
4. [Transform](#4-transform)
5. [Scene:环境参数与物理入口](#5-scene环境参数与物理入口)
6. [输入:Input / InputMap / 手柄](#6-输入input--inputmap--手柄)
7. [时间:Time](#7-时间time)
8. [物理:Rigidbody / Collider / CharacterController / 物理查询](#8-物理rigidbody--collider--charactercontroller--物理查询)
9. [协程](#9-协程)
10. [事件系统](#10-事件系统)
11. [场景管理:SceneManager](#11-场景管理scenemanager)
12. [运行时 UI:Canvas / Text / Image / Button / Slider / ScrollView](#12-运行时-uicanvas--text--image--button--slider--scrollview)
13. [音频:AudioSource / AudioClip / AudioEngine](#13-音频audiosource--audioclip--audioengine)
14. [存档:SaveGame](#14-存档savegame)
15. [文件读写:FileUtil](#15-文件读写fileutil)
16. [粒子:ParticleSystem](#16-粒子particlesystem)
17. [其他组件速查](#17-其他组件速查)
18. [序列化字段键名对照](#18-序列化字段键名对照)
19. [帧内顺序与语义约定](#19-帧内顺序与语义约定)

---

## 1. 头文件与命名空间

- 全部引擎类型在命名空间 `NovaEngine`。
- **总伞头**:`#include <NovaEngine/NovaEngine.h>` 一次包含绝大多数公共头。
- **例外**:`FileUtil`(波 3 新增写通道)不在总伞头内,需单独 `#include <NovaEngine/Core/FileUtil.h>`。
- 脚本专用头按需包含:`<NovaEngine/Scripting/Behaviour.h>`、`<NovaEngine/Scripting/Coroutine.h>`、`<NovaEngine/Scripting/ScriptRegistry.h>`。
- 数学类型直接用 GLM:`<glm/vec3.hpp>`、`<glm/gtc/quaternion.hpp>` 等。JSON 操作用 nlohmann/json(`#include <nlohmann/json.hpp>`,FetchContent 已提供)。
- 脚本不写 `main()`——脚本是「组件」,挂到场景对象上由引擎调用。

## 2. 脚本组件基础:Behaviour 与 Component

脚本组件 = 继承 `Behaviour` 的 C++ 类,构造函数必须转发 `GameObject* owner`:

```cpp
// MyRotator.h —— 挂在场景对象上,Play 时绕 Y 轴旋转
#pragma once
#include <NovaEngine/Scripting/Behaviour.h>

namespace MyGame {
class MyRotator : public NovaEngine::Behaviour {
public:
    explicit MyRotator(NovaEngine::GameObject* owner) : Behaviour(owner) {}

    // 序列化字段:公开成员 + onSerialize 写 JSON → Inspector 自动出现可调控件
    float speed = 30.0f;          // 度/秒

    void onStart() override;      // 进入 Play 时调用一次
    void onUpdate(float dt) override;   // Play 每帧调用
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};
}
```

```cpp
// MyRotator.cpp
#include "MyRotator.h"
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

namespace MyGame {

void MyRotator::onStart() { /* 可选 */ }

void MyRotator::onUpdate(float dt) {
    if (!enabled) return;   // 手动检查(与 Unity 一致:引擎不代你跳过)
    gameObject()->transform()->rotate(glm::vec3(0.0f, speed * dt, 0.0f));
}

void MyRotator::onSerialize(nlohmann::json& out) const {
    out["speed"] = speed;
}
void MyRotator::onDeserialize(const nlohmann::json& in) {
    if (in.contains("speed") && in["speed"].is_number())
        speed = in["speed"].get<float>();
}

}  // namespace MyGame

// 必须在 .cpp 文件末尾注册(匿名命名空间内,跨翻译单元无冲突)
NE_REGISTER_SCRIPT(MyGame::MyRotator)
```

### 2.1 Component 生命周期(所有组件的基类)

```cpp
class Component {
public:
    GameObject* gameObject() const;   // 所属对象
    Transform* transform() const;     // gameObject()->getComponent<Transform>() 便捷
    uint32_t typeId() const;          // 运行期类型 ID(类型名 FNV-1a 哈希,进程内唯一)
    virtual void onAwake() {}         // 挂载后调用(反序列化路径延迟到字段就绪后)
    virtual void onStart() {}         // 进入 Play 时调用一次(深度优先;跳过非激活子树与 enabled=false 的脚本)
    virtual void onUpdate(float dt) {}
    virtual void onSerialize(nlohmann::json& out) const {}   // 只写自己独有的字段
    virtual void onDeserialize(const nlohmann::json& in) {}
};
```

- **typeId 派生自类型名哈希**,跨 DLL(引擎 exe 与脚本 NovaGame.dll)同名同类 → 同 id;`getComponent<T>()` 跨模块安全(2026-11-23 修复)。
- 公共字段由 SceneSerializer 处理(如 Transform 的 position、Rigidbody 的 mass);子类 `onSerialize` 只写自己独有的字段(如上面的 `speed`)。
- 退出 Play 由快照还原:脚本对象被反序列化重建,`onDeserialize` 会把序列化字段写回——**非序列化成员(运行时中间变量)不保留**,与 Unity 的 C++ 内嵌脚本取舍一致。

### 2.2 Behaviour(脚本基类)

```cpp
class Behaviour : public Component {
public:
    bool enabled = true;    // false 时 Play 中不执行(onStart 也不调用;协程不暂停——与 Unity 一致)
    Coroutine* startCoroutine(Coroutine&& routine);   // §9
    void stopAllCoroutines();
    template<typename E>
    void subscribeEvent(const std::function<void(const E&)>& handler);  // 组件析构自动退订
};
```

- **注意**:`enabled=false` 时引擎不会替你跳过 `onUpdate`——示例脚本都在 `onUpdate` 开头手动 `if (!enabled) return;`(`onStart` 在进入 Play 时对 enabled=false 的脚本不广播)。
- **脚本查找**:所有脚本组件共用 Behaviour 的 typeId,按具体类型用 dynamic_cast 过滤:

```cpp
template<typename T>
T* findScript(GameObject* go);   // 取 go 上第一个 T 派生脚本;无则 nullptr
// 用法:auto* mgr = NovaEngine::findScript<NeonGameManager>(go);
// 注意:getComponent<具体脚本T>() 不保证命中(脚本共用 Behaviour 的 typeId),请用 findScript
```

- **静态单例模式(示例游戏范式)**:脚本类持有 `static T* s_instance`,onStart 注册、onUpdate 开头 `if (s_instance != this) s_instance = this;` 重新认领——**热重载后旧实例指针失效,此自愈模式是标准做法**(参见 samples/NovaSampleGame/NeonGameManager.cpp)。
- **协程与 lambda 的坑(MSVC 工具链已知问题)**:「lambda 协程 + 闭包捕获」在 MSVC 调试版下闭包未随帧复制,恢复时读到未初始化填充(string 捕获 → length_error,指针捕获 → 段错误)。**一律用自由协程函数 + 参数传递**(标准参数复制路径),见 §9 与 KNOWN-ISSUES.md。

## 3. GameObject 与场景对象查找

```cpp
class GameObject {
public:
    uint64_t id() const;                     // 64 位唯一 ID(场景序列化用)
    const std::string& name() const;
    void setName(const std::string& name);
    bool activeSelf() const;
    void setActiveSelf(bool active);         // 非激活子树不渲染、脚本不更新
    bool activeInHierarchy() const;          // 自身 && 所有祖先 activeSelf

    bool dontDestroyOnLoad() const;
    void setDontDestroyOnLoad(bool value);   // LoadScene 时该根子树跨场景存活(DDOL)

    GameObject* parent() const;
    void setParent(GameObject* newParent, bool keepWorldTransform = true);
    const std::vector<std::unique_ptr<GameObject>>& children() const;

    template<typename T, typename... Args>
    T* addComponent(Args&&... args);         // 同类型已存在则 assert
    template<typename T> T* getComponent();  // 按 typeId 线性扫描;未找到 nullptr
    template<typename T> const T* getComponent() const;
    template<typename T> bool hasComponent();
    template<typename T> void removeComponent();
    template<typename T> std::vector<T*> getComponents();   // 含派生类(dynamic_cast 过滤)
    size_t componentCount() const;
    Transform* transform() const;

    Scene* scene() const;                    // 所属场景
    void destroy();                          // 标记销毁(含子树),Scene 帧末真正删除
};
```

- `destroy()` 是**延迟**的:入销毁队列,`Scene::update` 开头 flush。调用后到帧末前对象仍存活(与 Unity 语义一致)。
- 脚本里拿子对象:`gameObject()->children()` 返回 `vector<unique_ptr<GameObject>>&`,遍历时用 `childPtr.get()`(参见 NeonCollectible.cpp 的 Burst 粒子查找)。

**场景内查找**(`Scene` 成员,§5 有完整 Scene 接口):

```cpp
scene->findGameObject(id)                       // 按 ID;未找到 nullptr
scene->findGameObjectsByName("Player")          // 按名;返回 vector,可能 0/1/多个
scene->rootObjects()                            // 根层级对象列表
scene->forEachObject(fn)                        // 深度优先遍历全部对象
scene->findComponents<T>()                      // 遍历所有 GO 过滤(含派生类)
```

命名约定:示例游戏里每类对象名字唯一(如 "Player"/"BGM"),按名查找取 `[0]`;查完判空再解引用(防弹窗纪律见 NeonLobbyUI.cpp)。

## 4. Transform

```cpp
class Transform : public Component {
public:
    // 局部 TRS(相对父级)
    const glm::vec3& localPosition() const;
    void setLocalPosition(const glm::vec3& v);
    const glm::quat& localRotation() const;
    void setLocalRotation(const glm::quat& q);
    const glm::vec3& localScale() const;
    void setLocalScale(const glm::vec3& v);
    glm::vec3 localEulerAngles() const;              // 度
    void setLocalEulerAngles(const glm::vec3& eulerDeg);
    glm::mat4 localMatrix() const;

    // 世界(惰性计算 + 脏标记)
    const glm::mat4& worldMatrix() const;
    glm::vec3 worldPosition() const;
    glm::quat worldRotation() const;

    // 世界方向(forward = +Z,right = +X,up = +Y)
    glm::vec3 right() const;
    glm::vec3 up() const;
    glm::vec3 forward() const;

    // 便捷操作(脚本常用)
    void translate(const glm::vec3& worldDelta);
    void rotate(const glm::vec3& eulerDeg);          // 绕自身轴累计旋转(度)
    void lookAt(const glm::vec3& worldTarget);
};
```

- 旋转内部存储四元数;欧拉角按度读写。
- **动态刚体(Rigidbody)的对象直改 Transform 会被物理覆盖**——用 addForce/setVelocity 驱动;CharacterController 对象同理,复活/瞬移请用 `CharacterController::setPosition`(物理传送,§8)。
- 每个脚本组件有 `transform()` 便捷方法,等价 `gameObject()->transform()`。

## 5. Scene:环境参数与物理入口

```cpp
class Scene {
public:
    GameObject* createGameObject(const std::string& name = "GameObject",
                                 GameObject* parent = nullptr);   // 自动附带 Transform
    void destroyGameObject(uint64_t id);                          // 入销毁队列
    GameObject* findGameObject(uint64_t id) const;
    std::vector<GameObject*> findGameObjectsByName(const std::string& name) const;
    std::vector<GameObject*> rootObjects() const;
    void forEachObject(const std::function<void(GameObject*)>& fn) const;
    template<typename T> std::vector<T*> findComponents() const;
    PhysicsWorld* physics() const;    // 场景级物理服务(§8)

    // ===== 环境(公开字段,SceneSerializer 直接读写;脚本可改,当帧生效)=====
    glm::vec3 skyColor{0.18f, 0.22f, 0.35f};
    glm::vec3 ambientColor{0.15f, 0.15f, 0.18f};
    bool showGrid = true;
    glm::vec3 fogColor{0.18f, 0.22f, 0.35f};
    SkyMode skyMode = SkyMode::Gradient;                          // Gradient / Cubemap
    glm::vec3 skyHorizonColor{0.55f, 0.62f, 0.72f};
    glm::vec3 skyZenithColor{0.22f, 0.30f, 0.48f};
    std::string skyCubemapPath;                                   // 空 = 渐变回退
    float skyRotation = 0.0f;                                     // 度
    float skyExposure = 1.0f;
    float iblIntensity = 1.0f;                                    // 0 = 关闭 IBL
    PostProcessSettings postProcess;                              // §17 后处理设置
    std::vector<std::string> physicsLayerNames;                   // 碰撞层名(0..15)
    std::array<std::array<bool, 16>, 16> physicsCollisionMatrix;  // 层碰撞矩阵(对称)
};
```

脚本中获取场景:`gameObject()->scene()`(可能为 nullptr,判空纪律)。

## 6. 输入:Input / InputMap / 手柄

### 6.1 Input(键鼠底层查询,全静态)

```cpp
enum class Key : int32_t {           // 值域与 GLFW 一致
    Unknown = -1, Space = 32,
    Num0 = 48, /* .. */ Num9, A = 65, /* .. */ Z,
    Escape = 256, Enter = 257, Tab = 258, Backspace = 259, Insert = 260, Delete = 261,
    Right = 262, Left = 263, Down = 264, Up = 265,
    PageUp = 266, PageDown = 267, Home = 268, End = 269,
    F1 = 290, /* .. */ F12,
    LeftShift = 340, LeftControl = 341, LeftAlt = 342,
    RightShift = 344, RightControl = 345, RightAlt = 346
};
enum class MouseButton : int32_t { Left = 0, Right = 1, Middle = 2 };

class Input {
public:
    static bool isKeyDown(Key key);              // 按住
    static bool isKeyPressed(Key key);           // 本帧刚按下(边沿)
    static bool isKeyReleased(Key key);          // 本帧刚松开
    static bool isMouseDown(MouseButton button);
    static bool isMousePressed(MouseButton button);
    static bool isMouseReleased(MouseButton button);
    static glm::vec2 mousePosition();            // 窗口逻辑坐标(左上原点)
    static glm::vec2 mouseDelta();
    static glm::vec2 scrollDelta();
};
```

`beginFrame()` 每帧由主循环调用,脚本只管查询。

### 6.2 手柄

```cpp
enum class GamepadButton : int32_t { A = 0, B, X, Y, LeftBumper, RightBumper, Back, Start,
    Guide, LeftThumb, RightThumb, DPadUp, DPadRight, DPadDown, DPadLeft, Count };
enum class GamepadAxis : int32_t { LeftX = 0, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count };

// Input 追加(手柄序号 j = 0..kMaxGamepads-1,最多 4 只)
static bool isGamepadConnected(int j = 0);
static int  connectedGamepadCount();
static const char* gamepadName(int j = 0);              // 未连接 nullptr;指针仅当帧有效
static bool isGamepadButtonDown(GamepadButton b, int j = 0);
static bool isGamepadButtonPressed(GamepadButton b, int j = 0);
static bool isGamepadButtonReleased(GamepadButton b, int j = 0);
static float gamepadAxis(GamepadAxis a, int j = 0);     // -1..1,死区已应用
static void setGamepadDeadzone(float d);                // 默认 0.15,钳制 [0,1)
```

### 6.3 InputMap(动作/轴绑定表,**推荐脚本层使用**)

绑定文件 `assets/input/default.novainput`(version 1,JSON;编辑器 File → Input Bindings... 编辑)。内置默认绑定:动作 `Jump`(Space/手柄 A)、`Submit`(Enter)、`Cancel`(Escape)、`Pause`(P)、`Dash`(LeftShift);轴 `Horizontal`(A/D + 手柄左摇杆 X)、`Vertical`(S/W + 左摇杆 Y,反向)。

```cpp
class InputMap {
public:
    static bool hasAction(const std::string& name);
    static bool getAction(const std::string& name);      // 按住(任意源)
    static bool getActionDown(const std::string& name);  // 本帧按下边沿
    static bool getActionUp(const std::string& name);    // 本帧松开边沿
    static float getAxis(const std::string& name);       // -1..1;缺失 warn 一次返回 0
    static bool load(const std::filesystem::path& path); // 失败 warn + 保留旧表
    static bool save(const std::filesystem::path& path);
    // 绑定管理(编辑器面板用):actionNames/axisNames/addAction/removeAction/addAxis/
    // removeAxis/setActionBindings/setAxisBindings/actionBindings/axisBindings
    // 键名互转:keyName/keyFromName/gamepadButtonName/... 等
};
```

```cpp
// 典型移动写法(CharacterWalk 范式):
float h = NovaEngine::InputMap::getAxis("Horizontal");
float v = NovaEngine::InputMap::getAxis("Vertical");
if (NovaEngine::InputMap::getActionDown("Jump")) { /* 跳跃 */ }
```

- InputBinding 结构:`{key, mouseButton, useMouseButton, gamepadButton, useGamepadButton, gamepadIndex, ctrl, shift, alt}`;AxisBinding:`{negativeKey, positiveKey, gamepadAxis, useGamepadAxis, gamepadScale, ctrl, shift, alt}`。键鼠/手柄多源并存,任一按下即触发。
- **排障技巧**:环境变量 `NOVA_DBG_KEYS=1` 启动时,按下/松开边沿逐键打日志「按下 XX 键」(编辑器 Console/终端/日志三通道可见),验收按键是否到达引擎的第一手段。

## 7. 时间:Time

```cpp
class Time {
public:
    static float deltaTime();    // 秒(帧间隔,上限钳制 0.1s)
    static float totalTime();    // 启动以来秒数
    static uint64_t frameCount();
};
```

- 计时/动画用 `deltaTime()` 增量或 `totalTime()` 差值均可(示例游戏计时起点 = onStart 里记录 `Time::totalTime()`)。
- 物理相关逻辑不要依赖 deltaTime 精确到物理步(物理固定 60Hz 步进,§8)。

## 8. 物理:Rigidbody / Collider / CharacterController / 物理查询

### 8.1 Rigidbody(动态/kinematic 刚体)

```cpp
class Rigidbody : public Component {
public:
    float mass = 1.0f;
    float linearDamping = 0.0f;
    float angularDamping = 0.05f;
    bool useGravity = true;
    bool isKinematic = false;

    void addForce(const glm::vec3& force);      // Play 中调用
    void addImpulse(const glm::vec3& impulse);
    glm::vec3 velocity() const;
    void setVelocity(const glm::vec3& v);
};
```

- **动态刚体直改 Transform 会被物理覆盖**——用 addForce/setVelocity 驱动;kinematic 由脚本改 Transform 驱动。
- body 未创建时(无 Collider / Edit 态)接口安全 no-op。
- **与 CharacterController 互斥**:同 GO 挂两者 = 启动 warn + CharacterController 优先。

### 8.2 Collider 家族(形状组件)

```cpp
class Collider : public Component {       // 基类
public:
    bool isTrigger = false;               // 触发器:不碰撞求解,只发 TriggerEvent
    glm::vec3 center{0.0f};               // 局部偏移
    int layer = 0;                        // 碰撞层 0..15(层间开关见 environment.physics 矩阵)
    float friction = 0.5f;                // 摩擦(body 摩擦 = 各 collider 平均)
    float restitution = 0.0f;             // 弹性(0 完全非弹性,1 完全弹性)
};
class BoxCollider : public Collider { public: glm::vec3 halfExtents{0.5f}; };
class SphereCollider : public Collider { public: float radius = 0.5f; };
class CapsuleCollider : public Collider { public: float radius = 0.5f, height = 2.0f; };
class MeshCollider : public Collider { public: std::string meshPath; };  // 仅 static/kinematic
```

- **挂载规则**:仅 Collider(无 Rigidbody)→ 静态体(地面类);Rigidbody 可无 Collider;Collider 可挂同 GO 或子 GO(子 GO 相对 TRS 烘焙为附加形状)。
- 层矩阵:场景级配置(Physics Layers 面板写 `environment.physics` 段);`Collider.layer` 决定归属,矩阵决定层间是否碰撞。

### 8.3 CharacterController(角色控制器,胶囊 + 楼梯步进)

```cpp
class CharacterController : public Component {
public:
    float radius = 0.4f, height = 1.8f;    // 胶囊
    float stepHeight = 0.35f;              // 楼梯步进高度
    float slopeLimitDeg = 45.0f;           // 坡度上限(超过 = 阻挡)
    float mass = 70.0f, gravityScale = 1.0f;

    bool isGrounded() const;               // Play 中有效
    void move(const glm::vec3& displacement);   // Play 中;Edit no-op
    glm::vec3 velocity() const;
    void setVelocity(const glm::vec3& v);
    void setPosition(const glm::vec3& worldPos);  // 物理传送(复活/瞬移);Play 中有效
};
```

- 封装 Jolt CharacterVirtual(楼梯步进/坡度限制/地面检测库内现成)。
- **位置约定**:Transform 世界位置 = 胶囊中心(站立地面时中心在地面上方 height/2)。
- 移动范式:脚本每帧读 `velocity()` → 改水平分量 → `setVelocity()`(pending override,固定步消费)。**起跳锁存**:高帧率下边沿帧注入的跳跃会被下一渲染帧覆盖吞掉——连续注入起跳速度直到离地(CharacterWalk.cpp 的 `m_jumpLatched` 实现,可抄)。
- **复活必须走 `setPosition`**:只改 Transform + setVelocity(0) 会被下一物理步 writeBack 回滚(波 3 实测教训)。
- 已知物理语义(验收实测):自由台面的实际可步进高度 > stepHeight(胶囊圆底可「骑」上边缘);设计跳跃需求时按 ≥1.0m 算阻挡。

### 8.4 物理查询(Scene::physics() 返回 PhysicsWorld*)

```cpp
struct RaycastHit {
    bool hit = false;
    GameObject* gameObject = nullptr;    // 旧名保留兼容
    GameObject* hitObject = nullptr;     // 命中对象(新代码统一读这个)
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f};
    float distance = 0.0f;
};
struct RaycastFilter {
    uint32_t layerMask = 0xFFFFFFFF;     // bit L = 层 L 参与(Unity layermask 语义)
    GameObject* ignore = nullptr;        // 命中忽略对象(nullptr = 不过滤)
};

class PhysicsWorld {
public:
    static constexpr float kFixedStep = 1.0f / 60.0f;   // 固定步进 60Hz(每帧最多 3 步)
    // 最近命中(单条);无命中 false
    bool raycast(const glm::vec3& origin, const glm::vec3& direction,
                 float maxDistance, RaycastHit& outHit);
    // 全命中射线(距离升序;触发器也计入);无命中 false(outHits 为空)
    bool raycastAll(const glm::vec3& origin, const glm::vec3& dir, float maxDist,
                    const RaycastFilter& filter, std::vector<RaycastHit>& outHits);
    // 形状重叠查询(球/盒;命中去重后返回 GO 列表,含触发器);无命中 false
    bool overlapSphere(const glm::vec3& center, float radius, const RaycastFilter& filter,
                       std::vector<GameObject*>& outObjects);
    bool overlapBox(const glm::vec3& center, const glm::vec3& halfExtents,
                    const RaycastFilter& filter, std::vector<GameObject*>& outObjects);

    void setGravity(const glm::vec3& g);
    glm::vec3 gravity() const;

    // 碰撞层管理(16 层;矩阵对称;写回 scene.physicsLayerNames)
    static constexpr int kMaxLayers = 16;
    void setLayerName(int layer, const std::string& name);
    const std::string& layerName(int layer);          // 越界 → 空串
    void setCollisionEnabled(int a, int b, bool enabled);
    bool collisionEnabled(int a, int b) const;        // 越界 → false
};
```

```cpp
// 视线检测示例:
NovaEngine::RaycastHit hit;
if (scene->physics()->raycast(eye, dir, 50.0f, hit) && hit.hitObject) {
    Logger::info("命中 {} @ ({:.2f},{:.2f},{:.2f})", hit.hitObject->name(),
                 hit.point.x, hit.point.y, hit.point.z);
}
```

### 8.5 触发器事件

3D 触发器(任一 Collider.isTrigger=true)不参与碰撞求解,通过事件系统投递:

```cpp
struct TriggerEvent {
    bool entered = false;                // true=Enter,false=Exit
    GameObject* gameObjectA = nullptr;   // 自身
    GameObject* gameObjectB = nullptr;   // 对方
};
// 脚本订阅:subscribeEvent<TriggerEvent>([](const TriggerEvent& e){ ... });
```

2D 物理(BoxCollider2D,§17 简表)另有完整事件族 `Collision2DEnterEvent/Stay/ExitEvent`、`Trigger2DEnterEvent/Stay/ExitEvent`(每个重叠对两侧各投递一次,`other` 指向对方,`overlap` 为穿透深度)。

## 9. 协程

C++20 协程,由 `Behaviour::startCoroutine` 持有,Scene::update 帧内推进。

```cpp
// yield 原语(engine/include/NovaEngine/Scripting/Coroutine.h)
struct WaitForSeconds { float seconds; };
struct WaitForEndOfFrame {};
struct WaitUntil { std::function<bool()> predicate; };
struct WaitForCoroutine { const Coroutine* other; };   // 嵌套:等待另一协程结束

// Behaviour 成员:
Coroutine* startCoroutine(Coroutine&& routine);   // list 保地址稳定(WaitForCoroutine 依赖)
void stopAllCoroutines();
```

```cpp
// 标准写法:自由协程函数 + 参数(不要用 lambda + 闭包捕获,见下)
static NovaEngine::Coroutine switchAfterDelay(std::string sceneName) {
    co_await NovaEngine::WaitForSeconds{0.5f};     // 0.5 秒后恢复
    NovaEngine::SceneManager::loadSceneAsync(sceneName, false);
}

// 在脚本 onStart/onUpdate 里启动:
startCoroutine(switchAfterDelay("NeonLevel1"));

// 其他原语:
co_await NovaEngine::WaitForEndOfFrame{};                     // 帧末(事件 flush 后)
co_await NovaEngine::WaitUntil([]{ return someFlag; });       // 条件满足即恢复
co_await NovaEngine::WaitForCoroutine{otherCoroutine};        // 等待另一协程结束
```

- **帧内顺序**:物理 step → onUpdate 广播 → 协程推进(普通 yield)→ 事件 flush → 协程推进(WaitForEndOfFrame)。
- **语义**:`enabled=false` 不暂停协程(与 Unity 一致);非激活 GO 上的协程照常推进(与 Unity 差异,已知取舍);对象销毁/退出 Play 协程自动终止。
- 同 Behaviour 内嵌套等待完全安全;**跨 Behaviour 的 WaitForCoroutine 可能悬垂——勿跨对象等待**。
- **已知坑(MSVC 工具链)**:避免「lambda 协程 + 闭包捕获」组合,一律自由函数 + 参数(见 §2.2 与 KNOWN-ISSUES.md 波 3 段)。
- 协程内未捕获异常 → 记录日志并终止(不重抛)。

## 10. 事件系统

```cpp
class Events {
public:
    using Subscription = uint64_t;    // 0 = 无效;generation+counter 防悬挂
    template<typename E>
    static Subscription subscribe(const std::function<void(const E&)>& handler);
    template<typename E>
    static void publish(const E& event);   // 同步:立即投递当前订阅者
    template<typename E>
    static void post(const E& event);      // 延迟:入队,帧末 flushDeferred 投递
    static void unsubscribe(Subscription sub);
    static void flushDeferred();           // Scene::update 末尾
};
```

- 类型 key = `std::type_index`,事件类型即订阅键。
- `publish` 拷贝 handler 快照防重入;flush 用 swap 模式防投递期间重入。
- **脚本推荐用 `Behaviour::subscribeEvent<E>(handler)` 成员版**——组件析构自动退订,无悬挂。
- 内置事件类型:`TriggerEvent`(物理触发器)、`SceneLoaded{name}`、`SceneUnloaded{name}`(场景切换/卸载,post 延迟投递)、2D 事件族(§8.5)。

```cpp
void onStart() override {
    subscribeEvent<NovaEngine::SceneLoaded>([](const NovaEngine::SceneLoaded& e) {
        NovaEngine::Logger::info("场景已加载: {}", e.name);
    });
}
```

## 11. 场景管理:SceneManager

```cpp
struct SceneBuildInfo {
    std::string name;                 // 唯一键(loadScene 用名字)
    std::filesystem::path path;       // 相对项目根,如 "assets/scenes/demo.nova.json"
};
struct SceneLoaded { std::string name; };      // 加载完成事件(post 延迟投递,下一帧收到)
struct SceneUnloaded { std::string name; };    // 卸载完成事件

class SceneManager {
public:
    static void init();     // 读盘 assets/build.novabuild(引擎启动时调用)
    static void shutdown();
    static void registerScene(const std::string& name, const std::filesystem::path& path);  // 运行期注册(同名覆盖)
    static const std::vector<SceneBuildInfo>& buildList();
    static bool saveBuildSettings(const std::vector<SceneBuildInfo>& scenes);

    // 同步加载:入队,帧末 flushPendingLoad 实际执行;查无 → error + false
    static bool loadScene(const std::string& name);
    static void flushPendingLoad();

    // ===== V1.0-2 多场景工作流 =====
    // 异步加载:入队到资产加载线程(后台 CPU 解码,GPU 上传回主线程,帧末生效)。
    // additive=true 挂新场景对象共存;false 替换主场景(等价 loadScene)。
    // 取消语义:任一新的加载请求(含同步 loadScene)先作废旧代际结果。
    static bool loadSceneAsync(const std::string& name, bool additive = false);
    // 卸载:仅 additive 场景可卸;activeScene 卸载 = error + false。帧末延迟销毁
    //(Scene 析构 + releaseUnused + SceneUnloaded 事件)。
    static bool unloadScene(const std::string& name);
    static const std::vector<Scene*>& loadedScenes();   // activeScene 在前
    static void updateAll(float dt);                    // 全部已加载场景 update(activeScene 先)

    // 场景间参数:loadScene/loadSceneAsync 调用瞬间快照参数表;新场景脚本 onStart
    // 期间 getSceneParam 读到该快照;挂载结束后恢复活表(setSceneParam 继续写活表)
    static void setSceneParam(const std::string& key, const std::string& jsonValue);
    static std::string getSceneParam(const std::string& key);    // 无 → 空串
    static void clearSceneParams();

    static Scene* activeScene();
    static void setActiveScene(Scene* scene);
    static void dontDestroyOnLoad(GameObject* go);   // 等价 go->setDontDestroyOnLoad(true)
};
```

```cpp
// 传参 + 切场景(示例游戏大厅 → 关卡):
NovaEngine::SceneManager::setSceneParam("level", R"({"index":1,"orbTotal":10})");
NovaEngine::SceneManager::loadSceneAsync("NeonLevel1", false);

// 关卡脚本 onStart 读参数(缺失/坏 JSON → 默认值兜底):
const std::string raw = NovaEngine::SceneManager::getSceneParam("level");
if (!raw.empty()) {
    try {
        const nlohmann::json j = nlohmann::json::parse(raw);
        if (j.contains("orbTotal") && j["orbTotal"].is_number_integer())
            m_orbTotal = j["orbTotal"].get<int>();
    } catch (const std::exception& e) { /* warn + 默认值 */ }
}
```

- **参数是 JSON 字符串**(不是类型化键值):复杂数据用 nlohmann/json 打包/解析。
- 加载候选集 = `assets/build.novabuild` 的场景列表(编辑器 File → Build Settings... 维护;Player 启动默认 = 列表第一项)。
- 异步加载进度查询:`AsyncLoader::progress()`(0..1,无任务 = 1)——做进度条 UI 用。
- DDOL:勾选 Inspector 的 Dont Destroy On Load(或脚本 `dontDestroyOnLoad(go)`),LoadScene 时该根子树迁移存活。

## 12. 运行时 UI:Canvas / Text / Image / Button / Slider / ScrollView

运行时 UI 与编辑器 ImGui 完全分离,可打包进 Player。**Canvas 是 UI 子树根组件**(屏幕空间恒全屏,Transform 忽略;`uiScale` v1 预留,实际固定 1.0)。

**布局语义(全部 UI 组件统一)**:

- 点锚公式:**位置 = anchor × 屏幕尺寸 + offset 像素**(anchor 归一化 0..1,offset 逻辑像素)。例如 anchor (0.5,0.5) offset (0,0) = 屏幕正中心;anchor (0,0) offset (24,24) = 左下角偏上 24px。
- **重要语义差异(与 Unity 不同)**:UI 子节点的 anchor 相对**屏幕**解析,**不是**相对父元素矩形(仅 ScrollView 内容传播平移)。「按钮内放标签子对象」不会随按钮走——把标签做成与按钮同 offset 的兄弟节点(波 3 示例游戏实测结论)。
- 尺寸 `size` 是逻辑像素;基准分辨率约定 = Player 窗口 1280×720(Player 固定窗口不可缩放)。
- 渲染进字体的图集批处理,每 Canvas 内容走字体图集时 1 draw call(每张精灵贴图 +1)。

### 12.1 Text

```cpp
enum class TextHAlign { Left, Center, Right };   // 相对 wrapWidth(wrap=true)或文本块最宽行

class Text : public Component {
public:
    std::string text;                  // UTF-8,含中文
    float fontSize = 32.0f;
    glm::vec4 color{1.0f};
    std::string fontPath;              // 空 = 默认字体链(Noto Sans SC → 等线 → 微软雅黑);
                                       // 可指 assets/fonts/*.ttf(可进 pak);加载失败回退默认(中文显示 ?)
    glm::vec2 anchor{0.5f}, offset{0.0f};
    bool wrap = false;                 // 按 wrapWidth 折行(按码点断行,CJK 安全;保留 '\n')
    float wrapWidth = 0.0f;            // <=0 时取当前 Canvas 屏幕宽
    TextHAlign hAlign = TextHAlign::Left;
};
```

```cpp
// HUD 倒计时(NeonLevelHud 范式):
txt->text = std::format("时间 {:.1f} 秒", elapsed);
```

### 12.2 Image

```cpp
enum class ImageFillType { None, Horizontal, Vertical, Radial90, Radial180, Radial360 };

class Image : public Component {
public:
    std::string spritePath;            // 空 = 纯色(atlas 白块)
    glm::vec4 color{1.0f};
    glm::vec4 slice9{0.0f};            // 左/上/右/下像素边距;全 0 = 普通拉伸
    glm::vec2 anchor{0.5f}, offset{0.0f}, size{100.0f};
    ImageFillType fillType = ImageFillType::None;
    float fillAmount = 1.0f;           // 0..1,渲染期钳制;None 时忽略
    bool fillClockwise = true;         // Radial 扫掠方向
};
```

### 12.3 Button

```cpp
class Button : public Component {      // 含 Image 全部字段(组合而非继承)
public:
    glm::vec4 color{1.0f}, hoverColor{0.9f}, pressedColor{0.8f};   // 三色组
    glm::vec4 slice9{0.0f};
    glm::vec2 anchor{0.5f}, offset{0.0f}, size{100.0f};
    std::function<void()> onClick;     // 脚本赋值(C++ 内嵌脚本无 Inspector 事件绑定)
    bool hovered() const;              // 脚本 onUpdate 查询
    bool pressedThisFrame() const;
};
```

```cpp
// onStart 里接线(NeonLobbyUI 范式):
if (auto* btn = btnGo->getComponent<NovaEngine::Button>())
    btn->onClick = [this]() { switchScene("NeonLevel1"); };
```

- onClick 在按下边沿触发;hover/press 状态由 UIRenderer::processInput 维护(Player 在 scene.update 前调用 → **脚本 onUpdate 读到本帧按钮状态**;编辑器 SceneView 为渲染期调用,脚本侧滞后一帧——已知取舍)。
- 按钮的文字标签:与按钮同 offset 的兄弟 Text(见布局语义注意)。

### 12.4 Slider(横向滑条)

```cpp
class Slider : public Component {
public:
    std::string backgroundSpritePath, fillSpritePath, handleSpritePath;  // 空 = 纯色
    glm::vec4 backgroundColor{0.15f, 0.15f, 0.15f, 1.0f};
    glm::vec4 fillColor{0.9f, 0.9f, 0.9f, 1.0f};
    glm::vec4 handleColor{1.0f};
    float fillThickness = 6.0f;          // 轨道/填充条厚度
    glm::vec2 handleSize{16.0f, 24.0f};
    glm::vec2 anchor{0.5f}, offset{0.0f}, size{200.0f, 30.0f};
    float minValue = 0.0f, maxValue = 1.0f;
    float value = 0.5f;                  // 序列化;钳制 [min,max]
    bool wholeNumbers = false;
    std::function<void(float)> onValueChanged;   // 拖拽中值变化即触发(连续)
    bool dragging() const;
};
```

### 12.6 鼠标坐标换算(DPI,V1.0-12 追加公开 API)

```cpp
// UIRenderer(engine/include/NovaEngine/UI/UIRenderer.h,V1.0-12 §28.2.1 追加):
// 窗口逻辑坐标(GLFW 光标坐标,左上原点;调用方已减去视口偏移/letterbox 起点)
// → 布局空间(逻辑像素 = 物理 FBO / setDpiScale)的一次性换算,内容缩放参与:
//   layout = windowPoint × contentScale / dpiScale。
// contentScale = 窗口内容缩放(Window::getContentScale / glfwGetWindowContentScale;
// GLFW_SCALE_TO_MONITOR 下与 ImGui 帧缓冲缩放比、fb/win 比值同源)。
// 缺省(≤0)取内部 dpiScale——调用方每帧先 setDpiScale(contentScale) 时两者一致、
// 换算恒等(布局空间 = 窗口逻辑像素);不一致时(如 dpi 设置滞后一帧)公式自动补偿。
static glm::vec2 UIRenderer::layoutFromWindowPoint(const glm::vec2& windowPoint,
                                                   float contentScale = -1.0f);

// Window(engine/include/NovaEngine/Core/Window.h,V1.0-12 追加):
// 内容缩放:窗口逻辑像素 → 帧缓冲像素比(glfwGetWindowContentScale 封装;
// GLFW_SCALE_TO_MONITOR 下 = 显示器缩放比,150% = 1.5)。
void Window::getContentScale(float& outX, float& outY) const;
```

- 这两个 API 主要供引擎/编辑器侧做「窗口逻辑 → 布局空间」统一换算(编辑器 SceneView 与 Player 主循环已接线);脚本侧一般不需要直接调用,但公开可用。
- 已知语义(<100% 自定义 DPI 时):layoutFromWindowPoint 的换算与原 fb/win 公式不再恒等(max(cs,1.0) 钳制);固定 1280×720 + 常规 DPI 下逐位等价(KNOWN-ISSUES 备案)。

### 12.7 ScrollView(滚动容器)

```cpp
class ScrollView : public Component {
public:
    glm::vec2 anchor{0.5f}, offset{0.0f}, size{300.0f, 200.0f};   // 可视区
    bool scrollHorizontal = false;
    bool scrollVertical = true;
    float scrollSensitivity = 20.0f;     // 滚轮每格像素
    glm::vec2 scrollOffset{0.0f};        // 正 = 内容向上/左移;序列化(位置记忆)
    glm::vec4 bgColor{0.10f, 0.11f, 0.13f, 0.85f};   // 可视区背景
    glm::vec4 borderColor{0.42f, 0.45f, 0.52f, 1.0f};
    float borderThickness = 1.5f;
    glm::vec2 contentSize() const;       // 内容包围盒(子物体 UI rect 并集)
};
```

- 内容 = 直接子物体的 UI 组件;渲染期 scissor 裁剪(嵌套取交集);滚轮滚动。v1 不做内容自动重排——内容布局由创作者摆放,滚动仅平移视口。

## 13. 音频:AudioSource / AudioClip / AudioEngine

### 13.1 AudioSource(音源组件,位置每帧跟随 Transform)

```cpp
class AudioSource : public Component {
public:
    std::string clipPath;                        // 资产相对路径(空 = 无效)
    float volume = 1.0f;
    bool loop = false;
    float spatialBlend = 1.0f;                   // 0 = 2D 全音量,1 = 3D 距离衰减
    float minDistance = 1.0f, maxDistance = 50.0f;
    bool playOnAwake = false;
    // ===== V1.0-8 追加 =====
    int bus = 0;                     // AudioBus 枚举值(0=Master,1=Music,2=Sfx,3=Ui)
    bool stream = false;             // true = 流式播放(长 BGM,不预解码);false = 整载 PCM
    float fadeInSeconds = 0.0f;      // 0 = 无淡变
    float fadeOutSeconds = 0.0f;     // 0 = 无淡变(Stop 直接停)

    void Play();
    void Stop();                     // fadeOutSeconds>0 时先淡出、到期再停(无爆音)
};
```

- BGM 场景范式:`stream:true, loop:true, spatialBlend:0, bus:1(Music), playOnAwake:true, fadeInSeconds:1.0, fadeOutSeconds:0.5`(参见 NeonLobby/NeonLevel1 的 BGM 对象)。
- **切场景防爆音**:切场景前对 BGM 调 `Stop()`(fadeOut),协程 `WaitForSeconds(0.5)` 后 `loadSceneAsync`(示例游戏 switchScene 范式)。

### 13.2 AudioListener

无字段;挂主相机 GO;每帧取第一个 active 的。

### 13.3 AudioClip(解码后 PCM 资产)

```cpp
class AudioClip {
public:
    static std::shared_ptr<AudioClip> load(const std::filesystem::path& relPath);  // 失败 nullptr + error
    const std::filesystem::path& path() const;
    const std::vector<float>& pcmF32() const;   // 交错 f32 样本(AudioEngine 专用)
    uint32_t channels() const;
    uint32_t sampleRate() const;
};
```

wav/mp3/flac/ogg 自动识别;v1 全量解码 f32(流式用 AudioSource.stream)。

### 13.4 AudioEngine(引擎级音频单例)

```cpp
using AudioHandle = uint64_t;          // 0 = 无效
enum class AudioBus : uint8_t { Master = 0, Music, Sfx, Ui, Count };

class AudioEngine {
public:
    static bool available();           // 无音频设备时 false(全部 API no-op 降级)
    static void setListener(const glm::vec3& pos, const glm::vec3& forward, const glm::vec3& up);
    // 一次性播放(SFX 范式):失败返回 0
    static AudioHandle play(const std::shared_ptr<AudioClip>& clip, const glm::vec3& pos,
                            float volume, bool loop, float spatialBlend,
                            float minDistance, float maxDistance);
    static void setPosition(AudioHandle h, const glm::vec3& pos);
    static void setVolume(AudioHandle h, float v);
    static void stop(AudioHandle h);
    static void stopAll();
    static void setMasterVolume(float v);

    // ===== V1.0-8 追加 =====
    static void setBusVolume(AudioBus bus, float v);     // Master 与 setMasterVolume 同源;调节即时应用
    static float busVolume(AudioBus bus);
    // 流式播放(长 BGM,不整载 PCM):失败返回 0
    static AudioHandle playStreaming(const std::filesystem::path& relPath, AudioBus bus,
                                     float volume, bool loop, float spatialBlend,
                                     float minDistance, float maxDistance);
    static void setBus(AudioHandle h, AudioBus bus);     // 播放中改总线
    static void fadeIn(AudioHandle h, float seconds);
    static void fadeOut(AudioHandle h, float seconds);   // seconds<=0 = no-op
    // 设置持久化(assets/audio/audio-settings.novaaudio,version 1;缺失自动生成默认)
    static bool loadSettings(const std::filesystem::path& relPath);
    static bool saveSettings(const std::filesystem::path& relPath);
};
```

```cpp
// SFX 范式(NeonGameManager::playSfx):缓存 clip,2D 播放
if (!NovaEngine::AudioEngine::available()) return;
auto clip = NovaEngine::AudioClip::load("assets/audio/sfx_pickup.wav");
if (clip)
    NovaEngine::AudioEngine::play(clip, glm::vec3(0.0f), 1.0f, false, 0.0f, 1.0f, 50.0f);
// spatialBlend=0:纯 2D 全音量,位置传原点即可
```

## 14. 存档:SaveGame

```cpp
struct SaveSlotInfo {
    int slot = 0;
    bool valid = false;                 // 文件存在且 version 校验通过
    std::string scenePath;              // 存档时的场景资产相对路径
    double timestampSeconds = 0.0;
    float playTimeSeconds = 0.0f;       // 调用方提供的游戏内计时
};

class SaveGame {                        // 全静态;Play 中可随时 save/load
public:
    static constexpr int kCurrentSaveVersion = 1;
    // 目录:默认 <项目根>/saves(编辑器=项目根/saves,Player=exe 目录/saves)
    static void setSaveDirectory(const std::filesystem::path& dir);
    static std::filesystem::path saveDirectory();
    static std::filesystem::path slotPath(int slot);     // <dir>/slot<slot>.novasave

    // 存档(原子写:tmp → rename);失败 error + false
    static bool save(const Scene& scene, int slot, const std::string& scenePath,
                     float playTimeSeconds = 0.0f);
    // 读档:原地重建同一 Scene 对象(对象 ID 保持)。失败 valid=false,场景保持不变
    static SaveSlotInfo load(Scene& target, int slot);
    static std::vector<SaveSlotInfo> listSlots();        // 按 slot 升序(浅校验)
    static bool deleteSlot(int slot);
    static bool slotExists(int slot);
};
```

```cpp
// 通关写档(示例游戏范式):
NovaEngine::SaveGame::save(*scene, 0, "assets/scenes/NeonLevel1.nova.json", elapsed);

// 只读元数据(大厅显示最佳纪录——绝不 load,load 会原地重建场景摧毁大厅):
for (const auto& slot : NovaEngine::SaveGame::listSlots())
    if (slot.slot == 0 && slot.valid)
        best = slot.playTimeSeconds;
```

- **语义要点**:`load` 走 SceneSerializer::deserialize **原地重建同一 Scene 对象**——脚本持有的裸 GameObject*/组件指针需重取(与 LoadScene 同约束);对象 ID 保持,外部 Scene*/按 ID 引用有效。
- 存档文件 = `<saveDirectory>/slot<N>.novasave`(version 1,内嵌场景 JSON version 4)。

## 15. 文件读写:FileUtil

**波 3(V1.2 竞速包)新增的公开写通道**——AssetStream 是只读通道(pak 优先),脚本存档/回放数据需要写通道。只写磁盘(不碰 pak),路径锚点与 `AssetDatabase::projectRoot()` 同语义(编辑器=项目根、Player=exe 目录)。

```cpp
#include <NovaEngine/Core/FileUtil.h>   // 不在总伞头 NovaEngine.h 内!

class FileUtil {
public:
    static std::filesystem::path resolveAbs(const std::filesystem::path& relativePath);
    // 原子写文本(临时文件 + rename;父目录不存在时自动创建)
    static bool writeText(const std::filesystem::path& relativePath, const std::string& text);
    // 读文本(不存在/读取失败返回 false)
    static bool readText(const std::filesystem::path& relativePath, std::string& out);
};
```

```cpp
// 本地排行榜(NeonGameManager 范式):
std::string old;
if (NovaEngine::FileUtil::readText("saves/leaderboard.novaboard", old)) { /* 解析 */ }
NovaEngine::FileUtil::writeText("saves/leaderboard.novaboard", outJson.dump());
```

「saves/xx」即 `SaveGame::saveDirectory()` 约定目录。

## 16. 粒子:ParticleSystem

```cpp
class ParticleSystem : public Component {
public:
    float emissionRate = 30.0f;      // 个/秒
    float lifetime = 1.0f;           // 秒(每粒子 ±20% 随机)
    float initialSpeed = 2.0f;       // m/s(实际 = initialSpeed × [0.6, 1.0])
    float gravity = 0.0f;            // 世界 -Y 加速度(m/s²)
    glm::vec4 startColor{1.0f};
    glm::vec4 endColor{1.0f, 1.0f, 1.0f, 0.0f};   // 寿命内线性插值
    float startSize = 0.2f;
    float endSize = 0.05f;
    std::string texturePath;         // 纹理条(flipbook)贴图;空 = 白块
    int tileX = 1, tileY = 1;        // 纹理条网格(寿命内按序播放)
    int maxParticles = 1000;
    bool loop = true;                // false = 发射 lifetime 秒后停发,已发射粒子自然消亡
    bool playOnAwake = true;
    bool billboard = true;           // v1 固定 true(相机面向 quad)
    bool blendAdditive = false;      // true = 加性混合(火花/火焰)

    void play(); void stop(); void clear();
    bool isPlaying() const;
    uint32_t aliveCount() const;
};
```

- 发射形状 v1 固定:以发射器 up 轴为轴、半角 30° 圆锥内均匀随机方向。
- Edit 态渲染「稳态快照」(确定性预填、时间冻结——参数编辑即时预览);Play 态 onUpdate 全速推进;退出 Play 自动清零。
- 拾取爆发范式:子对象挂 ParticleSystem{playOnAwake:false, loop:false},命中时 `ps->play()`(NeonCollectible 的 Burst)。

## 17. 其他组件速查

### MeshRenderer 与 Material

```cpp
class MeshRenderer : public Component {
public:
    std::shared_ptr<Mesh> mesh = nullptr;    // 运行时 GPU 网格
    std::string meshPath;                    // 资产相对路径(如 "assets/models/cube.obj");builtin:cube/sphere/plane 合法
    Material material;
};

enum class BlendMode { Opaque, AlphaBlend };
struct Material {
    glm::vec3 albedo{0.8f};
    float roughness = 0.6f, metallic = 0.0f;
    std::shared_ptr<Texture> albedoMap = nullptr;
    std::string albedoMapPath;
    float alpha = 1.0f;                       // AlphaBlend 时的混合系数
    BlendMode blendMode = BlendMode::Opaque;
    std::shared_ptr<Texture> normalMap, metallicMap, roughnessMap, emissionMap;
    std::string normalMapPath, metallicMapPath, roughnessMapPath, emissionMapPath;
    glm::vec3 emissionColor{0.0f};
    float emissionIntensity = 1.0f;
    std::shared_ptr<MaterialAsset> materialAsset;   // 非空 = 引用 .novamat(整块引用)
    std::string materialAssetPath;
    std::shared_ptr<Shader> shaderOverride = nullptr;   // Shader Graph 运行时
    std::string shaderGraphPath;                        // .novashadergraph 相对路径;空 = 默认 shader
};
```

脚本改材质当帧生效(示例:门激活换 `material.shaderGraphPath` 与 `emissionIntensity`,灯带点亮 `material.emissionIntensity = 2.0f`)。

### Camera

```cpp
enum class ProjectionMode { Perspective, Orthographic };
class Camera : public Component {
public:
    float fov = 60.0f;
    float nearPlane = 0.1f, farPlane = 1000.0f;
    glm::vec4 clearColor{0.18f, 0.22f, 0.35f, 1.0f};
    bool isMain = false;                       // Play 时优先使用(无则第一个 Camera)
    ProjectionMode projectionMode = ProjectionMode::Perspective;
    float orthoSize = 5.0f;                    // 正交半高(米)
};
```

- **相机无跟随逻辑**——第三人称 = 相机挂角色子节点(示例:local (0,2.5,5.0) 俯视)。

### DirectionalLight

```cpp
class DirectionalLight : public Component {
public:
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    bool castShadows = true;                   // 世界方向 = transform()->forward()
    float shadowBias = 0.0005f;
    float shadowRange = 60.0f;                 // 正交投影半宽(米)
    int shadowMapSize = 2048;                  // 512..4096
};
```

### 后处理设置(Scene::postProcess)

```cpp
struct PostProcessSettings {
    bool enabled = false;
    float bloomThreshold = 1.0f;   // 亮度提取阈值(线性空间)
    float bloomIntensity = 0.5f;
    int bloomLevels = 3;           // 1..3
    float exposure = 1.0f;
    bool ssaoEnabled = false;  float ssaoRadius = 0.5f;  float ssaoStrength = 1.0f;
    int ssaoSampleCount = 16;      // 8/16/32
    std::string lutPath;           // .cube 相对路径(空 = 关)
    float lutIntensity = 0.0f;     // 0..1(缺省 0 = 关闭)
    bool dofEnabled = false;  float dofFocusDistance = 5.0f, dofAperture = 0.05f;
    bool vignetteEnabled = false;  float vignetteIntensity = 0.4f;
    bool grainEnabled = false;     float grainAmount = 0.05f;
};
```

### SpriteRenderer / BoxCollider2D(2D 模式)

- `SpriteRenderer`:spritePath/color/anchor/offset/size 等字段(2D 精灵)。
- `BoxCollider2D`:2D AABB 碰撞(§8.5 事件族),Play 每帧 O(n²) 重叠对检测(上限 500 组件)。

### Profiler(帧时间线/内存快照,脚本可读)

```cpp
struct MemorySnapshot {
    size_t meshVertexBytes, textureBytes, audioBytes, cubemapBytes;
    size_t materialAssetCount, objectCount, componentCount, loadedSceneCount;
};
class Profiler {
public:
    static constexpr uint32_t kCapacity = 600;   // 10 秒 @60FPS 环形帧缓冲
    static MemorySnapshot memorySnapshot();
    static float lastOverheadMs();               // 自身开销(<1ms)
    static const FrameRecord* frame(uint32_t index);   // 0 = 最旧
    // NE_PROFILE_SCOPE("name") / NE_GPU_SCOPE("name") 宏包裹代码段(关闭 = 零开销)
};
```

### Logger(日志)

```cpp
enum class LogLevel { Debug, Info, Warn, Error };
// 格式串用 std::format 语法:
NovaEngine::Logger::info("收集能量球 {}/{}", m_collected, m_orbTotal);
NovaEngine::Logger::warn("音频加载失败 \"{}\"", path);
NovaEngine::Logger::error("loadSceneAsync(\"{}\") 失败", name);
```

输出到 stdout + OutputDebugString + 编辑器 Console 面板 + 日志文件(`<项目根>/Logs/`)。

## 18. 序列化字段键名对照

场景文件 `.nova.json`(schema **version 4**)。脚本写 onSerialize/onDeserialize 时键名自定(如 `"speed"`);内置组件键名如下(全部来自 SceneSerializer 实际读写,缺省 = 旧文件零变化兜底):

| 组件 | JSON 键 |
|---|---|
| GameObject | `id`(uint64 字符串或数)、`name`、`active`、`parent`(父 id;0 = 根)、`dontDestroyOnLoad` |
| Transform | `position`[x,y,z]、`rotation`[x,y,z,w] 四元数、`scale` |
| Camera | `fov`、`nearPlane`、`farPlane`、`clearColor`、`isMain`、`projectionMode`、`orthoSize` |
| DirectionalLight | `color`、`intensity`、`castShadows`、`shadowBias`、`shadowRange`、`shadowMapSize` |
| MeshRenderer | `mesh`(路径)、`material`{albedo/roughness/metallic/alpha/blendMode/albedoMap/normalMap/metallicMap/roughnessMap/emissionMap/emissionColor/emissionIntensity/materialAsset/shaderGraphPath} |
| Rigidbody | `mass`、`linearDamping`、`angularDamping`、`useGravity`、`isKinematic` |
| Collider 基类 | `isTrigger`、`center`、`layer`、`friction`、`restitution`;BoxCollider 加 `halfExtents`、SphereCollider 加 `radius`、CapsuleCollider 加 `radius`/`height`、MeshCollider 加 `meshPath` |
| CharacterController | `radius`、`height`、`stepHeight`、`slopeLimitDeg`、`mass`、`gravityScale` |
| Canvas | `uiScale` |
| Text | `text`、`fontSize`、`color`、`fontPath`、`anchor`、`offset`、`wrap`、`wrapWidth`、`hAlign` |
| Image | `spritePath`、`color`、`slice9`、`anchor`、`offset`、`size`、`fillType`、`fillAmount`、`fillClockwise` |
| Button | `color`、`hoverColor`、`pressedColor`、`slice9`、`anchor`、`offset`、`size` |
| AudioSource | `clipPath`、`volume`、`loop`、`spatialBlend`、`minDistance`、`maxDistance`、`playOnAwake`、`bus`、`stream`、`fadeInSeconds`、`fadeOutSeconds` |
| ParticleSystem | `emissionRate`、`lifetime`、`initialSpeed`、`gravity`、`startColor`、`endColor`、`startSize`、`endSize`、`texturePath`、`tileX`、`tileY`、`maxParticles`、`loop`、`playOnAwake`、`billboard`、`blendAdditive` |
| 脚本组件 | `type`(注册名)、`enabled` + 脚本 onSerialize 的自定义字段 |
| environment 段 | 天空 6 字段(skyMode/skyHorizonColor/skyZenithColor/skyCubemapPath/skyRotation/skyExposure)+ skyColor/ambientColor/showGrid/fogColor + `ibl`{iblIntensity} + `postProcess`(§17 键名)+ `physics`{layerNames, collisionMatrix} |

脚本注册名含 `::` 时(宏传入 `NovaGame::SpinningCube`),注册名取最后一段("SpinningCube")——场景 JSON 里 `type` 写不带命名空间的名字。

## 19. 帧内顺序与语义约定

**帧内顺序**(Player 主循环;编辑器 Play 同构):

1. pollEvents → Time/Input/InputMap beginFrame
2. UI processInput(Player 在 scene.update **前** → 脚本 onUpdate 读到本帧按钮状态;编辑器 SceneView 为渲染期调用,滞后一帧)
3. `SceneManager::updateAll`(activeScene 先):flushDestroyQueue → **物理 step(固定 60Hz,每帧最多 3 步)** → onUpdate 广播 → **协程推进(普通 yield)** → **Events flush → 协程推进(WaitForEndOfFrame)**
4. `AsyncLoader::flushPendingResults`(异步加载的 GPU 上传 + 场景挂载)
5. `SceneManager::flushPendingLoad`(延迟切场景/卸载)
6. 音频监听跟随主相机 → 渲染(3D + 后处理 + UI)→ 显示

**关键语义约定**:

- 所有 `move/addForce/setVelocity/setPosition` 等物理接口仅 Play 中有效,Edit 态安全 no-op。
- 动态刚体/CharacterController 的对象,Transform 由物理驱动——脚本改 Transform 会被覆盖。
- `enabled=false` 的脚本不收到 onStart/onUpdate(引擎不广播);协程不暂停(与 Unity 一致)。
- 脚本生命周期:`onAwake`(挂载/反序列化后)→ `onStart`(进 Play 一次)→ `onUpdate`(每帧)→ 退出 Play 快照还原。
- 热重载(编辑器 Scripts → Reload Scripts,Ctrl+Shift+R):DLL 卸载前快照脚本字段(onSerialize)→ 重载后按「对象 ID + 组件下标」原位恢复(onDeserialize)。**非序列化运行时状态丢失;静态单例指针失效**——用 §2.2 的自愈模式。
- `SceneManager::loadScene`/`loadSceneAsync`/`unloadScene` 都是**帧末生效**(延迟执行防广播中迭代器失效);返回 false 只表示入队失败(查无场景)。
- 场景参数在加载调用瞬间快照,新场景 `onStart` 期间可读,挂载结束后恢复活表。

---

*本文档由内容域 Coder 于 V1.0-14 编写,API 签名逐条对照 engine/include 核实(抽查清单见交付报告)。发现文档与代码不符时,以代码为准。*
