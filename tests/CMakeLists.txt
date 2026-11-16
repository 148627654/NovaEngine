# 临时无头测试目标（PlayCycleTest：Play 进出往返层级回归，用完即删）
add_executable(PlayCycleTest PlayCycleTest.cpp)
target_link_libraries(PlayCycleTest PRIVATE NovaEngine)
nova_configure_target(PlayCycleTest)
