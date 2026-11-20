#pragma once
// TestCommon.h —— 无头测试共享辅助（temp 目录 RAII / 场景工具）
// 声明与实现分置 TestCommon.h / TestMain.cpp；各测试文件直接 include。

#include <NovaEngine/NovaEngine.h>

#include <filesystem>
#include <string>

namespace NovaTest {

// 唯一临时目录 RAII：构造时在系统 temp 下建 "<prefix><序号>"，析构递归删除。
// 每个测试用例独立进程（catch_discover_tests 逐用例进程），无跨用例残留。
class TempDir {
public:
    explicit TempDir(const std::string& prefix = "novatest");
    ~TempDir();
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const std::filesystem::path& path() const;

private:
    std::filesystem::path m_path;
};

// 场景对象计数（深度优先）
inline int countObjects(const NovaEngine::Scene& scene) {
    int n = 0;
    scene.forEachObject([&n](NovaEngine::GameObject*) { ++n; });
    return n;
}

// 按名查找首个对象（无则 nullptr）
inline NovaEngine::GameObject* findFirst(NovaEngine::Scene& scene, const std::string& name) {
    const auto list = scene.findGameObjectsByName(name);
    return list.empty() ? nullptr : list.front();
}

}  // namespace NovaTest
