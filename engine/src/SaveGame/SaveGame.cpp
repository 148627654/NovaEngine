// SaveGame.cpp —— 存档系统（c2，V0.5-12）
//
// 存档文件 <saveDirectory>/slot<N>.novasave（version 1）：
//   { "version":1, "scene":"assets/scenes/demo.nova.json",
//     "timestamp":<秒>, "playTimeSeconds":<秒>,
//     "snapshot":{ SceneSerializer 场景 JSON（version 4）内嵌 } }
//
// 写入原子性：先写 slot<N>.novasave.tmp 再 filesystem::rename 覆盖正式文件
// （Windows 上 rename 可覆盖已存在文件；异常中断最多留下 .tmp，正式存档不损坏）。
// 读档复用 SceneSerializer::deserialize 原地重建：同一 Scene 对象、对象 ID 保持
// （外部 Scene*/选中集/按 ID 引用有效，与 LoadScene 同约束）。
// 浅校验（load/listSlots 共用）：JSON 合法 + version ≤ 当前 + snapshot 为场景对象
//（含 objects 数组键，防畸形文件把场景清空为合法空场景）；
// version 缺失按 0 尽力加载（warn），version > 当前拒绝（未来版本保护，warn）。

#include <NovaEngine/SaveGame/SaveGame.h>

#include <NovaEngine/Assets/AssetDatabase.h>    // projectRoot：默认存档目录
#include <NovaEngine/Assets/AssetStream.h>      // readAllText：中文路径安全
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Serialization/SceneSerializer.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <exception>
#include <format>
#include <fstream>
#include <string_view>
#include <system_error>

namespace NovaEngine {

namespace {

// 覆盖后的存档目录；空 = 未覆盖 → saveDirectory() 返回默认 <projectRoot>/saves
std::filesystem::path s_saveDirectory;

// 槽号合法性：listSlots 只认 "slot<数字>.novasave"，负槽写盘后会变成无人认领的文件
bool validSlot(int slot) {
    return slot >= 0;
}

// 从文件名解析槽号："slot<数字>.novasave" → 数字；不符合模式返回 false
//（.tmp 残留、非存档文件、数字溢出等一律忽略）
bool parseSlotFromFilename(const std::string& filename, int& outSlot) {
    constexpr std::string_view kPrefix = "slot";
    constexpr std::string_view kSuffix = ".novasave";
    if (filename.size() < kPrefix.size() + kSuffix.size() + 1) return false;
    if (filename.compare(0, kPrefix.size(), kPrefix) != 0) return false;
    if (filename.compare(filename.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) return false;
    const std::string_view digits(filename.data() + kPrefix.size(),
                                  filename.size() - kPrefix.size() - kSuffix.size());
    for (char c : digits)
        if (c < '0' || c > '9') return false;   // 排除 "slot-1"/"slot1a" 等非纯数字
    int value = 0;
    const auto [ptr, fc] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (fc != std::errc() || ptr != digits.data() + digits.size()) return false;   // 溢出等
    outSlot = value;
    return true;
}

// 槽文件读取结果
enum class SlotReadStatus {
    Ok,            // 浅校验通过，doc 为已解析根对象
    Missing,       // 文件不存在（不记日志，由调用方决定日志级别）
    Corrupt,       // JSON 非法/根非对象/snapshot 缺失（已 Logger::error）
    FutureVersion  // version > 当前（已 Logger::warn）
};

// 读槽文件并浅校验：JSON 合法 + version ≤ 当前 + snapshot 为对象
SlotReadStatus readSlotFile(const std::filesystem::path& path, nlohmann::json& outDoc) {
    std::string text;
    if (!AssetStream::readAllText(path, text)) return SlotReadStatus::Missing;

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(text);
    } catch (const std::exception& e) {
        Logger::error("SaveGame: 存档 {} 不是合法的 JSON（{}），按损坏处理", path.string(), e.what());
        return SlotReadStatus::Corrupt;
    }
    if (!doc.is_object()) {
        Logger::error("SaveGame: 存档 {} 根节点不是 JSON 对象，按损坏处理", path.string());
        return SlotReadStatus::Corrupt;
    }

    // version 校验：> 当前 → 拒绝（未来版本保护）；缺失/非整数按 0 尽力加载 + warn
    int version = 0;
    if (doc.contains("version")) {
        if (doc["version"].is_number_integer()) {
            version = doc["version"].get<int>();
        } else {
            Logger::warn("SaveGame: 存档 {} 的 version 字段非整数，按 0 处理", path.string());
        }
    } else {
        Logger::warn("SaveGame: 存档 {} 缺少 version 字段，按 0 尽力加载", path.string());
    }
    if (version > SaveGame::kCurrentSaveVersion) {
        Logger::warn("SaveGame: 存档 {} 来自更新版本（version={} > 当前 {}），已拒绝加载",
                     path.string(), version, SaveGame::kCurrentSaveVersion);
        return SlotReadStatus::FutureVersion;
    }

    if (!doc.contains("snapshot") || !doc["snapshot"].is_object()) {
        Logger::error("SaveGame: 存档 {} 缺少 snapshot 对象，按损坏处理", path.string());
        return SlotReadStatus::Corrupt;
    }
    // 真存档的 snapshot 必为 SceneSerializer 产物（含 "objects" 数组键）；缺键视为损坏——
    // 否则 {"snapshot":{}} 这类非场景 JSON 会经 deserialize 清空场景为「合法空场景」
    if (!doc["snapshot"].contains("objects") || !doc["snapshot"]["objects"].is_array()) {
        Logger::error("SaveGame: 存档 {} 的 snapshot 缺少 objects 数组，按损坏处理", path.string());
        return SlotReadStatus::Corrupt;
    }
    outDoc = std::move(doc);
    return SlotReadStatus::Ok;
}

// 从已校验的存档根对象填元数据（scene/timestamp/playTimeSeconds，缺字段保持默认值）
SaveSlotInfo infoFromDoc(int slot, const nlohmann::json& doc) {
    SaveSlotInfo info;
    info.slot = slot;
    info.valid = true;
    if (doc.contains("scene") && doc["scene"].is_string())
        info.scenePath = doc["scene"].get<std::string>();
    if (doc.contains("timestamp") && doc["timestamp"].is_number())
        info.timestampSeconds = doc["timestamp"].get<double>();
    if (doc.contains("playTimeSeconds") && doc["playTimeSeconds"].is_number())
        info.playTimeSeconds = static_cast<float>(doc["playTimeSeconds"].get<double>());
    return info;
}

// 无效槽元数据（load 失败 / listSlots 浅校验不过时返回）
SaveSlotInfo invalidInfo(int slot) {
    return SaveSlotInfo{slot, false, {}, 0.0, 0.0f};
}

}  // namespace

void SaveGame::setSaveDirectory(const std::filesystem::path& dir) {
    s_saveDirectory = dir;
}

std::filesystem::path SaveGame::saveDirectory() {
    if (!s_saveDirectory.empty())
        return s_saveDirectory;   // 显式覆盖优先（如 %APPDATA%）
    return AssetDatabase::projectRoot() / "saves";   // 默认 <projectRoot>/saves
}

std::filesystem::path SaveGame::slotPath(int slot) {
    return saveDirectory() / std::format("slot{}.novasave", slot);
}

bool SaveGame::save(const Scene& scene, int slot, const std::string& scenePath,
                    float playTimeSeconds) {
    if (!validSlot(slot)) {
        Logger::error("SaveGame: 槽号 {} 非法（需 ≥ 0），保存失败", slot);
        return false;
    }

    // snapshot：复用 SceneSerializer::serialize（与 Play 快照同一代码路径）再 parse 回对象内嵌
    nlohmann::json snapshot;
    try {
        snapshot = nlohmann::json::parse(SceneSerializer::serialize(scene, /*indent=*/-1));
    } catch (const std::exception& e) {
        Logger::error("SaveGame: 场景序列化结果异常（{}），保存失败", e.what());
        return false;
    }

    nlohmann::json doc;
    doc["version"] = kCurrentSaveVersion;
    doc["scene"] = scenePath;
    doc["timestamp"] =
        std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    doc["playTimeSeconds"] = playTimeSeconds;
    doc["snapshot"] = std::move(snapshot);

    const std::filesystem::path dir = saveDirectory();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);   // 目录不存在自动创建；已存在 no-op
    if (ec) {
        Logger::error("SaveGame: 创建存档目录失败 {}（{}）", dir.string(), ec.message());
        return false;
    }

    // 原子写：先写 .tmp 再 rename 覆盖正式文件（中断只丢 tmp，正式存档不损坏）
    const std::filesystem::path finalPath = slotPath(slot);
    const std::filesystem::path tmpPath = dir / std::format("slot{}.novasave.tmp", slot);
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);   // path 重载（中文路径安全）
        if (!out.is_open()) {
            Logger::error("SaveGame: 无法创建临时存档 {}（只读目录/无权限？）", tmpPath.string());
            return false;
        }
        out << doc.dump(2);
        out.close();
        if (!out) {
            Logger::error("SaveGame: 写入临时存档 {} 出错（磁盘满/无权限？）", tmpPath.string());
            std::error_code ignored;
            std::filesystem::remove(tmpPath, ignored);
            return false;
        }
    }  // 先关流再 rename（Windows 下被占用的文件无法替换）
    std::filesystem::rename(tmpPath, finalPath, ec);
    if (ec) {
        Logger::error("SaveGame: 替换存档 {} 失败（{}）", finalPath.string(), ec.message());
        std::error_code ignored;
        std::filesystem::remove(tmpPath, ignored);   // 清理残留 tmp，失败可忽略
        return false;
    }
    Logger::info("SaveGame: 已保存槽 {} → {}", slot, finalPath.string());
    return true;
}

SaveSlotInfo SaveGame::load(Scene& target, int slot) {
    if (!validSlot(slot)) {
        Logger::error("SaveGame: 槽号 {} 非法（需 ≥ 0），加载失败", slot);
        return invalidInfo(slot);
    }
    const std::filesystem::path path = slotPath(slot);
    nlohmann::json doc;
    const SlotReadStatus status = readSlotFile(path, doc);
    if (status == SlotReadStatus::Missing) {
        Logger::warn("SaveGame: 槽 {} 存档不存在（{}），场景保持不变", slot, path.string());
        return invalidInfo(slot);
    }
    if (status != SlotReadStatus::Ok) {
        // 损坏/未来版本：readSlotFile 已记日志，场景保持不变
        return invalidInfo(slot);
    }

    // snapshot 已通过浅校验（JSON 对象），dump 回字符串交给 deserialize 原地重建。
    // 防御捕获（Reviewer 修复）：deserialize 内部已兜底畸形字段，但脚本 onAwake 等路径仍可能
    // 抛异常——契约「损坏存档不崩溃」。注意：重建中途抛异常时场景可能已部分修改，
    // 无法完整还原旧状态（与 LoadScene 同约束），至少保证不崩编辑器。
    try {
        SceneSerializer::deserialize(target, doc["snapshot"].dump());
    } catch (const std::exception& e) {
        Logger::error("SaveGame: 槽 {} 读档重建场景时异常（{}），场景可能已部分修改", slot, e.what());
        return invalidInfo(slot);
    }
    const SaveSlotInfo info = infoFromDoc(slot, doc);
    Logger::info("SaveGame: 已加载槽 {}（场景 {}，游戏时间 {:.1f}s）", slot, info.scenePath,
                 info.playTimeSeconds);
    return info;
}

std::vector<SaveSlotInfo> SaveGame::listSlots() {
    std::vector<SaveSlotInfo> result;
    const std::filesystem::path dir = saveDirectory();
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec), end;
    if (ec) return result;   // 目录不存在/不可读：空列表（首次使用还没有 saves 目录）

    for (; !ec && it != end; it.increment(ec)) {
        std::error_code fileEc;
        if (!it->is_regular_file(fileEc)) continue;   // 子目录/非常规文件（含读取错误）忽略

        const std::string filename = it->path().filename().string();
        int slot = 0;
        if (!parseSlotFromFilename(filename, slot)) continue;   // 非存档文件（如 .tmp 残留）忽略

        nlohmann::json doc;
        const SlotReadStatus status = readSlotFile(it->path(), doc);
        if (status == SlotReadStatus::Missing) continue;        // 扫描间隙被删：忽略
        if (status == SlotReadStatus::Ok)
            result.push_back(infoFromDoc(slot, doc));
        else
            result.push_back(invalidInfo(slot));                // 损坏/未来版本：保留条目并标 invalid
    }
    std::sort(result.begin(), result.end(),
              [](const SaveSlotInfo& a, const SaveSlotInfo& b) { return a.slot < b.slot; });
    return result;
}

bool SaveGame::deleteSlot(int slot) {
    if (!validSlot(slot)) {
        Logger::warn("SaveGame: 槽号 {} 非法（需 ≥ 0），删除失败", slot);
        return false;
    }
    const std::filesystem::path path = slotPath(slot);
    std::error_code ec;
    const bool removed = std::filesystem::remove(path, ec);
    if (ec || !removed) {
        Logger::warn("SaveGame: 删除存档 {} 失败（{}）", path.string(),
                     ec ? ec.message() : std::string("文件不存在"));
        return false;
    }
    // 顺带清理可能残留的 .tmp（上次原子写中断遗留），失败可忽略
    std::error_code ignored;
    std::filesystem::remove(path.string() + ".tmp", ignored);
    Logger::info("SaveGame: 已删除槽 {}（{}）", slot, path.string());
    return true;
}

bool SaveGame::slotExists(int slot) {
    if (!validSlot(slot)) return false;
    std::error_code ec;
    return std::filesystem::exists(slotPath(slot), ec);
}

}  // namespace NovaEngine
