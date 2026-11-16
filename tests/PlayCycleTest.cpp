// PlayCycleTest —— 临时无头回归测试：模拟编辑器 Play 进出往返（serialize→deserialize），
// 验证父子层级不丢失。用完即删（不随仓库长期保留）。
#include <NovaEngine/NovaEngine.h>
#include <NovaEngine/Serialization/SceneSerializer.h>
#include <NovaEngine/Scene/Canvas.h>
#include <NovaEngine/Scene/Button.h>
#include <NovaEngine/Scene/Text.h>
#include <NovaEngine/Scene/Transform.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace NovaEngine;

static int g_failures = 0;

#define CHECK(cond, msg)                                   \
    do {                                                   \
        if (!(cond)) {                                     \
            std::printf("  [FAIL] %s\n", msg);             \
            ++g_failures;                                  \
        } else {                                           \
            std::printf("  [ok]   %s\n", msg);             \
        }                                                  \
    } while (0)

// 按用户场景搭：GameObject(Canvas) 下挂 Button、Text；另有 Cube→SmallCube 嵌套（demo 场景同款）
static void buildUserScene(Scene& s) {
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

static void dumpTree(const Scene& s, const char* tag) {
    std::printf("--- %s ---\n", tag);
    std::function<void(GameObject*, int)> rec = [&](GameObject* go, int depth) {
        std::string indent(depth * 2, ' ');
        std::printf("%s%s (parent=%s)\n", indent.c_str(), go->name().c_str(),
                    go->parent() ? go->parent()->name().c_str() : "(root)");
        for (const auto& c : go->children())
            rec(c.get(), depth + 1);
    };
    for (GameObject* r : s.rootObjects())
        rec(r, 0);
}

static bool verifyHierarchy(const Scene& s, const char* tag) {
    std::printf("== 校验: %s ==\n", tag);
    const int before = g_failures;
    auto find = [&](const char* name) -> GameObject* {
        const auto list = s.findGameObjectsByName(name);
        return list.empty() ? nullptr : list.front();
    };
    GameObject* canvasGO = find("GameObject");
    GameObject* button = find("Button");
    GameObject* text = find("Text");
    GameObject* cube = find("Cube");
    GameObject* small = find("SmallCube");

    CHECK(canvasGO != nullptr, "GameObject 存在");
    CHECK(button != nullptr && text != nullptr, "Button/Text 存在");
    CHECK(cube != nullptr && small != nullptr, "Cube/SmallCube 存在");
    if (button)
        CHECK(button->parent() == canvasGO, "Button 的父级是 GameObject");
    if (text)
        CHECK(text->parent() == canvasGO, "Text 的父级是 GameObject");
    if (small)
        CHECK(small->parent() == cube, "SmallCube 的父级是 Cube");
    return g_failures == before;   // 本轮无新增失败
}

int main() {
    std::printf("PlayCycleTest: 模拟编辑器 Play 往返（serialize→deserialize×N）\n");

    Scene scene("Test");
    buildUserScene(scene);
    dumpTree(scene, "初始");
    if (!verifyHierarchy(scene, "初始状态")) return 1;

    // 模拟 N 次 enterPlay（serialize 快照）→ exitPlay（deserialize 还原）
    for (int i = 1; i <= 5; ++i) {
        const std::string snap = SceneSerializer::serialize(scene);   // enterPlayMode 同款
        std::printf("\n[第 %d 轮] 快照长度=%zu\n", i, snap.size());
        SceneSerializer::deserialize(scene, snap);                    // exitPlayMode 同款
        if (!verifyHierarchy(scene, ("第 " + std::to_string(i) + " 轮往返后").c_str())) {
            dumpTree(scene, "第 N 轮失败现场");
            return 1;
        }
    }

    // 加一轮：中间夹一次 scene.start() + scene.update()（模拟 Play 帧循环）
    {
        std::printf("\n[带 Play 帧循环的往返]\n");
        const std::string snap = SceneSerializer::serialize(scene);
        scene.start();
        scene.update(1.0f / 60.0f);
        scene.update(1.0f / 60.0f);
        SceneSerializer::deserialize(scene, snap);
        if (!verifyHierarchy(scene, "Play 帧循环后还原")) {
            dumpTree(scene, "帧循环失败现场");
            return 1;
        }
    }

    // 文件往返（Ctrl+S → 重新打开）
    {
        std::printf("\n[文件往返]\n");
        const std::string file = "PlayCycleTest_tmp.nova.json";
        SceneSerializer::saveToFile(scene, file);
        Scene fresh("Fresh");
        SceneSerializer::loadFromFile(fresh, file);
        std::remove(file.c_str());
        if (!verifyHierarchy(fresh, "文件保存→加载")) {
            dumpTree(fresh, "文件往返失败现场");
            return 1;
        }
    }

    std::printf("\n%s（失败 %d 项）\n", g_failures == 0 ? "全部通过" : "存在失败", g_failures);
    return g_failures == 0 ? 0 : 1;
}
