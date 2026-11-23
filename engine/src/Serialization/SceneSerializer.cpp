#include <NovaEngine/Serialization/SceneSerializer.h>
#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Physics/PhysicsWorld.h>   // beginTeardown（批量销毁旗标，deserialize 清空前调用）
#include <NovaEngine/Scene/ComponentRegistry.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scripting/Behaviour.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Core/UUID.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <unordered_map>

// 内部共享头（V0.3 波 3）：GameObject 节点写/读、组件分发、glm<->JSON 辅助等
// 由 SceneSerializer 与 PrefabSerializer 共用；键名/键序/默认值零行为变更
#include "SerializationInternals.h"

namespace NovaEngine {

using namespace SerializationInternals;

std::string SceneSerializer::serialize(const Scene& scene, int indent) {
    nlohmann::json root;
    root["version"] = 4;   // V0.3-4/6：+PBR 材质字段 / materialAsset 材质库双轨（读时兼容 1/2/3/4）
    root["name"] = scene.name();

    {
        nlohmann::json env;
        env["skyColor"] = vec3ToJson(scene.skyColor);
        env["ambientColor"] = vec3ToJson(scene.ambientColor);
        env["showGrid"] = scene.showGrid;
        env["fogColor"] = vec3ToJson(scene.fogColor);   // V0.2-5：网格雾色（缺省随 skyColor 联动）
        // 天空（V0.2-7）：skyMode 存字符串
        env["skyMode"] = scene.skyMode == SkyMode::Cubemap ? "Cubemap" : "Gradient";
        env["skyHorizonColor"] = vec3ToJson(scene.skyHorizonColor);
        env["skyZenithColor"] = vec3ToJson(scene.skyZenithColor);
        env["skyCubemapPath"] = scene.skyCubemapPath;
        env["skyRotation"] = scene.skyRotation;
        env["skyExposure"] = scene.skyExposure;
        // IBL（V0.5-4）：environment.ibl 段（Scene::iblIntensity）
        {
            nlohmann::json ibl;
            ibl["iblIntensity"] = scene.iblIntensity;
            env["ibl"] = std::move(ibl);
        }
        // 碰撞层矩阵（V1.0-3，§27.2 骨架日布）：environment.physics 段
        // （16 层命名 + 16×16 对称矩阵；读时缺省 = Scene 构造缺省，旧文件零变化）
        {
            nlohmann::json phys;
            phys["layerNames"] = scene.physicsLayerNames;
            nlohmann::json mat = nlohmann::json::array();
            for (int a = 0; a < 16; ++a) {
                nlohmann::json row = nlohmann::json::array();
                for (int b = 0; b < 16; ++b)
                    row.push_back(scene.physicsCollisionMatrix[a][b]);
                mat.push_back(std::move(row));
            }
            phys["collisionMatrix"] = std::move(mat);
            env["physics"] = std::move(phys);
        }
        // 后处理（V0.3-14，波 3）：environment.postProcess 段，读时全默认值兜底
        {
            nlohmann::json pp;
            pp["enabled"] = scene.postProcess.enabled;
            pp["bloomThreshold"] = scene.postProcess.bloomThreshold;
            pp["bloomIntensity"] = scene.postProcess.bloomIntensity;
            pp["bloomLevels"] = scene.postProcess.bloomLevels;
            pp["exposure"] = scene.postProcess.exposure;
            // SSAO（V0.5-5）：四键追加（缺省关；旧文件读时兜底零警告）
            pp["ssaoEnabled"] = scene.postProcess.ssaoEnabled;
            pp["ssaoRadius"] = scene.postProcess.ssaoRadius;
            pp["ssaoStrength"] = scene.postProcess.ssaoStrength;
            pp["ssaoSampleCount"] = scene.postProcess.ssaoSampleCount;
            // 色彩分级（V1.0-4，§27.3 骨架日布）：九键追加（缺省全关；旧文件读时兜底零警告）
            pp["lutPath"] = scene.postProcess.lutPath;
            pp["lutIntensity"] = scene.postProcess.lutIntensity;
            pp["dofEnabled"] = scene.postProcess.dofEnabled;
            pp["dofFocusDistance"] = scene.postProcess.dofFocusDistance;
            pp["dofAperture"] = scene.postProcess.dofAperture;
            pp["vignetteEnabled"] = scene.postProcess.vignetteEnabled;
            pp["vignetteIntensity"] = scene.postProcess.vignetteIntensity;
            pp["grainEnabled"] = scene.postProcess.grainEnabled;
            pp["grainAmount"] = scene.postProcess.grainAmount;
            env["postProcess"] = std::move(pp);
        }
        root["environment"] = std::move(env);
    }
    {
        nlohmann::json cam;
        cam["target"] = vec3ToJson(scene.editorCameraTarget);
        cam["distance"] = scene.editorCameraDistance;
        cam["yaw"] = scene.editorCameraYaw;
        cam["pitch"] = scene.editorCameraPitch;
        root["editorCamera"] = std::move(cam);
    }

    // 对象平铺列表：forEachObject 深度优先
    nlohmann::json objects = nlohmann::json::array();
    scene.forEachObject([&objects](GameObject* go) {
        nlohmann::json node;
        writeGameObjectNode(node, go);
        // DDOL（V0.3-12，波 3）：仅 true 时写（缺省 false，旧文件往返无损）
        if (go->dontDestroyOnLoad())
            node["dontDestroyOnLoad"] = true;
        objects.push_back(std::move(node));
    });
    root["objects"] = std::move(objects);

    // indent = -1 时 nlohmann dump 输出紧凑单行（Undo 快照用，省内存）
    return root.dump(indent);
}

std::string SceneSerializer::serializeGameObject(GameObject& object) {
    // 单对象 JSON（含 parent 引用），编辑器复制/剪贴板用
    nlohmann::json node;
    writeGameObjectNode(node, &object);
    if (object.dontDestroyOnLoad())  // DDOL 随复制保留（仅 true 时写，缺省 false）
        node["dontDestroyOnLoad"] = true;
    return node.dump(4);
}

void SceneSerializer::deserialize(Scene& target, const std::string& json) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json);
    } catch (const std::exception& e) {
        Logger::error("反序列化场景失败：JSON 解析错误（{}），场景保持不变", e.what());
        return;
    }
    if (!root.is_object()) {
        Logger::error("反序列化场景失败：JSON 根节点不是对象，场景保持不变");
        return;
    }
    const int version = safeGet(root, "version", 0);
    // V0.3：写 4，读 1/2/3/4（旧文件默认值兜底）；其它版本 warn 但继续尝试加载
    if (version != 1 && version != 2 && version != 3 && version != 4)
        Logger::warn("场景文件版本为 {}（期望 1/2/3/4），仍尝试加载", version);

    // 1. 清空 target（保留 Scene 对象身份）：全部对象入销毁队列并立即执行
    {
        // 批量销毁旗标（2026-11-15 物理验收修复）：exitPlayMode 的原地重建会析构全部
        // 旧 GameObject——先置位，防组件析构期 unregister 重建/重挂（对已析构 GO 的 UAF）；
        // 下次 Play 的 syncFromTransforms 复位并全量重建（Edit 态本不步进物理，无功能损失）
        if (target.physics())
            target.physics()->beginTeardown();
        std::vector<uint64_t> doomed;
        target.forEachObject([&doomed](GameObject* go) { doomed.push_back(go->id()); });
        for (uint64_t id : doomed)
            target.destroyGameObject(id);
        target.flushDestroyQueue();
    }

    const nlohmann::json kEmptyObject = nlohmann::json::object();
    const nlohmann::json kEmptyArray = nlohmann::json::array();
    const nlohmann::json& objectsNode =
        (root.contains("objects") && root["objects"].is_array()) ? root["objects"] : kEmptyArray;

    // 2. 先建全部对象（instantiate 不自动加 Transform）
    struct ObjRecord {
        GameObject* go;
        const nlohmann::json* node;
    };
    std::vector<ObjRecord> records;
    records.reserve(objectsNode.size());
    for (const auto& obj : objectsNode) {
        if (!obj.is_object())
            continue;
        // id 硬化（2026-11-19）：超出 uint64 范围的 JSON 整数会被解析成浮点——
        // 浮点→uint64 强转是 UB（实测 cvttsd2si 越界饱和为 0x8000…），一律视为无效
        // 重新生成（此前只会打「冲突或无效」警告但值已是垃圾）
        const uint64_t id = (obj.contains("id") && obj["id"].is_number() &&
                             !obj["id"].is_number_float())
                                ? safeGet(obj, "id", uint64_t(UUID::kInvalid))
                                : uint64_t(UUID::kInvalid);
        const std::string name = safeGet(obj, "name", std::string("GameObject"));
        GameObject* go = target.instantiate(id, name);
        go->setActiveSelf(safeGet(obj, "active", true));
        go->setDontDestroyOnLoad(safeGet(obj, "dontDestroyOnLoad", false));  // V0.3-12：缺省 false
        records.push_back({go, &obj});
    }

    // 3. 逐组件 createComponentFromJson + attachComponent(callAwake=false) + onDeserialize
    //    （循环体已抽取至 SerializationInternals::readComponentsJson——Prefab 共用，逐行等价）
    for (const auto& rec : records) {
        const nlohmann::json& compsNode =
            (rec.node->contains("components") && (*rec.node)["components"].is_array())
                ? (*rec.node)["components"]
                : kEmptyArray;
        readComponentsJson(rec.go, compsNode);
    }

    // 3.5 无 Transform 防御（V0.2-14）：畸形文件/旧文件缺 Transform 时补默认 Transform，
    //     保证 setParent/transform() 等路径不空指针崩溃（callAwake=false，第 5 步统一补调）
    for (const auto& rec : records) {
        if (rec.go->transform() != nullptr) continue;
        Logger::warn("反序列化：对象 \"{}\" 缺少 Transform 组件，已补充默认 Transform", rec.go->name());
        rec.go->attachComponent(std::make_unique<Transform>(rec.go),
                                Component::componentTypeId<Transform>(),
                                /*callAwake=*/false);
    }

    // 4. 重建父子层级：全部对象已挂根，按 parent 字段 setParent(keepWorldTransform=false)
    //    （直接用文件里的局部值，不改动世界矩阵语义）
    for (const auto& rec : records) {
        const uint64_t parentId = safeGet(*rec.node, "parent", uint64_t(UUID::kInvalid));
        if (parentId == UUID::kInvalid)
            continue;
        if (GameObject* p = target.findGameObject(parentId))
            rec.go->setParent(p, false);
        else
            Logger::warn("反序列化：对象 \"{}\" 的父级 {} 不存在，保持根层级", rec.go->name(), parentId);
    }

    // 5. 全部组件统一补调 onAwake（此时字段已就绪、父级已存在）
    target.forEachObject([](GameObject* go) {
        for (Component* c : go->getComponents<Component>())
            c->onAwake();
    });

    // 6. 恢复环境 / 编辑器相机 / 场景名
    {
        const nlohmann::json& env =
            (root.contains("environment") && root["environment"].is_object()) ? root["environment"] : kEmptyObject;
        target.skyColor = jsonToVec3(env.value("skyColor", nlohmann::json()), glm::vec3(0.18f, 0.22f, 0.35f));
        target.ambientColor = jsonToVec3(env.value("ambientColor", nlohmann::json()), glm::vec3(0.15f, 0.15f, 0.18f));
        target.showGrid = safeGet(env, "showGrid", true);
        // V0.2-5：fogColor 缺省随 skyColor 联动（旧文件无此字段 → 取刚读到的 skyColor）
        target.fogColor = jsonToVec3(env.value("fogColor", nlohmann::json()), target.skyColor);

        // 天空（V0.2-7）：全部字段默认值兜底，旧 JSON 开箱即用
        const std::string skyModeStr = safeGet(env, "skyMode", std::string("Gradient"));
        if (skyModeStr == "Cubemap") {
            target.skyMode = SkyMode::Cubemap;
        } else {
            if (skyModeStr != "Gradient")
                Logger::warn("反序列化：未知天空模式 \"{}\"，回退渐变", skyModeStr);
            target.skyMode = SkyMode::Gradient;
        }
        target.skyHorizonColor =
            jsonToVec3(env.value("skyHorizonColor", nlohmann::json()), glm::vec3(0.55f, 0.62f, 0.72f));
        target.skyZenithColor =
            jsonToVec3(env.value("skyZenithColor", nlohmann::json()), glm::vec3(0.22f, 0.30f, 0.48f));
        target.skyCubemapPath = safeGet(env, "skyCubemapPath", std::string());
        target.skyRotation = safeGet(env, "skyRotation", 0.0f);
        target.skyExposure = safeGet(env, "skyExposure", 1.0f);
        // IBL（V0.5-4）：旧文件无 ibl 段 → 默认 1.0（有 cubemap 天空的旧场景打开后自动获得
        // IBL 增强，用户可调 0 关闭；无 HDRI 时本就无 IBL，缺省零影响）
        const nlohmann::json& iblNode = (env.contains("ibl") && env["ibl"].is_object())
                                            ? env["ibl"]
                                            : kEmptyObject;
        target.iblIntensity = safeGet(iblNode, "iblIntensity", 1.0f);

        // 碰撞层矩阵（V1.0-3，§27.2 骨架日布）：旧文件无此段 → 保持缺省（全通 + 默认层名，零警告）
        // P1-2 修复（2026-11-23 合波日）：编辑器复用同一 Scene 对象加载场景，必须先重置缺省
        // 再条件读——否则打开无 physics 段的旧文件会残留上一场景的矩阵/层名（对齐 postProcess
        // 的「先重置后读」模式；§27.6 schema 回归验收「旧文件参数不变」依赖此重置）
        for (int a = 0; a < 16; ++a)
            for (int b = 0; b < 16; ++b)
                target.physicsCollisionMatrix[a][b] = true;
        target.physicsLayerNames.clear();
        for (int i = 0; i < 16; ++i)
            target.physicsLayerNames.push_back(i == 0 ? "Default" : "Layer" + std::to_string(i));
        if (env.contains("physics") && env["physics"].is_object()) {
            const nlohmann::json& phys = env["physics"];
            if (phys.contains("layerNames") && phys["layerNames"].is_array() &&
                phys["layerNames"].size() == 16) {
                target.physicsLayerNames.clear();
                for (const auto& n : phys["layerNames"])
                    target.physicsLayerNames.push_back(
                        n.is_string() ? n.get<std::string>() : std::string());
            }
            if (phys.contains("collisionMatrix") && phys["collisionMatrix"].is_array() &&
                phys["collisionMatrix"].size() == 16) {
                for (int a = 0; a < 16; ++a) {
                    const auto& row = phys["collisionMatrix"][a];
                    if (!row.is_array() || row.size() != 16) continue;
                    for (int b = 0; b < 16; ++b)
                        target.physicsCollisionMatrix[a][b] =
                            row[b].is_boolean() ? row[b].get<bool>() : true;
                }
            }
        }

        // 后处理（V0.3-14，波 3）：旧文件无此段 → 全默认值兜底（与 PostProcessSettings 一致）
        const nlohmann::json& pp = (env.contains("postProcess") && env["postProcess"].is_object())
                                       ? env["postProcess"]
                                       : kEmptyObject;
        target.postProcess.enabled = safeGet(pp, "enabled", false);
        target.postProcess.bloomThreshold = safeGet(pp, "bloomThreshold", 1.0f);
        target.postProcess.bloomIntensity = safeGet(pp, "bloomIntensity", 0.5f);
        target.postProcess.bloomLevels = safeGet(pp, "bloomLevels", 3);
        target.postProcess.exposure = safeGet(pp, "exposure", 1.0f);
        // SSAO（V0.5-5）：四键缺省兜底（旧文件零警告）
        target.postProcess.ssaoEnabled = safeGet(pp, "ssaoEnabled", false);
        target.postProcess.ssaoRadius = safeGet(pp, "ssaoRadius", 0.5f);
        target.postProcess.ssaoStrength = safeGet(pp, "ssaoStrength", 1.0f);
        target.postProcess.ssaoSampleCount = safeGet(pp, "ssaoSampleCount", 16);
        // 色彩分级（V1.0-4）：九键缺省兜底（旧文件零警告）
        target.postProcess.lutPath = safeGet(pp, "lutPath", std::string(""));
        target.postProcess.lutIntensity = safeGet(pp, "lutIntensity", 1.0f);
        target.postProcess.dofEnabled = safeGet(pp, "dofEnabled", false);
        target.postProcess.dofFocusDistance = safeGet(pp, "dofFocusDistance", 5.0f);
        target.postProcess.dofAperture = safeGet(pp, "dofAperture", 0.05f);
        target.postProcess.vignetteEnabled = safeGet(pp, "vignetteEnabled", false);
        target.postProcess.vignetteIntensity = safeGet(pp, "vignetteIntensity", 0.4f);
        target.postProcess.grainEnabled = safeGet(pp, "grainEnabled", false);
        target.postProcess.grainAmount = safeGet(pp, "grainAmount", 0.05f);

        const nlohmann::json& cam =
            (root.contains("editorCamera") && root["editorCamera"].is_object()) ? root["editorCamera"] : kEmptyObject;
        target.editorCameraTarget = jsonToVec3(cam.value("target", nlohmann::json()), glm::vec3(0.0f, 1.0f, 0.0f));
        target.editorCameraDistance = safeGet(cam, "distance", 8.0f);
        target.editorCameraYaw = safeGet(cam, "yaw", 0.0f);
        target.editorCameraPitch = safeGet(cam, "pitch", -0.35f);

        target.setName(safeGet(root, "name", std::string("Untitled")));
    }
}

bool SceneSerializer::saveToFile(const Scene& scene, const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        Logger::error("保存场景失败：无法打开文件 {}", path.string());
        return false;
    }
    file << serialize(scene);
    if (!file.good()) {
        Logger::error("保存场景失败：写入文件 {} 出错", path.string());
        return false;
    }
    return true;
}

bool SceneSerializer::loadFromFile(Scene& target, const std::filesystem::path& path) {
    // 读取收敛到 AssetStream（中文路径安全；Wave 2 pak 挂载后场景也可从内存 entry 读）
    std::string content;
    if (!AssetStream::readAllText(path, content)) {
        Logger::error("加载场景失败：无法打开文件 {}", path.string());
        return false;
    }
    // 先做一次解析校验，失败时返回 false 且不改动场景
    try {
        static_cast<void>(nlohmann::json::parse(content));
    } catch (const std::exception& e) {
        Logger::error("加载场景失败：{} 不是合法的 JSON（{}）", path.string(), e.what());
        return false;
    }
    deserialize(target, content);
    return true;
}

void SceneSerializer::remapGameObjectIds(nlohmann::json& objectJson) {
    // 复制对象：递归换新 ID，含 parent 字段同步重映射。
    // 支持单对象 JSON（serializeGameObject 产物）与 {"objects":[...]} 数组两种形态；
    // 不在本 JSON 内的 parent 引用（原父级未随副本一起复制）保持不变 → 副本成为原父级的兄弟节点。
    std::unordered_map<uint64_t, uint64_t> remap;

    std::function<void(nlohmann::json&)> collect = [&](nlohmann::json& node) {
        if (node.is_object()) {
            if (node.contains("id") && node["id"].is_number_unsigned())
                remap.emplace(node["id"].get<uint64_t>(), UUID::kInvalid);
            for (const char* key : {"objects", "children"}) {
                if (node.contains(key) && node[key].is_array())
                    for (auto& child : node[key])
                        collect(child);
            }
        } else if (node.is_array()) {
            for (auto& child : node)
                collect(child);
        }
    };
    collect(objectJson);

    // 为每个旧 ID 分配新 ID（保证互不相同且非 0）
    for (auto& [oldId, newId] : remap) {
        (void)oldId;
        do {
            newId = UUID::generate();
        } while (newId == UUID::kInvalid || remap.contains(newId));
    }

    std::function<void(nlohmann::json&)> rewrite = [&](nlohmann::json& node) {
        if (node.is_object()) {
            if (node.contains("id") && node["id"].is_number_unsigned()) {
                auto it = remap.find(node["id"].get<uint64_t>());
                if (it != remap.end())
                    node["id"] = it->second;
            }
            if (node.contains("parent") && node["parent"].is_number_unsigned()) {
                auto it = remap.find(node["parent"].get<uint64_t>());
                if (it != remap.end())
                    node["parent"] = it->second;
            }
            for (auto& [key, value] : node.items()) {
                (void)key;
                rewrite(value);
            }
        } else if (node.is_array()) {
            for (auto& child : node)
                rewrite(child);
        }
    };
    rewrite(objectJson);
}

std::unique_ptr<Component> SceneSerializer::createComponentFromJson(GameObject* owner, const nlohmann::json& j) {
    // 分发逻辑已抽取至 SerializationInternals（V0.3 波 3，PrefabSerializer 共用）；
    // 本私有入口保留为转发壳，公共头 API 不变
    return SerializationInternals::createComponentFromJson(owner, j);
}

}  // namespace NovaEngine
