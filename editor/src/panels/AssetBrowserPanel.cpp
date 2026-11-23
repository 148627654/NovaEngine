#include "AssetBrowserPanel.h"
#include "MaterialEditorWidgets.h"   // .novamat 编辑模态复用 Inspector 材质编辑控件（V0.3-6）

#include "../DragDrop.h"
#include "../EditorContext.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>   // InputText(std::string*) 包装

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/MaterialAsset.h>
#include <NovaEngine/Core/Logger.h>
#include <fstream>   // Shader Graph 资产创建（V1.0-5 波 2 合波接线）
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/MeshRenderer.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace NovaEditor {

namespace {

struct Entry {
    std::filesystem::path rel;  // 相对项目根
    bool isDir = false;
};

// 不区分大小写的文件名排序（目录在前由调用方保证）
bool byName(const Entry& a, const Entry& b) {
    std::string an = a.rel.filename().string(), bn = b.rel.filename().string();
    std::transform(an.begin(), an.end(), an.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(bn.begin(), bn.end(), bn.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return an < bn;
}

// .novamat 材质资产文件判定（扩展名不区分大小写）
bool isMaterialFile(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".novamat";
}

// V0.3-6：保存 .novamat 后重新解析场景内全部材质资产引用——
// MeshRenderer 持有的 shared_ptr 不随 reloadMaterialAsset 自动更新，这里显式换新，
// 引用物体下一帧提交即变色；重新加载失败保持旧引用（不打回内联）并 warn（Reviewer P2-8）。
void refreshSceneMaterialRefs(EditorContext& ctx) {
    if (!ctx.scene) return;
    ctx.scene->forEachObject([](NovaEngine::GameObject* go) {
        auto* mr = go->getComponent<NovaEngine::MeshRenderer>();
        if (!mr) return;
        if (mr->material.materialAssetPath.empty()) return;
        auto asset =
            NovaEngine::AssetDatabase::instance().getMaterialAsset(mr->material.materialAssetPath);
        if (asset) {
            mr->material.materialAsset = asset;
        } else {
            NovaEngine::Logger::warn("材质资产刷新失败，保持旧引用: {}",
                                     mr->material.materialAssetPath);
        }
    });
}

}  // namespace

AssetBrowserPanel::AssetBrowserPanel() : Panel("Assets") {}

void AssetBrowserPanel::onGui(EditorContext& ctx, EditorCamera&) {
    namespace fs = std::filesystem;
    ImGui::Begin(m_name.c_str(), &open);

    // 上报屏幕区域（外部拖放目标判定用）
    ctx.assetBrowserScreenMin = ImGui::GetWindowPos();
    ctx.assetBrowserScreenMax = ImGui::GetWindowPos() + ImGui::GetWindowSize();

    const fs::path root = NovaEngine::AssetDatabase::projectRoot();
    const fs::path full = root / m_currentDir;

    // 面包屑导航（assets > models > ...）
    if (ImGui::Button("assets")) m_currentDir = "assets";
    fs::path cursor;
    for (auto it = m_currentDir.begin(); it != m_currentDir.end(); ++it) {
        if (*it == "assets") {
            cursor = *it;
            continue;
        }
        cursor /= *it;
        ImGui::SameLine();
        ImGui::TextUnformatted(">");
        ImGui::SameLine();
        if (ImGui::Button(it->string().c_str())) m_currentDir = cursor;
    }
    ImGui::Separator();

    ImGui::BeginChild("##asset_entries", ImVec2(0.0f, 0.0f), false);
    std::error_code ec;
    if (!fs::exists(full, ec)) {
        ImGui::TextUnformatted("Directory does not exist: assets/");
    } else {
        std::vector<Entry> dirs, files;
        for (fs::directory_iterator it(full, ec), end; !ec && it != end; it.increment(ec)) {
            const fs::directory_entry& de = *it;
            // V0.3-5：隐藏点开头条目（.novacache 等）与贴图 .novameta 伴随文件
            const std::string fname = de.path().filename().string();
            if (!fname.empty() && fname.front() == '.') continue;
            std::string fl = fname;
            std::transform(fl.begin(), fl.end(), fl.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (fl.ends_with(".novameta")) continue;
            fs::path rel = fs::relative(de.path(), root, ec);
            if (ec) { ec.clear(); continue; }
            if (de.is_directory(ec)) dirs.push_back({rel, true});
            else if (de.is_regular_file(ec)) files.push_back({rel, false});
            ec.clear();
        }
        std::sort(dirs.begin(), dirs.end(), byName);
        std::sort(files.begin(), files.end(), byName);

        // 上级目录
        if (m_currentDir != "assets")
            if (ImGui::Selectable("[ .. ]", false))
                m_currentDir = m_currentDir.parent_path();

        auto drawEntry = [&](const Entry& e) {
            const std::string prefix =
                e.isDir ? "[D] "
                : DragDrop::isModelFile(e.rel) ? "[M] "
                : DragDrop::isTextureFile(e.rel) ? "[T] "
                : DragDrop::isSceneFile(e.rel) ? "[S] "
                : DragDrop::isPrefabFile(e.rel) ? "[PF] "  // 预制体资产（V0.3-9 波 4）
                : isMaterialFile(e.rel) ? "[MA] "      // 材质资产（V0.3-6）
                                        : "[F] ";
            const std::string label = prefix + e.rel.filename().string();
            ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", e.rel.string().c_str());
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (e.isDir) {
                        m_currentDir = e.rel;  // 双击目录进入
                    } else if (DragDrop::isSceneFile(e.rel)) {
                        // 双击 .nova.json 加载场景
                        if (ctx.state == EditorContext::EditorState::Edit) {
                            if (openSceneCallback) openSceneCallback(e.rel);
                        } else {
                            NovaEngine::Logger::warn("Play 模式下不可加载场景");
                        }
                    } else if (DragDrop::isPrefabFile(e.rel)) {
                        // 双击 .novaprefab（V0.3-9 波 4）：v1 不支持资产级实例编辑场景，仅提示不崩溃
                        NovaEngine::Logger::info("预制体资产（v1 不支持双击编辑，请拖入场景实例化后经 Inspector 的 Prefab 区 Apply）: {}",
                                                 e.rel.string());
                    } else if (isMaterialFile(e.rel)) {
                        // 双击 .novamat 打开材质编辑模态（V0.3-6）
                        if (ctx.state == EditorContext::EditorState::Edit) {
                            // 直读磁盘（不经 weak_ptr 缓存），保证模态编辑的是最新文件内容
                            auto asset = NovaEngine::MaterialAsset::load(e.rel);
                            if (!asset) {
                                NovaEngine::Logger::warn("材质资产加载失败: {}", e.rel.string());
                            } else {
                                m_matEditPath = e.rel;
                                m_matEditMaterial = asset->material();
                                m_matEditOpen = true;
                            }
                        } else {
                            NovaEngine::Logger::warn("Play 模式下不可编辑材质资产");
                        }
                    }
                }
            }

            // 模型/贴图/预制体文件为拖拽源（payload = 绝对路径 UTF-8 字符串，含结尾 \0）
            // V0.3-9 波 4 修复：.novaprefab 漏在此条件外导致拖拽源不启动（接收端早已支持）
            if (!e.isDir && (DragDrop::isModelFile(e.rel) || DragDrop::isTextureFile(e.rel) ||
                             DragDrop::isPrefabFile(e.rel))) {
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    const std::string abs = (root / e.rel).string();
                    ImGui::SetDragDropPayload(DragDrop::kAssetPayload, abs.c_str(), abs.size() + 1);
                    ImGui::TextUnformatted(e.rel.filename().string().c_str());
                    ImGui::EndDragDropSource();
                }
            }

            // 贴图右键菜单：Texture Import Settings…（V0.3-5 .novameta 编辑入口）
            if (!e.isDir && DragDrop::isTextureFile(e.rel)) {
                const std::string ctxId = "asset_ctx_" + e.rel.string();
                if (ImGui::BeginPopupContextItem(ctxId.c_str())) {
                    if (ImGui::MenuItem("Texture Import Settings...")) {
                        // 打开模态前 loadOrCreate 读取当前值（meta 缺失时自动生成默认并落盘）
                        const fs::path abs = root / e.rel;
                        bool metaExisted = false;
                        m_metaEditSettings = NovaEngine::TextureMeta::loadOrCreate(abs, metaExisted);
                        m_metaEditTexPath = abs;
                        m_metaEditOpen = true;
                        if (!metaExisted)
                            NovaEngine::Logger::info("已为贴图生成默认导入设置: {}",
                                                     e.rel.string() + ".novameta");
                    }
                    ImGui::EndPopup();
                }
            }
        };
        for (const auto& e : dirs) drawEntry(e);
        for (const auto& e : files) drawEntry(e);

        // 右键空白处菜单：Create → Material（V0.3-6；条目级右键菜单优先于窗口级）
        if (ImGui::BeginPopupContextWindow("asset_create_ctx")) {
            if (ImGui::BeginMenu("Create")) {
                if (ImGui::MenuItem("Material")) {
                    m_createMaterialOpen = true;
                    m_createMaterialName = "NewMaterial";
                }
                // V1.0-5 波 2 合波接线：新建空 Shader Graph 资产（version 1 空图，面板打开编辑）
                if (ImGui::MenuItem("Shader Graph")) {
                    std::error_code sgEc;
                    const fs::path sgDir = root / m_currentDir;
                    const fs::path sgTarget = sgDir / "NewShaderGraph.novashadergraph";
                    if (std::filesystem::exists(sgTarget, sgEc)) {
                        NovaEngine::Logger::warn("目标已存在: {}", sgTarget.generic_string());
                    } else {
                        std::ofstream out(sgTarget);
                        out << "{\"version\":1,\"nodes\":[],\"edges\":[]}\n";
                        if (out.good())
                            NovaEngine::Logger::info("已创建 Shader Graph 资产: {}", sgTarget.generic_string());
                        else
                            NovaEngine::Logger::error("创建 Shader Graph 资产失败: {}", sgTarget.generic_string());
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            // V1.0-7 波 1 合波接线：Reference Checker 入口（面板由 EditorApplication 注入回调打开）
            if (ImGui::MenuItem("Reference Checker..."))
                if (openReferenceChecker) openReferenceChecker();
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();

    // ---- 贴图导入设置模态（V0.3-5：colorSpace/wrap/filter/mipmaps + Save/Reset/Close；
    //      V1.0-6 追加：Compression 下拉 + Normal Map 用途勾选）----
    if (m_metaEditOpen) {
        using CS = NovaEngine::TextureImportSettings::ColorSpace;
        using TC = NovaEngine::TextureCompression;
        ImGui::OpenPopup("Texture Import Settings");
        ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
        bool stayOpen = true;
        if (ImGui::BeginPopupModal("Texture Import Settings", &stayOpen,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {
            // 标题 = 项目内相对路径（跨盘等异常时回退绝对路径）
            std::error_code rec;
            const fs::path rel =
                fs::relative(m_metaEditTexPath, NovaEngine::AssetDatabase::projectRoot(), rec);
            ImGui::TextUnformatted(
                rec ? m_metaEditTexPath.string().c_str() : rel.generic_string().c_str());
            ImGui::Separator();

            const char* csName =
                m_metaEditSettings.colorSpace == CS::Srgb ? "sRGB" : "Linear";
            // V1.0-6 合波日修复（评审 P2-5）：NormalMap 勾选期间锁定 Color Space——
            // 此前勾选后再改回 sRGB 会被下次 loadOrCreate 静默覆盖（isNormalMap 仍 true，
            // 读出重写强制 Linear）；锁定后不再出现被静默覆盖的交互路径
            const bool csLocked = m_metaEditSettings.isNormalMap;
            if (csLocked) ImGui::BeginDisabled();
            if (ImGui::BeginCombo("Color Space", csName)) {
                if (ImGui::Selectable("sRGB", m_metaEditSettings.colorSpace == CS::Srgb))
                    m_metaEditSettings.colorSpace = CS::Srgb;
                if (ImGui::Selectable("Linear", m_metaEditSettings.colorSpace == CS::Linear))
                    m_metaEditSettings.colorSpace = CS::Linear;
                ImGui::EndCombo();
            }
            if (csLocked) ImGui::EndDisabled();

            // V1.0-6：法线用途勾选（.novameta type "NormalMap" 往返保真——修复旧版
            //「NormalMap 读出后 Save 重写为 Linear」的往返有损，KNOWN-ISSUES #5；
            // Auto 压缩策略据此选 BC5）。勾选即按线性采样（NormalMap 语义）
            {
                bool normalMap = m_metaEditSettings.isNormalMap;
                if (ImGui::Checkbox("Normal Map", &normalMap)) {
                    m_metaEditSettings.isNormalMap = normalMap;
                    if (normalMap) m_metaEditSettings.colorSpace = CS::Linear;
                }
            }

            // V1.0-6：压缩格式下拉（Auto/None/BC1/BC7/BC5 + 策略说明）
            const char* compName =
                m_metaEditSettings.compression == TC::Auto   ? "Auto"
                : m_metaEditSettings.compression == TC::None ? "None"
                : m_metaEditSettings.compression == TC::BC1  ? "BC1 (DXT1)"
                : m_metaEditSettings.compression == TC::BC7  ? "BC7 (BPTC)"
                                                             : "BC5 (RGTC2)";
            if (ImGui::BeginCombo("Compression", compName)) {
                if (ImGui::Selectable("Auto", m_metaEditSettings.compression == TC::Auto))
                    m_metaEditSettings.compression = TC::Auto;
                if (ImGui::Selectable("None", m_metaEditSettings.compression == TC::None))
                    m_metaEditSettings.compression = TC::None;
                if (ImGui::Selectable("BC1 (DXT1)", m_metaEditSettings.compression == TC::BC1))
                    m_metaEditSettings.compression = TC::BC1;
                if (ImGui::Selectable("BC7 (BPTC)", m_metaEditSettings.compression == TC::BC7))
                    m_metaEditSettings.compression = TC::BC7;
                if (ImGui::Selectable("BC5 (RGTC2)", m_metaEditSettings.compression == TC::BC5))
                    m_metaEditSettings.compression = TC::BC5;
                ImGui::EndCombo();
            }
            ImGui::TextDisabled(
                "Auto: 法线贴图 BC5 / 含 alpha BC7 / 其余 BC1；GPU 不支持自动回退");

            const char* wrapName = m_metaEditSettings.wrapMode == NovaEngine::TextureWrap::Repeat
                                       ? "Repeat"
                                       : "Clamp To Edge";
            if (ImGui::BeginCombo("Wrap Mode", wrapName)) {
                if (ImGui::Selectable("Repeat",
                                      m_metaEditSettings.wrapMode == NovaEngine::TextureWrap::Repeat))
                    m_metaEditSettings.wrapMode = NovaEngine::TextureWrap::Repeat;
                if (ImGui::Selectable("Clamp To Edge",
                                      m_metaEditSettings.wrapMode ==
                                          NovaEngine::TextureWrap::ClampToEdge))
                    m_metaEditSettings.wrapMode = NovaEngine::TextureWrap::ClampToEdge;
                ImGui::EndCombo();
            }

            const char* filterName =
                m_metaEditSettings.filterMode == NovaEngine::TextureFilter::Bilinear
                    ? "Bilinear"
                    : "Trilinear";
            if (ImGui::BeginCombo("Filter Mode", filterName)) {
                if (ImGui::Selectable(
                        "Bilinear",
                        m_metaEditSettings.filterMode == NovaEngine::TextureFilter::Bilinear))
                    m_metaEditSettings.filterMode = NovaEngine::TextureFilter::Bilinear;
                if (ImGui::Selectable(
                        "Trilinear",
                        m_metaEditSettings.filterMode == NovaEngine::TextureFilter::Trilinear))
                    m_metaEditSettings.filterMode = NovaEngine::TextureFilter::Trilinear;
                ImGui::EndCombo();
            }

            ImGui::Checkbox("Generate Mipmaps", &m_metaEditSettings.generateMipmaps);

            ImGui::Separator();
            if (ImGui::Button("Save", ImVec2(120, 0))) {
                if (NovaEngine::TextureMeta::save(m_metaEditTexPath, m_metaEditSettings)) {
                    NovaEngine::AssetDatabase::instance().clearCache();  // meta 变化 → 缓存失效重导
                    NovaEngine::Logger::info("贴图导入设置已保存: {}.novameta",
                                             m_metaEditTexPath.string());
                    m_metaEditOpen = false;
                } else {
                    NovaEngine::Logger::error("贴图导入设置保存失败: {}",
                                              m_metaEditTexPath.string());
                }
            }
            ImGui::SameLine();
            // 字段恢复默认（不落盘；确认生效需再点 Save）
            if (ImGui::Button("Reset to Default", ImVec2(140, 0)))
                m_metaEditSettings = NovaEngine::TextureImportSettings{};
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(80, 0))) m_metaEditOpen = false;
            ImGui::EndPopup();
        }
        if (!stayOpen && m_metaEditOpen) m_metaEditOpen = false;
    }

    // ---- 新建材质资产模态（V0.3-6：输入名 → 写默认 .novamat 落盘；列表每帧重扫自动刷新）----
    if (m_createMaterialOpen) {
        ImGui::OpenPopup("Create Material");
        bool stayOpen = true;
        if (ImGui::BeginPopupModal("Create Material", &stayOpen,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {
            ImGui::TextUnformatted("Name");
            ImGui::SetNextItemWidth(320.0f);
            ImGui::InputText("##material_name", &m_createMaterialName);
            ImGui::Separator();
            if (ImGui::Button("Create", ImVec2(120, 0))) {
                if (m_createMaterialName.empty()) {
                    NovaEngine::Logger::warn("材质资产名不能为空");
                } else {
                    // 已带扩展名则原样使用，否则补 .novamat
                    std::string name = m_createMaterialName;
                    if (!name.ends_with(".novamat")) name += ".novamat";
                    const fs::path rel = m_currentDir / name;
                    std::error_code existEc;
                    if (fs::exists(root / rel, existEc)) {
                        NovaEngine::Logger::warn("材质资产已存在: {}", rel.string());
                    } else if (NovaEngine::MaterialAsset::save(rel, NovaEngine::Material{})) {
                        NovaEngine::Logger::info("材质资产已创建: {}", rel.string());
                        m_createMaterialOpen = false;
                    }
                    // 写失败原因已由 MaterialAsset::save 记录
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80, 0))) m_createMaterialOpen = false;
            ImGui::EndPopup();
        }
        if (!stayOpen && m_createMaterialOpen) m_createMaterialOpen = false;
    }

    // ---- .novamat 编辑模态（V0.3-6：复用 Inspector 材质编辑控件；Save 落盘 + 刷新引用）----
    if (m_matEditOpen) {
        ImGui::OpenPopup("Edit Material Asset");
        ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Appearing);
        bool stayOpen = true;
        if (ImGui::BeginPopupModal("Edit Material Asset", &stayOpen,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {
            ImGui::TextUnformatted(m_matEditPath.string().c_str());
            ImGui::Separator();
            // 纯编辑不置脏（markDirty=false）：Save 才统一落盘 + reload + 刷新引用 + 置脏
            drawMaterialEditor(m_matEditMaterial, ctx, /*markDirty=*/false);
            ImGui::Separator();
            if (ImGui::Button("Save", ImVec2(120, 0))) {
                if (NovaEngine::MaterialAsset::save(m_matEditPath, m_matEditMaterial)) {
                    NovaEngine::AssetDatabase::instance().reloadMaterialAsset(m_matEditPath);
                    refreshSceneMaterialRefs(ctx);   // 引用物体下一帧提交即变色
                    ctx.markSceneDirty();
                    NovaEngine::Logger::info("材质资产已保存: {}", m_matEditPath.string());
                    m_matEditOpen = false;
                } else {
                    NovaEngine::Logger::error("材质资产保存失败: {}", m_matEditPath.string());
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(80, 0))) m_matEditOpen = false;
            ImGui::EndPopup();
        }
        if (!stayOpen && m_matEditOpen) m_matEditOpen = false;
    }

    ImGui::End();
}

}  // namespace NovaEditor
