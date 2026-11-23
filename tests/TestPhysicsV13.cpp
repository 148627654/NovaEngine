// TestPhysicsV13.cpp —— V1.0-3 物理增强验收自测（§27.2 六项，无 GL 可跑）
//
// 覆盖（逐条对应 §27.2 验收要点）：
//  1. 楼梯步进：0.3m 台阶（<stepHeight 0.35）可上、0.5m 阻挡；20°/30° 坡可走、60° 阻挡、
//     斜坡行走速度不衰减为 0
//  2. raycastAll：3 堆叠物命中距离升序 + hitObject 正确；ignore 过滤生效
//  3. 层碰撞矩阵：关断 → 互穿，打开 → 恢复碰撞（含 Play 中 setCollisionEnabled 即时生效）
//  4. 摩擦/弹性：同斜坡同初速 friction 0.1 vs 1.0 滑距差异；restitution 0.8 vs 0 弹跳差异
//  5. 30/60/144 FPS 固定步进一致（2.0s 模拟 = 120 固定步，三档终态一致）
//  6. CharacterController 序列化往返 + Collider 新字段往返 + environment.physics 往返 +
//     旧文件缺省兜底；CC×Rigidbody 互斥（CC 优先，Rigidbody 无 body）

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <NovaEngine/Physics/PhysicsWorld.h>
#include <NovaEngine/Scene/CharacterController.h>
#include <NovaEngine/Scene/Collider.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Rigidbody.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Serialization/SceneSerializer.h>

#include <nlohmann/json.hpp>

#include <crtdbg.h>

#include <glm/vec3.hpp>

#include <cmath>

#include "TestCommon.h"

using namespace NovaEngine;
using namespace NovaTest;

namespace {
// ===== 防弹窗纪律（主会话紧急反馈 2026-11-23）：MSVC Debug 断言默认弹模态窗口阻塞用户屏幕 =====
// 本 TU 进程启动即把 _CRT_ASSERT 重定向到 stderr（失败可见且不弹窗；断言随进程 abort）。
// 临时落点：cR 测试文件（合波日主会话统一收口到 NovaTests 的 TestMain.cpp 全局入口）。
struct AssertReportRedirect {
    AssertReportRedirect() {
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    }
};
const AssertReportRedirect s_assertRedirect;
}  // namespace

namespace {

constexpr float kStep = 1.0f / 60.0f;   // 固定步

// 静态地板/平台（box，顶面在 topY）
GameObject* makeStaticBox(Scene& s, const std::string& name, const glm::vec3& half,
                          const glm::vec3& center) {
    GameObject* go = s.createGameObject(name);
    go->transform()->setLocalPosition(center);
    auto* box = go->addComponent<BoxCollider>();
    box->halfExtents = half;
    return go;
}

// 静态斜板：绕 Z 轴旋转 +angleDeg（顶面朝 +X 抬升）。顶面低端固定在 (lowX, lowY)：
//   旋转 φ=+θ：世界 (x, y) = 中心 + (xl·cosθ - yl·sinθ, xl·sinθ + yl·cosθ)
//   顶面（yl=0.25）：x = centerX + xl·cosθ - 0.25·sinθ → xl = (x - centerX + 0.25·sinθ)/cosθ
//   y = centerY + xl·sinθ + 0.25·cosθ
//   低端 xl=-L/2 → centerX = lowX + (L/2)·cosθ + 0.25·sinθ；centerY = lowY + (L/2)·sinθ - 0.25·cosθ
GameObject* makeRamp(Scene& s, const std::string& name, float angleDeg, float length,
                     float lowX, float lowY) {
    GameObject* go = s.createGameObject(name);
    const float rad = glm::radians(angleDeg);
    const float centerX = lowX + length * 0.5f * std::cos(rad) + 0.25f * std::sin(rad);
    const float centerY = lowY + length * 0.5f * std::sin(rad) - 0.25f * std::cos(rad);
    go->transform()->setLocalPosition(glm::vec3(centerX, centerY, 0.0f));
    go->transform()->setLocalRotation(glm::angleAxis(rad, glm::vec3(0.0f, 0.0f, 1.0f)));
    auto* box = go->addComponent<BoxCollider>();
    box->halfExtents = glm::vec3(length * 0.5f, 0.25f, 3.0f);
    return go;
}

// 与 makeRamp 同构造参数下的顶面高度（x → 顶面 y）
float rampSurfaceY(float angleDeg, float length, float lowX, float lowY, float x) {
    const float rad = glm::radians(angleDeg);
    const float centerX = lowX + length * 0.5f * std::cos(rad) + 0.25f * std::sin(rad);
    const float centerY = lowY + length * 0.5f * std::sin(rad) - 0.25f * std::cos(rad);
    const float xl = (x - centerX + 0.25f * std::sin(rad)) / std::cos(rad);
    return centerY + xl * std::sin(rad) + 0.25f * std::cos(rad);
}

// 角色（胶囊中心 = Transform 位置；站立地面时中心在地面上方 height/2）
GameObject* makeCharacter(Scene& s, const glm::vec3& pos, float stepHeight = 0.35f,
                          float slopeLimitDeg = 45.0f) {
    GameObject* go = s.createGameObject("Player");
    go->transform()->setLocalPosition(pos);
    auto* cc = go->addComponent<CharacterController>();
    cc->stepHeight = stepHeight;
    cc->slopeLimitDeg = slopeLimitDeg;
    return go;
}

// 以固定步模拟 T 秒；每帧朝 +X 走（位移 = speed * kStep，模拟脚本每帧 move）
void walkFor(Scene& s, GameObject* player, float seconds, float speed) {
    const int frames = int(seconds / kStep);
    for (int i = 0; i < frames; ++i) {
        if (auto* cc = player->getComponent<CharacterController>())
            cc->move(glm::vec3(speed * kStep, 0.0f, 0.0f));
        s.update(kStep);
    }
}

}  // namespace

TEST_CASE("V1.0-3 stairs: 0.3m climbable / 0.5m blocked", "[phys][character][stairs]") {
    // 台阶 A：高 0.3m（< stepHeight 0.35）→ 应爬上（追踪全程最高点：越过台阶后回到地面）
    {
        Scene scene("Stairs03");
        makeStaticBox(scene, "Ground", glm::vec3(10.0f, 0.5f, 10.0f), glm::vec3(0.0f, -0.5f, 0.0f));
        makeStaticBox(scene, "Step03", glm::vec3(1.2f, 0.2f, 2.0f), glm::vec3(1.5f, 0.1f, 0.0f));
        GameObject* player = makeCharacter(scene, glm::vec3(-2.0f, 0.9f, 0.0f));
        scene.start();
        float maxY = 0.0f;
        const int frames = int(4.0f / kStep);
        for (int i = 0; i < frames; ++i) {
            player->getComponent<CharacterController>()->move(glm::vec3(2.0f * kStep, 0.0f, 0.0f));
            scene.update(kStep);
            maxY = std::max(maxY, player->transform()->worldPosition().y);
        }
        const glm::vec3 p = player->transform()->worldPosition();
        INFO("0.3m 台阶: 最终 x=" << p.x << " y=" << p.y << " 全程最高 y=" << maxY);
        CHECK(maxY > 1.15f);   // 曾站上 0.3m 台面（胶囊中心 > 0.3 + 0.9 - 余量）
        CHECK(p.x > 1.8f);     // 越过台阶主体（台阶面 x=0.3..2.7）
    }
    // 台阶 B：高 0.5m（> stepHeight）→ 阻挡
    {
        Scene scene("Stairs05");
        makeStaticBox(scene, "Ground", glm::vec3(10.0f, 0.5f, 10.0f), glm::vec3(0.0f, -0.5f, 0.0f));
        makeStaticBox(scene, "Step05", glm::vec3(1.2f, 0.3f, 2.0f), glm::vec3(1.5f, 0.2f, 0.0f));
        GameObject* player = makeCharacter(scene, glm::vec3(-2.0f, 0.9f, 0.0f));
        scene.start();
        walkFor(scene, player, 4.0f, 2.0f);
        const glm::vec3 p = player->transform()->worldPosition();
        INFO("0.5m 台阶 4s 后位置: x=" << p.x << " y=" << p.y);
        CHECK(p.y < 1.15f);    // 未能站上 0.5m 台面
        CHECK(p.x < 1.2f);     // 被挡在台阶前（台阶面 x=0.3，胶囊半径 0.4 → 中心 x < 0.7 附近）
    }
}

TEST_CASE("V1.0-3 slopes: 20/30deg walkable, 60deg blocked, speed not zero", "[phys][character][slope]") {
    // 20° 坡（低端 0.15m 小台阶可迈上）：沿坡持续推进，速度不衰减为 0
    {
        Scene scene("Slope20");
        makeStaticBox(scene, "Ground", glm::vec3(20.0f, 0.5f, 10.0f), glm::vec3(0.0f, -0.5f, 0.0f));
        makeRamp(scene, "Ramp20", 20.0f, 12.0f, 0.2f, 0.15f);
        GameObject* player = makeCharacter(scene, glm::vec3(-2.0f, 0.9f, 0.0f));
        scene.start();
        walkFor(scene, player, 3.0f, 2.0f);
        const glm::vec3 p = player->transform()->worldPosition();
        const float surface = rampSurfaceY(20.0f, 12.0f, 0.2f, 0.15f, p.x);
        INFO("20° 坡 3s 后位置: x=" << p.x << " y=" << p.y << " 该处坡面 y=" << surface);
        CHECK(p.y > surface + 0.6f);   // 站在坡面上（中心 ≈ 坡面 + 0.9）
        CHECK(p.x > 2.0f);             // 沿坡持续推进
        // 后段速度不为 0：继续走 0.5s，x 仍显著前进
        const float x0 = p.x;
        walkFor(scene, player, 0.5f, 2.0f);
        const float x1 = player->transform()->worldPosition().x;
        INFO("20° 坡 0.5s 位移: " << (x1 - x0));
        CHECK(x1 - x0 > 0.5f);
    }
    // 30° 坡：可走，速度不衰减为 0
    {
        Scene scene("Slope30");
        makeStaticBox(scene, "Ground", glm::vec3(20.0f, 0.5f, 10.0f), glm::vec3(0.0f, -0.5f, 0.0f));
        makeRamp(scene, "Ramp30", 30.0f, 12.0f, 0.2f, 0.15f);
        GameObject* player = makeCharacter(scene, glm::vec3(-2.0f, 0.9f, 0.0f));
        scene.start();
        walkFor(scene, player, 3.0f, 2.0f);
        const glm::vec3 p = player->transform()->worldPosition();
        const float surface = rampSurfaceY(30.0f, 12.0f, 0.2f, 0.15f, p.x);
        INFO("30° 坡 3s 后位置: x=" << p.x << " y=" << p.y << " 该处坡面 y=" << surface);
        CHECK(p.y > surface + 0.6f);
        CHECK(p.x > 1.5f);
        const float x0 = p.x;
        walkFor(scene, player, 0.5f, 2.0f);
        const float x1 = player->transform()->worldPosition().x;
        INFO("30° 坡 0.5s 位移: " << (x1 - x0));
        CHECK(x1 - x0 > 0.3f);   // 速度不衰减为 0
    }
    // 60° 坡：阻挡（> slopeLimit 45°）——全程最高点不超过坡脚小台阶
    {
        Scene scene("Slope60");
        makeStaticBox(scene, "Ground", glm::vec3(20.0f, 0.5f, 10.0f), glm::vec3(0.0f, -0.5f, 0.0f));
        makeRamp(scene, "Ramp60", 60.0f, 12.0f, 0.2f, 0.15f);
        GameObject* player = makeCharacter(scene, glm::vec3(-2.0f, 0.9f, 0.0f));
        scene.start();
        float maxY = 0.0f;
        const int frames = int(4.0f / kStep);
        for (int i = 0; i < frames; ++i) {
            player->getComponent<CharacterController>()->move(glm::vec3(2.0f * kStep, 0.0f, 0.0f));
            scene.update(kStep);
            maxY = std::max(maxY, player->transform()->worldPosition().y);
        }
        const glm::vec3 p = player->transform()->worldPosition();
        INFO("60° 坡 4s 后位置: x=" << p.x << " y=" << p.y << " 全程最高 y=" << maxY);
        CHECK(maxY < 1.3f);    // 未爬上 60° 坡（爬上去的话中心 y 随坡面显著升高）
        CHECK(p.x < 1.2f);     // 被挡在坡脚（对比 20°/30° 的 x > 1.5）
    }
}

TEST_CASE("V1.0-3 raycastAll: 3 hits ascending + hitObject + ignore filter", "[phys][query]") {
    Scene scene("Raycast");
    auto* box1 = makeStaticBox(scene, "Stack1", glm::vec3(0.5f), glm::vec3(0.0f, 0.5f, 0.0f));
    auto* box2 = makeStaticBox(scene, "Stack2", glm::vec3(0.5f), glm::vec3(0.0f, 1.5f, 0.0f));
    auto* box3 = makeStaticBox(scene, "Stack3", glm::vec3(0.5f), glm::vec3(0.0f, 2.5f, 0.0f));
    scene.start();

    PhysicsWorld* pw = scene.physics();
    REQUIRE(pw != nullptr);

    std::vector<RaycastHit> hits;
    REQUIRE(pw->raycastAll(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 100.0f,
                           PhysicsWorld::RaycastFilter{}, hits));
    REQUIRE(hits.size() == 3);
    // 距离升序
    CHECK(hits[0].distance < hits[1].distance);
    CHECK(hits[1].distance < hits[2].distance);
    // 命中对象 = 自上而下 box3 / box2 / box1；gameObject 与 hitObject 同值
    CHECK(hits[0].hitObject == box3);
    CHECK(hits[0].gameObject == hits[0].hitObject);
    CHECK(hits[1].hitObject == box2);
    CHECK(hits[2].hitObject == box1);
    // 命中点/法线合理：点在该盒顶面附近，法线朝上
    CHECK(hits[0].point.y == Catch::Approx(3.0f).margin(0.05f));   // box3 顶面 y=3
    CHECK(hits[0].normal.y > 0.9f);
    // ignore 过滤：忽略 box3 → 只剩 2 命中
    PhysicsWorld::RaycastFilter ignoreFilter;
    ignoreFilter.ignore = box3;
    REQUIRE(pw->raycastAll(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 100.0f,
                           ignoreFilter, hits));
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].hitObject == box2);
    CHECK(hits[1].hitObject == box1);
    // layerMask 过滤：box1 归层 1，mask 只含层 0 → 命中只剩 box3/box2
    box1->getComponent<BoxCollider>()->layer = 1;
    scene.start();   // 重建 body 使层生效
    PhysicsWorld::RaycastFilter maskFilter;
    maskFilter.layerMask = 1u;   // 只含层 0
    REQUIRE(pw->raycastAll(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 100.0f,
                           maskFilter, hits));
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].hitObject == box3);
    CHECK(hits[1].hitObject == box2);
    // overlapSphere / overlapBox
    std::vector<GameObject*> objects;
    REQUIRE(pw->overlapSphere(glm::vec3(0.0f, 1.5f, 0.0f), 1.2f, PhysicsWorld::RaycastFilter{},
                              objects));
    CHECK(objects.size() == 3);   // 三盒都在球内
    objects.clear();
    REQUIRE(pw->overlapBox(glm::vec3(0.0f, 1.5f, 0.0f), glm::vec3(0.6f, 1.6f, 0.6f),
                           PhysicsWorld::RaycastFilter{}, objects));
    CHECK(objects.size() == 3);
}

TEST_CASE("V1.0-3 layer matrix: disabled passes through / enabled restores", "[phys][layers]") {
    // 层 1 = 坠落物，层 2 = 平台；矩阵关断 → 互穿，打开 → 恢复碰撞
    {
        Scene scene("LayerOff");
        scene.physicsLayerNames[1] = "Enemy";
        scene.physicsLayerNames[2] = "Platform";
        scene.physicsCollisionMatrix[1][2] = false;
        scene.physicsCollisionMatrix[2][1] = false;

        GameObject* platform = makeStaticBox(scene, "Platform", glm::vec3(4.0f, 0.25f, 4.0f),
                                             glm::vec3(0.0f, 1.0f, 0.0f));
        platform->getComponent<BoxCollider>()->layer = 2;
        GameObject* faller = scene.createGameObject("Faller");
        faller->transform()->setLocalPosition(glm::vec3(0.0f, 3.0f, 0.0f));
        auto* rb = faller->addComponent<Rigidbody>();
        rb->mass = 1.0f;
        auto* collider = faller->addComponent<BoxCollider>();
        collider->halfExtents = glm::vec3(0.25f);
        collider->layer = 1;
        scene.start();
        for (int i = 0; i < 180; ++i)   // 3s
            scene.update(kStep);
        const float y = faller->transform()->worldPosition().y;
        INFO("关断后坠落物 y=" << y);
        CHECK(y < 0.5f);   // 穿过平台（平台顶面 1.25，若碰撞应停在其上 y≈1.75）
    }
    // 打开（缺省全通）：坠落物停在平台上
    {
        Scene scene("LayerOn");
        scene.physicsLayerNames[1] = "Enemy";
        scene.physicsLayerNames[2] = "Platform";
        GameObject* platform = makeStaticBox(scene, "Platform", glm::vec3(4.0f, 0.25f, 4.0f),
                                             glm::vec3(0.0f, 1.0f, 0.0f));
        platform->getComponent<BoxCollider>()->layer = 2;
        GameObject* faller = scene.createGameObject("Faller");
        faller->transform()->setLocalPosition(glm::vec3(0.0f, 3.0f, 0.0f));
        faller->addComponent<Rigidbody>()->mass = 1.0f;
        auto* collider = faller->addComponent<BoxCollider>();
        collider->halfExtents = glm::vec3(0.25f);
        collider->layer = 1;
        scene.start();
        for (int i = 0; i < 180; ++i)
            scene.update(kStep);
        const float y = faller->transform()->worldPosition().y;
        INFO("打开后坠落物 y=" << y);
        CHECK(y > 1.4f);   // 停在平台顶面 1.25 + 半高 0.25 = 1.5 附近（容许小穿透）
    }
    // Play 中 setCollisionEnabled 即时生效（refreshBroadphasePairs 路径）：关断 → 停住的物体下坠
    {
        Scene scene("LayerToggle");
        scene.physicsLayerNames[1] = "Enemy";
        scene.physicsLayerNames[2] = "Platform";
        GameObject* platform = makeStaticBox(scene, "Platform", glm::vec3(4.0f, 0.25f, 4.0f),
                                             glm::vec3(0.0f, 1.0f, 0.0f));
        platform->getComponent<BoxCollider>()->layer = 2;
        GameObject* faller = scene.createGameObject("Faller");
        faller->transform()->setLocalPosition(glm::vec3(0.0f, 3.0f, 0.0f));
        faller->addComponent<Rigidbody>()->mass = 1.0f;
        auto* collider = faller->addComponent<BoxCollider>();
        collider->halfExtents = glm::vec3(0.25f);
        collider->layer = 1;
        scene.start();
        for (int i = 0; i < 120; ++i)
            scene.update(kStep);   // 2s：停在平台上
        CHECK(faller->transform()->worldPosition().y > 1.4f);
        scene.physics()->setCollisionEnabled(1, 2, false);   // 关断 → 应下坠
        for (int i = 0; i < 120; ++i)
            scene.update(kStep);
        CHECK(faller->transform()->worldPosition().y < 0.5f);   // 穿透平台继续下落
        // 打开恢复：新坠落物 C 停回平台上
        scene.physics()->setCollisionEnabled(1, 2, true);
        GameObject* c = scene.createGameObject("FallerC");
        c->transform()->setLocalPosition(glm::vec3(1.5f, 3.0f, 0.0f));
        c->addComponent<Rigidbody>()->mass = 1.0f;
        auto* cCollider = c->addComponent<BoxCollider>();
        cCollider->halfExtents = glm::vec3(0.25f);
        cCollider->layer = 1;
        for (int i = 0; i < 180; ++i)
            scene.update(kStep);
        const float cy = c->transform()->worldPosition().y;
        INFO("恢复后新坠落物 y=" << cy);
        CHECK(cy > 1.4f);
        CHECK(scene.physics()->collisionEnabled(1, 2));
        CHECK(scene.physics()->collisionEnabled(2, 1));   // 对称
    }
}

TEST_CASE("V1.0-3 friction and restitution: slide distance and bounce", "[phys][material]") {
    // 同斜坡（20°）同初速：friction 0.1 滑距 > friction 1.0 滑距（box 与坡面同摩擦值，
    // 组合规则取平均 → 接触摩擦 = 该值；起点在坡面上，沿坡下滑）
    auto slideDistance = [](float friction) {
        Scene scene("Friction");
        makeStaticBox(scene, "Ground", glm::vec3(20.0f, 0.5f, 10.0f), glm::vec3(0.0f, -0.5f, 0.0f));
        GameObject* ramp = makeRamp(scene, "Ramp", 20.0f, 14.0f, 0.2f, 0.05f);
        ramp->getComponent<BoxCollider>()->friction = friction;
        GameObject* box = scene.createGameObject("Box");
        const float rad = glm::radians(20.0f);
        const float sx = 7.0f;
        const float sy = rampSurfaceY(20.0f, 14.0f, 0.2f, 0.05f, sx) + 0.35f;
        box->transform()->setLocalPosition(glm::vec3(sx, sy, 0.0f));
        auto* rb = box->addComponent<Rigidbody>();
        rb->mass = 1.0f;
        auto* collider = box->addComponent<BoxCollider>();
        collider->halfExtents = glm::vec3(0.25f);
        collider->friction = friction;
        collider->restitution = 0.0f;
        scene.start();
        // 沿坡下坡方向初速（-X 下坡）
        rb->setVelocity(glm::vec3(-std::cos(rad), -std::sin(rad), 0.0f) * 3.0f);
        for (int i = 0; i < 180; ++i)
            scene.update(kStep);
        return sx - box->transform()->worldPosition().x;   // 下坡滑距
    };
    const float dF01 = slideDistance(0.1f);
    const float dF10 = slideDistance(1.0f);
    INFO("friction 0.1 滑距=" << dF01 << " friction 1.0 滑距=" << dF10);
    CHECK(dF01 > dF10 * 1.6f);   // 低摩擦显著滑更远
    CHECK(dF01 > 2.0f);          // 0.1 至少滑 2m
    CHECK(dF10 < 1.5f);          // 1.0 快速停下

    // restitution：0.8 弹跳 vs 0 落地不弹（地面与球同弹性，组合取平均 = 该值）
    auto bouncePeak = [](float restitution) {
        Scene scene("Restitution");
        GameObject* ground = makeStaticBox(scene, "Ground", glm::vec3(10.0f, 0.5f, 10.0f),
                                           glm::vec3(0.0f, -0.5f, 0.0f));
        ground->getComponent<BoxCollider>()->restitution = restitution;
        GameObject* ball = scene.createGameObject("Ball");
        ball->transform()->setLocalPosition(glm::vec3(0.0f, 5.0f, 0.0f));
        ball->addComponent<Rigidbody>()->mass = 1.0f;
        auto* collider = ball->addComponent<BoxCollider>();
        collider->halfExtents = glm::vec3(0.25f);
        collider->restitution = restitution;
        scene.start();
        float peak = 0.0f;
        bool fell = false;
        for (int i = 0; i < 300; ++i) {
            scene.update(kStep);
            const float y = ball->transform()->worldPosition().y;
            if (y < 1.0f)
                fell = true;   // 已经触地（首落经过 y<1）
            if (fell)
                peak = std::max(peak, y);
        }
        return peak;   // 触地后的最高回升（无弹跳 = 停在 0.25 附近，峰值 ≈ 触发点 1.0 以下）
    };
    const float peak08 = bouncePeak(0.8f);
    const float peak00 = bouncePeak(0.0f);
    INFO("restitution 0.8 回弹峰值=" << peak08 << " restitution 0 峰值=" << peak00);
    CHECK(peak08 > peak00 + 1.0f);   // 0.8 明显弹起（≈3m 级），0 不弹
}

TEST_CASE("V1.0-3 fixed step: 30/60/144 FPS consistent", "[phys][fixedstep]") {
    // 同场景三档帧率各模拟 2.0s（恰 = 120 固定步）：下落盒终态一致
    auto fallFinalY = [](float fps) {
        Scene scene("Fps");
        makeStaticBox(scene, "Ground", glm::vec3(10.0f, 0.5f, 10.0f), glm::vec3(0.0f, -0.5f, 0.0f));
        GameObject* box = scene.createGameObject("Box");
        box->transform()->setLocalPosition(glm::vec3(0.0f, 10.0f, 0.0f));
        box->addComponent<Rigidbody>()->mass = 1.0f;
        auto* collider = box->addComponent<BoxCollider>();
        collider->halfExtents = glm::vec3(0.25f);
        collider->restitution = 0.0f;
        scene.start();
        const float dt = 1.0f / fps;
        for (int i = 0; i < int(2.0f / dt); ++i)
            scene.update(dt);
        return box->transform()->worldPosition().y;
    };
    const float y30 = fallFinalY(30.0f);
    const float y60 = fallFinalY(60.0f);
    const float y144 = fallFinalY(144.0f);
    INFO("终态 y: 30fps=" << y30 << " 60fps=" << y60 << " 144fps=" << y144);
    CHECK(y30 == Catch::Approx(y60).margin(0.01f));
    CHECK(y60 == Catch::Approx(y144).margin(0.01f));
    CHECK(y30 > 0.2f);   // 停在半高 0.25 附近（没有穿地）
}

TEST_CASE("V1.0-3 CharacterController and Collider serialization roundtrip", "[phys][serialize]") {
    Scene a("Serialize");
    a.physicsLayerNames[3] = "Bullet";
    a.physicsCollisionMatrix[3][4] = false;
    a.physicsCollisionMatrix[4][3] = false;
    GameObject* player = a.createGameObject("Player");
    auto* cc = player->addComponent<CharacterController>();
    cc->radius = 0.5f;
    cc->height = 2.0f;
    cc->stepHeight = 0.5f;
    cc->slopeLimitDeg = 50.0f;
    cc->mass = 90.0f;
    cc->gravityScale = 0.5f;
    GameObject* box = a.createGameObject("Box");
    auto* bc = box->addComponent<BoxCollider>();
    bc->layer = 7;
    bc->friction = 0.9f;
    bc->restitution = 0.6f;

    const std::string json = SceneSerializer::serialize(a);
    const nlohmann::json doc = nlohmann::json::parse(json);
    CHECK(doc["version"] == 4);
    // 组件段键名抽查（§27.2 序列化键名锁定）
    bool foundCc = false, foundStep = false, foundLayer = false, foundFriction = false;
    for (const auto& obj : doc["objects"])
        for (const auto& comp : obj["components"]) {
            if (comp.value("type", "") == "CharacterController") {
                foundCc = true;
                foundStep = foundStep || (comp.value("stepHeight", 0.0f) == 0.5);
            }
            if (comp.value("type", "") == "BoxCollider") {
                foundLayer = foundLayer || (comp.value("layer", -1) == 7);
                foundFriction = foundFriction || (comp.value("friction", -1.0) == Catch::Approx(0.9));
            }
        }
    CHECK(foundCc);
    CHECK(foundStep);
    CHECK(foundLayer);
    CHECK(foundFriction);

    Scene b("SerializeB");
    SceneSerializer::deserialize(b, json);
    auto* ccB = findFirst(b, "Player")->getComponent<CharacterController>();
    REQUIRE(ccB != nullptr);
    CHECK(ccB->radius == Catch::Approx(0.5f));
    CHECK(ccB->height == Catch::Approx(2.0f));
    CHECK(ccB->stepHeight == Catch::Approx(0.5f));
    CHECK(ccB->slopeLimitDeg == Catch::Approx(50.0f));
    CHECK(ccB->mass == Catch::Approx(90.0f));
    CHECK(ccB->gravityScale == Catch::Approx(0.5f));
    auto* bcB = findFirst(b, "Box")->getComponent<BoxCollider>();
    REQUIRE(bcB != nullptr);
    CHECK(bcB->layer == 7);
    CHECK(bcB->friction == Catch::Approx(0.9f));
    CHECK(bcB->restitution == Catch::Approx(0.6f));
    CHECK(b.physicsLayerNames[3] == "Bullet");
    CHECK_FALSE(b.physicsCollisionMatrix[3][4]);
    CHECK_FALSE(b.physicsCollisionMatrix[4][3]);

    // 旧文件缺省兜底：无新字段的组件 JSON → 默认值（version 不变，零警告语义）
    nlohmann::json old = nlohmann::json::parse(
        R"({"version":4,"objects":[{"id":1,"name":"Old","components":[
        {"type":"CharacterController"},
        {"type":"BoxCollider","halfExtents":[1,1,1]}]}]})");
    Scene c("OldScene");
    SceneSerializer::deserialize(c, old.dump());
    auto* ccOld = findFirst(c, "Old")->getComponent<CharacterController>();
    REQUIRE(ccOld != nullptr);
    CHECK(ccOld->stepHeight == Catch::Approx(0.35f));   // 缺省
    CHECK(ccOld->slopeLimitDeg == Catch::Approx(45.0f));
    auto* bcOld = findFirst(c, "Old")->getComponent<BoxCollider>();
    REQUIRE(bcOld != nullptr);
    CHECK(bcOld->layer == 0);
    CHECK(bcOld->friction == Catch::Approx(0.5f));
    CHECK(bcOld->restitution == Catch::Approx(0.0f));
}

TEST_CASE("V1.0-3 CC vs Rigidbody exclusivity: CC wins", "[phys][character][exclusive]") {
    Scene scene("Exclusive");
    GameObject* go = scene.createGameObject("Both");
    go->transform()->setLocalPosition(glm::vec3(0.0f, 0.9f, 0.0f));
    go->addComponent<Rigidbody>();   // 挂两者：warn + CC 优先
    auto* cc = go->addComponent<CharacterController>();
    auto* ownCollider = go->addComponent<BoxCollider>();   // 有 collider 时若无 CC 守卫会建 body
    ownCollider->halfExtents = glm::vec3(0.3f);
    makeStaticBox(scene, "Ground", glm::vec3(10.0f, 0.5f, 10.0f), glm::vec3(0.0f, -0.5f, 0.0f));
    scene.start();
    // Rigidbody 无 body（被 CC 跳过）：velocity 查询 = 安全 0，不坠落
    for (int i = 0; i < 60; ++i)
        scene.update(kStep);
    const glm::vec3 p = go->transform()->worldPosition();
    INFO("互斥 GO 位置: x=" << p.x << " y=" << p.y);
    CHECK(p.y > 0.85f);   // 未因 Rigidbody 重力坠落（CC 优先，Rigidbody 未模拟）
    CHECK(go->getComponent<Rigidbody>()->velocity() == glm::vec3(0.0f));
    // CC 自身可用：move 生效 + isGrounded 地面检测
    cc->move(glm::vec3(1.0f, 0.0f, 0.0f));
    scene.update(kStep);
    CHECK(go->transform()->worldPosition().x > 0.5f);
    CHECK(cc->isGrounded());
}
