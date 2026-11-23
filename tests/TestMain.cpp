// TestMain.cpp —— 共享辅助实现（TempDir）。
// 测试入口 main 由 Catch2::Catch2WithMain 提供：每个测试用例在 Catch2 的
// try/catch 包裹下执行（未捕获异常 → 用例红，不 abort 弹窗——§26.1
// 「测试 harness 必须 try/catch 防弹窗」纪律由此满足）。

#include "TestCommon.h"

#include <atomic>
#include <crtdbg.h>   // _CrtSetReportMode（断言防弹窗收口）
#include <system_error>

namespace NovaTest {

namespace {
std::atomic<int> s_tempCounter{0};
}  // namespace

// ===== 全局断言防弹窗（V1.0 波 2 合波收口，cR 波 2 报告移交）=====
// MSVC 调试断言默认弹模态窗（2026-11-23 用户屏幕被 NovaTests 断言窗阻塞的教训）；
// 静态初始化器把断言重定向到 stderr——测试失败即进程退出,不再弹窗。
// try/catch 只能防异常,防不了 _DEBUG 迭代器断言,必须走 _CrtSetReportMode。
namespace {
struct AssertDialogSuppressor {
    AssertDialogSuppressor() {
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    }
};
AssertDialogSuppressor s_assertDialogSuppressor;
}  // namespace

TempDir::TempDir(const std::string& prefix) {
    std::error_code ec;
    const std::filesystem::path base = std::filesystem::temp_directory_path(ec);
    std::filesystem::path dir;
    for (int attempt = 0; attempt < 100; ++attempt) {
        dir = base / (prefix + std::to_string(s_tempCounter.fetch_add(1)));
        if (!std::filesystem::exists(dir, ec))
            break;
    }
    std::filesystem::create_directories(dir, ec);
    m_path = dir;
}

TempDir::~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(m_path, ec);
}

const std::filesystem::path& TempDir::path() const {
    return m_path;
}

}  // namespace NovaTest
