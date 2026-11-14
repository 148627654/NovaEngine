# ============================================================
# 统一配置本项目 target（NovaEngine / NovaScripts / NovaEditor）
# 注意：编译选项只加在本项目 target 上，第三方 target 保持各自默认，
# 避免 assimp 等在 /W4 下被轰出警告墙（ARCHITECTURE.md §11-1）
# ============================================================
function(nova_configure_target t)
    target_compile_features(${t} PRIVATE cxx_std_20)
    # GLM 1.0.3 的 gtx/* 头未定义该宏时是硬 #error（GameObject/GizmoController 用 gtx/matrix_decompose.hpp）
    target_compile_definitions(${t} PRIVATE GLM_ENABLE_EXPERIMENTAL)
    if(MSVC)
        target_compile_options(${t} PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8 /MP /EHsc)
        # GLFW_INCLUDE_NONE：防止 glfw3.h 拉入系统 gl.h 与 glad 冲突；
        # NOMINMAX / WIN32_LEAN_AND_MEAN：Windows 头污染最小化
        target_compile_definitions(${t} PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            GLFW_INCLUDE_NONE
            _CRT_SECURE_NO_WARNINGS)
    endif()
endfunction()
