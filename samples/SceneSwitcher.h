#pragma once
#include <NovaEngine/Scripting/Behaviour.h>

namespace NovaGame {

// 验收脚本（V0.3-12 人工验收工具）：
//   进入 Play（onStart）即把宿主设为 DontDestroyOnLoad——切场景时随迁移存活（DDOL 证据）；
//   K = loadScene("Second")，L = loadScene("demo")（Build Settings 勾选后生效）。
// 注意：DDOL 在 onStart（而非 onAwake）设置——Edit 态挂载不污染场景存档，Stop 后快照还原。
class SceneSwitcher : public NovaEngine::Behaviour {
public:
    explicit SceneSwitcher(NovaEngine::GameObject* owner);
    void onStart() override;
    void onUpdate(float dt) override;
};

}  // namespace NovaGame
