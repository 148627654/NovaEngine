# NovaEngine 静态库
# engine/src 下全部 .cpp 自动收录：Core/Render 由 Coder A 实现，
# Scene/Assets/Scripting/Serialization 由 Coder B/C 实现。
# 新源文件直接放入对应子目录即可（无需改本文件，避免并行开发的合并冲突）。
file(GLOB_RECURSE NOVA_ENGINE_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(NovaEngine STATIC ${NOVA_ENGINE_SOURCES})

# 公共头目录：编辑器与 samples 一律 #include <NovaEngine/...>
target_include_directories(NovaEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# PUBLIC 链接：上层 target（editor/samples）自动继承全部依赖
target_link_libraries(NovaEngine PUBLIC
    glfw
    glad_gl_core_45
    glm::glm
    assimp
    nlohmann_json::nlohmann_json
    stb)

nova_configure_target(NovaEngine)
