#pragma once

// NetSpikeSample —— 网络多人预研 spike（V0.5-15，分支 spike/network-v1，不进 main）
// 锁步样例场景（纯代码构建，无需序列化文件）：相机 + 方向光 + 地面 + 两个 NetCube
// （0=主机红 / 1=从机蓝）+ Canvas HUD（角色/状态/帧号/双方位置/校验和）。
// 主机与从机跑同一份场景（构建逻辑一致 → 初始状态与注册顺序一致 → 模拟可确定性同步）。
namespace NovaEngine {

class Scene;

void buildNetSpikeScene(Scene& scene);

}  // namespace NovaEngine
