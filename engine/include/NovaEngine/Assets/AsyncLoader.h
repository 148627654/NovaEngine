#pragma once
#include <cstdint>
#include <string>

namespace NovaEngine {

// ===== V1.0-2 多场景异步加载（§27.1 冻结接口，2026-11-23 波 2 骨架日布）=====
// 架构裁定（ROADMAP §10-D）：唯一新线程 = 资产加载线程。铁律：线程只做 CPU 侧
//（JSON 解析、MeshData/贴图解码、Assimp）；GPU 上传（Mesh::create/Texture 创建/
// 材质资产解析挂 GPU）一律回主线程 flush 执行；物理、渲染、脚本、UI 保持主线程。
// 头文件签名冻结：实现归 cR（engine/src/Assets/AsyncLoader.cpp）。
class AsyncLoader {
public:
    static void init();       // 引擎启动（Renderer::init 之后）；线程启动即阻塞等待任务
    static void shutdown();   // 停止 + join（丢弃未完成任务；须先于 Renderer::shutdown）
    // 入队场景加载：线程内 = 读 JSON + 全部资产 CPU 解码（MeshData/贴图像素/材质资产）
    // → 结果封装成主线程执行体（GPU 上传 + 场景挂载）入结果队列
    static bool loadSceneAsync(const std::string& name, bool additive);  // 经 SceneManager 解析路径；查无 → false
    static void flushPendingResults();   // 主线程帧末（SceneManager::flushPendingLoad 之前）：依次执行结果体
    static float progress();             // 0..1（完成/总数；无任务 = 1）
    static uint32_t generation();        // 取消语义：每次 reset/加载目标变更递增，旧代际结果作废
    static void reset();                 // 清队列 + generation++（退出 Play/LoadScene 时调用）
};

}  // namespace NovaEngine
