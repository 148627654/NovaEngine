#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace NovaEngine {

class Scene;

// 槽元数据（编辑器槽列表/读档返回值）
struct SaveSlotInfo {
    int slot = 0;
    bool valid = false;                 // 文件存在且 version 校验通过
    std::string scenePath;              // 存档时的场景资产相对路径（可为空）
    double timestampSeconds = 0.0;      // 存档时刻（system_clock）
    float playTimeSeconds = 0.0f;       // 调用方提供的游戏内计时
};

// 存档系统（c2，V0.5-12）：全静态工具类，Play 中可随时 save/load。
// 存档文件 <saveDirectory>/slot<N>.novasave（version 1）：
//   { version, scene, timestamp, playTimeSeconds, snapshot(场景 JSON version 4 内嵌) }
// 写入原子（tmp → rename）；读档走 SceneSerializer::deserialize 原地重建同一 Scene（对象 ID 保持）。
class SaveGame {
public:
    static constexpr int kCurrentSaveVersion = 1;
    // 目录：默认 <AssetDatabase::projectRoot()>/saves（编辑器=项目根/saves、Player=exe 目录/saves）
    static void setSaveDirectory(const std::filesystem::path& dir);   // 可覆盖（如 %APPDATA%）
    static std::filesystem::path saveDirectory();
    static std::filesystem::path slotPath(int slot);   // <dir>/slot<slot>.novasave

    // 存档（Play 中调用）：复用 SceneSerializer::serialize 产出场景 JSON（与 Play 快照同一代码路径）。
    // 原子写：先写 slot<N>.novasave.tmp 再 rename 覆盖；失败 Logger::error 返回 false
    static bool save(const Scene& scene, int slot, const std::string& scenePath,
                     float playTimeSeconds = 0.0f);
    // 读档（Play 中调用）：SceneSerializer::deserialize 原地重建同一 Scene 对象
    //（对象 ID 保持 → 外部 Scene*/选中集/按 ID 引用全部有效；脚本持有的裸 GameObject*
    // 需重取——与 LoadScene 同约束）。失败（文件缺失/损坏/未来版本）返回 valid=false，
    // 场景保持不变，不崩溃
    static SaveSlotInfo load(Scene& target, int slot);
    // 槽列表（编辑器面板用）：目录扫描 + 浅校验（JSON 合法 + version ≤ 当前 + snapshot 为场景对象（含 objects 数组））
    static std::vector<SaveSlotInfo> listSlots();     // 按 slot 升序
    static bool deleteSlot(int slot);                 // 失败 Logger::warn
    static bool slotExists(int slot);
};

}  // namespace NovaEngine
