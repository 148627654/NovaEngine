#pragma once
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
