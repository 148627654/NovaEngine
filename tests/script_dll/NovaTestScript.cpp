// NovaTestScript.cpp —— V1.0-13 热重载审计测试脚本 DLL（§28.1 用例③④专用）
//
// 与 NovaGame.dll 同构的最小形态：SHARED 库链接 NovaTests 导入库（引擎符号绑定到
// 「加载它的 exe」——NovaTests.exe 经 ENABLE_EXPORTS + 引擎 .def 导出，§25.5 符号方向）。
// 静态初始化器两件事：
//   1. 调用 exe 导出钩子 nova_test_hook_on_init()——跨模块实例单调计数，
//      FreeLibrary 语义的权威判据（旧 DLL 引用计数未归零 → LoadLibrary 返回旧模块、
//      初始化器不重跑 → 计数不增，用例④红）；
//   2. 注册 HotReloadProbe 脚本（ScriptRegistry 属 exe 侧单例，经导入表写入）。
//
// 纪律：本 DLL 不含任何引擎代码实现（glm/json 仅头文件），引擎引用全部经
// NovaTests.exe 导出绑定——落引擎代码进 DLL = 静态成员复制/注册表分裂（§25.5）。

#include <NovaEngine/Scripting/Behaviour.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

#include <nlohmann/json.hpp>

#include <memory>

using namespace NovaEngine;

// exe 导出钩子（NovaTests 侧实现；本 DLL 经导入表调用）
extern "C" __declspec(dllimport) void nova_test_hook_on_init();

namespace {

// 热重载字段恢复探针：onSerialize/onDeserialize 产出的字段经 reload 的
// 「整场景 JSON 重建 + 原位恢复」往返，用例③逐轮校验 hits/speed 不丢。
class HotReloadProbe : public Behaviour {
public:
    explicit HotReloadProbe(GameObject* owner) : Behaviour(owner) {}
    void onStart() override { ++starts; }
    void onUpdate(float) override { ++updates; }
    void onSerialize(nlohmann::json& out) const override {
        out["hits"] = hits;
        out["speed"] = speed;
    }
    void onDeserialize(const nlohmann::json& in) override {
        hits = in.value("hits", 0);
        speed = in.value("speed", 0.0f);
    }
    int hits = 0;
    float speed = 0.0f;
    int starts = 0;
    int updates = 0;
};

// 每模块实例各自静态独立：恒 1（用例④ GetProcAddress 交叉校验）
int s_initCount = 0;

struct Registrar {
    Registrar() {
        ++s_initCount;
        nova_test_hook_on_init();   // exe 钩子：跨实例单调计数
        ScriptRegistry::registerScript("HotReloadProbe",
                                       [](GameObject* go) {
                                           return std::make_unique<HotReloadProbe>(go);
                                       });
    }
};
const Registrar s_registrar;

}  // namespace

// DLL 自有初始化计数（测试经 GetProcAddress 读取；每次新实例加载初始化器重跑 → 恒 1）
extern "C" __declspec(dllexport) int nova_test_script_init_count() {
    return s_initCount;
}
