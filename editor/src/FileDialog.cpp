#include "FileDialog.h"

#include <NovaEngine/Assets/AssetDatabase.h>

// windows.h 放最前（NOMINMAX 防 min/max 宏污染）；本文件不含 glfw 头，无 APIENTRY 冲突问题
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

#include <filesystem>
#include <vector>

namespace NovaEditor {

bool browseOpenFile(std::string& outPath, const std::string& title,
                    const std::string& filter, const std::string& startDir) {
    // 筛选串要求双 \0 终止（多组描述+模式对）；调用方传入含单 \0 的字符串，此处补终止
    std::vector<char> filterBuf(filter.begin(), filter.end());
    filterBuf.push_back('\0');
    filterBuf.push_back('\0');

    std::vector<char> fileBuf(2048, '\0');

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrTitle = title.c_str();
    ofn.lpstrFilter = filterBuf.data();
    ofn.lpstrFile = fileBuf.data();
    ofn.nMaxFile = static_cast<DWORD>(fileBuf.size());
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!startDir.empty()) {
        ofn.lpstrInitialDir = startDir.c_str();
        ofn.Flags |= OFN_DONTADDTORECENT;
    }

    if (GetOpenFileNameA(&ofn) == FALSE) {
        // 用户取消（CommDlgExtendedError 0）或错误——统一视为未选择，不打断编辑流程
        return false;
    }
    outPath = ofn.lpstrFile;
    return true;
}

std::string toProjectRelative(const std::string& absPath) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root = NovaEngine::AssetDatabase::projectRoot();
    const fs::path rel = fs::relative(fs::path(absPath), root, ec);
    if (!ec && !rel.empty() && rel.native()[0] != '.')
        return rel.generic_string();
    return absPath;   // 项目外 → 保持绝对（AssetStream 磁盘回退直读）
}

}  // namespace NovaEditor
