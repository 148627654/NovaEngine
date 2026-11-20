#pragma once
#include "../Panel.h"

#include <string>
#include <vector>

namespace NovaEditor {

// 引用完整性检查面板（V1.0-7，§26.4）：
//   - Scan Project：AssetReferenceScanner::scanProject 全项目扫描 → 缺失清单
//     （kind / 缺失路径 / 引用方文件；双击引用方以系统默认程序打开定位）
//   - Move Asset 对话框：移动/重命名资产 + 全项目引用重链（确认模态，成功后自动复扫）
// 不 dock（合波日接线）。安全纪律（§26.4 注意）：重写引用方文件是「编辑资产文件」——
// 必须先扫后修、用户确认后执行；本面板只经确认模态按钮触发 moveAsset，不静默改文件。
class ReferenceCheckerPanel : public Panel {
public:
    explicit ReferenceCheckerPanel();
    void onGui(EditorContext& ctx, EditorCamera& camera) override;

private:
    struct MissingEntry {
        std::string kind;      // 缺失引用类型（AssetReferenceScanner kind 常量）
        std::string path;      // 缺失路径（项目相对，正斜杠）
        std::string fromFile;  // 引用方文件（项目相对）
    };

    void runScan();
    void runMoveAsset(const std::string& from, const std::string& to);

    std::vector<MissingEntry> m_missing;
    size_t m_totalRefs = 0;
    bool m_scanned = false;

    // Move Asset 模态状态
    bool m_showMoveDialog = false;
    char m_moveFrom[512] = {};
    char m_moveTo[512] = {};
    std::string m_lastMoveResult;
};

}  // namespace NovaEditor
