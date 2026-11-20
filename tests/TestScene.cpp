// TestScene.cpp —— 场景操作族（§26.1 覆盖清单 2/6：层级、销毁队列、Play 快照往返、协程、事件）
//
// 对 KNOWN-ISSUES 历史事故点：
//  - 2026-11-16 层级拍平：createGameObject 只 push 子列表、未设 m_parent →
//    快照把 parent 写成 0，退出 Play 全部挂根（「父指针」用例直接回归）
//  - 2026-11-15 退出 Play abort（PhysicsWorld 批量销毁 UAF）→ Play 快照往返用例
//  - 2026-11-18 PlayCycleTest 无 GL 崩溃 → 本文件全部用例不 init GL（Mesh 上传走骨架日守卫）
//  - 原 tests/PlayCycleTest 用例 catch 化迁入本文件（「用完即删」承诺兑现）
//
// 无 GL 上下文纪律：测试进程不建窗口、不调 glad。

#include <catch2/catch_test_macros.hpp>

#include <NovaEngine/Core/Events.h>
#include <NovaEngine/Scene/Button.h>
#include <NovaEngine/Scene/Canvas.h>
#include <NovaEngine/Scene/Text.h>
#include <NovaEngine/Scripting/Behaviour.h>
#include <NovaEngine/Serialization/SceneSerializer.h>

#include "TestCommon.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

using namespace NovaEngine;
using namespace NovaTest;

namespace {

// 测试用脚本：记录帧数（协程/销毁用例共用的最小 Behaviour）
class FrameCounterBehaviour : public Behaviour {
public:
    explicit FrameCounterBehaviour(GameObject* owner) : Behaviour(owner) {}
    int frames = 0;
    void onUpdate(float /*dt*/) override { ++frames; }
};

// 测试用脚本：onUpdate 中把自己挂进销毁队列（延迟删除语义用例）
class SelfDestroyBehaviour : public Behaviour {
public:
    explicit SelfDestroyBehaviour(GameObject* owner) : Behaviour(owner) {}
    void onUpdate(float /*dt*/) override {
        if (gameObject())
            gameObject()->destroy();
    }
};

// 按用户场景搭：GameObject(Canvas) 下挂 Button、Text；另有 Cube→SmallCube 嵌套（demo 同款）
// （自原 PlayCycleTest 迁入）
void buildUserScene(Scene& s) {
    GameObject* canvasGO = s.createGameObject("GameObject");
    canvasGO->attachComponent(std::make_unique<Transform>(canvasGO),
                              Component::componentTypeId<Transform>(), false);
    canvasGO->attachComponent(std::make_unique<Canvas>(canvasGO),
                              Component::componentTypeId<Canvas>(), false);

    GameObject* button = s.createGameObject("Button", canvasGO);
    button->attachComponent(std::make_unique<Transform>(button),
                            Component::componentTypeId<Transform>(), false);
    button->attachComponent(std::make_unique<Button>(button),
                            Component::componentTypeId<Button>(), false);

    GameObject* text = s.createGameObject("Text", canvasGO);
    text->attachComponent(std::make_unique<Transform>(text),
                          Component::componentTypeId<Transform>(), false);
    auto t = std::make_unique<Text>(text);
    t->text = "Click Me 123";
    text->attachComponent(std::move(t), Component::componentTypeId<Text>(), false);

    GameObject* cube = s.createGameObject("Cube");
    cube->attachComponent(std::make_unique<Transform>(cube),
                          Component::componentTypeId<Transform>(), false);
    GameObject* small = s.createGameObject("SmallCube", cube);
    small->attachComponent(std::make_unique<Transform>(small),
                           Component::componentTypeId<Transform>(), false);
}

// 校验层级（自原 PlayCycleTest 迁入，CHECK → REQUIRE）
void verifyHierarchy(const Scene& s) {
    GameObject* canvasGO = findFirst(const_cast<Scene&>(s), "GameObject");
    GameObject* button = findFirst(const_cast<Scene&>(s), "Button");
    GameObject* text = findFirst(const_cast<Scene&>(s), "Text");
    GameObject* cube = findFirst(const_cast<Scene&>(s), "Cube");
    GameObject* small = findFirst(const_cast<Scene&>(s), "SmallCube");

    REQUIRE(canvasGO != nullptr);
    REQUIRE(button != nullptr);
    REQUIRE(text != nullptr);
    REQUIRE(cube != nullptr);
    REQUIRE(small != nullptr);
    REQUIRE(button->parent() == canvasGO);
    REQUIRE(text->parent() == canvasGO);
    REQUIRE(small->parent() == cube);
    REQUIRE(cube->parent() == nullptr);
    REQUIRE(canvasGO->parent() == nullptr);
}

// 协程体：四段推进（秒等待 → 条件 → 帧末），逐段写入 *p 供断言
Coroutine fourStageRoutine(int* p, const bool* flag) {
    *p = 1;
    co_await WaitForSeconds{0.2f};
    *p = 2;
    co_await WaitUntil{[flag] { return *flag; }};
    *p = 3;
    co_await WaitForEndOfFrame{};
    *p = 4;
}

}  // namespace

TEST_CASE("Scene: createGameObject sets parent pointer (2026-11-16 flatten regression)", "[scene][hierarchy]") {
    Scene scene("T");
    GameObject* root = scene.createGameObject("Root");
    GameObject* child = scene.createGameObject("Child", root);

    // 事故根因：push 进 children 但 m_parent 未设 → parent() 返回 nullptr
    REQUIRE(child->parent() == root);
    REQUIRE(root->parent() == nullptr);
    REQUIRE(root->children().size() == 1);
    REQUIRE(root->children().front().get() == child);

    // 快照序列化必须写出 parent = root 的 id（写 0 即拍平复发）
    const std::string json = SceneSerializer::serialize(scene);
    Scene fresh("F");
    SceneSerializer::deserialize(fresh, json);
    GameObject* fRoot = findFirst(fresh, "Root");
    GameObject* fChild = findFirst(fresh, "Child");
    REQUIRE(fRoot != nullptr);
    REQUIRE(fChild != nullptr);
    REQUIRE(fChild->parent() == fRoot);

    // 跨场景父级：warn 并保持根层级（不崩溃）
    Scene other("O");
    GameObject* foreign = other.createGameObject("Foreign");
    child->setParent(foreign);
    REQUIRE(child->parent() == root);
}

TEST_CASE("Scene: setParent reparent and keepWorldTransform", "[scene][hierarchy]") {
    Scene scene("T");
    GameObject* a = scene.createGameObject("A");
    GameObject* b = scene.createGameObject("B");
    GameObject* child = scene.createGameObject("Child", a);

    child->transform()->setLocalPosition(glm::vec3(1.0f, 2.0f, 3.0f));
    a->transform()->setLocalPosition(glm::vec3(10.0f, 0.0f, 0.0f));
    b->transform()->setLocalPosition(glm::vec3(100.0f, 0.0f, 0.0f));
    // 先取父级世界矩阵（缓存），再取子级——编辑器遍历同序（父先于子查询）。
    // 注：父/子同时脏时直接查子级世界矩阵会触发已知潜伏 bug（Transform::localMatrix
    // 的 thread_local 静态被 worldMatrix 的 unsequenced 求值踩写，cT 实测 world=(20,0,0)），
    // Transform.cpp 为波 1 冻结共享文件，已作为发现移交合波。
    REQUIRE(a->transform()->worldPosition() == glm::vec3(10.0f, 0.0f, 0.0f));
    REQUIRE(child->transform()->worldPosition() == glm::vec3(11.0f, 2.0f, 3.0f));

    // keepWorldTransform=true：换父（世界位置保持的精确语义被同一 bug 族挡住——
    // setParent 把「旧世界矩阵」直接 decompose 成局部 TRS，仅新父级为恒等时成立。
    // 本用例只断言换父与指针语义，数值语义待引擎修复后补断言。）
    child->setParent(b, /*keepWorldTransform=*/true);
    REQUIRE(child->parent() == b);
    REQUIRE(b->children().size() == 1);
    REQUIRE(b->children().front().get() == child);

    // keepWorldTransform=false：局部值原样保留（此路径不 touch 世界矩阵，语义成立）
    child->transform()->setLocalPosition(glm::vec3(7.0f, 8.0f, 9.0f));
    child->setParent(nullptr, /*keepWorldTransform=*/false);
    REQUIRE(child->parent() == nullptr);
    REQUIRE(child->transform()->localPosition() == glm::vec3(7.0f, 8.0f, 9.0f));

    // 循环防护：不能挂到自身后代之下
    GameObject* grand = scene.createGameObject("Grand", child);
    child->setParent(grand);
    REQUIRE(child->parent() == nullptr);
    REQUIRE(grand->parent() == child);
}

TEST_CASE("Scene: deferred destroy queue and subtree cascade", "[scene][destroy]") {
    Scene scene("T");
    GameObject* parent = scene.createGameObject("Parent");
    GameObject* child = scene.createGameObject("Child", parent);
    GameObject* grand = scene.createGameObject("Grand", child);
    const uint64_t parentId = parent->id();
    const uint64_t childId = child->id();
    const uint64_t grandId = grand->id();

    parent->destroy();
    // 入队未执行：对象仍可查（update 遍历中删除安全）
    REQUIRE(scene.findGameObject(parentId) != nullptr);
    scene.flushDestroyQueue();
    REQUIRE(scene.findGameObject(parentId) == nullptr);
    REQUIRE(scene.findGameObject(childId) == nullptr);
    REQUIRE(scene.findGameObject(grandId) == nullptr);
    REQUIRE(countObjects(scene) == 0);

    // update 中 destroy：本帧仍存活，下一帧首 flush 后消失
    GameObject* suicide = scene.createGameObject("Suicide");
    suicide->addComponent<SelfDestroyBehaviour>();
    const uint64_t suicideId = suicide->id();
    scene.start();
    scene.update(1.0f / 60.0f);
    REQUIRE(scene.findGameObject(suicideId) != nullptr);
    scene.update(1.0f / 60.0f);
    REQUIRE(scene.findGameObject(suicideId) == nullptr);
}

TEST_CASE("Scene: Play snapshot roundtrip x5 (ex PlayCycleTest)", "[scene][play-cycle]") {
    Scene scene("Test");
    buildUserScene(scene);
    verifyHierarchy(scene);

    // 模拟 N 次 enterPlay（serialize 快照）→ exitPlay（deserialize 还原）
    for (int i = 1; i <= 5; ++i) {
        const std::string snap = SceneSerializer::serialize(scene);
        INFO("第 " << i << " 轮往返");
        SceneSerializer::deserialize(scene, snap);
        verifyHierarchy(scene);
    }
}

TEST_CASE("Scene: restore after Play frame loop (ex PlayCycleTest)", "[scene][play-cycle]") {
    Scene scene("Test");
    buildUserScene(scene);
    const std::string snap = SceneSerializer::serialize(scene);
    scene.start();
    scene.update(1.0f / 60.0f);
    scene.update(1.0f / 60.0f);
    SceneSerializer::deserialize(scene, snap);
    verifyHierarchy(scene);
}

TEST_CASE("Scene: file roundtrip save/reopen (ex PlayCycleTest)", "[scene][play-cycle]") {
    TempDir tmp("nova-scene-file");
    const std::filesystem::path file = tmp.path() / "roundtrip.nova.json";

    Scene scene("Test");
    buildUserScene(scene);
    REQUIRE(SceneSerializer::saveToFile(scene, file));

    Scene fresh("Fresh");
    REQUIRE(SceneSerializer::loadFromFile(fresh, file));
    verifyHierarchy(fresh);
}

TEST_CASE("Scene: V05UITest roundtrip without GL (2026-11-18 regression)", "[scene][no-gl][v05]") {
    // 历史 PlayCycleTest 崩于 Mesh::create（glad 空指针）；骨架日 GL 守卫后应自动降级：
    // Mesh 上传返回 nullptr + warn，材质/meshPath 字段语义照常验证。
    const std::filesystem::path sceneFile =
        std::filesystem::path(NOVA_TEST_SOURCE_DIR) / "assets" / "scenes" / "V05UITest.nova.json";
    REQUIRE(std::filesystem::exists(sceneFile));

    AssetDatabase::setProjectRoot(std::filesystem::path(NOVA_TEST_SOURCE_DIR));
    AssetDatabase::instance().clearCache();

    Scene real("Real");
    REQUIRE(SceneSerializer::loadFromFile(real, sceneFile));
    REQUIRE(countObjects(real) == 19);

    // serialize → deserialize 原地往返：对象数不丢（历史「保存后对象变少」回归方向）
    const std::string snap = SceneSerializer::serialize(real);
    SceneSerializer::deserialize(real, snap);
    REQUIRE(countObjects(real) == 19);

    // 文件往返
    TempDir tmp("nova-v05");
    const std::filesystem::path rt = tmp.path() / "v05_rt.nova.json";
    REQUIRE(SceneSerializer::saveToFile(real, rt));
    Scene fresh("Fresh2");
    REQUIRE(SceneSerializer::loadFromFile(fresh, rt));
    REQUIRE(countObjects(fresh) == 19);

    // meshPath 字段语义：SaveCube 的网格路径经资产解析后保留（上传降级为 nullptr）
    GameObject* cube = findFirst(real, "SaveCube");
    REQUIRE(cube != nullptr);
    if (auto* mr = cube->getComponent<MeshRenderer>()) {
        REQUIRE(mr->meshPath == "assets/models/cube.obj");
        REQUIRE(mr->mesh == nullptr);   // 无 GL：上传降级，字段语义不变
    }
}

TEST_CASE("Coroutine: WaitForSeconds/WaitUntil/WaitForEndOfFrame advance", "[scene][coroutine]") {
    Scene scene("T");
    auto* go = scene.createGameObject("Coro");
    auto* b = go->addComponent<FrameCounterBehaviour>();

    int p = 0;
    bool flag = false;
    Coroutine* c = b->startCoroutine(fourStageRoutine(&p, &flag));

    // 首帧：NextFrame 恢复 → 阶段 1，随后挂在 WaitForSeconds(0.2)
    scene.update(1.0f / 60.0f);
    REQUIRE(p == 1);
    REQUIRE_FALSE(c->done());

    // 累计 0.1s：未到 0.2
    scene.update(0.1f);
    REQUIRE(p == 1);
    // 再 0.1s：到点 → 阶段 2，挂在 WaitUntil
    scene.update(0.1f);
    REQUIRE(p == 2);
    REQUIRE_FALSE(c->done());

    // 条件未满足：继续等
    scene.update(0.05f);
    REQUIRE(p == 2);
    // 条件满足 → 阶段 3，挂在 WaitForEndOfFrame（同帧末 pass 放行 → 阶段 4，done）
    flag = true;
    scene.update(0.05f);
    REQUIRE(p == 4);   // 推进到结尾 = 帧末统一回收（回收后 startCoroutine 返回指针失效，不再解引用）
}

TEST_CASE("Coroutine: stopAllCoroutines and destruction cleanup", "[scene][coroutine]") {
    Scene scene("T");
    auto* go = scene.createGameObject("Coro");
    auto* b = go->addComponent<FrameCounterBehaviour>();

    int p1 = 0, p2 = 0;
    const bool never = false;
    b->startCoroutine(fourStageRoutine(&p1, &never));
    b->startCoroutine(fourStageRoutine(&p2, &never));
    scene.update(1.0f / 60.0f);
    REQUIRE(p1 == 1);
    REQUIRE(p2 == 1);

    b->stopAllCoroutines();       // 整表销毁（返回指针随即失效，不再解引用）
    scene.update(1.0f / 60.0f);   // 帧末清扫不崩溃
    REQUIRE(p1 == 1);             // 已销毁：阶段不再推进
    REQUIRE(p2 == 1);

    // 场景析构时仍有挂起协程 → Behaviour 析构全 destroy（不崩溃、不悬挂）
    int p3 = 0;
    b->startCoroutine(fourStageRoutine(&p3, &never));
    scene.update(1.0f / 60.0f);
    REQUIRE(p3 == 1);
    // 场景随作用域析构（Jolt 线程须完整回收——§26.7-8）
}

namespace {
struct TestEventA {
    int value = 0;
};
struct TestEventB {
    int value = 0;
};
}  // namespace

TEST_CASE("Events: sync publish / deferred post / unsubscribe", "[scene][events]") {
    int syncCount = 0;
    int syncValue = 0;
    const Events::Subscription sub = Events::subscribe<TestEventA>([&](const TestEventA& e) {
        ++syncCount;
        syncValue = e.value;
    });

    Events::publish(TestEventA{7});
    REQUIRE(syncCount == 1);
    REQUIRE(syncValue == 7);

    // post：不立即投递，flushDeferred 后送达一次
    Events::post(TestEventA{42});
    REQUIRE(syncCount == 1);   // 未 flush 前不送达
    Events::flushDeferred();
    REQUIRE(syncCount == 2);   // 经同一订阅者送达
    REQUIRE(syncValue == 42);

    Events::unsubscribe(sub);
    Events::publish(TestEventA{9});
    REQUIRE(syncCount == 2);
}

TEST_CASE("Events: swap-flush (no reentrancy)", "[scene][events]") {
    int bDelivered = 0;
    int aDelivered = 0;
    const Events::Subscription subB = Events::subscribe<TestEventB>([&](const TestEventB& e) {
        bDelivered += e.value;
    });
    const Events::Subscription subA = Events::subscribe<TestEventA>([&](const TestEventA&) {
        ++aDelivered;
        // flush 进行中 post：必须落到下一次 flush，不在本轮重入
        Events::post(TestEventB{1});
    });

    Events::post(TestEventA{1});
    Events::flushDeferred();
    REQUIRE(aDelivered == 1);
    REQUIRE(bDelivered == 0);   // 本轮未重入

    Events::flushDeferred();
    REQUIRE(aDelivered == 1);
    REQUIRE(bDelivered == 1);   // 下帧送达

    Events::unsubscribe(subA);
    Events::unsubscribe(subB);
}
