// NetworkManager —— 网络多人预研 spike（V0.5-15，分支 spike/network-v1，不进 main）
// 锁步（确定性帧同步）核心：非阻塞 WinSock2 + 单线程泵 + 固定步进推进。
// 注意包含顺序：winsock2.h 必须在 windows.h 之前（CMake 已定义 WIN32_LEAN_AND_MEAN）。
#include <NovaEngine/Network/NetworkManager.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>   // GetCurrentProcessId（在 winsock2 之后，防 winsock.h 冲突）

#include <NovaEngine/Core/Input.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Network/NetCube.h>
#include <NovaEngine/Scene/Transform.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace NovaEngine {

NetworkManager& NetworkManager::instance() {
    static NetworkManager mgr;   // 进程内唯一实例（单线程主循环使用，无锁）
    return mgr;
}

NetworkManager::~NetworkManager() {
    shutdown();
}

// ===== 启动 =====

bool NetworkManager::initWinsock() {
    static bool inited = false;
    if (inited) return true;
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        Logger::error("[NetSync] WSAStartup 失败（错误码 {}）", WSAGetLastError());
        return false;
    }
    inited = true;
    return true;
}

bool NetworkManager::startHost(uint16_t port, int delayFrames) {
    if (m_phase != NetPhase::Idle) {
        Logger::warn("[NetSync] 网络已启动（角色 {}），忽略重复启动", roleName());
        return false;
    }
    if (!initWinsock()) return false;

    const SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        Logger::error("[NetSync] socket 创建失败（错误码 {}）", WSAGetLastError());
        return false;
    }
    u_long nb = 1;
    ioctlsocket(sock, FIONBIO, &nb);   // 非阻塞：与单线程主循环契合
    const int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        Logger::error("[NetSync] bind 0.0.0.0:{} 失败（错误码 {}）", port, WSAGetLastError());
        closesocket(sock);
        return false;
    }
    if (listen(sock, 1) == SOCKET_ERROR) {
        Logger::error("[NetSync] listen 失败（错误码 {}）", WSAGetLastError());
        closesocket(sock);
        return false;
    }

    m_role = NetRole::Host;
    m_phase = NetPhase::WaitingPeer;
    m_socket = static_cast<uintptr_t>(sock);
    m_port = port;
    m_delayFrames = std::clamp(delayFrames, 0, 30);
    openSyncLog();
    Logger::info("[NetSync] 主机监听 0.0.0.0:{}（输入延迟 {} 帧），等待从机接入…", port, m_delayFrames);
    return true;
}

bool NetworkManager::startJoin(const std::string& ip, uint16_t port, int delayFrames) {
    if (m_phase != NetPhase::Idle) {
        Logger::warn("[NetSync] 网络已启动（角色 {}），忽略重复启动", roleName());
        return false;
    }
    if (!initWinsock()) return false;

    const SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        Logger::error("[NetSync] socket 创建失败（错误码 {}）", WSAGetLastError());
        return false;
    }
    u_long nb = 1;
    ioctlsocket(sock, FIONBIO, &nb);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        Logger::error("[NetSync] IP 地址无效: {}", ip);
        closesocket(sock);
        return false;
    }

    const int ret = connect(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (ret == SOCKET_ERROR) {
        const int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {   // 非阻塞 connect：WSAEWOULDBLOCK = 连接进行中（正常）
            Logger::error("[NetSync] connect {}:{} 失败（错误码 {}）", ip, port, err);
            closesocket(sock);
            return false;
        }
    }

    m_role = NetRole::Join;
    m_phase = NetPhase::Handshaking;   // 握手含「TCP 连接完成 → 收 Hello → 回 Ready」全流程
    m_socket = static_cast<uintptr_t>(sock);
    m_port = port;
    m_peerIp = ip;
    m_connected = (ret == 0);          // 本机回环可能立即连上
    m_delayFrames = std::clamp(delayFrames, 0, 30);   // 仅本地参考值：以主机 Hello 为准
    openSyncLog();
    Logger::info("[NetSync] 从机连接 {}:{} 中…", ip, port);
    return true;
}

void NetworkManager::shutdown() {
    if (m_phase == NetPhase::Idle) return;
    if (m_phase == NetPhase::Syncing)
        Logger::info("[NetSync] 关闭：已同步 {} 帧（发送 {} 包 / 接收 {} 包 / 停等 {} 帧）",
                     m_simFrame, m_sentPackets, m_recvPackets, m_stallFrames);
    closeSocket();
    m_role = NetRole::Offline;
    m_phase = NetPhase::Idle;
    m_localInputs.clear();
    m_remoteQueue.clear();
    m_lagQueue.clear();
    m_sendBuf.clear();
    m_recvBuf.clear();
    for (auto& slot : m_cubeSlot) slot = nullptr;
    if (m_syncLog.is_open()) m_syncLog.close();
}

void NetworkManager::setLogDir(const std::filesystem::path& dir) {
    m_logDir = dir;
}

void NetworkManager::setSimulatedLagMs(int ms) {
    m_lagMs = std::clamp(ms, 0, 2000);
    if (m_lagMs > 0)
        Logger::info("[NetSync] 延迟注入已开启：{} ms（验收用）", m_lagMs);
    else
        Logger::info("[NetSync] 延迟注入已关闭");
}

void NetworkManager::setAutoMove(bool enable) {
    m_autoMove = enable;
    if (enable) Logger::info("[NetSync] 自动输入脚本开启（各方向 4 秒循环，单机验证用）");
}

// ===== 主循环入口 =====

void NetworkManager::update(float renderDt) {
    if (m_phase == NetPhase::Idle || m_phase == NetPhase::Disconnected) return;
    pumpNetwork();
    if (m_phase != NetPhase::Syncing) return;

    // 固定步进累加器：与 PhysicsWorld::step 同款（60Hz，每渲染帧最多 3 tick）
    m_accum += renderDt;
    int ticks = 0;
    while (m_accum >= Net::kTickDt && ticks < Net::kMaxTicksPerRenderFrame) {
        stepTick();
        m_accum -= Net::kTickDt;
        ++ticks;
    }
}

void NetworkManager::pumpNetwork() {
    if (m_phase == NetPhase::Idle || m_phase == NetPhase::Disconnected) return;
    if (m_phase == NetPhase::WaitingPeer) {
        tryAccept();   // 非阻塞 accept 即足以接客，监听 socket 不参与 recv（避免误判断连）
        return;
    }

    const SOCKET sock = static_cast<SOCKET>(m_socket);
    fd_set readSet, writeSet;
    FD_ZERO(&readSet);
    FD_ZERO(&writeSet);
    const bool connectPending = (m_role == NetRole::Join && !m_connected);
    if (!connectPending) FD_SET(sock, &readSet);   // 未连上时不 recv（否则 WSAENOTCONN 误判断连）
    const bool wantWrite = !m_sendBuf.empty() || connectPending;
    if (wantWrite) FD_SET(sock, &writeSet);
    timeval tv{0, 0};
    const int sel = select(0, &readSet, &writeSet, nullptr, &tv);
    if (sel == SOCKET_ERROR) {
        if (WSAGetLastError() != WSAEINTR) onDisconnected("select 失败");
        return;
    }

    // 可读：接收并定长分帧（16 字节一包）
    if (sel > 0 && FD_ISSET(sock, &readSet)) {
        char buf[1024];
        for (;;) {
            const int n = recv(sock, buf, sizeof(buf), 0);
            if (n > 0) {
                m_recvBuf.insert(m_recvBuf.end(), buf, buf + n);
                while (m_recvBuf.size() >= Net::kPacketSize) {
                    handlePacket(m_recvBuf.data());
                    m_recvBuf.erase(m_recvBuf.begin(), m_recvBuf.begin() + Net::kPacketSize);
                }
                continue;
            }
            if (n == 0) {   // 对端优雅关闭
                onDisconnected("对端关闭连接");
                return;
            }
            const int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) break;
            onDisconnected("recv 失败");
            return;
        }
    }

    // 可写：从机非阻塞 connect 完成判定 + 冲刷发送缓冲
    if (sel > 0 && FD_ISSET(sock, &writeSet)) {
        if (m_role == NetRole::Join && !m_connected) {
            int err = 0;
            int len = sizeof(err);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
            if (err != 0) {
                onDisconnected("TCP 连接失败");
                return;
            }
            m_connected = true;
            Logger::info("[NetSync] 已连接 {}:{}，等待主机 Hello…", m_peerIp, m_port);
        }
        trySendAll();
    }

    // 延迟注入队列推进（时间戳在 pumpNetwork 内统一推进，与收发同帧）
    promoteLagQueue(steadyNowMs());
}

void NetworkManager::tryAccept() {
    const SOCKET sock = static_cast<SOCKET>(m_socket);
    sockaddr_in peer{};
    int len = sizeof(peer);
    const SOCKET client = accept(sock, reinterpret_cast<sockaddr*>(&peer), &len);
    if (client == INVALID_SOCKET) {
        const int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) return;   // 无接入请求，继续监听
        onDisconnected("accept 失败");
        return;
    }
    closesocket(sock);   // 单对单 spike：接入后关闭监听 socket
    m_socket = static_cast<uintptr_t>(client);
    m_connected = true;
    char ipBuf[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &peer.sin_addr, ipBuf, sizeof(ipBuf));
    m_peerIp = ipBuf;
    m_phase = NetPhase::Handshaking;
    const Net::HelloPacket hello;
    queuePacket(&hello, sizeof(hello));
    Logger::info("[NetSync] 从机已接入 {}:{}，已发送 Hello（步进 {}Hz / 输入延迟 {} 帧）",
                 m_peerIp, ntohs(peer.sin_port), Net::kTickRate, m_delayFrames);
}

void NetworkManager::trySendAll() {
    while (!m_sendBuf.empty()) {
        const SOCKET sock = static_cast<SOCKET>(m_socket);
        const int toSend = static_cast<int>(std::min<size_t>(m_sendBuf.size(), 4096u));
        const int n = send(sock, reinterpret_cast<const char*>(m_sendBuf.data()), toSend, 0);
        if (n > 0) {
            m_sendBuf.erase(m_sendBuf.begin(), m_sendBuf.begin() + n);
            continue;
        }
        const int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) break;   // 发送窗口满，下帧续传
        onDisconnected("send 失败");
        return;
    }
}

void NetworkManager::queuePacket(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    m_sendBuf.insert(m_sendBuf.end(), bytes, bytes + size);
}

void NetworkManager::handlePacket(const uint8_t* data) {
    Net::PacketHeader hdr;
    std::memcpy(&hdr, data, sizeof(hdr));
    if (hdr.magic != Net::kMagic) {
        Logger::warn("[NetSync] 未知报文（magic=0x{:08X}），丢弃", hdr.magic);
        return;
    }
    const auto type = static_cast<Net::PacketType>(hdr.type);

    switch (type) {
    case Net::PacketType::Hello: {
        if (m_role != NetRole::Join) {
            Logger::warn("[NetSync] 主机收到 Hello（异常），忽略");
            return;
        }
        Net::HelloPacket hello;
        std::memcpy(&hello, data, sizeof(hello));
        if (hello.protoVersion != Net::kProtoVersion) {
            onDisconnected("协议版本不匹配");
            return;
        }
        if (hello.tickRate != static_cast<uint16_t>(Net::kTickRate)) {
            onDisconnected("固定步进率不匹配");
            return;
        }
        m_delayFrames = static_cast<int>(std::clamp<uint16_t>(hello.delayFrames, uint16_t{0}, uint16_t{30}));
        const Net::ReadyPacket ready;
        queuePacket(&ready, sizeof(ready));
        m_phase = NetPhase::Syncing;
        resetSim();
        Logger::info("[NetSync] 握手完成，开始同步（输入延迟 {} 帧，帧号从 0 起）", m_delayFrames);
        break;
    }
    case Net::PacketType::Ready: {
        if (m_role != NetRole::Host || m_phase != NetPhase::Handshaking) {
            Logger::warn("[NetSync] 收到意外的 Ready（忽略）");
            return;
        }
        m_phase = NetPhase::Syncing;
        resetSim();
        Logger::info("[NetSync] 握手完成，开始同步（输入延迟 {} 帧，帧号从 0 起）", m_delayFrames);
        break;
    }
    case Net::PacketType::Input: {
        Net::InputPacket pkt;
        std::memcpy(&pkt, data, sizeof(pkt));
        ++m_recvPackets;
        if (m_lagMs > 0)
            m_lagQueue.emplace_back(steadyNowMs(), pkt);
        else
            m_remoteQueue.push_back(pkt);
        break;
    }
    }
}

void NetworkManager::promoteLagQueue(uint64_t nowMs) {
    while (!m_lagQueue.empty() && nowMs >= m_lagQueue.front().first + static_cast<uint64_t>(m_lagMs))
        m_remoteQueue.push_back(m_lagQueue.front().second), m_lagQueue.pop_front();
}

void NetworkManager::onDisconnected(const char* reason) {
    if (m_phase == NetPhase::Idle || m_phase == NetPhase::Disconnected) return;
    Logger::warn("[NetSync] 连接断开：{}（已模拟 {} 帧 / 停等 {} 帧）。模拟停住，程序继续运行（Esc 退出）",
                 reason, m_simFrame, m_stallFrames);
    if (m_syncLog.is_open())
        m_syncLog << "disconnected: " << reason << " frame=" << m_simFrame << std::endl;
    closeSocket();
    m_phase = NetPhase::Disconnected;
}

void NetworkManager::closeSocket() {
    if (m_socket != kInvalidSocket) {
        closesocket(static_cast<SOCKET>(m_socket));
        m_socket = kInvalidSocket;
    }
    m_connected = false;
}

void NetworkManager::resetSim() {
    m_accum = 0.0f;
    m_localFrame = 0;
    m_simFrame = 0;
    m_localInputs.clear();
    m_remoteQueue.clear();
    m_lagQueue.clear();
    m_checksum = 0;
    m_stallFrames = 0;
    m_stalled = false;
    m_sentPackets = 0;
    m_recvPackets = 0;
    // 方块位置回注册时的初始位置（双方场景一致 → 初态一致）
    for (int p = 0; p < Net::kMaxPlayers; ++p) {
        if (m_cubeSlot[p] != nullptr)
            m_cubePos[p] = m_cubeSlot[p]->transform()->localPosition();
    }
}

// ===== 固定步进（锁步核心）=====

void NetworkManager::stepTick() {
    // 1. 收集本机输入并发送（先发后模拟：本 tick 的输入留给对端未来帧应用）
    const Net::FrameInput local = collectLocalInput();
    m_localInputs.push_back(local);
    const Net::InputPacket pkt{Net::kMagic, static_cast<uint8_t>(Net::PacketType::Input),
                               m_localFrame, local.x, local.y, {}};
    queuePacket(&pkt, sizeof(pkt));
    ++m_sentPackets;
    ++m_localFrame;

    // 2. 只模拟「已发送帧号 - 输入延迟」以内的帧：留出网络往返缓冲
    m_stalled = false;
    while (m_simFrame + static_cast<uint32_t>(m_delayFrames) < m_localFrame) {
        if (!m_remoteQueue.empty() && m_remoteQueue.front().frame < m_simFrame) {
            // TCP 保序：队头过期只可能是重连/异常，丢弃防永久停等
            Logger::warn("[NetSync] 丢弃过期远端输入（frame={} < 已模拟 {}）",
                         m_remoteQueue.front().frame, m_simFrame);
            m_remoteQueue.pop_front();
            continue;
        }
        if (m_remoteQueue.empty() || m_remoteQueue.front().frame != m_simFrame) {
            m_stalled = true;      // 远端输入未到：停等（不预测、不插值）
            ++m_stallFrames;
            break;
        }
        const Net::InputPacket remote = m_remoteQueue.front();
        m_remoteQueue.pop_front();
        const Net::FrameInput localForFrame = m_localInputs.front();
        m_localInputs.pop_front();
        const bool isHost = (m_role == NetRole::Host);
        const Net::FrameInput hostIn = isHost ? localForFrame : Net::FrameInput{remote.x, remote.y};
        const Net::FrameInput guestIn = isHost ? Net::FrameInput{remote.x, remote.y} : localForFrame;
        simulateTick(hostIn, guestIn);
        ++m_simFrame;
        // 验收日志在「恰好整 60 帧」处记一行（放在循环内：单 tick 多帧推进时两侧帧号仍对齐）
        if (m_simFrame % 60u == 0u) logSyncLine();
    }
}

Net::FrameInput NetworkManager::collectLocalInput() const {
    if (m_autoMove) return scriptedInput();   // 验证脚本：帧号驱动，免人工按键
    const bool up = Input::isKeyDown(Key::W) || Input::isKeyDown(Key::Up);
    const bool down = Input::isKeyDown(Key::S) || Input::isKeyDown(Key::Down);
    const bool left = Input::isKeyDown(Key::A) || Input::isKeyDown(Key::Left);
    const bool right = Input::isKeyDown(Key::D) || Input::isKeyDown(Key::Right);
    Net::FrameInput in;
    in.x = static_cast<int8_t>((right ? 1 : 0) - (left ? 1 : 0));   // X 轴（D/A）
    in.y = static_cast<int8_t>((up ? 1 : 0) - (down ? 1 : 0));      // Z 轴（W/S）
    return in;
}

Net::FrameInput NetworkManager::scriptedInput() const {
    // 确定性输入脚本：每 240 tick（4 秒）切换方向：+X → +Z → -X → -Z → 循环。
    // 双方脚本一致 → 双方方块走相同轨迹（含边界环绕），日志逐帧可精确比对。
    const uint32_t phase = (m_localFrame / 240u) % 4u;
    Net::FrameInput in;
    switch (phase) {
    case 0: in.x = 1; break;
    case 1: in.y = 1; break;
    case 2: in.x = -1; break;
    default: in.y = -1; break;
    }
    return in;
}

void NetworkManager::simulateTick(const Net::FrameInput& hostIn, const Net::FrameInput& guestIn) {
    // 确定性积分：pos += dir * speed * dt，XZ 平面，边界环绕。
    // 两台机器跑同一二进制、同一输入对、同一运算顺序 → 逐位一致。
    // 每 tick 校验和累计双方位置位型：跨机日志可直接比对。
    for (int p = 0; p < Net::kMaxPlayers; ++p) {
        NetCube* cube = m_cubeSlot[p];
        if (cube == nullptr) continue;
        const Net::FrameInput& in = (p == 0) ? hostIn : guestIn;
        glm::vec2 dir(static_cast<float>(in.x), static_cast<float>(in.y));
        const float len = glm::length(dir);
        if (len > 1e-6f) dir /= len;   // 斜向归一化：速度恒定
        glm::vec3 pos = m_cubePos[p];
        pos.x += dir.x * cube->speed * Net::kTickDt;
        pos.z += dir.y * cube->speed * Net::kTickDt;
        pos.x = wrapBounds(pos.x);
        pos.z = wrapBounds(pos.z);
        m_cubePos[p] = pos;
        cube->transform()->setLocalPosition(pos);   // 写回场景（渲染读取）

        const uint32_t bits = std::bit_cast<uint32_t>(pos.x) ^ (std::bit_cast<uint32_t>(pos.z) * 0x9E3779B1u);
        m_checksum ^= bits;
        m_checksum *= 0x01000193u;
    }
}

float NetworkManager::wrapBounds(float v) {
    // v ∈ [-kBounds, kBounds] → v + kBounds ∈ [0, 2kBounds] → fmod 非负
    return std::fmod(v + kBounds, 2.0f * kBounds) - kBounds;
}

void NetworkManager::logSyncLine() {
    char line[320];
    std::snprintf(line, sizeof(line),
                  "frame=%u checksum=%08X host=(%.6f, %.6f, %.6f) guest=(%.6f, %.6f, %.6f) stall=%llu",
                  m_simFrame, m_checksum,
                  m_cubePos[0].x, m_cubePos[0].y, m_cubePos[0].z,
                  m_cubePos[1].x, m_cubePos[1].y, m_cubePos[1].z,
                  static_cast<unsigned long long>(m_stallFrames));
    if (m_syncLog.is_open()) m_syncLog << line << std::endl;   // 逐行落盘（-F 强杀也不丢）
    Logger::info("[NetSync] {}", line);
}

void NetworkManager::openSyncLog() {
    std::error_code ec;
    std::filesystem::create_directories(m_logDir, ec);
    char name[96];
    std::snprintf(name, sizeof(name), "NetSync_%s_pid%lu.log", roleName(),
                  static_cast<unsigned long>(GetCurrentProcessId()));
    m_syncLog.open(m_logDir / name);
    if (m_syncLog.is_open()) {
        m_syncLog << "NovaEngine 网络帧同步 spike（V0.5-15）：角色=" << roleName()
                  << " 端口=" << m_port << " 输入延迟=" << m_delayFrames << " 帧" << std::endl
                  << "格式：frame=帧号 checksum=双方位置累计校验和 host/guest=双方模拟的方块位置"
                  << std::endl;
    } else {
        Logger::warn("[NetSync] 同步日志打开失败: {}", (m_logDir / name).string());
    }
}

// ===== 状态查询 =====

NetRole NetworkManager::role() const { return m_role; }
NetPhase NetworkManager::phase() const { return m_phase; }
bool NetworkManager::syncing() const { return m_phase == NetPhase::Syncing; }

const char* NetworkManager::roleName() const {
    switch (m_role) {
    case NetRole::Host: return "Host";
    case NetRole::Join: return "Join";
    default: return "Offline";
    }
}

const char* NetworkManager::phaseName() const {
    switch (m_phase) {
    case NetPhase::Idle: return "未启动";
    case NetPhase::WaitingPeer: return "等待从机接入";
    case NetPhase::Handshaking: return "握手中";
    case NetPhase::Syncing: return "同步中";
    default: return "已断开";
    }
}

uint32_t NetworkManager::localFrame() const { return m_localFrame; }
uint32_t NetworkManager::simFrame() const { return m_simFrame; }
bool NetworkManager::stalled() const { return m_stalled; }
uint64_t NetworkManager::stallFrames() const { return m_stallFrames; }
uint64_t NetworkManager::sentPackets() const { return m_sentPackets; }
uint64_t NetworkManager::recvPackets() const { return m_recvPackets; }
int NetworkManager::delayFrames() const { return m_delayFrames; }
int NetworkManager::simulatedLagMs() const { return m_lagMs; }
uint32_t NetworkManager::syncChecksum() const { return m_checksum; }

glm::vec3 NetworkManager::cubePosition(int playerIndex) const {
    if (playerIndex < 0 || playerIndex >= Net::kMaxPlayers) return glm::vec3(0.0f);
    return m_cubePos[playerIndex];
}

void NetworkManager::registerCube(NetCube* cube) {
    if (cube == nullptr) return;
    if (cube->playerIndex < 0 || cube->playerIndex >= Net::kMaxPlayers) {
        Logger::warn("[NetSync] NetCube playerIndex {} 越界（仅支持 0..{}），忽略注册",
                     cube->playerIndex, Net::kMaxPlayers - 1);
        return;
    }
    if (m_cubeSlot[cube->playerIndex] == cube) return;   // 幂等
    if (m_cubeSlot[cube->playerIndex] != nullptr)
        Logger::warn("[NetSync] NetCube playerIndex {} 重复注册，覆盖", cube->playerIndex);
    m_cubeSlot[cube->playerIndex] = cube;
    m_cubePos[cube->playerIndex] = cube->transform()->localPosition();
}

void NetworkManager::unregisterCube(NetCube* cube) {
    if (cube == nullptr) return;
    for (auto& slot : m_cubeSlot)
        if (slot == cube) slot = nullptr;
}

uint64_t NetworkManager::steadyNowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

}  // namespace NovaEngine
