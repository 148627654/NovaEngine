#pragma once
#include <NovaEngine/Scripting/Behaviour.h>

namespace NovaGame {

// 大厅 UI 接线（§28.3.5 大厅逻辑）：
// 一次性工作全在 onStart——StartBtn onClick（sfx_ui_click + 传 level 参数 + 切关卡）、
// 最佳纪录显示（SaveGame::listSlots 只读槽 0 元数据，不用 load，§28.3.12 裁定）、
// 上次通关显示（getSceneParam("lastResult") 回传参数）。无逐帧逻辑。
class NeonLobbyUI : public NovaEngine::Behaviour {
public:
    explicit NeonLobbyUI(NovaEngine::GameObject* owner);

    void onStart() override;
    void onUpdate(float /*dt*/) override {}   // 无逐帧工作
};

}  // namespace NovaGame
