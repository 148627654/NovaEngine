# Unity → NovaEngine 迁移对照表

> 给 Unity 开发者:C# MonoBehaviour 项目迁移到 NovaEngine C++20 脚本的常用 API 对照与语义差异。快照时间 2026-11-25(V1.0 波 3),以 engine/include 实际代码为准。
> 先读 [TUTORIAL.md](TUTORIAL.md) 第 4 步建立「脚本怎么注册/挂载」的直觉,再按本表逐项迁移。完整签名见 [API-REFERENCE.md](API-REFERENCE.md)。

## 0. 项目形态总览(先读)

| Unity | NovaEngine |
|---|---|
| C# 脚本,引擎自动编译 | C++20 脚本,`samples/` 下源码 → 手动构建 `NovaGame.dll` → 编辑器 Scripts → Reload Scripts 热重载 |
| `MonoBehaviour` | 继承 `NovaEngine::Behaviour`;.cpp 末尾 `NE_REGISTER_SCRIPT(命名空间::类名)` 注册 |
| Inspector 显示 public 字段 | `onSerialize/onDeserialize` 成对手写字段(JSON),Inspector 按类型自动出控件(bool/float/int/string) |
| 场景 `.unity` | 纯 JSON 场景 `.nova.json`(schema version 4,可手改) |
| Prefab | `.novaprefab`(Hierarchy 右键 Create Prefab;拖入场景实例化;Inspector Apply/Revert) |
| 共享材质资产 | `.novamat` 材质库(改一处、处处同步) |
| Scene 列表 Build Settings | File → Build Settings... 写 `assets/build.novabuild` |

## 1. GameObject

| Unity | Nova | 注意点 |
|---|---|---|
| `gameObject.name` / `.name = x` | `gameObject()->name()` / `setName(x)` | 脚本内用 `gameObject()` 取所属对象 |
| `gameObject.SetActive(b)` | `gameObject()->setActiveSelf(b)` | `activeInHierarchy()` = 自身&&祖先全激活 |
| `gameObject.activeSelf` | `gameObject()->activeSelf()` | |
| `gameObject.transform` | `gameObject()->transform()`(或脚本内 `transform()`) | |
| `GameObject.Find("Player")` | `scene->findGameObjectsByName("Player")` | 返回 vector(可能 0/1/多个);`findGameObject(id)` 按 ID |
| `new GameObject("X")` / `Instantiate` | `scene->createGameObject("X", parent)`(自动附 Transform);Prefab 实例化走编辑器拖放 | 运行时动态创建对象是可行的,但无运行时 Prefab 实例化 API(v1) |
| `gameObject.GetComponent<T>()` | `gameObject()->getComponent<T>()` / `getComponents<T>()` | 按 typeId 线性扫描;**脚本类查找用 `findScript<具体脚本T>(go)`**(脚本共用 Behaviour typeId) |
| `gameObject.AddComponent<T>()` | `gameObject()->addComponent<T>()` | 同类型已存在则 assert(编辑器 Add Component 不去重) |
| `Destroy(gameObject)` | `gameObject()->destroy()`(或 `scene->destroyGameObject(id)`) | 延迟到帧末真正删除(与 Unity 同语义) |
| `gameObject.transform.parent` | `gameObject()->parent()` / `setParent(p, keepWorld=true)` | keepWorldTransform 默认 true |
| `DontDestroyOnLoad(go)` | `SceneManager::dontDestroyOnLoad(go)` 或 Inspector 勾选 DDOL | 根子树跨场景存活 |

## 2. Transform

| Unity | Nova | 注意点 |
|---|---|---|
| `transform.position` / `.position = v` | `worldPosition()` / `setLocalPosition(v)`(局部) | 无世界 setter——设局部,或先算局部值 |
| `transform.localPosition` | `localPosition()` / `setLocalPosition(v)` | |
| `transform.rotation`(Quaternion) | `localRotation()` / `setLocalRotation(q)` / `worldRotation()` | 内部四元数;Inspector 显示欧拉角(度) |
| `transform.eulerAngles` | `localEulerAngles()` / `setLocalEulerAngles(v)` | **单位是度** |
| `transform.localScale` | `localScale()` / `setLocalScale(v)` | |
| `transform.Translate(d)` | `translate(worldDelta)` | 世界系位移 |
| `transform.Rotate(e)` | `rotate(eulerDeg)` | 绕自身轴累计旋转,度 |
| `transform.LookAt(t)` | `lookAt(worldTarget)` | |
| `transform.forward / right / up` | `forward()` / `right()` / `up()` | 同约定:forward = +Z,right = +X,up = +Y |

## 3. Input

| Unity | Nova | 注意点 |
|---|---|---|
| `Input.GetKey(KeyCode.W)` | `Input::isKeyDown(Key::W)` | 枚举 `Key` 值域与 GLFW 一致 |
| `Input.GetKeyDown/GetKeyUp` | `Input::isKeyPressed/isKeyReleased` | 边沿语义相同(本帧) |
| `Input.GetMouseButton(0)` | `Input::isMouseDown(MouseButton::Left)` | |
| `Input.GetAxis("Horizontal")` | `InputMap::getAxis("Horizontal")` | 绑定表 `assets/input/default.novainput`;编辑器 File → Input Bindings... |
| `Input.GetButton("Jump")` | `InputMap::getAction("Jump")` | |
| `Input.GetButtonDown("Jump")` | `InputMap::getActionDown("Jump")` | |
| 新版 Input System | 无对应(v1 用 InputMap 动作/轴两态) | 键鼠/手柄多源并存(绑定里各写各的) |
| `Input.GetJoystick` / `Input.GetAxis("LHorizontal")` | `Input::isGamepadButtonDown(...)` / `Input::gamepadAxis(...)` | 最多 4 只手柄,死区默认 0.15 |
| `Input.mousePosition` | `Input::mousePosition()` | 窗口逻辑坐标,左上原点(与 Unity 左下原点不同) |

## 4. Time

| Unity | Nova | 注意点 |
|---|---|---|
| `Time.deltaTime` | `Time::deltaTime()` | 秒,上限钳制 0.1s |
| `Time.time` | `Time::totalTime()` | 启动以来秒数(计时起点用它做差) |
| `Time.frameCount` | `Time::frameCount()` | |

## 5. Physics

| Unity | Nova | 注意点 |
|---|---|---|
| `Rigidbody` | `Rigidbody` 组件(mass/useGravity/isKinematic/linearDamping/angularDamping) | 同 GO 与 CharacterController **互斥**(挂两者 = warn + CC 优先) |
| `rb.AddForce(f)` | `rb->addForce(f)` / `addImpulse(f)` | Play 中调用;Edit 态安全 no-op |
| `rb.velocity` | `rb->velocity()` / `setVelocity(v)` | |
| `CharacterController.Move(d)` | `CharacterController::move(displacement)` | 封装 Jolt CharacterVirtual(楼梯步进/坡度限制) |
| `cc.isGrounded` | `cc->isGrounded()` | Play 中有效 |
| `cc.enabled = false`(瞬移) | `cc->setPosition(worldPos)` | **物理传送**——只改 Transform 会被物理覆盖(波 3 实测:复活必须走 setPosition) |
| `Physics.Raycast(origin, dir, out hit, dist)` | `scene->physics()->raycast(origin, dir, maxDist, hit)` | hit 结构:point/normal/distance/hitObject |
| `Physics.RaycastAll` | `raycastAll(origin, dir, maxDist, filter, outHits)` | 距离升序;filter = layerMask + ignore |
| `Physics.OverlapSphere/OverlapBox` | `overlapSphere/overlapBox(center, 参数, filter, outObjects)` | 返回 GO 列表(含触发器) |
| Layer 与碰撞矩阵 | `Collider.layer`(0..15)+ 场景级 16×16 矩阵(Physics Layers 面板) | 矩阵对称,写 environment.physics 段 |
| Physic Material | Collider 内联 `friction` / `restitution` | body 摩擦 = 各 collider 平均 |
| `OnTriggerEnter/Exit`(3D) | `subscribeEvent<TriggerEvent>`(entered / gameObjectA / gameObjectB) | 事件系统投递,**不是组件虚回调**;物理层没有 3D OnCollisionEnter 事件(v1) |
| `OnCollisionEnter2D` 等(2D) | `subscribeEvent<Collision2DEnterEvent/Stay/ExitEvent>`、`Trigger2DEnterEvent/Stay/ExitEvent` | 2D 六事件族齐全,`other` + `overlap` 字段 |
| 固定步进 | 固定 60Hz 累加器(每帧最多 3 步) | 不可配置(v1) |

## 6. UI(运行时 UI,可打包进 Player)

| Unity(uGUI) | Nova | 注意点 |
|---|---|---|
| Canvas | `Canvas` 组件(UI 子树根,屏幕空间全屏) | Transform 忽略 |
| `Text`(或 TextMeshPro) | `Text` 组件(text/fontSize/color/anchor/offset/wrap/wrapWidth/hAlign) | UTF-8 中文;默认字体链 Noto Sans SC → 等线 → 微软雅黑 |
| `Image` | `Image` 组件(spritePath/color/slice9/fillType/fillAmount) | slice9 = 九宫格(左/上/右/下像素边距) |
| `Button.onClick.AddListener(...)` | `Button::onClick = [this](){...}`(std::function 直接赋值) | 无 Inspector 事件绑定——脚本接线;`hovered()`/`pressedThisFrame()` 可查询 |
| RectTransform 锚点(相对父) | `anchor`(0..1)+ `offset`(像素) | **位置 = anchor × 屏幕尺寸 + offset**;**子节点 anchor 相对屏幕解析,不是相对父 Rect**(仅 ScrollView 内容传播平移)——「按钮内放标签子对象」不会随按钮走,标签做同 offset 兄弟节点 |
| 全锚点拉伸 | 无(v1 点锚;`size` 固定像素) | 用 ScrollView 做滚动内容 |
| Slider | `Slider` 组件(横向 v1;onValueChanged 回调) | |
| ScrollRect | `ScrollView` 组件(内容 = 直接子物体;scissor 裁剪;滚轮滚动) | v1 不做内容自动重排 |

## 7. SceneManager(场景管理)

| Unity | Nova | 注意点 |
|---|---|---|
| `SceneManager.LoadScene("L")` | `SceneManager::loadScene("L")` | 候选集 = build.novabuild 列表;**帧末生效**(入队) |
| `LoadSceneAsync("L", additive)` | `loadSceneAsync("L", additive)` | 后台资产加载线程;进度 `AsyncLoader::progress()` |
| `SceneManager.UnloadSceneAsync` | `unloadScene("L")` | 仅 additive 可卸;帧末延迟销毁(内存回落) |
| `SceneManager.GetActiveScene().name` | `activeScene()->name()` / `loadedScenes()`(active 在前) | |
| 传参(Unity 无内置,靠 static/持久对象) | `setSceneParam(key, jsonValue)` / `getSceneParam(key)` | **参数是 JSON 字符串**;加载瞬间快照,新场景 onStart 可读 |
| 切场景事件 | `subscribeEvent<SceneLoaded>` / `SceneUnloaded` | post 延迟投递(下一帧收到) |
| `DontDestroyOnLoad` | `SceneManager::dontDestroyOnLoad(go)` | |

## 8. Coroutine

| Unity | Nova | 注意点 |
|---|---|---|
| `StartCoroutine(IEnumerator)` | `startCoroutine(协程函数调用)`(C++20 协程) | 协程对象由 Behaviour 持有,销毁自动终止 |
| `yield return new WaitForSeconds(2)` | `co_await WaitForSeconds{2.0f}` | |
| `yield return null`(下一帧) | `co_await WaitForEndOfFrame{}`(帧末,事件 flush 后) | 普通 `co_await` 无原语时按「下一帧」推进 |
| `yield return new WaitUntil(pred)` | `co_await WaitUntil{[]{...}}` | |
| 等待另一协程 | `co_await WaitForCoroutine{other}` | **只等同一 Behaviour 内的协程**(跨对象可能悬垂) |
| `StopAllCoroutines` | `stopAllCoroutines()` | |

**重要差异**:
- **MSVC 工具链已知坑**:避免「lambda 协程 + 闭包捕获」(调试版闭包未随帧复制 → 崩溃),一律**自由协程函数 + 参数**。
- 协程未捕获异常 → 记日志终止(不重抛)。
- `enabled=false` 不暂停协程(与 Unity 一致);非激活 GO 上的协程照常推进(与 Unity 差异)。

## 9. 事件系统(UnityEvent / C# event 替代)

| Unity | Nova |
|---|---|
| `event Action OnX;` / `UnityEvent` | 类型化事件:`Events::subscribe<E>(handler)` / `publish<E>(e)`(同步)/ `post<E>(e)`(帧末延迟) |
| 组件上挂监听 | `Behaviour::subscribeEvent<E>(handler)`(析构自动退订) |

事件类型即订阅键(std::type_index)。内置:`TriggerEvent`、`SceneLoaded`、`SceneUnloaded`、2D 碰撞六事件。

## 10. 音频

| Unity | Nova | 注意点 |
|---|---|---|
| `AudioSource` 组件 | `AudioSource` 组件(clipPath/volume/loop/spatialBlend/minDistance/maxDistance/playOnAwake) | 追加字段:`bus`(0=Master/1=Music/2=Sfx/3=Ui)、`stream`(BGM 流式)、`fadeInSeconds/fadeOutSeconds` |
| `audioSource.Play()/Stop()` | `Play()` / `Stop()` | Stop 带 fadeOutSeconds>0 时先淡出无爆音 |
| `AudioMixer` 总线 | `AudioEngine::setBusVolume(bus, v)` / Mixer 面板 | 4 总线固定;设置持久化 `assets/audio/audio-settings.novaaudio` |
| `AudioClip` | `AudioClip::load(path)` + `AudioEngine::play(clip, pos, volume, loop, spatialBlend, minD, maxD)` | SFX 范式:spatialBlend=0 纯 2D;失败返回句柄 0 |
| `AudioListener` | `AudioListener` 组件(无字段,挂主相机 GO) | |

## 11. 存档

| Unity | Nova | 注意点 |
|---|---|---|
| `PlayerPrefs` | `SaveGame`(槽位式)+ `FileUtil::readText/writeText` | 无键值偏好存储——用 FileUtil 写 JSON 到 "saves/xx" |
| `JsonUtility` / 手写序列化 | nlohmann/json(引擎依赖,脚本可直接 include) | |
| 场景存档 | `SaveGame::save(scene, slot, scenePath, playTimeSeconds)` / `load(scene, slot)` | **load 原地重建场景**(对象 ID 保持;脚本持有的裸指针需重取);`listSlots()` 只读元数据 |

## 12. 常见迁移「踩坑清单」

1. **脚本字段要成对写 onSerialize/onDeserialize**——Unity 的 public 字段自动序列化在这里不存在,漏写 = Inspector 看不到/不保存。
2. **`getComponent<具体脚本T>()` 不命中脚本类**(脚本共用 Behaviour 的 typeId)——用 `findScript<T>(go)`。
3. **动态刚体/CharacterController 的对象直改 Transform 会被物理覆盖**——用 addForce/setVelocity/move/setPosition。
4. **UI 子锚点相对屏幕**——按钮+标签 = 同 offset 兄弟节点,不是父子。
5. **协程别用 lambda + 闭包捕获**——自由函数 + 参数。
6. **触发器是事件不是回调**——`subscribeEvent<TriggerEvent>`,在 onStart 里订阅一次。
7. **场景切换都是帧末生效**——loadScene 后场景还活着,别立刻访问新场景对象;在 onStart/SceneLoaded 里做初始化。
8. **Camera 无跟随组件**——第三人称 = 相机挂角色子节点(示例游戏方案)。
9. **角色复活走 `CharacterController::setPosition`**(物理传送),只改 Transform 会被回滚。
10. **自由台面可步进高度 > stepHeight**(胶囊圆底骑边)——设计阻挡高度按 ≥1.0m。
11. **Player 窗口固定 1280×720**(当前版本),UI 按此基准布局。
12. **进出 Play 由快照还原**——脚本非序列化运行时成员不保留(与 Unity 编辑器 Play 一致,但这里连「对象身份」都重建,静态单例需 onUpdate 自愈重注册)。

## 13. 无对应功能(迁移前确认不依赖)

- **3D 碰撞回调**(OnCollisionEnter/Stay/Exit):v1 只有 TriggerEvent(触发器)+ 2D 六事件族。需要碰撞反馈的场景(如弹跳音效)用触发器或轮询替代。
- **点光/聚光灯**:只有 DirectionalLight(暗环境靠低环境光 + 自发光材质营造)。
- **NavMesh/寻路**:无(v1)。
- **动画事件/BlendTree 细节**:有 AnimatorController 状态机(蒙皮动画),接口见 ARCHITECTURE §25.7。
- **UnityEvent 可视化连线**:脚本 std::function 接线。
- **AssetBundle/Addressables**:无——.novapak 打包 + pak 挂载替代。

---

*本文档由内容域 Coder 于 V1.0-14 编写;对照表 API 逐条核实 engine/include。迁移中遇到表外 API 请查 [API-REFERENCE.md](API-REFERENCE.md)。*
