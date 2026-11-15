# ============================================================
# NovaEngine 第三方依赖：全部 FetchContent
# GIT_TAG 已逐一验证存在（2026-11-14），见 docs/IMPLEMENTATION-NOTES.md
# ============================================================
include(FetchContent)

# CMake >= 3.30 起 FetchContent_Populate 被标记弃用（CMP0169）。
# imgui / imguizmo / stb 仓库没有根 CMakeLists.txt，必须走 Populate 路径
# （MakeAvailable 会尝试 add_subdirectory 而报错），显式保持 OLD 行为避免 CMake 4.x 弃用警告。
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

# 全项目统一 /MD（Debug 为 /MDd）CRT，防止第三方静态库与引擎 CRT 混用（ARCHITECTURE.md §11-3）
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "" FORCE)

# ---------- GLFW 3.4 ----------
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

# ---------- glad2 v2.0.8（configure 期用 Python 生成 GL 4.5 core 加载器）----------
FetchContent_Declare(glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.8
    GIT_SHALLOW TRUE)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# ---------- GLM 1.0.3（纯头文件）----------
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.3
    GIT_SHALLOW TRUE)

# ---------- Dear ImGui v1.93.0-docking ----------
# 只取源码（imgui 仓库无根 CMakeLists.txt）：editor 直接编译其源文件进自身 target，
# 暴露 imgui_SOURCE_DIR 给 editor/CMakeLists.txt 使用
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.93.0-docking
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

# ---------- ImGuizmo 1.10 ----------
# 同上：只 Populate，暴露 imguizmo_SOURCE_DIR 给 editor
FetchContent_Declare(imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG 1.10
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(imguizmo)
if(NOT imguizmo_POPULATED)
    FetchContent_Populate(imguizmo)
endif()

# ---------- Assimp 5.4.3（重度裁剪：只留 OBJ/FBX/glTF/Collada 导入器）----------
FetchContent_Declare(assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG v5.4.3
    GIT_SHALLOW TRUE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_COLLADA_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)   # FBX 需要 zlib（用 Assimp 自带 contrib/zlib）

# ---------- nlohmann/json v3.12.0 ----------
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

# ---------- stb（纯头文件，pin commit 9600522；无 CMakeLists，手动 Populate 成 INTERFACE 库）----------
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 9600522de53973336fa033a289858a4354cbb04c
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

# ---------- Jolt Physics v5.7.0（§12.1：仓库根无 CMakeLists.txt → Populate + add_subdirectory(Build)）----------
# 注意：必须显式保持 CMP0169 OLD（本文件开头已设置），Jolt 走 Populate 路径
FetchContent_Declare(jolt
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.7.0
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(jolt)
if(NOT jolt_POPULATED)
    FetchContent_Populate(jolt)
endif()
# 选项铁律（§12.1/§12.3 逐条）：漏设 USE_STATIC_MSVC_RUNTIME_LIBRARY=OFF 会 /MT 混用 → LNK2038；
# FLOATING_POINT_EXCEPTIONS_ENABLED=ON 时 Debug 下 FP 异常中断；其余为项目规则要求的裁剪
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE ON CACHE BOOL "" FORCE)   # 碰撞体线框提取（debugDraw 依赖）
add_subdirectory(${jolt_SOURCE_DIR}/Build ${jolt_BINARY_DIR})

# ---------- miniaudio 0.11.26（§12.2：单头文件无 CMakeLists → Populate 成 INTERFACE 库）----------
# MINIAUDIO_IMPLEMENTATION 唯一定义于 engine/src/Audio/AudioEngine.cpp（stb_image 同款纪律）
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.26
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(miniaudio)
if(NOT miniaudio_POPULATED)
    FetchContent_Populate(miniaudio)
endif()
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${miniaudio_SOURCE_DIR})

# ---------- 拉取并配置其余依赖（imgui/imguizmo/stb/jolt/miniaudio 已单独 Populate）----------
FetchContent_MakeAvailable(glfw glm assimp nlohmann_json glad)

# glad v2.0.8 仓库根没有 CMakeLists.txt（MakeAvailable 只拉取、不 add_subdirectory），
# 其 CMake 集成入口在 glad/cmake 子目录，载入后才提供 glad_add_library()
add_subdirectory(${glad_SOURCE_DIR}/cmake ${glad_BINARY_DIR})

# 生成 GL 4.5 core 加载器静态库（configure 期调 Python 生成源码，依赖上面的 find_package(Python3)）
glad_add_library(glad_gl_core_45 STATIC REPRODUCIBLE API gl:core=4.5)
