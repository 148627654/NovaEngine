// TestMemory.cpp —— 生命周期与内存族（§26.1 覆盖清单补充：场景/Jolt 完整析构）
//
// 对 KNOWN-ISSUES：
//  - 2026-11-15 「退出 Play abort」：析构期重建 UAF（beginTeardown 修复回归）——
//    物理场景进 Play → 快照还原 → 析构循环，abort/悬挂即红
//  - §26.7-8：NovaTests 链接 NovaEngine 含 Jolt——测试退出须完整析构 Scene，
//    防悬挂线程挂死 ctest（本文件所有用例以场景析构完成收尾）
//  - 销毁队列清空后索引一致性（findGameObject/forEachObject 不残留）

#include <catch2/catch_test_macros.hpp>

#include <NovaEngine/Profiling/Profiler.h>
#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/MaterialAsset.h>
#include <NovaEngine/Scene/Collider.h>
#include <NovaEngine/Scene/Rigidbody.h>
#include <NovaEngine/Scene/SceneManager.h>
#include <NovaEngine/Scripting/Behaviour.h>
#include <NovaEngine/Serialization/SceneSerializer.h>

#include "TestCommon.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using namespace NovaEngine;
using namespace NovaTest;

TEST_CASE("Memory: 50 physics scene create/play/destroy cycles",
          "[memory][physics-lifecycle]") {
    // 每次循环 = 编辑器一次 Play 进出 + 场景销毁：PhysicsWorld 构造（Jolt job 线程）→
    // syncFromTransforms 全量重建 body → deserialize 批量销毁（beginTeardown 路径）→ ~Scene
    // 完整析构。任何一步 UAF/abort/悬挂线程都会让本用例红或挂死 ctest。
    for (int i = 0; i < 50; ++i) {
        Scene scene("Cycle");
        GameObject* go = scene.createGameObject("PhysicsBody");
        go->addComponent<Rigidbody>();
        go->addComponent<BoxCollider>();
        GameObject* ground = scene.createGameObject("Ground");
        ground->addComponent<BoxCollider>();   // 无 Rigidbody → static body

        const std::string snap = SceneSerializer::serialize(scene);
        scene.start();                         // 进入 Play：物理全量重建
        scene.update(1.0f / 60.0f);
        scene.update(1.0f / 60.0f);
        SceneSerializer::deserialize(scene, snap);   // 退出 Play：批量销毁 + 原地重建
        REQUIRE(countObjects(scene) == 2);
        // 循环末场景析构：残留 body 由 teardownAll 统一销毁
    }
    REQUIRE(true);   // 到达即通过（中途 abort/挂死即失败）
}

TEST_CASE("Memory: destroy queue leaves consistent index", "[memory][destroy-index]") {
    Scene scene("T");
    GameObject* parent = scene.createGameObject("Parent");
    scene.createGameObject("Child", parent);
    scene.createGameObject("Grand", findFirst(scene, "Child"));
    GameObject* solo = scene.createGameObject("Solo");
    const uint64_t soloId = solo->id();

    REQUIRE(countObjects(scene) == 4);
    parent->destroy();   // 子树级联入队
    scene.flushDestroyQueue();
    REQUIRE(countObjects(scene) == 1);
    REQUIRE(scene.findGameObject(soloId) == solo);
    REQUIRE(scene.rootObjects().size() == 1);
}

TEST_CASE("Memory: in-place rebuild leaves no stale ids", "[memory][rebuild]") {
    Scene scene("T");
    for (int i = 0; i < 5; ++i)
        scene.createGameObject("Old" + std::to_string(i));
    const uint64_t oldId = findFirst(scene, "Old3")->id();

    Scene fresh("F");
    fresh.createGameObject("New0");
    fresh.createGameObject("New1");
    SceneSerializer::deserialize(scene, SceneSerializer::serialize(fresh));

    REQUIRE(countObjects(scene) == 2);
    REQUIRE(scene.findGameObject(oldId) == nullptr);
    REQUIRE(findFirst(scene, "New0") != nullptr);
    REQUIRE(findFirst(scene, "New1") != nullptr);
}

TEST_CASE("Memory: Profiler::memorySnapshot real assertions", "[memory][snapshot]") {
    // cG 波 1 已实现 AssetDatabase::memorySnapshot（Reviewer P2-7 升级）：本用例用
    // 无 GL 可走通的资产路径做真断言。textureBytes 聚合循环可执行（空缓存 → 0）——
    // 贴图加载需 GL 上下文（Texture.cpp 上传路径尚未守卫），真值断言待 GL 守卫后补。
    TempDir tmp("nova-mem-snap");
    AssetDatabase::setProjectRoot(tmp.path());

    // 空缓存基线：聚合循环可执行，全部为零
    {
        const MemorySnapshot empty = AssetDatabase::memorySnapshot();
        REQUIRE(empty.textureBytes == 0);
        REQUIRE(empty.cubemapBytes == 0);
        REQUIRE(empty.materialAssetCount == 0);
    }

    // 材质资产计数：加载最小 .novamat（无贴图引用 → 不触 GL/纹理解码）
    const std::filesystem::path matDir = tmp.path() / "assets" / "materials";
    std::filesystem::create_directories(matDir);
    {
        std::ofstream out(matDir / "test.novamat");
        out << R"({"version":1})";
    }
    std::shared_ptr<MaterialAsset> held =
        AssetDatabase::instance().getMaterialAsset("assets/materials/test.novamat");
    REQUIRE(held != nullptr);
    {
        const MemorySnapshot withMat = AssetDatabase::memorySnapshot();
        REQUIRE(withMat.materialAssetCount == 1);   // 外部强引用存活 → 计数 1
    }
    held.reset();   // 释放后 weak_ptr 锁失败 → 回零（缓存清扫语义不依赖 releaseUnused）
    {
        const MemorySnapshot released = AssetDatabase::memorySnapshot();
        REQUIRE(released.materialAssetCount == 0);
    }

    // 活动场景对象/组件计数（Profiler::memorySnapshot 转发 + SceneManager 统计）
    Scene scene("Snap");
    scene.createGameObject("A")->addComponent<Behaviour>();
    scene.createGameObject("B");
    SceneManager::setActiveScene(&scene);
    {
        const MemorySnapshot snap = Profiler::memorySnapshot();
        REQUIRE(snap.objectCount == 2);
        REQUIRE(snap.componentCount == 3);    // 2 Transform + 1 Behaviour
        REQUIRE(snap.loadedSceneCount == 1);  // 波 1 恒 1（V1.0-2 落地后为实际数）
    }
    SceneManager::shutdown();   // 清活动场景指针，防悬垂
}

TEST_CASE("Memory: 100 empty scene create/destroy cycles", "[memory][empty-cycle]") {
    for (int i = 0; i < 100; ++i) {
        Scene scene("Empty");
        scene.start();
        scene.update(1.0f / 60.0f);
        // 作用域末析构：PhysicsWorld/Jolt 全部回收
    }
    REQUIRE(true);
}
