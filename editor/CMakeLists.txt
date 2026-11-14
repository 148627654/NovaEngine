# NovaEditor：编辑器可执行程序（§9.4）
# - ImGui/ImGuizmo 源码直接编进本 target（绕开其自身 CMake 联动问题）
# - NovaScripts 以 OBJECT 库的 .obj 直接并入，保证 NE_REGISTER_SCRIPT 静态注册不被链接器裁剪
# - 保持控制台子系统（stdout 日志可见，§11.11）

add_executable(NovaEditor
    src/main.cpp
    src/EditorApplication.cpp
    src/EditorContext.cpp
    src/EditorCamera.cpp
    src/GizmoController.cpp
    src/DragDrop.cpp
    src/panels/SceneViewPanel.cpp
    src/panels/HierarchyPanel.cpp
    src/panels/InspectorPanel.cpp
    src/panels/ConsolePanel.cpp
    src/panels/AssetBrowserPanel.cpp
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
)

# NovaEngine PUBLIC 链接 glfw/glad/glm/assimp/json/stb，编辑器和 imgui 源码自动继承其 include 目录
target_link_libraries(NovaEditor PRIVATE
    NovaEngine
    $<TARGET_OBJECTS:NovaScripts>
)

# VS 调试运行目录 = 项目根：相对路径 assets/... 直接可用
set_target_properties(NovaEditor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

nova_configure_target(NovaEditor)
