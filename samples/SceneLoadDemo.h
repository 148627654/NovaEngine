#pragma once
#include <NovaEngine/Scripting/Behaviour.h>

namespace NovaGame {

// V1.0-2 验收演示脚本（波 2 #2）：数字键驱动异步场景加载——
// 小键盘/主键盘 1 = loadSceneAsync("V10AdditiveLevel", true)  叠加加载（大厅+关卡共存）
//   2 = unloadScene("V10AdditiveLevel")                         卸载（内存回落）
//   3 = loadSceneAsync("V10AdditiveLevel", false)               异步整场景切换
//   4 = loadSceneAsync("V10MultiSceneDemo", false)              切回大厅
// 本脚本 GO 设 DontDestroyOnLoad（SceneSwitcher 同款），场景切换后按键仍然有效；
// 加载进度与卸载事件走 Logger 输出（Console 面板/Logs 文件可见）。
class SceneLoadDemo : public NovaEngine::Behaviour {
public:
    explicit SceneLoadDemo(NovaEngine::GameObject* owner);
    void onStart() override;
    void onUpdate(float dt) override;
};

}  // namespace NovaGame
