#include <NovaEngine/Core/Logger.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <fstream>
#include <mutex>
#include <vector>

#ifdef _WIN32
#include <windows.h>   // OutputDebugStringA / SetUnhandledExceptionFilter（target 已定义 WIN32_LEAN_AND_MEAN）
#endif

namespace NovaEngine {

namespace {

std::vector<Logger::Sink> g_sinks;
std::mutex g_sinkMutex;

// ===== 文件日志状态（2026-11-16）=====
std::ofstream g_logFile;          // 文件 sink 打开的文件（关闭后 sink 静默 no-op）
std::mutex g_logFileMutex;        // 文件写互斥（Logger::log 多线程可达）
bool g_fileSinkRegistered = false;

const char* levelPrefix(LogLevel level) {
    switch (level) {
    case LogLevel::Debug: return "[Debug] ";
    case LogLevel::Info:  return "[Info]  ";
    case LogLevel::Warn:  return "[Warn]  ";
    case LogLevel::Error: return "[Error] ";
    }
    return "[?] ";
}

// 本地时间 "YYYY-MM-DD HH:MM:SS.mmm"（线程安全：localtime_s/_r 各自取静态/调用方缓冲）
std::string timestampNow() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const int ms = int(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
    const std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    return buf;
}

// 文件名时间戳 "YYYYMMDD_HHMMSS"
std::string fileStamp(const std::tm& tm) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

std::tm localTimeNow() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

// 文件 sink：时间戳 + 级别 + 内容，逐行落盘（崩溃时已写内容不丢）
void fileSink(LogLevel level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_logFileMutex);
    if (!g_logFile.is_open()) return;
    g_logFile << timestampNow() << " " << levelPrefix(level) << msg << "\n";
    g_logFile.flush();
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

void Logger::initFileLog(const std::filesystem::path& dir, const std::string& appName) {
    // 关旧文件（重复调用语义：换新文件）
    {
        std::lock_guard<std::mutex> lock(g_logFileMutex);
        if (g_logFile.is_open())
            g_logFile.close();
    }

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        // 目录创建失败：文件日志降级为无（stdout/调试器/Console 面板不受影响）
        Logger::warn("日志目录创建失败 {}: {}", dir.string(), ec.message());
        return;
    }

    // 时间戳文件名；重名（同秒多次启动）自动加 _1/_2 后缀
    const std::string stamp = fileStamp(localTimeNow());
    std::filesystem::path path = dir / (appName + "_" + stamp + ".log");
    for (int i = 1; std::filesystem::exists(path, ec); ++i) {
        ec.clear();
        path = dir / std::format("{}_{}_{}.log", appName, stamp, i);
    }

    {
        std::lock_guard<std::mutex> lock(g_logFileMutex);
        g_logFile.open(path);   // path 重载（中文路径安全，MSVC）
    }
    if (!g_logFile.is_open()) {
        Logger::warn("日志文件创建失败: {}", path.string());
        return;
    }
    if (!g_fileSinkRegistered) {
        addSink(fileSink);   // 注册一次；文件关闭后 sink 静默 no-op（不影响 Console 等其它 sink）
        g_fileSinkRegistered = true;
    }
    Logger::info("日志文件: {}", path.string());
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(g_logFileMutex);
    if (g_logFile.is_open()) {
        g_logFile.flush();
        g_logFile.close();
    }
}

void Logger::installCrashHandlers() {
    static std::atomic_bool s_installed{false};
    if (s_installed.exchange(true))
        return;   // 幂等：两 exe 各装一次即可

    // C++ 未捕获异常：原因写进日志文件（逐行落盘保证已写内容不丢），再维持默认 abort 行为
    std::set_terminate([] {
        Logger::error("未捕获异常触发 std::terminate（崩溃）——详见上方日志");
        Logger::shutdown();
        std::abort();
    });

#ifdef _WIN32
    // SEH 未处理异常（访问违例/断言等）：记录异常码后交还系统默认处理（崩溃对话框/WER）
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* ep) -> LONG {
        const DWORD code =
            (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionCode : 0;
        Logger::error("未处理异常（SEH 异常码 {:#x}）——详见上方日志", static_cast<unsigned>(code));
        Logger::shutdown();
        return EXCEPTION_CONTINUE_SEARCH;
    });
#endif
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
