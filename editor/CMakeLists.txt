# NovaEditor：编辑器可执行程序（§9.4）
# - ImGui/ImGuizmo 源码直接编进本 target（绕开其自身 CMake 联动问题）
# - NOVA_SCRIPTS_DLL=ON（V0.5-7 默认）：脚本走 NovaGame.dll 运行时加载 + 热重载；
#   引擎符号导出表（文末「V0.5-7 引擎符号导出表」段：ENABLE_EXPORTS + 从 NovaEngine.lib
#   生成 .def + /DEF）供脚本 DLL 引用绑定编辑器导出；/WHOLEARCHIVE 保证完整引擎符号面在 exe 内
# - NOVA_SCRIPTS_DLL=OFF（回滚）：NovaScripts 以 OBJECT 库 .obj 直接并入
# - 保持控制台子系统（stdout 日志可见，§11.11）

add_executable(NovaEditor
    src/main.cpp
    src/EditorApplication.cpp
    src/EditorContext.cpp
    src/EditorCamera.cpp
    src/GizmoController.cpp
    src/DragDrop.cpp
    src/UndoRedoManager.cpp
    src/PrefabManager.cpp   # DevOps 补入：C 线波 4 新文件，漏入显式源清单 → LNK2019（9 个 PrefabManager 符号）
    src/Packager.cpp
    src/panels/SceneViewPanel.cpp
    src/panels/HierarchyPanel.cpp
    src/panels/InspectorPanel.cpp
    src/panels/EnvironmentPanel.cpp
    src/panels/ConsolePanel.cpp
    src/panels/AssetBrowserPanel.cpp
    src/panels/StatsPanel.cpp
    src/panels/BuildSettingsPanel.cpp
    src/panels/InputBindingsPanel.cpp   # V0.5-11：输入绑定面板（整合者补入显式源清单）
    src/panels/TerrainPanel.cpp         # V0.5-8：地形面板（c6；dock 接线归合波日 c0）
    src/panels/AnimatorPanel.cpp   # V0.5-9：动画状态机面板（c7 动画；接线归合波日）
    # Dear ImGui（docking）核心源码
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    # ImGui 1.90+ 移除了 InputText(std::string*) 内建重载，用官方 stdlib 包装补回
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    # ImGuizmo（1.10 起源码位于 src/ 子目录，仓库根无 ImGuizmo.cpp）
    ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp
)

target_include_directories(NovaEditor PRIVATE
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imguizmo_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/samples
)

# imgui_impl_opengl3 用 glad 加载 GL；ImGuizmo 需要 ImVec2 运算符
target_compile_definitions(NovaEditor PRIVATE
    IMGUI_IMPL_OPENGL_LOADER_GLAD
    IMGUI_DEFINE_MATH_OPERATORS
    # 编译期注入项目根：main.cpp 启动时切工作目录用（双击 exe 时 cwd 是 exe 目录）
    "NOVA_PROJECT_ROOT=\"${CMAKE_SOURCE_DIR}\""
    # 编译期注入 NovaPlayer.exe 构建产物路径（player target 由引擎线提供；$<CONFIG> 适配 VS 多配置）
    "NOVA_PLAYER_EXE_PATH=\"${CMAKE_BINARY_DIR}/player/$<CONFIG>/NovaPlayer.exe\""
    # V0.5-7：脚本 DLL 热重载（§25.5 编译期注入）
    "NOVA_GAME_DLL_PATH=\"${CMAKE_BINARY_DIR}/samples/$<CONFIG>/NovaGame.dll\""
    "NOVA_BUILD_CONFIG=\"$<CONFIG>\""
)
if(NOVA_SCRIPTS_DLL)
    target_compile_definitions(NovaEditor PRIVATE
        NOVA_SCRIPTS_DLL=1
        # Packager 拷贝用：Player 形态脚本 DLL 构建产物路径
        "NOVA_GAME_PLAYER_DLL_PATH=\"${CMAKE_BINARY_DIR}/player/$<CONFIG>/NovaGame.dll\""
    )
endif()

# NovaEngine PUBLIC 链接 glfw/glad/glm/assimp/json/stb，编辑器和 imgui 源码自动继承其 include 目录
if(NOVA_SCRIPTS_DLL)
    target_link_libraries(NovaEditor PRIVATE
        NovaEngine
        # Windows 原生对话框：commdlg（打开/保存场景）+ IFileDialog（输出目录选择，shell32/ole32）
        comdlg32
        shell32
        ole32
    )
    # V0.5-7：/WHOLEARCHIVE 强制拉入 NovaEngine 全部 .obj（完整引擎符号面随 exe 导出，
    # 脚本 DLL 的引擎引用经 WINDOWS_EXPORT_ALL_SYMBOLS 绑定到本 exe）
    target_link_options(NovaEditor PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:NovaEngine>")
else()
    target_link_libraries(NovaEditor PRIVATE
        NovaEngine
        $<TARGET_OBJECTS:NovaScripts>
        # Windows 原生对话框：commdlg（打开/保存场景）+ IFileDialog（输出目录选择，shell32/ole32）
        comdlg32
        shell32
        ole32
    )
endif()

# VS 调试运行目录 = 项目根：相对路径 assets/... 直接可用
set_target_properties(NovaEditor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

nova_configure_target(NovaEditor)

# ============================================================
# V0.5-7 引擎符号导出表（脚本 DLL 引用依赖）
# WINDOWS_EXPORT_ALL_SYMBOLS 只扫 target 自身 .obj，不扫静态库成员（CMake 文档限制，
# 已实测：objects.txt 仅含本 target .obj）→ 用 dumpbin 从 NovaEngine.lib 生成去重 .def。
# dumpbin 全库输出数百 MB：CMake 逐行解析 >5 分钟，改用本机 Python 单遍线性解析 <10s
# （仅标准库，无新增项目依赖）。自定义命令按 .lib 时间戳缓存：引擎不变不重跑。
# 解析规则（dumpbin /symbols）：
#   <idx> <value> <SECT> <type> <storage> | <name> [类型注解]
#   - 仅取含 External 的行；SECT=UNDEF 的未定义引用（导入）跳过
#   - 数据符号：type 列无 "()"（函数符号为 "notype ()"）→ 追加 DATA 关键字
#   - "deleting destructor" 的 thunk 被 /OPT:REF 折叠，导出触发 LNK4102；删除一律发生在
#     exe 侧，DLL 无需导入 → 跳过
# ============================================================
if(NOVA_SCRIPTS_DLL)
    get_filename_component(_editor_msvc_bin "${CMAKE_LINKER}" DIRECTORY)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gen_engine_exports.py [=[
import re
import subprocess
import sys

MANGLE = re.compile(r"[A-Za-z0-9_?.@$]+")   # 合法符号名：拒绝中文注解被误切产生的碎片 token

def main():
    if len(sys.argv) != 4:
        print("usage: gen_engine_exports.py <dumpbin> <lib> <out>", file=sys.stderr)
        return 1
    dumpbin, lib, out = sys.argv[1], sys.argv[2], sys.argv[3]
    r = subprocess.run([dumpbin, "/symbols", lib], capture_output=True, text=True,
                       errors="replace")
    if r.returncode != 0:
        print("dumpbin failed:", r.stderr[-500:], file=sys.stderr)
        return 1
    names = set()
    for line in r.stdout.splitlines():
        if "External" not in line or "UNDEF" in line:
            continue
        if "deleting destructor" in line:
            continue
        pipe = line.rfind("|")
        if pipe < 0:
            continue
        rest = line[pipe + 1:].strip()
        name = rest.split()[0] if rest else ""
        if not MANGLE.fullmatch(name):
            continue
        if "()" not in line[:pipe]:
            name += " DATA"
        names.add(name)
    with open(out, "w", newline="\n") as f:
        f.write("EXPORTS\n\t" + "\n\t".join(sorted(names)) + "\n")
    print(f"engine exports def: {out} ({len(names)} symbols)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
]=])
    set(_editor_def "${CMAKE_CURRENT_BINARY_DIR}/editor_engine_exports_$<CONFIG>.def")
    add_custom_command(
        OUTPUT ${_editor_def}
        COMMAND python ${CMAKE_CURRENT_BINARY_DIR}/gen_engine_exports.py
                "${_editor_msvc_bin}/dumpbin.exe" "$<TARGET_FILE:NovaEngine>" "${_editor_def}"
        DEPENDS NovaEngine
        COMMENT "Generating engine exports def (NovaEngine.lib)")
    add_custom_target(EditorEngineExports ALL DEPENDS ${_editor_def})
    add_dependencies(NovaEditor EditorEngineExports)
    set_target_properties(NovaEditor PROPERTIES ENABLE_EXPORTS ON)
    target_link_options(NovaEditor PRIVATE "/DEF:${_editor_def}")
endif()
