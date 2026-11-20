# NovaTests —— 无头单元测试（V1.0-9，Catch2 v3 + ctest 集成）
#
# 无 GL 上下文纪律（§26.1）：测试进程不 init GL——Mesh 上传路径靠 §26.0 骨架日
# GL 守卫（Renderer::isInitialized 未就绪 → Mesh::create 返回 nullptr + warn）自动降级；
# 场景往返测试用含 MeshRenderer 的场景 JSON，材质/meshPath 字段语义照常验证。
#
# NOVA_TEST_SOURCE_DIR：仓库根绝对路径（编译期注入），供资产依赖测试
#（V05UITest 真实场景往返）设置 AssetDatabase::setProjectRoot，CI/本机均可用。

enable_testing()

file(GLOB NOVA_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(NovaTests ${NOVA_TEST_SOURCES})
target_link_libraries(NovaTests PRIVATE NovaEngine Catch2::Catch2WithMain)
target_compile_definitions(NovaTests PRIVATE NOVA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
nova_configure_target(NovaTests)

# ctest 注册：构建期自动枚举用例（每个 Catch2 测试用例一个 ctest 条目，失败单点可查）
include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests(NovaTests)
