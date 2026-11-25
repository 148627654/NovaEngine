# NovaTests —— 无头单元测试（V1.0-9，Catch2 v3 + ctest 集成）
#
# 无 GL 上下文纪律（§26.1）：测试进程不 init GL——Mesh 上传路径靠 §26.0 骨架日
# GL 守卫（Renderer::isInitialized 未就绪 → Mesh::create 返回 nullptr + warn）自动降级；
# 场景往返测试用含 MeshRenderer 的场景 JSON，材质/meshPath 字段语义照常验证。
#
# NOVA_TEST_SOURCE_DIR：仓库根绝对路径（编译期注入），供资产依赖测试
#（V05UITest 真实场景往返）设置 AssetDatabase::setProjectRoot，CI/本机均可用。
#
# V1.0-13 追加（§28.1 用例③④）：NovaTestScripts 测试脚本 DLL（热重载/FreeLibrary 语义）。
# 与 NovaGame↔NovaEditor 同构——NovaTests 经 ENABLE_EXPORTS + 引擎 .def 导出全部引擎符号，
# DLL 链接 NovaTests 导入库绑定「加载它的 exe」（§25.5 符号方向，引擎代码绝不落入 DLL）。

enable_testing()

file(GLOB NOVA_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(NovaTests ${NOVA_TEST_SOURCES})
target_link_libraries(NovaTests PRIVATE NovaEngine Catch2::Catch2WithMain)
target_compile_definitions(NovaTests PRIVATE NOVA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
nova_configure_target(NovaTests)

# ============================================================
# V1.0-13 引擎符号导出表（测试脚本 DLL 引用依赖；editor/CMakeLists.txt §V0.5-7 同款）
# WINDOWS_EXPORT_ALL_SYMBOLS 只扫 target 自身 .obj，不扫静态库成员 → 用 dumpbin 从
# NovaEngine.lib 生成去重 .def（Python 单遍线性解析，仅标准库）。
# /WHOLEARCHIVE 强制拉入 NovaEngine 全部 .obj：完整引擎符号面随 exe 导出，
# 脚本 DLL 的引擎引用经导出表绑定到本 exe（与 NovaEditor 一致）。
# ENABLE_EXPORTS 必须先于 NovaTestScripts 的 target_link_libraries(NovaTests ...)
# 设置（CMake 校验：exe 作为链接库要求已声明导出）。
# ============================================================
if(MSVC)
    set_target_properties(NovaTests PROPERTIES ENABLE_EXPORTS ON)
    target_link_options(NovaTests PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:NovaEngine>")
    get_filename_component(_tests_msvc_bin "${CMAKE_LINKER}" DIRECTORY)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gen_tests_exports.py [=[
import re
import subprocess
import sys

MANGLE = re.compile(r"[A-Za-z0-9_?.@$]+")   # 合法符号名：拒绝中文注解被误切产生的碎片 token

def main():
    if len(sys.argv) != 4:
        print("usage: gen_tests_exports.py <dumpbin> <lib> <out>", file=sys.stderr)
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
    set(_tests_def "${CMAKE_CURRENT_BINARY_DIR}/tests_engine_exports_$<CONFIG>.def")
    add_custom_command(
        OUTPUT ${_tests_def}
        COMMAND python ${CMAKE_CURRENT_BINARY_DIR}/gen_tests_exports.py
                "${_tests_msvc_bin}/dumpbin.exe" "$<TARGET_FILE:NovaEngine>" "${_tests_def}"
        DEPENDS NovaEngine
        COMMENT "Generating engine exports def (NovaTests, NovaEngine.lib)")
    add_custom_target(NovaTestsEngineExports ALL DEPENDS ${_tests_def})
    add_dependencies(NovaTests NovaTestsEngineExports)
    target_link_options(NovaTests PRIVATE "/DEF:${_tests_def}")
endif()

# ============================================================
# V1.0-13 测试脚本 DLL（热重载审计；§25.5 符号方向复刻 editor/NovaGame 形态）
# NovaTestScripts → NovaTests 单向链接（NovaTests 不回依赖 DLL——CMake 禁止
# exe/SHARED 构建图环路，NovaGame↔NovaEditor 同构）。CI 全量构建 ALL 目标覆盖
# DLL；本地仅构建 NovaTests 时用例③④因 DLL 缺失红（LoadLibrary 失败日志可见），
# 需要热重载用例时构建 NovaTests + NovaTestScripts 两 target。
# ============================================================
add_library(NovaTestScripts SHARED script_dll/NovaTestScript.cpp)
target_include_directories(NovaTestScripts PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)
# 链接 NovaTests 导入库（引擎符号绑定 NovaTests.exe 导出）；glm/json 仅头文件接口目标
target_link_libraries(NovaTestScripts PRIVATE NovaTests glm::glm nlohmann_json::nlohmann_json)
nova_configure_target(NovaTestScripts)
# 编译期注入 DLL 绝对路径（用例③④ setDllPath 用）
target_compile_definitions(NovaTests PRIVATE
    "NOVA_TEST_SCRIPT_DLL=\"$<TARGET_FILE:NovaTestScripts>\"")

# ctest 注册：构建期自动枚举用例（每个 Catch2 测试用例一个 ctest 条目，失败单点可查）
include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests(NovaTests)
