#pragma once
#include <cstdint>

// NetTypes.h —— 网络多人预研 spike（V0.5-15，分支 spike/network-v1，不进 main）
// 锁步（确定性帧同步）协议公共类型：全部报文定长 16 字节（#pragma pack(1) 保证无对齐填充），
// TCP 字节流按定长分帧，免变长头解析。第 5 字节为显式报文类型（magic 相同，不靠内容猜类型）。
// spike 仅支持 x86/x64 小端主机（MSVC），协议按主机字节序直传；
// 真实跨平台版本需显式小端序列化（结论见最终报告）。
namespace NovaEngine::Net {

// ===== 常量 =====
inline constexpr uint32_t kMagic = 0x4E455431u;      // 报文魔数 "NET1"
inline constexpr uint16_t kProtoVersion = 1;         // 协议版本（握手校验）
inline constexpr int kTickRate = 60;                 // 锁步固定步进（与 PhysicsWorld::kFixedStep 一致）
inline constexpr float kTickDt = 1.0f / static_cast<float>(kTickRate);
inline constexpr uint16_t kDefaultPort = 27777;      // 默认监听端口
inline constexpr int kDefaultDelayFrames = 3;        // 输入延迟缓冲（帧）：吸收 LAN 抖动，约 50ms 缓冲
inline constexpr int kMaxTicksPerRenderFrame = 3;    // 每渲染帧最多推进 tick 数（与物理同款防死亡螺旋）
inline constexpr int kMaxPlayers = 2;                // spike 固定双人（主机 0 / 从机 1）
inline constexpr size_t kPacketSize = 16;            // 所有报文定长 16 字节

// ===== 报文（第 5 字节 = 显式类型）=====
enum class PacketType : uint8_t {
    Hello = 1,   // 主机 → 从机：参数协商（步进率 / 输入延迟帧数）
    Ready = 2,   // 从机 → 主机：握手完成
    Input = 3    // 双向：帧号 + 该帧输入（每 tick 一条）
};

#pragma pack(push, 1)

struct PacketHeader {
    uint32_t magic = kMagic;
    uint8_t type = 0;
};
static_assert(sizeof(PacketHeader) == 5);

// 握手报文（主机发送）：magic + 类型 + 协议版本 + 步进率 + 输入延迟帧数
struct HelloPacket {
    uint32_t magic = kMagic;
    uint8_t type = static_cast<uint8_t>(PacketType::Hello);
    uint16_t protoVersion = kProtoVersion;
    uint16_t tickRate = static_cast<uint16_t>(kTickRate);
    uint16_t delayFrames = static_cast<uint16_t>(kDefaultDelayFrames);
    uint8_t pad[5] = {};
};
static_assert(sizeof(HelloPacket) == kPacketSize);

// 就绪报文（从机发送）：magic + 类型，其余填充
struct ReadyPacket {
    uint32_t magic = kMagic;
    uint8_t type = static_cast<uint8_t>(PacketType::Ready);
    uint8_t pad[11] = {};
};
static_assert(sizeof(ReadyPacket) == kPacketSize);

// 输入报文（双向）：帧号 + 有符号移动输入（x/y ∈ {-1,0,1}，XZ 平面）
struct InputPacket {
    uint32_t magic = kMagic;
    uint8_t type = static_cast<uint8_t>(PacketType::Input);
    uint32_t frame = 0;
    int8_t x = 0;
    int8_t y = 0;
    uint8_t pad[5] = {};
};
static_assert(sizeof(InputPacket) == kPacketSize);

#pragma pack(pop)

// 一帧的输入状态（模拟层的纯数据输入，与报文一一对应）
struct FrameInput {
    int8_t x = 0;
    int8_t y = 0;
};

}  // namespace NovaEngine::Net
