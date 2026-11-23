#pragma once
#include <filesystem>

namespace NovaEditor {

// 资源收集 + 打包（V0.5-1 编辑器侧，ARCHITECTURE-FINAL 裁定 11 / §10.4）
// v1：单场景 —— 解析 .nova.json 收集引用资源 → PakArchive::pack 打包
// → 拷贝 NovaPlayer.exe。发布目录 = exe + pak；缺失资源打包即报错（不带到运行时）。
class Packager {
public:
    // 打包场景 sceneJsonPath（项目内相对或绝对路径）到输出目录：
    //   <输出目录>/<场景文件stem>.novapak + <输出目录>/NovaPlayer.exe
    // 成功返回 true；失败时缺失资源/错误原因已逐条写入 Logger，返回 false。
    static bool buildPlayer(const std::filesystem::path& sceneJsonPath,
                            const std::filesystem::path& outputDir);

    // 发布构建（V1.0-11）：buildPlayer 的发行级扩展（buildPlayer 保留，语义不变）
    // 收集 = build.novabuild 全部场景 + 全场景引用资产（AssetReferenceScanner 预检照常）+
    // 新资产形态（.novashadergraph/.cube/16bit 高度图）+ shaders + 字体 + 脚本 DLL
    // → 单 pak（v1 单 pak 多场景）→ 输出目录结构：
    //   <out>/NovaEngine_<版本>/NovaPlayer.exe + NovaEngine.pak + README.txt（生成：运行说明/最低配置）
    //   + docs/（拷贝）+ steam/（depotbuild 目录占位 + steam_appid.txt 占位 + STEAM-说明.txt）
    // 版本元数据写入 README（NOVA_PRODUCT/NOVA_VERSION，editor/CMakeLists.txt 编译期注入）。
    // 可复现：RC 元数据固定（无时间戳注入）+ pak 内 std::map 确定性（既有）→ 两次 buildRelease 产物字节一致。
    // 成功返回 true；失败时错误原因已逐条写入 Logger，返回 false。
    // 全部预检（build 清单/引用完整性/缺失资源/exe/dll 存在性）先于输出目录创建——
    // 预检失败不产出目录；产物写入阶段失败仅报告（残留目录可整体删除重跑）。
    static bool buildRelease(const std::filesystem::path& outputDir);
};

}  // namespace NovaEditor
