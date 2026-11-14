#pragma once
#include <cstdint>

namespace NovaEngine {

class UUID {
public:
    static constexpr uint64_t kInvalid = 0;  // 保留值：表示「无父级」
    static uint64_t generate();              // mt19937_64 随机 64 位，保证非 0
};

}  // namespace NovaEngine
