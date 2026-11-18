#pragma once
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include <glm/vec3.hpp>
#include <NovaEngine/Network/NetTypes.h>

// NetworkManager —— 网络多人预研 spike（V0.5-15，分支 spike/network-v1，不进 main）
//
// 确定性帧同步（锁步）最小实现：
//   1. 每个固定 tick（60Hz，与物理步进一致）收集本地输入（WASD/方向键），
//      按帧号经 TCP 发给对端；
//   2. 双方在同一帧号应用「本地输入 + 远端输入」各自推进同一份确定性模拟
//      （方块 XZ 平面匀速积分 + 边界环绕，绕开 Jolt，见最终报告 Jolt 确定性评估）；
//   3. 输入延迟缓冲（默认 3 帧 ≈ 50ms）：只模拟「已发送帧号 - 延迟」以内的帧，
//      LAN 抖动被缓冲吸收；远端输入缺失时停等（stall），不预测不插值。
//
// 线程模型：无网络线程——非阻塞 WinSock2 + 每渲染帧 pump，与引擎单线程主循环
// 天然契合（不违反「不要多线程渲染」约束，也免去跨线程队列与锁）。
// 用法（Player 主循环）：Input::beginFrame 之后、scene.update 之前调 update(dt)。
namespace NovaEngine {

class NetCube;

enum class NetRole : uint8_t { Offline, Host, Join };
enum class NetPhase : uint8_t { Idle, WaitingPeer, Handshaking, Syncing, Disconnected };

class NetworkManager {
public:
    static NetworkManager& instance();

    // ===== 启动（Player main 解析 --net 参数后、scene.start() 之前调用）=====
    bool startHost(uint16_t port, int delayFrames);            // 监听并等待从机接入
    bool startJoin(const std::string& ip, uint16_t port, int delayFrames);
    void shutdown();                                           // 关 socket、收日志、回 Idle

    // 同步日志目录（默认 "Logs"，相对工作目录；Player 传入 exe 目录下 Logs）
    void setLogDir(const std::filesystem::path& dir);

    // 延迟注入（验收「延迟下不崩溃」）：模拟单向网络延迟 ms（0 = 关闭）
    void setSimulatedLagMs(int ms);

    // 自动输入脚本（单机双进程验证用）：本机输入由帧号驱动（各方向 4 秒循环），
    // 免人工按键即可长时间跑同步并比对日志
    void setAutoMove(bool enable);

    // ===== 主循环帧同步入口：泵网络 + 固定步进推进（每渲染帧一次）=====
    void update(float renderDt);

    // ===== 状态查询（HUD / 日志 / 验收）=====
    NetRole role() const;
    NetPhase phase() const;
    bool syncing() const;
    const char* roleName() const;
    const char* phaseName() const;
    uint32_t localFrame() const;      // 已收集并发送的输入帧数（下一帧号）
    uint32_t simFrame() const;        // 已完成模拟的帧数
    bool stalled() const;             // 上一 tick 因等待远端输入停等
    uint64_t stallFrames() const;     // 累计停等帧数
    uint64_t sentPackets() const;
    uint64_t recvPackets() const;
    int delayFrames() const;
    int simulatedLagMs() const;
    uint32_t syncChecksum() const;           // 双方位置累计校验和（跨机日志比对核心）
    glm::vec3 cubePosition(int playerIndex) const;   // 模拟状态（0=主机方块 1=从机方块）

    // ===== NetCube 注册（onStart 注册 / 析构注销）=====
    void registerCube(NetCube* cube);
    void unregisterCube(NetCube* cube);

private:
    NetworkManager() = default;
    ~NetworkManager();

    void pumpNetwork();               // 收发报文 + 延迟队列推进 + 断连检测
    void tryAccept();                 // 主机：非阻塞 accept
    void trySendAll();                // 冲刷发送缓冲（部分发送续传）
    void handlePacket(const uint8_t* data);          // 定长 16 字节分帧后的单包处理
    void promoteLagQueue(uint64_t nowMs);            // 延迟注入：到期报文进入可用队列
    void onDisconnected(const char* reason);         // 优雅断连：不崩溃，模拟停住
    void resetSim();                  // 进入 Syncing 时清空帧号/历史/停等计数
    void closeSocket();
    void openSyncLog();
    void queuePacket(const void* data, size_t size);
    static bool initWinsock();
    static uint64_t steadyNowMs();
    void stepTick();                  // 固定步进：收集→发送→按序应用并模拟
    void simulateTick(const Net::FrameInput& hostIn, const Net::FrameInput& guestIn);
    Net::FrameInput collectLocalInput() const;       // 键盘（或 --net-automove 脚本）→ 帧输入
    Net::FrameInput scriptedInput() const;           // 帧号驱动的确定性输入脚本（验证用）
    void logSyncLine();               // 每秒一行：帧号/校验和/双方位置（文件 + 控制台）
    static float wrapBounds(float v); // 确定性边界环绕
    static constexpr float kBounds = 5.0f;           // 移动边界（±5 米，环绕）
    static constexpr uintptr_t kInvalidSocket = ~uintptr_t{0};

    NetRole m_role = NetRole::Offline;
    NetPhase m_phase = NetPhase::Idle;
    uintptr_t m_socket = kInvalidSocket;   // SOCKET（winsock2 头不进公共头，见 .cpp）
    bool m_connected = false;
    uint16_t m_port = Net::kDefaultPort;
    std::string m_peerIp;
    int m_delayFrames = Net::kDefaultDelayFrames;
    int m_lagMs = 0;
    bool m_autoMove = false;               // 自动输入脚本（--net-automove）
    float m_accum = 0.0f;                  // 固定步进累加器（与物理同款）
    uint32_t m_localFrame = 0;             // 已发送输入帧数（下一帧号）
    uint32_t m_simFrame = 0;               // 已模拟帧数
    std::deque<Net::FrameInput> m_localInputs;         // 本机各帧输入历史（帧号 = simFrame + 下标）
    std::deque<Net::InputPacket> m_remoteQueue;        // 已收到、待按帧号应用的远端输入
    std::deque<std::pair<uint64_t, Net::InputPacket>> m_lagQueue;  // 延迟注入缓存（时间戳 + 包）
    std::vector<uint8_t> m_sendBuf;                    // 部分发送续传缓冲
    std::vector<uint8_t> m_recvBuf;                    // 定长分帧接收缓冲
    NetCube* m_cubeSlot[Net::kMaxPlayers] = {};        // 按 playerIndex 槽位（双方场景一致）
    glm::vec3 m_cubePos[Net::kMaxPlayers] = {};
    uint32_t m_checksum = 0;               // 双方位置逐 tick 累计校验和（跨机比对）
    uint64_t m_stallFrames = 0;
    bool m_stalled = false;
    uint64_t m_sentPackets = 0;
    uint64_t m_recvPackets = 0;
    std::filesystem::path m_logDir = "Logs";
    std::ofstream m_syncLog;               // Logs/NetSync_<角色>.log（精确位置 + 校验和）
};

}  // namespace NovaEngine
