# NovaPlayer：打包运行时播放器（§10.5；无 ImGui——引擎本就无 ImGui 依赖，解耦≈0）
# NovaScripts 以 OBJECT 库 .obj 直接并入：保证 NE_REGISTER_SCRIPT 静态注册不被链接器裁剪（§9.4）
add_executable(NovaPlayer
    src/main.cpp
)

target_link_libraries(NovaPlayer PRIVATE
    NovaEngine
    $<TARGET_OBJECTS:NovaScripts>
)

# VS 调试运行目录 = 项目根：无 pak 时相对路径 assets/... 直接可用
set_target_properties(NovaPlayer PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

nova_configure_target(NovaPlayer)
