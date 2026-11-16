#pragma once
#include <filesystem>
#include <format>
#include <functional>
#include <string>

namespace NovaEngine {

enum class LogLevel { Debug, Info, Warn, Error };

class Logger {
public:
    using Sink = std::function<void(LogLevel, const std::string&)>;
    static void addSink(Sink sink);     // ConsolePanel 注册
    static void clearSinks();

    // ===== 文件日志（2026-11-16：bug 排查用固定位置日志文件）=====
    // 写入 <dir>/<appName>_<YYYYMMDD_HHMMSS>.log（重名自动加 _1/_2 后缀）；目录不存在自动
    // 创建。每行带毫秒级时间戳并逐行落盘（崩溃不丢）。重复调用先关旧文件再开新文件。
    static void initFileLog(const std::filesystem::path& dir, const std::string& appName);
    static void shutdown();             // 关闭文件日志（进程退出前调用；此后文件 sink 静默 no-op）
    static void installCrashHandlers(); // terminate/SEH 兜底：崩溃原因写进日志文件再走默认处理

    template<typename... Args>
    static void debug(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Debug, std::format(fmt, std::forward<Args>(args)...));
    }
    template<typename... Args>
    static void info(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
    }
    template<typename... Args>
    static void warn(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...));
    }
    template<typename... Args>
    static void error(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
    }

private:
    static void log(LogLevel level, const std::string& msg);  // 写 stdout + OutputDebugString + 全部 sink
};

}  // namespace NovaEngine
