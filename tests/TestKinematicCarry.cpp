// TestKinematicCarry.cpp —— V1.3 §28.3.15.4 引擎增强：CC 站在 kinematic 平台上被携带
//
// 实现 = 路线 B 位置级叠加（PhysicsWorld.cpp stepCharacters 内 ExtendedUpdate 之后）：
// kinematic 支撑 → 位置叠加 body 本步位移差分；离地步 → 水平速度惯性注入（carryVelocity）。
// 本文件只经公开 CC API（setVelocity/isGrounded/velocity/setPosition）与
// Rigidbody/Collider 组件驱动场景，headless 无 GL。
//
// 覆盖（对应 §28.3.15.4 可测验收 1-6）：
//  1. 静立携带：X 轴 ±1m/6s 平台 60s → 相对 X 恒定（出生首步滞后 ≤1.75cm 非漂移）、isGrounded 保持
//  2. 垂直贴面：±0.65m/6s 升降 60s → 相对顶面高度恒定（±0.02m）、不悬浮不穿透
//  3. 起跳惯性：平台 1m/s 平移、无输入起跳 → 离地期水平位移 ≈ 1.0×滞空时间（±0.05m）
//  4. 输入组合：平台运动 + 走/冲刺/跳输入速度叠加、互不干扰、无穿透
//  5. 回归：static 平台/动态体支撑零行为变化；setPosition 传送硬化（断携带/清惯性）
//  6. 形态：携带块每角色每步 O(1)（8 平台×8 角色独立携带功能验证）；
//     30FPS 双固定步帧级相对位置一致（差分仅单步出现）

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <NovaEngine/Physics/PhysicsWorld.h>
#include <NovaEngine/Scene/CharacterController.h>
#include <NovaEngine/Scene/Collider.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Rigidbody.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/Transform.h>

#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "TestCommon.h"

using namespace NovaEngine;
using namespace NovaTest;

namespace {

constexpr float kStep = 1.0f / 60.0f;   // 固定步
constexpr float kPi = 3.14159265358979f;

// 静态地板（box，顶面在 center.y + half.y）
GameObject* makeStaticBox(Scene& s, const std::string& name, const glm::vec3& half,
                          const glm::vec3& center) {
    GameObject* go = s.createGameObject(name);
    go->transform()->setLocalPosition(center);
    auto* box = go->addComponent<BoxCollider>();
    box->halfExtents = half;
    return go;
}

// kinematic 平台（Rigidbody isKinematic + BoxCollider；Transform 由测试每帧驱动，
// writeKinematics 每固定步读入 body——与 NeonMovingPlatform 脚本驱动同构）
GameObject* makeKinematicPlatform(Scene& s, const std::string& name, const glm::vec3& half,
                                  const glm::vec3& center) {
    GameObject* go = s.createGameObject(name);
    go->transform()->setLocalPosition(center);
    auto* rb = go->addComponent<Rigidbody>();
    rb->isKinematic = true;
    auto* box = go->addComponent<BoxCollider>();
    box->halfExtents = half;
    return go;
}

// 角色（胶囊中心 = Transform 位置；站立地面时中心在地面上方 height/2 = 0.9）
GameObject* makeCharacter(Scene& s, const glm::vec3& pos) {
    GameObject* go = s.createGameObject("Player");
    go->transform()->setLocalPosition(pos);
    go->addComponent<CharacterController>();
    return go;
}

// CharacterWalk 复刻驱动（§28.3.15.4 ENG-5 依据）：每步 setVelocity 覆盖（读回 vel.y 交还
// 物理积分，水平由输入决定）；起跳锁存——物理消费（读回 vy > jumpSpeed×0.5）后解锁。
struct WalkDriver {
    bool latched = false;
    float jumpSpeed = 5.0f;
    void jump() { latched = true; }
    void drive(CharacterController* cc, float inputX) {
        glm::vec3 vel = cc->velocity();
        if (latched) {
            vel.y = jumpSpeed;
            if (cc->velocity().y > jumpSpeed * 0.5f)
                latched = false;   // 物理已消费起跳
        }
        vel.x = inputX;
        vel.z = 0.0f;
        cc->setVelocity(vel);
    }
};

}  // namespace

TEST_CASE("V1.3 carry: idle CC follows X platform 60s no drift", "[phys][character][carry]") {
    // 验收 1：CC 静立（零输入）于 X 轴 ±1m/6s kinematic 平台 → 相对 X 恒定（±0.02m），
    // 60s 无漂移、无滑落、isGrounded 保持。出生首步不叠加 → 恒定滞后（非漂移）。
    Scene scene("CarryX");
    GameObject* plat = makeKinematicPlatform(scene, "Plat", glm::vec3(3.0f, 0.25f, 3.0f),
                                             glm::vec3(0.0f, 1.0f, 0.0f));
    GameObject* player = makeCharacter(scene, glm::vec3(0.0f, 2.15f, 0.0f));   // 顶面 1.25 + 0.9
    scene.start();
    auto* cc = player->getComponent<CharacterController>();
    REQUIRE(cc != nullptr);

    float rel0 = 0.0f;
    bool haveRel0 = false;
    float maxDrift = 0.0f;
    bool groundedAll = true;
    const int frames = int(60.0f / kStep);
    for (int i = 1; i <= frames; ++i) {
        const float t = i * kStep;
        plat->transform()->setLocalPosition(
            glm::vec3(1.0f * std::sin(2.0f * kPi * t / 6.0f), 1.0f, 0.0f));
        scene.update(kStep);
        const float rel = player->transform()->worldPosition().x -
                          plat->transform()->worldPosition().x;
        if (!haveRel0) {
            rel0 = rel;
            haveRel0 = true;
        }
        maxDrift = std::max(maxDrift, std::abs(rel - rel0));
        groundedAll = groundedAll && cc->isGrounded();
    }
    INFO("出生首步后相对 X=" << rel0 << " 60s 最大漂移=" << maxDrift);
    CHECK(std::abs(rel0) < 0.02f);      // 出生首步滞后 ≤1.75cm（峰值），容差 2cm
    CHECK(maxDrift < 0.005f);           // 60s 无漂移（恒定滞后，非累计漂移）
    CHECK(groundedAll);                 // 无滑落
}

TEST_CASE("V1.3 carry: vertical platform lifts and lowers, CC stays flush", "[phys][character][carry]") {
    // 验收 2：垂直平台 ±0.65m/6s 升降 → CC 相对顶面高度恒定（±0.02m），全程贴面不悬浮。
    // 差分含垂直分量：升台随动、降台贴面（StickToFloor step-down 不介入）。
    Scene scene("CarryY");
    GameObject* plat = makeKinematicPlatform(scene, "Plat", glm::vec3(3.0f, 0.25f, 3.0f),
                                             glm::vec3(0.0f, 1.0f, 0.0f));
    GameObject* player = makeCharacter(scene, glm::vec3(0.0f, 2.15f, 0.0f));
    scene.start();
    auto* cc = player->getComponent<CharacterController>();
    REQUIRE(cc != nullptr);

    float maxRide = 0.0f, minRide = 0.0f;
    bool groundedAll = true;
    const int frames = int(60.0f / kStep);
    for (int i = 1; i <= frames; ++i) {
        const float t = i * kStep;
        const float platY = 1.0f + 0.65f * std::sin(2.0f * kPi * t / 6.0f);
        plat->transform()->setLocalPosition(glm::vec3(0.0f, platY, 0.0f));
        scene.update(kStep);
        const float topY = plat->transform()->worldPosition().y + 0.25f;
        const float ride = player->transform()->worldPosition().y - topY - 0.9f;
        maxRide = std::max(maxRide, ride);
        minRide = std::min(minRide, ride);
        groundedAll = groundedAll && cc->isGrounded();
    }
    INFO("60s 相对顶面高度偏差: 最大=" << maxRide << " 最小=" << minRide);
    CHECK(maxRide < 0.02f);    // 不悬浮（上限 2cm）
    CHECK(minRide > -0.02f);   // 不穿透（下限 -2cm）
    CHECK(groundedAll);        // 全程贴面
}

TEST_CASE("V1.3 carry: jump inertia from 1m/s platform", "[phys][character][carry]") {
    // 验收 3：平台以 1m/s 平移、玩家无输入起跳 → 离地期水平位移 ≈ 平台速度×滞空时间
    // （±0.05m）。惯性自离地步次一步生效（单步延迟 ≤1.75cm）；落地后不再额外继承。
    Scene scene("CarryJump");
    GameObject* plat = makeKinematicPlatform(scene, "Plat", glm::vec3(6.0f, 0.25f, 3.0f),
                                             glm::vec3(0.0f, 1.0f, 0.0f));
    GameObject* player = makeCharacter(scene, glm::vec3(0.0f, 2.15f, 0.0f));
    scene.start();
    auto* cc = player->getComponent<CharacterController>();
    REQUIRE(cc != nullptr);

    WalkDriver driver;
    int leaveStep = -1, landStep = -1;
    float xLeave = 0.0f, xLand = 0.0f;
    const int frames = int(4.0f / kStep);
    for (int i = 1; i <= frames; ++i) {
        plat->transform()->setLocalPosition(glm::vec3(i * kStep * 1.0f, 1.0f, 0.0f));
        if (i == 30)
            driver.jump();          // 静立 0.5s 后起跳（出生滞后已建立）
        driver.drive(cc, 0.0f);     // 无输入：vel.x 恒 0（CharacterWalk 每帧覆盖语义）
        scene.update(kStep);
        const bool groundedNow = cc->isGrounded();
        if (i >= 30 && leaveStep < 0 && !groundedNow) {
            leaveStep = i;
            xLeave = player->transform()->worldPosition().x;
        }
        if (leaveStep >= 0 && landStep < 0 && groundedNow) {
            landStep = i;
            xLand = player->transform()->worldPosition().x;
        }
    }
    REQUIRE(leaveStep > 0);
    REQUIRE(landStep > leaveStep);
    const float airtime = (landStep - leaveStep) * kStep;
    const float dxAir = xLand - xLeave;
    INFO("离地步=" << leaveStep << " 落地步=" << landStep << " 滞空=" << airtime
                   << "s 离地期水平位移=" << dxAir << "m");
    CHECK(airtime > 0.8f);                              // 5m/s 起跳 → 滞空 ≈1.02s
    CHECK(airtime < 1.25f);
    CHECK(std::abs(dxAir - 1.0f * airtime) < 0.05f);    // 水平位移 ≈ 平台速度×滞空时间
    // 落地后不再额外继承：复乘回归（相对 X 恒定 = 落地步滞后，非漂移）
    const float relLand = xLand - landStep * kStep * 1.0f;
    const glm::vec3 pEnd = player->transform()->worldPosition();
    const float relEnd = pEnd.x - frames * kStep * 1.0f;
    INFO("落地步相对 X=" << relLand << " 终步相对 X=" << relEnd);
    CHECK(cc->isGrounded());
    CHECK(std::abs(relEnd - relLand) < 0.01f);
}

TEST_CASE("V1.3 carry 3.1: pure CC readback path, one jump setVelocity, no accumulation",
          "[phys][character][carry]") {
    // 验收 3.1（P1-1 终审追加）：起跳仅注入一次 setVelocity，后续零脚本输入（走读回分支）→
    // 离地期水平位移 ≈ 平台速度×滞空时间，无逐步累积（carryVelocity 不重复叠加：
    // 读回路径自带离地步注入，速度阶段不再叠加）。
    Scene scene("CarryReadback");
    GameObject* plat = makeKinematicPlatform(scene, "Plat", glm::vec3(6.0f, 0.25f, 3.0f),
                                             glm::vec3(0.0f, 1.0f, 0.0f));
    GameObject* player = makeCharacter(scene, glm::vec3(0.0f, 2.15f, 0.0f));
    scene.start();
    auto* cc = player->getComponent<CharacterController>();
    REQUIRE(cc != nullptr);

    int leaveStep = -1, landStep = -1;
    float xLeave = 0.0f, xLand = 0.0f;
    float maxAirVx = 0.0f, maxPostLandVx = 0.0f;
    const int frames = int(4.0f / kStep);
    for (int i = 1; i <= frames; ++i) {
        plat->transform()->setLocalPosition(glm::vec3(i * kStep * 1.0f, 1.0f, 0.0f));
        if (i == 30)
            cc->setVelocity(glm::vec3(0.0f, 5.0f, 0.0f));   // 唯一一次注入
        scene.update(kStep);
        const bool groundedNow = cc->isGrounded();
        if (i >= 30 && leaveStep < 0 && !groundedNow) {
            leaveStep = i;
            xLeave = player->transform()->worldPosition().x;
        }
        if (leaveStep >= 0 && landStep < 0 && groundedNow) {
            landStep = i;
            xLand = player->transform()->worldPosition().x;
        }
        if (leaveStep >= 0 && landStep < 0)
            maxAirVx = std::max(maxAirVx, cc->velocity().x);   // 空中：无累积（2× 会到 2.0）
        if (landStep >= 0)
            maxPostLandVx = std::max(maxPostLandVx, cc->velocity().x);   // 落地后：不额外继承
    }
    REQUIRE(leaveStep > 0);
    REQUIRE(landStep > leaveStep);
    const float airtime = (landStep - leaveStep) * kStep;
    const float dxAir = xLand - xLeave;
    INFO("读回路径: 离地步=" << leaveStep << " 落地步=" << landStep << " 滞空=" << airtime
                             << "s 离地期水平位移=" << dxAir << "m 空中最大 vx=" << maxAirVx);
    CHECK(std::abs(dxAir - 1.0f * airtime) < 0.05f);   // 位移 ≈ 平台速度×滞空时间
    CHECK(maxAirVx < 1.2f);                            // 无累积（不逼近 2×）
    CHECK(maxPostLandVx < 1.2f);                       // 落地后不再额外继承
}

TEST_CASE("V1.3 carry 3.2: 30FPS double fixed step, readback branch no double-add",
          "[phys][character][carry]") {
    // 验收 3.2（P1-1 终审追加）：30FPS 双固定步——每渲染帧一次 setVelocity、两步 1/60
    // 固定步（累加器路径与 step(1/30) 同构）；第二步走读回分支 → 验证不二次叠加惯性：
    // 离地期水平漂移 ≈ 1× 平台速度。平台按固定步驱动（隔离 30FPS 帧级差分估算的固有
    // 2× 偏差，该偏差已在交付报告备案）。
    Scene scene("Carry30Jump");
    GameObject* plat = makeKinematicPlatform(scene, "Plat", glm::vec3(6.0f, 0.25f, 3.0f),
                                             glm::vec3(0.0f, 1.0f, 0.0f));
    GameObject* player = makeCharacter(scene, glm::vec3(0.0f, 2.15f, 0.0f));
    scene.start();
    auto* cc = player->getComponent<CharacterController>();
    REQUIRE(cc != nullptr);

    WalkDriver driver;   // CharacterWalk 复刻：每帧一次 setVelocity（含起跳锁存）
    int leaveStep = -1, landStep = -1;
    float xLeave = 0.0f, xLand = 0.0f;
    const int frames = int(4.0f / kStep);
    for (int i = 1; i <= frames; ++i) {
        plat->transform()->setLocalPosition(glm::vec3(i * kStep * 1.0f, 1.0f, 0.0f));
        if (i % 2 == 1) {                  // 每渲染帧（= 两个固定步）一次 setVelocity
            if (i == 31)
                driver.jump();
            driver.drive(cc, 0.0f);
        }
        scene.update(kStep);
        const bool groundedNow = cc->isGrounded();
        if (i >= 31 && leaveStep < 0 && !groundedNow) {
            leaveStep = i;
            xLeave = player->transform()->worldPosition().x;
        }
        if (leaveStep >= 0 && landStep < 0 && groundedNow) {
            landStep = i;
            xLand = player->transform()->worldPosition().x;
        }
    }
    REQUIRE(leaveStep > 0);
    REQUIRE(landStep > leaveStep);
    const float airtime = (landStep - leaveStep) * kStep;
    const float dxAir = xLand - xLeave;
    INFO("30FPS 双固定步: 离地步=" << leaveStep << " 落地步=" << landStep << " 滞空=" << airtime
                                   << "s 离地期水平漂移=" << dxAir << "m");
    CHECK(std::abs(dxAir - 1.0f * airtime) < 0.05f);   // 漂移 ≈ 1× 平台速度（不二次叠加）
}

TEST_CASE("V1.3 carry: input combined with platform motion", "[phys][character][carry]") {
    // 验收 4：平台运动 + 玩家输入（走 2m/s / 冲刺 5m/s / 带输入起跳）不互相干扰、
    // 无穿透、无抖动。叠加为纯位置量 + 空中纯速度量，与输入速度语义正交。
    Scene scene("CarryCombo");
    GameObject* plat = makeKinematicPlatform(scene, "Plat", glm::vec3(20.0f, 0.25f, 3.0f),
                                             glm::vec3(0.0f, 1.0f, 0.0f));
    GameObject* player = makeCharacter(scene, glm::vec3(0.0f, 2.15f, 0.0f));
    scene.start();
    auto* cc = player->getComponent<CharacterController>();
    REQUIRE(cc != nullptr);

    WalkDriver driver;
    float rideMax = 0.0f, rideMin = 0.0f;
    float relAt120 = 0.0f, relAt150 = 0.0f;
    int leaveStep = -1, landStep = -1;
    float relAtLeave = 0.0f, relAtLand = 0.0f;
    float airVxMax = 0.0f, airVxMin = 10.0f;
    bool airSampled = false;
    const int frames = int(4.5f / kStep);
    for (int i = 1; i <= frames; ++i) {
        plat->transform()->setLocalPosition(glm::vec3(i * kStep * 1.0f, 1.0f, 0.0f));
        const float inputX = (i <= 120) ? 2.0f : (i <= 150) ? 5.0f : 1.0f;
        if (i == 150)
            driver.jump();
        driver.drive(cc, inputX);
        scene.update(kStep);
        const float rel = player->transform()->worldPosition().x -
                          plat->transform()->worldPosition().x;
        const float ride = player->transform()->worldPosition().y - 1.25f - 0.9f;
        const bool groundedNow = cc->isGrounded();
        if (groundedNow) {
            rideMax = std::max(rideMax, ride);
            rideMin = std::min(rideMin, ride);
        }
        if (i == 120)
            relAt120 = rel;
        if (i == 150)
            relAt150 = rel;
        if (i >= 150 && leaveStep < 0 && !groundedNow) {
            leaveStep = i;
            relAtLeave = rel;
        }
        if (leaveStep >= 0 && landStep < 0 && groundedNow) {
            landStep = i;
            relAtLand = rel;
        }
        if (leaveStep >= 0 && landStep < 0 && i > 150) {
            airVxMax = std::max(airVxMax, cc->velocity().x);
            airVxMin = std::min(airVxMin, cc->velocity().x);
            airSampled = true;
        }
    }
    INFO("走段相对位移=" << relAt120 << " 冲刺段=" << (relAt150 - relAt120)
         << " 着地 ride∈[" << rideMin << "," << rideMax << "]");
    // 走 2m/s×2s → 相对位移 4m；冲刺 5m/s×0.5s → +2.5m
    CHECK(std::abs(relAt120 - 4.0f) < 0.1f);
    CHECK(std::abs((relAt150 - relAt120) - 2.5f) < 0.15f);
    // 着地全程贴面（无穿透、无悬浮）
    CHECK(rideMax < 0.02f);
    CHECK(rideMin > -0.02f);
    // 空中水平速度 = 输入 1m/s + 平台 1m/s = 2m/s（无干扰）
    REQUIRE(leaveStep > 0);
    REQUIRE(landStep > leaveStep);
    REQUIRE(airSampled);
    INFO("空中 vx∈[" << airVxMin << "," << airVxMax << "]");
    CHECK(std::abs(airVxMax - 2.0f) < 0.1f);
    CHECK(std::abs(airVxMin - 2.0f) < 0.1f);
    // 离地期相对位移 ≈ (2-1)m/s × 滞空时间（起跳步只含输入分量，容差 15cm）
    const float airtime = (landStep - leaveStep) * kStep;
    const float relGain = relAtLand - relAtLeave;
    INFO("离地期相对位移=" << relGain << " 滞空=" << airtime);
    CHECK(std::abs(relGain - 1.0f * airtime) < 0.15f);
    // 落地后复乘：继续走 1m/s 至终帧，相对位移按输入累积
    const float relEnd = player->transform()->worldPosition().x - frames * kStep * 1.0f;
    const float expect = relAtLand + (frames - landStep) * kStep * 1.0f;
    INFO("终帧相对 X=" << relEnd << " 期望=" << expect);
    CHECK(std::abs(relEnd - expect) < 0.1f);
    CHECK(cc->isGrounded());
}

TEST_CASE("V1.3 carry: static/dynamic ground unchanged + teleport hardening", "[phys][character][carry]") {
    // 验收 5（新增路径回归）：static 平台与 dynamic 体支撑一律不携带（ENG-4 隔离）；
    // setPosition 传送硬化：断开携带记账（无首步吸附）、清除空中惯性。
    // (a) static 地面走路：位置只由输入决定（新路径不介入）
    {
        Scene scene("NoCarryStatic");
        makeStaticBox(scene, "Ground", glm::vec3(10.0f, 0.5f, 10.0f), glm::vec3(0.0f, -0.5f, 0.0f));
        GameObject* player = makeCharacter(scene, glm::vec3(0.0f, 0.9f, 0.0f));
        scene.start();
        auto* cc = player->getComponent<CharacterController>();
        WalkDriver driver;
        const int frames = int(1.0f / kStep);
        for (int i = 1; i <= frames; ++i) {
            driver.drive(cc, 1.0f);
            scene.update(kStep);
        }
        const float x = player->transform()->worldPosition().x;
        INFO("static 地面走 1s: x=" << x);
        CHECK(std::abs(x - 1.0f) < 0.05f);
        CHECK(cc->isGrounded());
    }
    // (b) dynamic 体支撑：CC 站立其上 → 不携带（rb 非 kinematic）、盒子不被推走
    {
        Scene scene("NoCarryDynamic");
        makeStaticBox(scene, "Ground", glm::vec3(10.0f, 0.5f, 10.0f), glm::vec3(0.0f, -0.5f, 0.0f));
        GameObject* box = scene.createGameObject("DynBox");
        box->transform()->setLocalPosition(glm::vec3(0.0f, 0.25f, 0.0f));
        auto* rb = box->addComponent<Rigidbody>();
        rb->mass = 10.0f;
        auto* collider = box->addComponent<BoxCollider>();
        collider->halfExtents = glm::vec3(1.0f, 0.25f, 1.0f);
        GameObject* player = makeCharacter(scene, glm::vec3(0.0f, 1.4f, 0.0f));   // 盒顶 0.5 + 0.9
        scene.start();
        auto* cc = player->getComponent<CharacterController>();
        for (int i = 0; i < 120; ++i)
            scene.update(kStep);
        const glm::vec3 p = player->transform()->worldPosition();
        const glm::vec3 b = box->transform()->worldPosition();
        INFO("dynamic 支撑: 角色 y=" << p.y << " 盒子 (" << b.x << "," << b.y << ")");
        CHECK(cc->isGrounded());
        CHECK(std::abs(p.y - 1.4f) < 0.05f);   // 仍站盒顶
        CHECK(std::abs(b.x) < 0.05f);          // 盒子未被推走
        CHECK(std::abs(b.y - 0.25f) < 0.05f);
    }
    // (c) 传送硬化：kinematic 平台上 setPosition → 无首步吸附、空中无惯性残留
    {
        Scene scene("Teleport");
        GameObject* plat = makeKinematicPlatform(scene, "Plat", glm::vec3(6.0f, 0.25f, 3.0f),
                                                 glm::vec3(0.0f, 1.0f, 0.0f));
        GameObject* player = makeCharacter(scene, glm::vec3(0.0f, 2.15f, 0.0f));
        scene.start();
        auto* cc = player->getComponent<CharacterController>();
        // 先复乘 30 步建立携带
        for (int i = 1; i <= 30; ++i) {
            plat->transform()->setLocalPosition(glm::vec3(i * kStep * 1.0f, 1.0f, 0.0f));
            scene.update(kStep);
        }
        // 台上传送：下一固定步不叠加平台位移（lastGroundBody 已复位 → 首步仅记录）
        const float platX = plat->transform()->worldPosition().x;
        cc->setPosition(glm::vec3(platX + 1.0f, 2.15f, 0.0f));
        plat->transform()->setLocalPosition(glm::vec3(31 * kStep * 1.0f, 1.0f, 0.0f));   // 平台继续前进
        scene.update(kStep);
        const float xAfter1 = player->transform()->worldPosition().x;
        INFO("台上传送后 1 步: x=" << xAfter1 << " 期望=" << (platX + 1.0f));
        CHECK(std::abs(xAfter1 - (platX + 1.0f)) < 0.01f);   // 无吸附
        // 此后重新复乘：相对 X 恒定
        const float rel0 = xAfter1 - (31 * kStep * 1.0f);
        float maxDrift = 0.0f;
        for (int i = 32; i <= 90; ++i) {
            plat->transform()->setLocalPosition(glm::vec3(i * kStep * 1.0f, 1.0f, 0.0f));
            scene.update(kStep);
            const float rel = player->transform()->worldPosition().x - i * kStep * 1.0f;
            maxDrift = std::max(maxDrift, std::abs(rel - rel0));
        }
        INFO("复乘漂移=" << maxDrift);
        CHECK(maxDrift < 0.01f);
        CHECK(cc->isGrounded());
        // 空中传送：无水平惯性残留（垂直下落，X 不随旧平台漂移；落地后复乘属正常携带，
        // 不纳入偏差统计）
        const float telX = plat->transform()->worldPosition().x + 1.5f;
        cc->setPosition(glm::vec3(telX, 4.0f, 0.0f));
        float maxDev = 0.0f;
        bool landed = false;
        for (int i = 91; i <= 210; ++i) {
            plat->transform()->setLocalPosition(glm::vec3(i * kStep * 1.0f, 1.0f, 0.0f));
            scene.update(kStep);
            if (!cc->isGrounded())
                maxDev = std::max(maxDev,
                                  std::abs(player->transform()->worldPosition().x - telX));
            if (cc->isGrounded())
                landed = true;
        }
        INFO("空中传送下落 X 最大偏差=" << maxDev);
        CHECK(maxDev < 0.05f);   // 无惯性残留（旧平台速度不注入）
        CHECK(landed);           // 落回平台（复乘继续）
        CHECK(cc->isGrounded());
    }
}

TEST_CASE("V1.3 carry: 8 platforms x 8 characters independent, 30FPS consistent", "[phys][character][carry]") {
    // 验收 6 功能面 + 30FPS 边界：携带块每角色每步 O(1)（无场景遍历/分配），
    // 8 平台×8 角色独立携带互不串扰；30FPS 双固定步下帧级相对位置一致（差分仅单步）。
    // 30FPS：平台 Transform 每帧一变 → 双固定步中仅一步有差分，帧末相对位置精确。
    {
        Scene scene("Multi");
        std::vector<GameObject*> plats;
        std::vector<GameObject*> players;
        for (int i = 0; i < 8; ++i) {
            plats.push_back(makeKinematicPlatform(scene, "Plat" + std::to_string(i),
                                                  glm::vec3(2.0f, 0.25f, 2.0f),
                                                  glm::vec3(8.0f * i, 1.0f, 0.0f)));
            players.push_back(makeCharacter(scene, glm::vec3(8.0f * i, 2.15f, 0.0f)));
        }
        scene.start();
        std::vector<float> rel0(8, 0.0f);
        std::vector<bool> haveRel0(8, false);
        std::vector<float> maxDrift(8, 0.0f);
        bool groundedAll = true;
        const int frames = int(2.0f / kStep);
        for (int i = 1; i <= frames; ++i) {
            const float t = i * kStep;
            for (int k = 0; k < 8; ++k) {
                plats[k]->transform()->setLocalPosition(
                    glm::vec3(8.0f * k + 1.0f * std::sin(2.0f * kPi * t / 6.0f + 0.7f * k),
                              1.0f, 0.0f));
            }
            scene.update(kStep);
            for (int k = 0; k < 8; ++k) {
                const float rel = players[k]->transform()->worldPosition().x -
                                  plats[k]->transform()->worldPosition().x;
                if (!haveRel0[k]) {
                    rel0[k] = rel;
                    haveRel0[k] = true;
                }
                maxDrift[k] = std::max(maxDrift[k], std::abs(rel - rel0[k]));
                groundedAll = groundedAll &&
                              players[k]->getComponent<CharacterController>()->isGrounded();
            }
        }
        float worst = 0.0f;
        for (int k = 0; k < 8; ++k)
            worst = std::max(worst, maxDrift[k]);
        INFO("8 角色最大漂移=" << worst);
        CHECK(worst < 0.005f);
        CHECK(groundedAll);
    }
    // 30FPS：每帧 1/30s（双固定步）→ 帧级相对 X 恒定，无漂移
    {
        Scene scene("Carry30");
        GameObject* plat = makeKinematicPlatform(scene, "Plat", glm::vec3(3.0f, 0.25f, 3.0f),
                                                 glm::vec3(0.0f, 1.0f, 0.0f));
        GameObject* player = makeCharacter(scene, glm::vec3(0.0f, 2.15f, 0.0f));
        scene.start();
        auto* cc = player->getComponent<CharacterController>();
        float rel0 = 0.0f;
        bool haveRel0 = false;
        float maxDrift = 0.0f;
        bool groundedAll = true;
        const int frames = int(6.0f * 30.0f);
        for (int i = 1; i <= frames; ++i) {
            const float t = i / 30.0f;
            plat->transform()->setLocalPosition(
                glm::vec3(1.0f * std::sin(2.0f * kPi * t / 6.0f), 1.0f, 0.0f));
            scene.update(1.0f / 30.0f);
            const float rel = player->transform()->worldPosition().x -
                              plat->transform()->worldPosition().x;
            if (!haveRel0) {
                rel0 = rel;
                haveRel0 = true;
            }
            maxDrift = std::max(maxDrift, std::abs(rel - rel0));
            groundedAll = groundedAll && cc->isGrounded();
        }
        INFO("30FPS 首帧后相对 X=" << rel0 << " 6s 最大漂移=" << maxDrift);
        CHECK(maxDrift < 0.005f);   // 帧级精确跟随（双固定步差分仅单步，无累计）
        CHECK(groundedAll);
    }
}
