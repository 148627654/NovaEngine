// TestSaveGame.cpp —— 存档族（§26.1 覆盖清单 5：SaveGame 原子写与浅校验）
//
// 对 KNOWN-ISSUES：
//  - V0.5-12 契约：tmp→rename 原子写；读档原地重建同一 Scene（对象 ID 保持）
//  - DevOps 修复回归：snapshot 浅校验补 objects 数组键（{"snapshot":{}} 不得把场景清空）
//  - Reviewer 修复：load 对 deserialize 补 try/catch（损坏存档不崩溃）
//  - 未来版本保护：version > 当前拒绝

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <NovaEngine/SaveGame/SaveGame.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Serialization/SceneSerializer.h>

#include "TestCommon.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace NovaEngine;
using namespace NovaTest;

namespace {

// 搭一个带父子层级 + 可辨识字段的存档场景
void buildSavableScene(Scene& scene) {
    GameObject* root = scene.createGameObject("SaveRoot");
    GameObject* child = scene.createGameObject("SaveChild", root);
    child->transform()->setLocalPosition(glm::vec3(4.0f, 5.0f, 6.0f));
}

// 目录下除 .tmp 残留外的正式存档文件集合
std::vector<std::string> listDirFiles(const std::filesystem::path& dir) {
    std::vector<std::string> out;
    for (const auto& e : std::filesystem::directory_iterator(dir))
        out.push_back(e.path().filename().string());
    return out;
}

}  // namespace

TEST_CASE("SaveGame: save/listSlots/load atomic write", "[savegame]") {
    TempDir tmp("nova-save");
    SaveGame::setSaveDirectory(tmp.path());

    Scene scene("Play");
    buildSavableScene(scene);

    REQUIRE(SaveGame::save(scene, 0, "assets/scenes/demo.nova.json", 12.5f));
    REQUIRE(SaveGame::slotExists(0));

    // 原子写：目录中只有 slot0.novasave，无 .tmp 残留
    const auto files = listDirFiles(tmp.path());
    REQUIRE(files.size() == 1);
    REQUIRE(files[0] == "slot0.novasave");

    // 槽列表浅校验通过
    const auto slots = SaveGame::listSlots();
    REQUIRE(slots.size() == 1);
    REQUIRE(slots[0].slot == 0);
    REQUIRE(slots[0].valid);
    REQUIRE(slots[0].scenePath == "assets/scenes/demo.nova.json");
    REQUIRE(slots[0].playTimeSeconds == Catch::Approx(12.5f));
    REQUIRE(slots[0].timestampSeconds > 0.0);

    // 读档：原地重建，字段还原
    const SaveSlotInfo info = SaveGame::load(scene, 0);
    REQUIRE(info.valid);
    REQUIRE(info.scenePath == "assets/scenes/demo.nova.json");
    GameObject* child = findFirst(scene, "SaveChild");
    REQUIRE(child != nullptr);
    REQUIRE(child->parent() == findFirst(scene, "SaveRoot"));
    REQUIRE(child->transform()->localPosition() == glm::vec3(4.0f, 5.0f, 6.0f));
}

TEST_CASE("SaveGame: in-place rebuild keeps object IDs", "[savegame]") {
    TempDir tmp("nova-save-id");
    SaveGame::setSaveDirectory(tmp.path());

    Scene scene("Play");
    buildSavableScene(scene);
    const uint64_t rootId = findFirst(scene, "SaveRoot")->id();
    const uint64_t childId = findFirst(scene, "SaveChild")->id();

    REQUIRE(SaveGame::save(scene, 0, "assets/scenes/demo.nova.json"));

    // 修改场景（新增对象 + 移动既有对象）后读档：既有对象以原 ID 恢复
    scene.createGameObject("TempExtra");
    REQUIRE(SaveGame::load(scene, 0).valid);
    REQUIRE(scene.findGameObject(rootId) != nullptr);
    REQUIRE(scene.findGameObject(childId) != nullptr);
    REQUIRE(findFirst(scene, "TempExtra") == nullptr);   // 快照外的对象消失
    REQUIRE(countObjects(scene) == 2);
}

TEST_CASE("SaveGame: corrupt JSON rejected scene unchanged", "[savegame]") {
    TempDir tmp("nova-save-corrupt");
    SaveGame::setSaveDirectory(tmp.path());

    Scene scene("Play");
    buildSavableScene(scene);
    const int before = countObjects(scene);

    // ① 非 JSON
    {
        std::ofstream out(tmp.path() / "slot1.novasave", std::ios::binary);
        out << "{ this is garbage !!!";
    }
    // ② snapshot 缺 objects 键（DevOps 修复回归：{"snapshot":{}} 曾把场景清空）
    {
        std::ofstream out(tmp.path() / "slot2.novasave", std::ios::binary);
        out << R"({"version":1,"scene":"x","snapshot":{}})";
    }
    // ③ 未来版本（version > 当前拒绝）
    {
        std::ofstream out(tmp.path() / "slot3.novasave", std::ios::binary);
        out << R"({"version":99,"scene":"x","snapshot":{"version":4,"objects":[]}})";
    }

    // 三个槽全部加载失败且场景保持原样
    for (int slot : {1, 2, 3}) {
        const SaveSlotInfo info = SaveGame::load(scene, slot);
        REQUIRE_FALSE(info.valid);
        REQUIRE(countObjects(scene) == before);
        REQUIRE(findFirst(scene, "SaveRoot") != nullptr);
    }

    // 槽列表：损坏槽保留条目并标 invalid
    const auto slots = SaveGame::listSlots();
    REQUIRE(slots.size() == 3);
    for (const auto& s : slots)
        REQUIRE_FALSE(s.valid);
}

TEST_CASE("SaveGame: deleteSlot and invalid slot", "[savegame]") {
    TempDir tmp("nova-save-del");
    SaveGame::setSaveDirectory(tmp.path());

    Scene scene("Play");
    REQUIRE(SaveGame::save(scene, 0, "assets/scenes/demo.nova.json"));
    REQUIRE(SaveGame::deleteSlot(0));
    REQUIRE_FALSE(SaveGame::slotExists(0));
    REQUIRE_FALSE(SaveGame::deleteSlot(0));   // 已删：失败但不崩

    // 非法槽号：save/load/delete 全部拒绝
    REQUIRE_FALSE(SaveGame::save(scene, -1, "x"));
    REQUIRE_FALSE(SaveGame::load(scene, -1).valid);
    REQUIRE_FALSE(SaveGame::deleteSlot(-1));
    REQUIRE_FALSE(SaveGame::slotExists(-1));
}
