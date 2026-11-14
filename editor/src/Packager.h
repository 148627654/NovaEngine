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
};

}  // namespace NovaEditor
