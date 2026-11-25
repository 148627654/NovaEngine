#include "EditorSettings.h"

#include <NovaEngine/Core/Input.h>
#include <NovaEngine/Core/Logger.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <tuple>

namespace NovaEditor {

namespace {

// 核心快捷键静态表（动作 → 默认 chord）。动作集合固定不可移除（§28.2：核心快捷键
// Ctrl+Z/Ctrl+S/Ctrl+P 等保留不可移除）；chord 可重绑（冲突检测在 SettingsPanel）。
const std::vector<std::pair<std::string, std::string>>& defaultBindings() {
    static const std::vector<std::pair<std::string, std::string>> s = {
        {"Play/Stop", "Ctrl+P"},
        {"Save Scene", "Ctrl+S"},
        {"Undo", "Ctrl+Z"},
        {"Redo", "Ctrl+Y"},
        {"New Scene", "Ctrl+N"},
        {"Open Scene", "Ctrl+O"},
        {"Reload Scripts", "Ctrl+Shift+R"},
        {"Step", "F10"},
    };
    return s;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// 键名 → Key 枚举底层值；未知名返回 Key::Unknown（-1）
int keyCodeFromName(const std::string& name) {
    using NovaEngine::Key;
    const std::string n = toLower(name);
    if (n.size() == 1) {
        const char c = n[0];
        if (c >= 'a' && c <= 'z') return static_cast<int>(Key::A) + (c - 'a');
        if (c >= '0' && c <= '9') return static_cast<int>(Key::Num0) + (c - '0');
        return -1;
    }
    if (n == "space") return static_cast<int>(Key::Space);
    if (n == "escape" || n == "esc") return static_cast<int>(Key::Escape);
    if (n == "enter" || n == "return") return static_cast<int>(Key::Enter);
    if (n == "tab") return static_cast<int>(Key::Tab);
    if (n == "backspace") return static_cast<int>(Key::Backspace);
    if (n == "delete" || n == "del") return static_cast<int>(Key::Delete);
    if (n == "insert" || n == "ins") return static_cast<int>(Key::Insert);
    if (n == "home") return static_cast<int>(Key::Home);
    if (n == "end") return static_cast<int>(Key::End);
    if (n == "pageup") return static_cast<int>(Key::PageUp);
    if (n == "pagedown") return static_cast<int>(Key::PageDown);
    if (n == "up") return static_cast<int>(Key::Up);
    if (n == "down") return static_cast<int>(Key::Down);
    if (n == "left") return static_cast<int>(Key::Left);
    if (n == "right") return static_cast<int>(Key::Right);
    if (n.size() == 2 && n[0] == 'f' && n[1] >= '1' && n[1] <= '9')
        return static_cast<int>(Key::F1) + (n[1] - '1');
    if (n == "f10") return static_cast<int>(Key::F10);
    if (n == "f11") return static_cast<int>(Key::F11);
    if (n == "f12") return static_cast<int>(Key::F12);
    return -1;
}

}  // namespace

EditorSettings& EditorSettings::instance() {
    static EditorSettings s;
    return s;
}

std::filesystem::path EditorSettings::filePath() {
    const char* appData = std::getenv("APPDATA");
    if (appData && *appData)
        return std::filesystem::path(appData) / "NovaEngine" / "editor-settings.json";
    return "editor-settings.json";  // 极端兜底（Windows 恒有 APPDATA）
}

void EditorSettings::load() {
    using nlohmann::json;
    data = Data{};  // 回到默认基线，逐项覆盖
    // 快捷键静态表全量兜底（缺文件/缺项都保证核心动作有 chord）
    for (const auto& [action, defChord] : defaultBindings())
        data.keybindings[action] = defChord;
    std::ifstream f(filePath());
    if (!f) {
        // 首次启动：无文件 = 全默认，不打扰（不写盘，等首次变更再落盘）
        return;
    }
    json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        NovaEngine::Logger::warn("编辑器设置解析失败，使用默认值: {}（{}）",
                                 filePath().string(), e.what());
        return;
    }
    // P1-1：字段级解析整体包 try/catch——语义损坏（类型不匹配，如 {"version":"1"}、
    // snap.move 为字符串）抛 nlohmann::type_error 时回默认值 + warn，绝不崩溃。
    // 上面的语法层 try/catch 只包 f >> j；此处覆盖版本检查与全部字段读取。
    try {
        if (!j.is_object() || j.value("version", 0) != 1) {
            NovaEngine::Logger::warn("编辑器设置版本不符（期望 1），使用默认值: {}",
                                     filePath().string());
            return;
        }

        // 吸附
        if (j.contains("snap") && j["snap"].is_object()) {
            const auto& s = j["snap"];
            data.snapMove = s.value("move", 0.5f);
            data.snapRotate = s.value("rotate", 15.0f);
            data.snapScale = s.value("scale", 0.5f);
            data.snapMove = std::clamp(data.snapMove, 0.0001f, 10000.0f);
            data.snapRotate = std::clamp(data.snapRotate, 0.0f, 360.0f);
            data.snapScale = std::clamp(data.snapScale, 0.0001f, 10000.0f);
        }
        // 主题
        if (j.contains("theme") && j["theme"].is_string()) {
            const std::string t = j["theme"].get<std::string>();
            if (t == "dark" || t == "light" || t == "classic") data.theme = t;
            else NovaEngine::Logger::warn("未知主题名 \"{}\"，回退 dark", t);
        }
        // ImGuiStyle 自定义覆盖
        if (j.contains("style") && j["style"].is_object()) {
            const auto& st = j["style"];
            if (st.contains("colors") && st["colors"].is_array() &&
                st["colors"].size() == static_cast<size_t>(ImGuiCol_COUNT)) {
                data.styleColors = st["colors"].get<std::vector<uint32_t>>();
                data.styleCustom = true;
            }
            data.windowRounding = st.value("windowRounding", -1.0f);
            data.frameRounding = st.value("frameRounding", -1.0f);
        }
        // 快捷键：默认表已在 load 开头全量兜底，此处仅覆盖合法项；
        // P2-10：未知动作键丢弃（与「8 动作固定表」裁定对齐）
        if (j.contains("keybindings") && j["keybindings"].is_object()) {
            for (const auto& [action, chord] : j["keybindings"].items()) {
                if (!chord.is_string()) continue;
                if (!data.keybindings.contains(action)) {
                    NovaEngine::Logger::warn("快捷键动作 \"{}\" 不在核心表中，已丢弃", action);
                    continue;
                }
                bool c = false, s = false, a = false;
                int k = -1;
                const std::string chordStr = chord.get<std::string>();
                // P2-4：load 路径同样做规范化冲突检测（工具键/他动作冲突 → 默认值），
                // 与 SettingsPanel 重绑检测同源——手改设置文件不可绕过
                const std::string conflict = checkConflict(data, action, chordStr);
                if (parseChord(chordStr, c, s, a, k) && k >= 0 && conflict.empty())
                    data.keybindings[action] = chordStr;
                else
                    NovaEngine::Logger::warn("快捷键 \"{}\" = \"{}\" {}，使用默认值", action,
                                             chordStr,
                                             !conflict.empty() ? ("与 " + conflict + " 冲突").c_str()
                                                               : "非法");
            }
        }
        // 最近文件：存在性过滤（已删除文件不进列表）
        if (j.contains("recentFiles") && j["recentFiles"].is_array()) {
            for (const auto& item : j["recentFiles"]) {
                if (!item.is_string()) continue;
                std::error_code ec;
                const std::filesystem::path p(item.get<std::string>());
                if (!p.empty() && std::filesystem::exists(p, ec) && data.recentFiles.size() < 10)
                    data.recentFiles.push_back(p.string());
            }
        }
        // 多视口
        data.multiViewport = j.value("multiViewport", false);
    } catch (const std::exception& e) {
        NovaEngine::Logger::warn("编辑器设置字段解析失败（语义损坏），使用默认值: {}（{}）",
                                 filePath().string(), e.what());
        data = Data{};
        for (const auto& [action, defChord] : defaultBindings())
            data.keybindings[action] = defChord;
        return;
    }

    NovaEngine::Logger::info("编辑器设置已加载: {}（主题 {} / 最近 {} 个 / 多视口 {}）",
                             filePath().string(), data.theme, data.recentFiles.size(),
                             data.multiViewport ? "开" : "关");
}

void EditorSettings::save() const {
    using nlohmann::json;
    json j;
    j["version"] = 1;
    j["snap"] = {{"move", data.snapMove}, {"rotate", data.snapRotate}, {"scale", data.snapScale}};
    j["theme"] = data.theme;
    if (data.styleCustom) {
        j["style"] = {{"colors", data.styleColors},
                      {"windowRounding", data.windowRounding},
                      {"frameRounding", data.frameRounding}};
    }
    json kb = json::object();
    for (const auto& [action, chord] : data.keybindings) kb[action] = chord;
    j["keybindings"] = kb;
    j["recentFiles"] = data.recentFiles;
    j["multiViewport"] = data.multiViewport;

    // P1-1：tmp + rename 原子写——写一半崩溃/断电不损坏既有文件
    //（MSVC std::filesystem::rename = MoveFileExW(REPLACE_EXISTING)，同卷原子替换）
    std::error_code ec;
    std::filesystem::create_directories(filePath().parent_path(), ec);  // 已存在则 no-op
    const std::filesystem::path tmp = filePath().string() + ".tmp";
    {
        std::ofstream f(tmp);
        if (!f) {
            NovaEngine::Logger::warn("编辑器设置保存失败（无法打开临时文件）: {}",
                                     filePath().string());
            return;
        }
        f << j.dump(2) << std::endl;
        if (!f) {
            NovaEngine::Logger::warn("编辑器设置写盘失败: {}", filePath().string());
            std::filesystem::remove(tmp, ec);
            return;
        }
    }
    std::filesystem::rename(tmp, filePath(), ec);
    if (ec) {
        NovaEngine::Logger::warn("编辑器设置原子替换失败: {}（{}）", filePath().string(),
                                 ec.message());
        std::filesystem::remove(tmp, ec);
    }
}

const std::vector<std::pair<std::string, std::string>>& EditorSettings::defaultKeybindings() {
    return defaultBindings();
}

std::string EditorSettings::chordFor(const std::string& action) {
    const auto& kb = instance().data.keybindings;
    if (auto it = kb.find(action); it != kb.end()) return it->second;
    for (const auto& [name, defChord] : defaultBindings())
        if (name == action) return defChord;
    return {};
}

bool EditorSettings::parseChord(const std::string& chord, bool& ctrl, bool& shift, bool& alt,
                                int& key) {
    ctrl = shift = alt = false;
    key = -1;
    // "Ctrl+Shift+R"：按 '+' 切分，忽略空段（容错双空格）
    std::string token;
    auto flush = [&](std::string t) {
        t = toLower(std::move(t));
        if (t == "ctrl" || t == "control") ctrl = true;
        else if (t == "shift") shift = true;
        else if (t == "alt") alt = true;
        else key = keyCodeFromName(t);
    };
    for (char c : chord) {
        if (c == '+') { flush(token); token.clear(); }
        else if (c != ' ' && c != '\t') token.push_back(c);
    }
    flush(token);
    return key >= 0;
}

bool EditorSettings::chordMatches(const std::string& chord) {
    if (chord.empty()) return false;
    bool ctrl = false, shift = false, alt = false;
    int key = -1;
    if (!parseChord(chord, ctrl, shift, alt, key)) return false;
    using NovaEngine::Input;
    using NovaEngine::Key;
    const bool ctrlDown =
        Input::isKeyDown(Key::LeftControl) || Input::isKeyDown(Key::RightControl);
    const bool shiftDown =
        Input::isKeyDown(Key::LeftShift) || Input::isKeyDown(Key::RightShift);
    const bool altDown = Input::isKeyDown(Key::LeftAlt) || Input::isKeyDown(Key::RightAlt);
    return ctrlDown == ctrl && shiftDown == shift && altDown == alt &&
           Input::isKeyPressed(static_cast<Key>(key));
}

std::string EditorSettings::checkConflict(const Data& d, const std::string& action,
                                          const std::string& chord) {
    bool c = false, s = false, a = false;
    int k = -1;
    if (!parseChord(chord, c, s, a, k)) return "(非法 chord)";
    // P2-4：冲突比较基于解析结果（"Ctrl+P" 与 "Ctrl + P" 规范化后等价）而非原始字符串
    auto sameChord = [&](bool oc, bool os, bool oa, int ok) {
        return oc == c && os == s && oa == a && ok == k;
    };
    // 工具键冲突集合（不进设置表，但不可被重绑占用——否则与 gizmo/面板快捷键每帧双触发）
    using NovaEngine::Key;
    static const std::vector<std::tuple<bool, bool, bool, int>> kToolKeys = {
        {false, false, false, static_cast<int>(Key::W)},
        {false, false, false, static_cast<int>(Key::E)},
        {false, false, false, static_cast<int>(Key::R)},
        {false, false, false, static_cast<int>(Key::X)},
        {false, false, false, static_cast<int>(Key::F8)},
    };
    for (const auto& [tc, ts, ta, tk] : kToolKeys)
        if (sameChord(tc, ts, ta, tk)) return "W/E/R/X/F8（工具键）";
    for (const auto& [other, otherChord] : d.keybindings) {
        if (other == action) continue;
        bool oc = false, os = false, oa = false;
        int ok = -1;
        if (parseChord(otherChord, oc, os, oa, ok) && sameChord(oc, os, oa, ok)) return other;
    }
    return {};
}

void EditorSettings::applyTheme(const std::string& theme) {
    if (theme == "light") ImGui::StyleColorsLight();
    else if (theme == "classic") ImGui::StyleColorsClassic();
    else ImGui::StyleColorsDark();
}

void EditorSettings::applyStyleCustom(const Data& d) {
    if (!d.styleCustom || d.styleColors.size() != static_cast<size_t>(ImGuiCol_COUNT)) return;
    ImGuiStyle& style = ImGui::GetStyle();
    for (int i = 0; i < ImGuiCol_COUNT; ++i)
        style.Colors[i] = ImGui::ColorConvertU32ToFloat4(d.styleColors[i]);
    if (d.windowRounding >= 0.0f) style.WindowRounding = d.windowRounding;
    if (d.frameRounding >= 0.0f) style.FrameRounding = d.frameRounding;
}

void EditorSettings::captureStyle(Data& d) {
    ImGuiStyle& style = ImGui::GetStyle();
    d.styleColors.resize(ImGuiCol_COUNT);
    for (int i = 0; i < ImGuiCol_COUNT; ++i)
        d.styleColors[i] = ImGui::ColorConvertFloat4ToU32(style.Colors[i]);
    d.windowRounding = style.WindowRounding;
    d.frameRounding = style.FrameRounding;
    d.styleCustom = true;
}

}  // namespace NovaEditor
