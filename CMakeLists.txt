cmake_minimum_required(VERSION 3.24)
# C 语言一并启用：glad 的 cmake/CMakeLists.txt 内部声明 project(glad C)
project(NovaEngine VERSION 0.1.0 LANGUAGES C CXX)

# 第三方依赖（全部 FetchContent，见 cmake/Dependencies.cmake）
include(cmake/Dependencies.cmake)
# 本项目 target 统一编译选项（nova_configure_target，见 cmake/Common.cmake）
include(cmake/Common.cmake)

# 注意：samples/ 与 editor/ 的 CMakeLists.txt 由 Coder B / Coder C 提供，
# 三个模块合并后统一构建；缺少对应目录时 configure 会报错，属预期
add_subdirectory(engine)
add_subdirectory(samples)
add_subdirectory(editor)
add_subdirectory(player)
add_subdirectory(tests)  # 临时：PlayCycleTest（Play 进出往返层级回归，用后即删）
