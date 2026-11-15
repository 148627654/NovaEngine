#pragma once
#include "../Panel.h"

#include <NovaEngine/Assets/TextureMeta.h>
#include <NovaEngine/Render/Material.h>

#include <filesystem>
#include <functional>
#include <string>

namespace NovaEditor {

// 资源浏览器（§6.5）：std::filesystem 遍历 assets/、面包屑导航、
// 双击目录进入、双击 .nova.json 加载场景、模型/贴图文件为拖拽源（kAssetPayload）
// V0.3-5：隐藏点开头条目与 .novameta 伴随文件；贴图右键「Texture Import Settings…」模态编辑
// V0.3-6：右键「Create → Material」新建 .novamat；双击 .novamat 打开材质编辑模态
class AssetBrowserPanel : public Panel {
public:
    explicit AssetBrowserPanel();
    void onGui(EditorContext& ctx, EditorCamera& camera) override;

    // 由 EditorApplication 注入（双击场景文件时回调）
    std::function<void(const std::filesystem::path&)> openSceneCallback;

private:
    std::filesystem::path m_currentDir = "assets";  // 相对项目根

    // 贴图导入设置（.novameta）编辑模态状态（V0.3-5）
    std::filesystem::path m_metaEditTexPath;             // 正在编辑的贴图绝对路径；空 = 无模态
    bool m_metaEditOpen = false;
    NovaEngine::TextureImportSettings m_metaEditSettings;

    // 材质资产（.novamat）创建/编辑模态状态（V0.3-6）
    bool m_createMaterialOpen = false;                   // 创建模态（输入名）
    std::string m_createMaterialName = "NewMaterial";
    std::filesystem::path m_matEditPath;                 // 正在编辑的 .novamat 相对路径；空 = 无模态
    bool m_matEditOpen = false;
    NovaEngine::Material m_matEditMaterial;              // 编辑缓冲（Save 时落盘）
};

}  // namespace NovaEditor
