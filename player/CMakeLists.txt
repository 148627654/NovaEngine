# NovaPlayer：打包运行时播放器（§10.5；无 ImGui——引擎本就无 ImGui 依赖，解耦≈0）
# V0.5-7（NOVA_SCRIPTS_DLL=ON 默认）：脚本经 NovaGamePlayer.dll 运行时加载
#   （exe 目录/NovaGame.dll，Packager 拷贝）。exe 必须导出全部引擎符号供 DLL 内脚本类
#   引用（vtable/RTTI 绑定 exe 导出，每进程单一引擎实例）——由本文件末尾的
#   「引擎符号导出表」机制落地（ENABLE_EXPORTS + 从 NovaEngine.lib 生成 .def + /DEF）。
# 回滚（NOVA_SCRIPTS_DLL=OFF）：NovaScripts 以 OBJECT 库 .obj 直接并入（§9.4）。
add_executable(NovaPlayer
    src/main.cpp
)

if(NOVA_SCRIPTS_DLL)
    target_compile_definitions(NovaPlayer PRIVATE NOVA_SCRIPTS_DLL=1)
else()
    target_link_libraries(NovaPlayer PRIVATE
        $<TARGET_OBJECTS:NovaScripts>
    )
endif()

target_link_libraries(NovaPlayer PRIVATE
    NovaEngine
)

# 根治静态库自注册裁剪：NovaPlayer 不直引各组件符号时，MSVC 会裁剪 .obj
# 导致 NE_REGISTER_COMPONENT 静态注册器不执行（波 0 回归）。/WHOLEARCHIVE 强制
# 拉入 NovaEngine 全部 .obj——波 2-4 新增组件无需逐个维护 link-guard 列表；
# 同时保证下方导出表覆盖完整引擎符号面（脚本 DLL 引用依赖）
target_link_options(NovaPlayer PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:NovaEngine>")

# VS 调试运行目录 = 项目根：无 pak 时相对路径 assets/... 直接可用
set_target_properties(NovaPlayer PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

nova_configure_target(NovaPlayer)

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
    get_filename_component(_player_msvc_bin "${CMAKE_LINKER}" DIRECTORY)
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
    set(_player_def "${CMAKE_CURRENT_BINARY_DIR}/player_engine_exports_$<CONFIG>.def")
    add_custom_command(
        OUTPUT ${_player_def}
        COMMAND python ${CMAKE_CURRENT_BINARY_DIR}/gen_engine_exports.py
                "${_player_msvc_bin}/dumpbin.exe" "$<TARGET_FILE:NovaEngine>" "${_player_def}"
        DEPENDS NovaEngine
        COMMENT "Generating engine exports def (NovaEngine.lib)")
    add_custom_target(PlayerEngineExports ALL DEPENDS ${_player_def})
    add_dependencies(NovaPlayer PlayerEngineExports)
    set_target_properties(NovaPlayer PROPERTIES ENABLE_EXPORTS ON)
    target_link_options(NovaPlayer PRIVATE "/DEF:${_player_def}")
endif()
