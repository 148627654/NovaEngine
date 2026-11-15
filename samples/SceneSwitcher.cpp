#include "SceneSwitcher.h"

#include <NovaEngine/Core/Input.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/SceneManager.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

namespace NovaGame {

SceneSwitcher::SceneSwitcher(NovaEngine::GameObject* owner) : Behaviour(owner) {}

void SceneSwitcher::onStart() {
    NovaEngine::SceneManager::dontDestroyOnLoad(gameObject());
    NovaEngine::Logger::info("SceneSwitcher: {} 已设为 DontDestroyOnLoad（切场景时随迁移存活）",
                             gameObject()->name());
}

void SceneSwitcher::onUpdate(float /*dt*/) {
    if (!enabled) return;
    if (NovaEngine::Input::isKeyPressed(NovaEngine::Key::K)) {
        NovaEngine::Logger::info("SceneSwitcher: K → loadScene(\"Second\")");
        NovaEngine::SceneManager::loadScene("Second");
    } else if (NovaEngine::Input::isKeyPressed(NovaEngine::Key::L)) {
        NovaEngine::Logger::info("SceneSwitcher: L → loadScene(\"demo\")");
        NovaEngine::SceneManager::loadScene("demo");
    }
}

}  // namespace NovaGame

NE_REGISTER_SCRIPT(NovaGame::SceneSwitcher)
