# NovaGame：游戏脚本（V0.5-7 双形态，NOVA_SCRIPTS_DLL 开关；回滚路径保留）
#
# NOVA_SCRIPTS_DLL=ON（默认）：SHARED 两个形态，同一份脚本源——
#   NovaGame：链接 NovaEditor 导入库（编辑器热重载；--target NovaGame 构建，
#             输出 samples/<Config>/NovaGame.dll，NOVA_GAME_DLL_PATH 指向它）
#   NovaGamePlayer：链接 NovaPlayer 导入库（Player 发布形态；输出 player/<Config>/NovaGame.dll，
#             Packager 拷入发布目录，Player 启动按 exe 目录加载）
#   符号方向（§25.5）：引擎代码静态链入 exe（/WHOLEARCHIVE + 导出全部引擎符号），
#   脚本类的引擎引用（vtable/RTTI）绑定到「加载它的 exe」的导出——每进程单一引擎实例。
#   注意两个形态必须各链各的 exe：DLL 导入表按模块名解析（"NovaEditor.exe"/"NovaPlayer.exe"），
#   单形态无法同时服务两个 exe（另一 exe 进程内找不到该模块名 → LoadLibrary 失败）。
#
# NOVA_SCRIPTS_DLL=OFF（回滚）：NovaScripts OBJECT 库，.obj 直接并入 exe（§9.4，静态注册不被裁剪）
option(NOVA_SCRIPTS_DLL "脚本以 DLL 形态交付（V0.5-7 热重载）" ON)

set(NOVA_GAME_SOURCES
    SpinningCube.cpp
    OrbitCamera.cpp
    SceneSwitcher.cpp
    MoveWithInput.cpp
    CharacterWalk.cpp      # V1.0-3 验收演示：WASD 驱动 CharacterController
    SceneLoadDemo.cpp      # V1.0-2 验收演示：数字键驱动异步场景加载/卸载
    NovaSampleGame/NeonGameManager.cpp      # 示例游戏《霓虹收集者》：全局单例（计分/切场景/音效/存档）
    NovaSampleGame/NeonLobbyUI.cpp          # 示例游戏：大厅 UI 接线（开始按钮/最佳纪录/上次通关）
    NovaSampleGame/NeonLevelHud.cpp         # 示例游戏：关卡 HUD/计时/结算面板
    NovaSampleGame/NeonCollectible.cpp      # 示例游戏：能量球（摆动自转 + 距离轮询拾取）
    NovaSampleGame/NeonGate.cpp             # 示例游戏：终点传送门（激活序列 + 胜利区判定）
    NovaSampleGame/NeonPlayerWatcher.cpp    # 示例游戏：玩家看守（掉落复活 + 跳跃音效）
    NovaSampleGame/NeonCameraShake.cpp      # V1.1：收集镜头震动（挂 Main Camera）
    NovaSampleGame/NeonGhost.cpp            # V1.2：幽灵回放（最佳纪录轨迹重放）
    NovaSampleGame/NeonMovingPlatform.cpp   # V1.3：移动平台（sin 周期位置脚本，无物理调用）
    NovaSampleGame/NeonAchievements.cpp     # V1.3：本地成就（12 成就边沿检测 + toast 队列 + 存档）
)

if(NOVA_SCRIPTS_DLL)
    # 编辑器形态（热重载对象）
    add_library(NovaGame SHARED ${NOVA_GAME_SOURCES})
    target_include_directories(NovaGame PUBLIC ${CMAKE_SOURCE_DIR}/engine/include)
    # NovaEditor 导入库：引擎符号绑定编辑器导出；glm/json 仅接口目标（头文件），
    # 不携带任何 .obj——引擎代码绝不落入 DLL（落进去 = 静态成员复制/注册表分裂）
    target_link_libraries(NovaGame PRIVATE NovaEditor glm::glm nlohmann_json::nlohmann_json)
    set_target_properties(NovaGame PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/samples")
    nova_configure_target(NovaGame)

    # Player 形态（打包发布）：输出名固定 NovaGame.dll（Player 启动按 exe 目录/NovaGame.dll 加载；
    # 与编辑器形态同名但不同目录——player/<Config>/ 与 samples/<Config>/ 互不冲突）
    add_library(NovaGamePlayer SHARED ${NOVA_GAME_SOURCES})
    target_include_directories(NovaGamePlayer PUBLIC ${CMAKE_SOURCE_DIR}/engine/include)
    target_link_libraries(NovaGamePlayer PRIVATE NovaPlayer glm::glm nlohmann_json::nlohmann_json)
    set_target_properties(NovaGamePlayer PROPERTIES
        OUTPUT_NAME NovaGame
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/player")   # player/<Config>/NovaGame.dll（冒烟即用）
    nova_configure_target(NovaGamePlayer)
else()
    # 回滚形态：OBJECT 库（MSVC 链接器会丢弃「无引用」的静态库 .obj，OBJECT 直接并入 exe
    # 保证 NE_REGISTER_SCRIPT 静态初始化存活，见 §9.4）
    add_library(NovaScripts OBJECT ${NOVA_GAME_SOURCES})
    target_include_directories(NovaScripts PUBLIC ${CMAKE_SOURCE_DIR}/engine/include)
    target_link_libraries(NovaScripts PUBLIC NovaEngine)
    nova_configure_target(NovaScripts)
endif()
