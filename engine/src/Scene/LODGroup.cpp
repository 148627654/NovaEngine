// LODGroup.cpp —— LOD 系统实现（V0.5-3，c1）：接口冻结于 §25.1；本文件为 c1 独占。
// resolveMeshFor：每帧每 GO 一次 O(lods) 选档（相机世界位置与 GO 世界位置距离）；
// 无 LODGroup / disabled / lods 空 / 无可用档 → 返回 nullptr，调用方回退 mr->mesh（旧路径零变化）。
#include <NovaEngine/Scene/LODGroup.h>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/ComponentRegistry.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Transform.h>

#include <glm/geometric.hpp>   // glm::distance（vec3）

#include <nlohmann/json.hpp>

#include <filesystem>
#include <unordered_set>

namespace NovaEngine {

namespace {

// 网格加载失败路径缓存：失败仅首次 warn 一次，之后每帧 O(1) 命中直接跳过该档，
// 避免反复磁盘 IO + 日志刷屏。场景重新加载（onDeserialize）会移除本组件各档路径的
// 失败标记——修复资产后重载场景即可重试。
std::unordered_set<std::string> g_failedMeshPaths;

// 选档验证日志开关：每进程只打一次（首个带 LODGroup 的 GO 首次选档时），
// 输出相机距离与选择结果（验收自证用；之后每帧零额外日志开销）。
bool g_selectionLogged = false;

}  // namespace

LODGroup::LODGroup(GameObject* owner) : Component(owner) {}

std::shared_ptr<Mesh> LODGroup::resolveMeshFor(GameObject* go, const glm::vec3& cameraPos) {
    if (!go) return nullptr;
    LODGroup* lod = go->getComponent<LODGroup>();
    if (!lod || !lod->enabled || lod->lods.empty()) return nullptr;   // 无组件/禁用/空档 → 回退 mr->mesh
    const Transform* t = go->transform();
    if (!t) return nullptr;   // 无 Transform 无法计算世界位置 → 回退 mr->mesh（与 SceneRenderer 收集守卫一致）

    // 相机世界位置与 GO 世界位置距离（Edit/Play 一致生效）
    const float dist = glm::distance(cameraPos, t->worldPosition());

    std::shared_ptr<Mesh> chosen;
    size_t chosenIndex = 0;
    float chosenDistance = 0.0f;
    std::string chosenPath;
    bool hasChoice = false;

    // 距离升序扫描，选满足 distance <= 相机距离 的最大档（= 列表序最后一个满足者）。
    // 不提前 break：加载失败的档跳过继续找下一档；乱序 lods（运行期脚本写入）按列表序确定。
    for (size_t i = 0; i < lod->lods.size(); ++i) {
        LODLevel& level = lod->lods[i];
        if (level.distance > dist) continue;          // 该档不可用（相机更近）
        if (level.meshPath.empty()) continue;         // 空路径档静默跳过（getMesh 对空路径同样返回 nullptr）
        if (g_failedMeshPaths.contains(level.meshPath)) continue;   // 已知失败档跳过（不再 warn）

        // 运行时缓存优先；未加载则经 AssetDatabase::getMesh 解析（builtin: 图元零 IO）
        std::shared_ptr<Mesh> mesh = level.mesh;
        if (!mesh) {
            mesh = AssetDatabase::instance().getMesh(std::filesystem::path(level.meshPath));
            if (!mesh) {
                Logger::warn("LODGroup 网格加载失败，跳过该档：{}（GO: {}）", level.meshPath,
                             go->name());
                g_failedMeshPaths.insert(level.meshPath);
                continue;
            }
            level.mesh = mesh;   // 加载成功即缓存（LODLevel.mesh 为运行时字段，不序列化）
        }
        chosen = mesh;
        chosenIndex = i;
        chosenDistance = level.distance;
        chosenPath = level.meshPath;
        hasChoice = true;
    }

    // 选档验证日志（每进程一次）：加载后首次渲染的相机距离与选择结果（验收自证）
    if (!g_selectionLogged) {
        g_selectionLogged = true;
        if (hasChoice)
            Logger::info(
                "LOD 选档验证：GO \"{}\" 相机距离 {:.1f} → LOD{}（mesh={}，distance={:.1f}）",
                go->name(), dist, chosenIndex, chosenPath, chosenDistance);
        else
            Logger::info("LOD 选档验证：GO \"{}\" 相机距离 {:.1f} → 无可用档（回退 MeshRenderer.mesh）",
                         go->name(), dist);
    }
    return chosen;   // 全档加载失败或无匹配档 → nullptr（调用方回退 mr->mesh）
}

void LODGroup::onSerialize(nlohmann::json& out) const {
    // 键名逐字按 §25.1：enabled / crossFade / lods[{mesh, distance}]（"type" 由 SceneSerializer 写）
    out["enabled"] = enabled;
    out["crossFade"] = crossFade;
    nlohmann::json arr = nlohmann::json::array();
    for (const LODLevel& level : lods) {
        nlohmann::json j;
        j["mesh"] = level.meshPath;
        j["distance"] = level.distance;
        arr.push_back(std::move(j));
    }
    out["lods"] = std::move(arr);
}

void LODGroup::onDeserialize(const nlohmann::json& in) {
    // 缺省兜底 + 未知键忽略；畸形类型字段按缺省处理（防单个坏字段中断其余档的解析）
    enabled = (in.contains("enabled") && in["enabled"].is_boolean())
                  ? in["enabled"].get<bool>()
                  : true;
    crossFade = (in.contains("crossFade") && in["crossFade"].is_boolean())
                    ? in["crossFade"].get<bool>()
                    : false;

    lods.clear();
    if (in.contains("lods") && in["lods"].is_array()) {
        for (const auto& j : in["lods"]) {
            if (!j.is_object()) continue;   // 非对象条目忽略
            LODLevel level;
            level.meshPath = (j.contains("mesh") && j["mesh"].is_string())
                                 ? j["mesh"].get<std::string>()
                                 : std::string();
            level.distance = (j.contains("distance") && j["distance"].is_number())
                                 ? j["distance"].get<float>()
                                 : 0.0f;
            lods.push_back(std::move(level));
        }
    }
    // lods 非数组/空 → 保持空 = 视为无 LOD（resolveMeshFor 返回 nullptr → 回退 mr->mesh）

    // 场景重载重试机会：移除本组件各档路径的加载失败标记（修复资产后重载场景即恢复）
    for (const LODLevel& level : lods)
        if (!level.meshPath.empty())
            g_failedMeshPaths.erase(level.meshPath);
}

}  // namespace NovaEngine

// 组件静态注册（§25.0 纪律 4；注册名经 ComponentRegistry 裁剪为 "LODGroup"）
NE_REGISTER_COMPONENT(NovaEngine::LODGroup)
