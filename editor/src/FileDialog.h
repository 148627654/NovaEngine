#pragma once
#include <string>

namespace NovaEditor {

// 原生文件选择框（2026-11-24 用户需求：路径字段可浏览选择）：
// Windows GetOpenFileNameA——无新依赖，模态于编辑器窗口。
// 返回 false = 用户取消；true = outPath 为所选文件的绝对路径。
// filter 形如 "LUT 文件 (*.cube)\0*.cube\0所有文件 (*.*)\0*.*\0"（调用方用 std::string 构造，
// 内部补双 \0 终止）。
// startDir 空 = 项目根（AssetDatabase::projectRoot()）。
bool browseOpenFile(std::string& outPath, const std::string& title,
                    const std::string& filter, const std::string& startDir);

// 绝对路径 → 项目内相对路径（用于存序列化字段的路径）；
// 项目外保持绝对（引擎 AssetStream 磁盘回退直读，与编辑器运行同语义）
std::string toProjectRelative(const std::string& absPath);

}  // namespace NovaEditor
