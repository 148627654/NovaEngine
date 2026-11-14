# NovaScripts：游戏脚本 OBJECT 库
# 用 OBJECT 库而非 STATIC：MSVC 链接器会丢弃「无引用」的静态库 .obj，
# 导致脚本静态注册代码从不执行；OBJECT 库的 .obj 直接并入 NovaEditor，
# 保证 NE_REGISTER_SCRIPT 静态初始化存活（见 docs/ARCHITECTURE.md §9.4）。
add_library(NovaScripts OBJECT
    SpinningCube.cpp
    OrbitCamera.cpp
)

target_include_directories(NovaScripts PUBLIC ${CMAKE_SOURCE_DIR}/engine/include)
target_link_libraries(NovaScripts PUBLIC NovaEngine)
nova_configure_target(NovaScripts)
