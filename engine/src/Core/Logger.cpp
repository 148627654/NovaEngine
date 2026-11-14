#include <NovaEngine/Core/Logger.h>

#include <cstdio>
#include <mutex>
#include <vector>

#ifdef _WIN32
#include <windows.h>   // OutputDebugStringA（target 已定义 WIN32_LEAN_AND_MEAN）
#endif

namespace NovaEngine {

namespace {

std::vector<Logger::Sink> g_sinks;
std::mutex g_sinkMutex;

const char* levelPrefix(LogLevel level) {
    switch (level) {
    case LogLevel::Debug: return "[Debug] ";
    case LogLevel::Info:  return "[Info]  ";
    case LogLevel::Warn:  return "[Warn]  ";
    case LogLevel::Error: return "[Error] ";
    }
    return "[?] ";
}

}  // namespace

void Logger::addSink(Sink sink) {
    if (!sink) return;
    std::lock_guard<std::mutex> lock(g_sinkMutex);
    g_sinks.push_back(std::move(sink));
}

void Logger::clearSinks() {
    std::lock_guard<std::mutex> lock(g_sinkMutex);
    g_sinks.clear();
}

void Logger::log(LogLevel level, const std::string& msg) {
    const char* prefix = levelPrefix(level);

    // 1) stdout（编辑器为控制台子系统，双击运行也可见）
    std::printf("%s%s\n", prefix, msg.c_str());
    std::fflush(stdout);

    // 2) 调试器输出窗口（VS 调试运行时可见）
#ifdef _WIN32
    OutputDebugStringA(prefix);
    OutputDebugStringA(msg.c_str());
    OutputDebugStringA("\n");
#endif

    // 3) 已注册 sink（Console 面板）
    // 先拷贝后调用：sink 内部再调 Logger::log 也不会死锁
    std::vector<Sink> sinks;
    {
        std::lock_guard<std::mutex> lock(g_sinkMutex);
        sinks = g_sinks;
    }
    for (const auto& sink : sinks) {
        try {
            sink(level, msg);
        } catch (...) {
            // 单个 sink 异常不中断整条日志链路
        }
    }
}

}  // namespace NovaEngine
