#include <NovaEngine/Core/UUID.h>

#include <chrono>
#include <limits>
#include <random>

namespace NovaEngine {

uint64_t UUID::generate() {
    // 进程级 mt19937_64：种子 = random_device 异或 steady_clock 时间戳（ARCHITECTURE.md §4.3）
    static std::mt19937_64 gen = [] {
        std::random_device rd;
        const uint64_t seed =
            (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd()) ^
            static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        return std::mt19937_64(seed);
    }();

    // [1, UINT64_MAX] 均匀分布：保证非 0（0 保留为「无父级」）
    static std::uniform_int_distribution<uint64_t> dist(1, std::numeric_limits<uint64_t>::max());
    return dist(gen);
}

}  // namespace NovaEngine
