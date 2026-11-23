#include "SceneLoadDemo.h"

#include <NovaEngine/Core/Input.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/SceneManager.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

namespace NovaGame {

SceneLoadDemo::SceneLoadDemo(NovaEngine::GameObject* owner) : Behaviour(owner) {}

void SceneLoadDemo::onStart() {
    NovaEngine::SceneManager::dontDestroyOnLoad(gameObject());
    NovaEngine::Logger::info("SceneLoadDemo: 数字键驱动异步场景加载——1 叠加 / 2 卸载 / 3 切换关卡 / 4 切回大厅");
}

void SceneLoadDemo::onUpdate(float /*dt*/) {
    if (!enabled) return;
    using NovaEngine::Input;
    using NovaEngine::Key;
    if (Input::isKeyPressed(Key::Num1)) {
        NovaEngine::Logger::info("SceneLoadDemo: 1 → loadSceneAsync(\"V10AdditiveLevel\", additive=true)");
        NovaEngine::SceneManager::loadSceneAsync("V10AdditiveLevel", true);
    } else if (Input::isKeyPressed(Key::Num2)) {
        NovaEngine::Logger::info("SceneLoadDemo: 2 → unloadScene(\"V10AdditiveLevel\")");
        NovaEngine::SceneManager::unloadScene("V10AdditiveLevel");
    } else if (Input::isKeyPressed(Key::Num3)) {
        NovaEngine::Logger::info("SceneLoadDemo: 3 → loadSceneAsync(\"V10AdditiveLevel\", additive=false)");
        NovaEngine::SceneManager::loadSceneAsync("V10AdditiveLevel", false);
    } else if (Input::isKeyPressed(Key::Num4)) {
        NovaEngine::Logger::info("SceneLoadDemo: 4 → loadSceneAsync(\"V10MultiSceneDemo\", additive=false)");
        NovaEngine::SceneManager::loadSceneAsync("V10MultiSceneDemo", false);
    }
}

}  // namespace NovaGame

NE_REGISTER_SCRIPT(NovaGame::SceneLoadDemo)
