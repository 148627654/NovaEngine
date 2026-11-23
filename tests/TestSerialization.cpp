// TestSerialization.cpp —— 序列化往返族（§26.1 覆盖清单 1/7）
//
// 对 KNOWN-ISSUES 历史事故点：
//  - 2026-11-19 演示场景全白：MeshRenderer 网格字段写成 meshPath、反序列化读 mesh →
//    网格全空静默跳过渲染（「meshPath 字段名」正反用例）
//  - 2026-11-19 c7 教训：deserialize 读类型不符字段抛 type_error 未捕获 → abort 弹窗
//    （崩溃防御族：畸形 JSON / 类型不符字段，全部经 Catch2 try/catch 包裹执行）
//  - V0.2-14 无 Transform 防御 / ScrollView 负灵敏度钳制（DevOps 修复回归）
//  - 破坏性改动拦截：字段名改坏 → 对应用例红（验收要点 2）
//
// 无 GL 上下文纪律：测试进程不 init GL（Mesh 上传走骨架日守卫降级）。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <NovaEngine/Core/UUID.h>
#include <NovaEngine/Scene/ComponentRegistry.h>
#include <NovaEngine/Scene/MeshRenderer.h>
#include <NovaEngine/Serialization/SceneSerializer.h>
#include <NovaEngine/UI/ScrollView.h>

#include <nlohmann/json.hpp>

#include "TestCommon.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

using namespace NovaEngine;
using namespace NovaTest;

namespace {

// 内置组件 26 项（ComponentRegistry.cpp link-guard 清单逐项对照；波 2 骨架日 +CharacterController）
const char* const kBuiltinComponentNames[] = {
    "Transform",        "MeshRenderer",   "Camera",           "DirectionalLight", "AudioSource",
    "AudioListener",    "Canvas",         "Image",            "Text",             "Button",
    "Rigidbody",        "BoxCollider",    "SphereCollider",   "CapsuleCollider",  "MeshCollider",
    "PrefabInstance",   "Slider",         "ScrollView",       "LODGroup",         "ParticleSystem",
    "Terrain",          "SpriteRenderer", "BoxCollider2D",    "SkeletalMeshRenderer", "Animator",
    "CharacterController",
};

// 组装场景并返回其序列化文本（组件经注册表创建 → 与反序列化路径同源分发）
std::string serializeSceneWith(Scene& scene) {
    return SceneSerializer::serialize(scene);
}

// 按组件名在 GO 上查找组件（按 typeId 精确匹配）
Component* findComponentByType(GameObject* go, const std::string& name) {
    const uint32_t typeId = ComponentRegistry::typeIdForName(name);
    if (typeId == 0)
        return nullptr;
    for (auto* c : go->getComponents<Component>())
        if (c->typeId() == typeId)
            return c;
    return nullptr;
}

}  // namespace

TEST_CASE("Serialization: builtin component registry has 26 entries", "[serialization][registry]") {
    const std::vector<std::string> names = ComponentRegistry::builtinNames();
    REQUIRE(names.size() == 26);
    for (const char* expect : kBuiltinComponentNames) {
        INFO("缺注册组件: " << expect);
        REQUIRE(ComponentRegistry::exists(expect));
    }
    // 注册表完备性 = 序列化分发完备性（反例：组件存在但 registry 缺失 → 「未知组件跳过」）
    for (const std::string& n : names) {
        INFO("注册表多出: " << n);
        REQUIRE(ComponentRegistry::typeIdForName(n) != 0);
    }
}

TEST_CASE("Serialization: 26 components JSON lossless roundtrip", "[serialization][roundtrip]") {
    for (const char* name : kBuiltinComponentNames) {
        DYNAMIC_SECTION("组件 " << name) {
            Scene src("Src");
            GameObject* go = src.createGameObject(name);
            // 经注册表创建（link-guard 覆盖验证：静态库链接裁剪防护是否仍完整）
            std::unique_ptr<Component> comp = ComponentRegistry::create(name, go);
            REQUIRE(comp != nullptr);
            go->attachComponent(std::move(comp), ComponentRegistry::typeIdForName(name),
                                /*callAwake=*/false);

            const std::string text1 = serializeSceneWith(src);
            Scene dst("Dst");
            SceneSerializer::deserialize(dst, text1);

            // 对象与组件都在
            REQUIRE(findFirst(dst, name) != nullptr);
            GameObject* dgo = findFirst(dst, name);
            REQUIRE(findComponentByType(dgo, name) != nullptr);

            // 全场景 JSON 往返无损：字段名/字段值/键序逐一相同（字段名改坏即红）
            const std::string text2 = serializeSceneWith(dst);
            REQUIRE(text1 == text2);
        }
    }
}

TEST_CASE("Serialization: Transform value roundtrip", "[serialization][transform]") {
    Scene src("Src");
    GameObject* go = src.createGameObject("T");
    go->transform()->setLocalPosition(glm::vec3(1.5f, -2.25f, 3.125f));
    go->transform()->setLocalEulerAngles(glm::vec3(30.0f, 45.0f, 60.0f));
    go->transform()->setLocalScale(glm::vec3(2.0f, 3.0f, 4.0f));

    Scene dst("Dst");
    SceneSerializer::deserialize(dst, SceneSerializer::serialize(src));
    GameObject* dgo = findFirst(dst, "T");
    REQUIRE(dgo != nullptr);
    REQUIRE(dgo->transform()->localPosition() == glm::vec3(1.5f, -2.25f, 3.125f));
    REQUIRE(glm::length(dgo->transform()->localScale() - glm::vec3(2.0f, 3.0f, 4.0f)) < 1e-5f);
    // 欧拉角往返（含正负与多圈取模差异之外的值）
    const glm::vec3 euler = dgo->transform()->localEulerAngles();
    REQUIRE(glm::length(glm::radians(euler) - glm::radians(glm::vec3(30.0f, 45.0f, 60.0f))) < 1e-4f);
}

TEST_CASE("Serialization: mesh key name is 'mesh' (2026-11-19 white-screen regression)",
          "[serialization][mesh-key]") {
    Scene src("Src");
    GameObject* go = src.createGameObject("MR");
    auto mr = std::make_unique<MeshRenderer>(go);
    mr->meshPath = "assets/models/cube.obj";
    go->attachComponent(std::move(mr), Component::componentTypeId<MeshRenderer>(), false);

    const std::string text = SceneSerializer::serialize(src);
    // 正：JSON 里必须是 "mesh" 键（写错成 "meshPath" 即红）
    REQUIRE(text.find("\"mesh\"") != std::string::npos);
    // 反：不得出现历史事故键名（meshPath 是组件成员名，不是 JSON 键名）
    REQUIRE(text.find("\"meshPath\"") == std::string::npos);

    // 往返后字段语义保持；无 GL 时 mesh 上传降级为 nullptr（骨架日守卫）
    Scene dst("Dst");
    SceneSerializer::deserialize(dst, text);
    GameObject* dgo = findFirst(dst, "MR");
    REQUIRE(dgo != nullptr);
    auto* dmr = dgo->getComponent<MeshRenderer>();
    REQUIRE(dmr != nullptr);
    REQUIRE(dmr->meshPath == "assets/models/cube.obj");
    REQUIRE(dmr->mesh == nullptr);
}

TEST_CASE("Serialization: wrong 'meshPath' key silently ignored (hazard pin)",
          "[serialization][mesh-key]") {
    // 2026-11-19 事故现场：场景 JSON 把网格字段写成 meshPath → 反序列化读 mesh →
    // 网格全空、静默跳过渲染（空路径无警告）。本用例固化该行为边界：
    // 任何把读键改成 meshPath 的改动会在此处变红（正用例），任何把写键改成 meshPath
    // 的改动会在正用例变红——双方向锁定字段名。
    const std::string json = R"({
        "version": 4,
        "name": "Neg",
        "objects": [{
            "id": 7, "name": "MR",
            "components": [
                {"type": "Transform", "position": [0,0,0], "rotation": [0,0,0,1], "scale": [1,1,1]},
                {"type": "MeshRenderer", "meshPath": "assets/models/cube.obj"}
            ]
        }]
    })";
    Scene scene("S");
    SceneSerializer::deserialize(scene, json);
    GameObject* go = findFirst(scene, "MR");
    REQUIRE(go != nullptr);
    auto* mr = go->getComponent<MeshRenderer>();
    REQUIRE(mr != nullptr);
    REQUIRE(mr->meshPath.empty());   // 读键是 mesh：meshPath 键被忽略
    REQUIRE(mr->mesh == nullptr);
}

TEST_CASE("Serialization: unknown component warns and skips", "[serialization][unknown-component]") {
    const std::string json = R"({
        "version": 4,
        "objects": [{
            "id": 1, "name": "U",
            "components": [
                {"type": "Transform", "position": [0,0,0], "rotation": [0,0,0,1], "scale": [1,1,1]},
                {"type": "TotallyUnknownComponent", "someField": 123}
            ]
        }]
    })";
    Scene scene("S");
    SceneSerializer::deserialize(scene, json);
    REQUIRE(countObjects(scene) == 1);
    GameObject* go = findFirst(scene, "U");
    REQUIRE(go != nullptr);
    REQUIRE(go->transform() != nullptr);
    REQUIRE(go->getComponents<Component>().size() == 1);   // 未知组件被跳过，Transform 保留
}

TEST_CASE("Serialization: version 1/2/3/4 legacy files load", "[serialization][version]") {
    for (int version : {1, 2, 3, 4}) {
        DYNAMIC_SECTION("version " << version) {
            // 旧版 schema 无 environment/ibl/postProcess 段：全默认值兜底、零崩溃
            const std::string json = R"({
                "version": )" + std::to_string(version) + R"(,
                "name": "Old",
                "objects": [{
                    "id": 42, "name": "A", "parent": 0,
                    "components": [
                        {"type": "Transform", "position": [1,2,3], "rotation": [0,0,0,1], "scale": [2,2,2]}
                    ]
                }]
            })";
            Scene scene("S");
            SceneSerializer::deserialize(scene, json);
            REQUIRE(scene.name() == "Old");
            REQUIRE(countObjects(scene) == 1);
            GameObject* go = findFirst(scene, "A");
            REQUIRE(go != nullptr);
            REQUIRE(go->id() == 42);   // 旧文件对象 ID 原样保留
            REQUIRE(go->transform()->localPosition() == glm::vec3(1.0f, 2.0f, 3.0f));
            REQUIRE(go->transform()->localScale() == glm::vec3(2.0f, 2.0f, 2.0f));
        }
    }
}

TEST_CASE("Serialization: unknown version warns but attempts load", "[serialization][version]") {
    const std::string json = R"({
        "version": 99,
        "objects": [{
            "id": 1, "name": "A",
            "components": [{"type": "Transform", "position": [0,0,0], "rotation": [0,0,0,1], "scale": [1,1,1]}]
        }]
    })";
    Scene scene("S");
    SceneSerializer::deserialize(scene, json);
    REQUIRE(countObjects(scene) == 1);
}

TEST_CASE("Serialization: environment and editor camera roundtrip", "[serialization][environment]") {
    Scene src("Src");
    src.skyColor = glm::vec3(0.1f, 0.2f, 0.3f);
    src.fogColor = glm::vec3(0.4f, 0.5f, 0.6f);
    src.iblIntensity = 0.5f;
    src.postProcess.enabled = true;
    src.postProcess.bloomIntensity = 0.25f;
    src.postProcess.ssaoEnabled = true;
    src.editorCameraDistance = 12.5f;

    Scene dst("Dst");
    SceneSerializer::deserialize(dst, SceneSerializer::serialize(src));
    REQUIRE(dst.skyColor == glm::vec3(0.1f, 0.2f, 0.3f));
    REQUIRE(dst.fogColor == glm::vec3(0.4f, 0.5f, 0.6f));
    REQUIRE(dst.iblIntensity == Catch::Approx(0.5f));
    REQUIRE(dst.postProcess.enabled);
    REQUIRE(dst.postProcess.bloomIntensity == Catch::Approx(0.25f));
    REQUIRE(dst.postProcess.ssaoEnabled);
    REQUIRE(dst.editorCameraDistance == Catch::Approx(12.5f));
}

TEST_CASE("Crash defense: malformed JSON keeps scene unchanged", "[serialization][malformed]") {
    Scene scene("S");
    scene.createGameObject("Keep");

    SceneSerializer::deserialize(scene, "{ this is not json !!!");
    REQUIRE(countObjects(scene) == 1);
    REQUIRE(findFirst(scene, "Keep") != nullptr);

    // 根节点非对象
    SceneSerializer::deserialize(scene, "[1, 2, 3]");
    REQUIRE(countObjects(scene) == 1);
    REQUIRE(findFirst(scene, "Keep") != nullptr);
}

TEST_CASE("Crash defense: missing Transform auto-added", "[serialization][no-transform]") {
    const std::string json = R"({
        "version": 4,
        "objects": [{"id": 1, "name": "NoTransform", "components": []}]
    })";
    Scene scene("S");
    SceneSerializer::deserialize(scene, json);
    GameObject* go = findFirst(scene, "NoTransform");
    REQUIRE(go != nullptr);
    REQUIRE(go->transform() != nullptr);   // 已补默认 Transform，setParent/transform() 不再空指针
}

TEST_CASE("Crash defense: type-mismatch fields fall back (2026-11-19 dialog regression)", "[serialization][type-mismatch]") {
    // id 字符串 / name 数字 / position 字符串 / mesh 数字 / material 字符串——
    // 全链路 safeGet/jsonTo* 回退，不抛（历史：nlohmann type_error 未捕获 → abort 弹窗挂死）
    const std::string json = R"({
        "version": 4,
        "objects": [{
            "id": "abc",
            "name": 123,
            "components": [
                {"type": "Transform", "position": "not-a-vec3", "rotation": [], "scale": {}},
                {"type": "MeshRenderer", "mesh": 7, "material": "bad-material"}
            ]
        }]
    })";
    Scene scene("S");
    SceneSerializer::deserialize(scene, json);
    REQUIRE(countObjects(scene) == 1);
    GameObject* go = findFirst(scene, "GameObject");   // name 类型不符 → 默认名
    REQUIRE(go != nullptr);
    REQUIRE(go->id() != 0);                            // id 类型不符 → 重新生成
    REQUIRE(go->transform()->localPosition() == glm::vec3(0.0f));   // position 类型不符 → 默认
    auto* mr = go->getComponent<MeshRenderer>();
    REQUIRE(mr != nullptr);
    REQUIRE(mr->meshPath.empty());                     // mesh 类型不符 → 默认空
    REQUIRE(mr->material.albedo == glm::vec3(0.8f));   // material 段非对象 → 全默认（P2-5 防御）
}

TEST_CASE("Crash defense: out-of-range float id regenerated", "[serialization][id-hardening]") {
    // 2026-11-19 硬化：超范围 JSON 整数被解析成浮点，浮点→uint64 强转是 UB——
    // 现一律视为无效重新生成（此前只会警告但值已是垃圾）
    const std::string json = R"({
        "version": 4,
        "objects": [{
            "id": 1e30, "name": "Big",
            "components": [{"type": "Transform", "position": [0,0,0], "rotation": [0,0,0,1], "scale": [1,1,1]}]
        }]
    })";
    Scene scene("S");
    SceneSerializer::deserialize(scene, json);
    GameObject* go = findFirst(scene, "Big");
    REQUIRE(go != nullptr);
    REQUIRE(go->id() != 0);            // 无效值重新生成（UUID 为随机 64 位，高位可为 1）
    REQUIRE(go->id() != UUID::kInvalid);
}

TEST_CASE("Crash defense: dangling parent ref stays root", "[serialization][bad-parent]") {
    const std::string json = R"({
        "version": 4,
        "objects": [{
            "id": 1, "name": "Orphan", "parent": 999999,
            "components": [{"type": "Transform", "position": [0,0,0], "rotation": [0,0,0,1], "scale": [1,1,1]}]
        }]
    })";
    Scene scene("S");
    SceneSerializer::deserialize(scene, json);
    GameObject* go = findFirst(scene, "Orphan");
    REQUIRE(go != nullptr);
    REQUIRE(go->parent() == nullptr);
}

TEST_CASE("Crash defense: ScrollView negative clamp",
          "[serialization][scrollview-clamp]") {
    const std::string json = R"({
        "version": 4,
        "objects": [{
            "id": 1, "name": "SV",
            "components": [
                {"type": "Transform", "position": [0,0,0], "rotation": [0,0,0,1], "scale": [1,1,1]},
                {"type": "ScrollView", "scrollSensitivity": -5.0, "scrollOffset": [-3.0, -7.0]}
            ]
        }]
    })";
    Scene scene("S");
    SceneSerializer::deserialize(scene, json);
    GameObject* go = findFirst(scene, "SV");
    REQUIRE(go != nullptr);
    auto* sv = go->getComponent<ScrollView>();
    REQUIRE(sv != nullptr);
    REQUIRE(sv->scrollSensitivity >= 0.0f);   // 负值会反向滚动，钳 0
    REQUIRE(sv->scrollOffset.x >= 0.0f);
    REQUIRE(sv->scrollOffset.y >= 0.0f);
}
