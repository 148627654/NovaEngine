// ===== V1.0-5 ShaderGraphCodeGen（§27.4）真实实现（cG，2026-11-23）=====
// 图 → GLSL 片元源码：
//   - uniform 契约与 assets/shaders/blinn_phong.frag 逐名一致（§27.7-3：错位 = 花屏，
//     评审逐名核对；两文件内 PBR 模板源码注释互指，改动必须同步两处）。
//   - 每节点代码块带 #line + 注释标记；映射表（行号 → 节点 id）在生成期构建，
//     shader 编译失败仍可用（GL log 行号反查节点 → 面板高亮）。
//   - 顶点复用 blinn_phong.vert（提交路径经 Shader::createFromSource 传入 vs 源码）。
//   - 图纹理节点 sampler 分配 unit 9+i（避开材质 0-5 与 IBL 6-8），路径顺序经
//     lastTexturePaths 交给 SceneRenderer 绑定（§27.4 采样器契约）。

#include <NovaEngine/ShaderGraph/ShaderGraphCodeGen.h>

#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/ShaderGraph/ShaderGraphAsset.h>

#include <glad/gl.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <functional>
#include <map>
#include <sstream>
#include <vector>

namespace NovaEngine {

namespace {

// ===== 生成期状态（单线程引擎；generate 后立刻消费，与引擎其余 static 风格一致）=====
std::vector<std::pair<int, std::string>> s_lineMap;   // (行号 → 节点 id)
std::vector<std::string> s_texturePaths;              // 纹理节点路径（unit 9+i 顺序）

// GLSL 类型名
std::string glslType(GraphPortType t) {
    switch (t) {
        case GraphPortType::Float: return "float";
        case GraphPortType::Vec2: return "vec2";
        case GraphPortType::Vec3: return "vec3";
        case GraphPortType::Vec4: return "vec4";
        case GraphPortType::Sampler2D: return "sampler2D";
    }
    return "?";
}

// 数值类型隐式转换表达式（GLSL 4.5 合法构造：升维 splat / 降维 swizzle；
// vecN→float 只能 .x；vec2→vec3 补 0、vec3→vec4 补 1（颜色语义））
std::string convertExpr(const std::string& expr, GraphPortType from, GraphPortType to) {
    if (from == to) return expr;
    switch (to) {
        case GraphPortType::Float: return "(" + expr + ").x";
        case GraphPortType::Vec2:
            switch (from) {
                case GraphPortType::Float: return "vec2(" + expr + ")";
                case GraphPortType::Vec3: return "(" + expr + ").xy";
                case GraphPortType::Vec4: return "(" + expr + ").xy";
                default: break;
            }
            break;
        case GraphPortType::Vec3:
            switch (from) {
                case GraphPortType::Float: return "vec3(" + expr + ")";
                case GraphPortType::Vec2: return "vec3(" + expr + ", 0.0)";
                case GraphPortType::Vec4: return "(" + expr + ").xyz";
                default: break;
            }
            break;
        case GraphPortType::Vec4:
            switch (from) {
                case GraphPortType::Float: return "vec4(" + expr + ")";
                case GraphPortType::Vec2: return "vec4(" + expr + ", 0.0, 0.0)";
                case GraphPortType::Vec3: return "vec4(" + expr + ", 1.0)";
                default: break;
            }
            break;
        default: break;
    }
    return expr;   // 不可达（validateGraph 已拦截 sampler2D 互连）
}

// 节点 id → 变量名（非字母数字 → '_'，防注入）
std::string varName(const std::string& id) {
    std::string out = "g_";
    for (char c : id)
        out += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    return out;
}

// 参数字符串 → float 列表（空格/逗号分隔）；失败返回 false
bool parseFloats(const std::string& s, std::vector<float>& out) {
    std::string cur;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == ',') {
            if (!cur.empty()) {
                try {
                    out.push_back(std::stof(cur));
                } catch (...) {
                    return false;
                }
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) {
        try {
            out.push_back(std::stof(cur));
        } catch (...) {
            return false;
        }
    }
    return true;
}

std::string paramOf(const ShaderGraphNode& node, const std::string& key) {
    const auto it = node.params.find(key);
    return it != node.params.end() ? it->second : ShaderGraphAsset::defaultParam(node.type, key);
}

// ===== 源构建：逐行 + #line 标记（行号 = 原始 1 基行号，映射表与 GL log 同源）=====
struct SourceBuilder {
    std::string src;
    int lines = 0;   // 已输出行数

    void append(const std::string& line) {
        src += line;
        src += '\n';
        ++lines;
    }
    // 节点代码块：记录映射（该块注释行的原始行号 → 节点 id）并原样输出
    void nodeBlock(const std::string& nodeId, const std::string& nodeType,
                   const std::vector<std::string>& code) {
        append(std::format("#line {} 1", lines + 2));   // 下一行的原始行号
        s_lineMap.push_back({lines + 1, nodeId});        // 注释行（#line 指向处）
        append(std::format("    // ===== node {} ({}) =====", nodeId, nodeType));
        for (const auto& c : code) append(c);
    }
};

// 图输入端口 → 上游表达式（带隐式转换；调用方保证校验已通过）
std::string inputExpr(const ShaderGraphAsset& graph, const ShaderGraphNode& node,
                      const std::string& port, GraphPortType toType,
                      const std::map<std::string, const ShaderGraphNode*>& byId,
                      const std::map<std::string, const GraphNodeSpec*>& specs) {
    for (const auto& e : graph.edges) {
        if (e.toNode == node.id && e.toPort == port) {
            const ShaderGraphNode* from = byId.at(e.fromNode);
            const GraphNodeSpec* fromSpec = specs.at(from->type);
            GraphPortType fromType = GraphPortType::Float;
            for (const auto& p : fromSpec->outputs)
                if (p.name == e.fromPort) fromType = p.type;
            return convertExpr(varName(from->id), fromType, toType);
        }
    }
    return "vec3(0.0)";   // 不可达（校验已拦截缺失输入）
}

// 深度优先拓扑：从 Master 输入回溯，后序（依赖先输出）；游离节点按 id 顺序追加
void topoOrder(const ShaderGraphAsset& graph, const std::map<std::string, const ShaderGraphNode*>& byId,
               std::vector<const ShaderGraphNode*>& out) {
    std::map<std::string, bool> visited;
    std::function<void(const std::string&)> dfs = [&](const std::string& id) {
        if (visited[id]) return;
        visited[id] = true;
        const ShaderGraphNode* node = byId.at(id);
        for (const auto& e : graph.edges) {
            if (e.toNode == id) dfs(e.fromNode);
        }
        (void)node;
        out.push_back(byId.at(id));
    };
    for (const auto& n : graph.nodes)
        if (n.type == "Master") dfs(n.id);
    // 游离节点（未接入 Master）：仍生成（无害死代码），确定性顺序
    std::vector<const ShaderGraphNode*> rest;
    for (const auto& n : graph.nodes)
        if (!visited[n.id]) rest.push_back(&n);
    std::sort(rest.begin(), rest.end(),
              [](const ShaderGraphNode* a, const ShaderGraphNode* b) { return a->id < b->id; });
    out.insert(out.end(), rest.begin(), rest.end());
}

// 单个节点 → 代码行（不含 #line/注释外壳）
bool nodeCode(const ShaderGraphAsset& graph, const ShaderGraphNode& node,
              const std::map<std::string, const ShaderGraphNode*>& byId,
              const std::map<std::string, const GraphNodeSpec*>& specs,
              const std::map<std::string, int>& texUnitOf,
              std::string& outLine, std::string& error) {
    const std::string v = varName(node.id);
    const auto get = [&](const std::string& port, GraphPortType toType) {
        return inputExpr(graph, node, port, toType, byId, specs);
    };
    const auto floats = [&](const std::string& key, int expect, std::vector<float>& fs) {
        fs.clear();
        if (!parseFloats(paramOf(node, key), fs) ||
            (expect > 0 && static_cast<int>(fs.size()) != expect)) {
            error = std::format("节点 \"{}\"（{}）参数 \"{}\" 非法: \"{}\"（应为 {} 个数值）",
                                node.id, node.type, key, paramOf(node, key), expect);
            return false;
        }
        return true;
    };

    if (node.type == "UV") outLine = "    vec2 " + v + " = vUV;";
    else if (node.type == "Time") outLine = "    float " + v + " = uTime;";
    else if (node.type == "Color") {
        std::vector<float> f;
        if (!floats("color", 4, f)) return false;
        outLine = std::format("    vec4 {} = vec4({}, {}, {}, {});", v, f[0], f[1], f[2], f[3]);
    } else if (node.type == "Scalar") {
        std::vector<float> f;
        if (!floats("value", 1, f)) return false;
        outLine = std::format("    float {} = {};", v, f[0]);
    } else if (node.type == "Vector2") {
        std::vector<float> f;
        if (!floats("value", 2, f)) return false;
        outLine = std::format("    vec2 {} = vec2({}, {});", v, f[0], f[1]);
    } else if (node.type == "Vector3") {
        std::vector<float> f;
        if (!floats("value", 3, f)) return false;
        outLine = std::format("    vec3 {} = vec3({}, {}, {});", v, f[0], f[1], f[2]);
    } else if (node.type == "TextureSample") {
        const std::string path = paramOf(node, "path");
        if (path.empty()) {
            error = std::format("节点 \"{}\"（TextureSample）参数 path 为空（请指定贴图路径）",
                                node.id);
            return false;
        }
        outLine = std::format("    vec4 {} = texture(gTex{}, vUV);", v, texUnitOf.at(node.id));
    } else if (node.type == "Add")
        outLine = std::format("    vec3 {} = {} + {};", v, get("a", GraphPortType::Vec3),
                              get("b", GraphPortType::Vec3));
    else if (node.type == "Sub")
        outLine = std::format("    vec3 {} = {} - {};", v, get("a", GraphPortType::Vec3),
                              get("b", GraphPortType::Vec3));
    else if (node.type == "Mul")
        outLine = std::format("    vec4 {} = {} * {};", v, get("a", GraphPortType::Vec4),
                              get("b", GraphPortType::Vec4));
    else if (node.type == "Div")
        outLine = std::format("    vec3 {} = {} / max({}, vec3(1e-6));", v,
                              get("a", GraphPortType::Vec3), get("b", GraphPortType::Vec3));
    else if (node.type == "Lerp")
        outLine = std::format("    vec3 {} = mix({}, {}, {});", v, get("a", GraphPortType::Vec3),
                              get("b", GraphPortType::Vec3), get("t", GraphPortType::Float));
    else if (node.type == "Clamp") {
        std::vector<float> fmin, fmax;
        if (!floats("min", 1, fmin) || !floats("max", 1, fmax)) return false;
        outLine = std::format("    float {} = clamp({}, {}, {});", v,
                              get("x", GraphPortType::Float), fmin[0], fmax[0]);
    } else if (node.type == "OneMinus")
        outLine = std::format("    vec3 {} = vec3(1.0) - {};", v, get("x", GraphPortType::Vec3));
    else if (node.type == "Normalize")
        outLine = std::format("    vec3 {} = normalize({});", v, get("x", GraphPortType::Vec3));
    else if (node.type == "Dot")
        outLine = std::format("    float {} = dot({}, {});", v, get("a", GraphPortType::Vec3),
                              get("b", GraphPortType::Vec3));
    else {
        error = std::format("节点 \"{}\" 类型 \"{}\" 代码生成未实现（明确提示而非静默）", node.id,
                            node.type);
        return false;
    }
    return true;
}

// PBR 光照函数模板（与 assets/shaders/blinn_phong.frag 同步维护——改任一处必须同步另一处，
// §27.7-3 错位 = 花屏）。注意：此字符串逐行输出也参与行号计数。
const char* kPbrTemplate = R"GLSL(
const float PI = 3.14159265359;

// PCF 3x3 手动比较 + 斜率自适应偏置（blinn_phong.frag 同步维护）
float shadowFactor(vec3 worldPos, vec3 normal, vec3 lightDir) {
    vec4 lightSpacePos = uLightViewProj * vec4(worldPos, 1.0);
    if (lightSpacePos.w <= 0.0) return 1.0;
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;
    if (abs(proj.x) > 1.0 || abs(proj.y) > 1.0 || proj.z > 1.0) return 1.0;
    vec2 uv = proj.xy * 0.5 + 0.5;
    float current = proj.z * 0.5 + 0.5;
    float bias = max(uShadowBias * (1.0 - abs(dot(normalize(normal), lightDir))), uShadowBias * 0.5);
    float texel = 1.0 / uShadowMapSize;
    float shadow = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            float closest = texture(uShadowMap, uv + vec2(float(x), float(y)) * texel).r;
            shadow += (closest < current - bias) ? 1.0 : 0.0;
        }
    return shadow / 9.0;
}

float distributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-4);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    float k = roughness * roughness * 0.5;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
)GLSL";

// Master 主块生成：未连接槽 → 材质 uniform 回退（与 blinn_phong.frag 同款语义）。
// connectedExpr：槽名 → 连接表达式（空 = 未连接）
void masterCode(const ShaderGraphAsset& graph, const ShaderGraphNode& master,
                const std::map<std::string, const ShaderGraphNode*>& byId,
                const std::map<std::string, const GraphNodeSpec*>& specs,
                SourceBuilder& sb) {
    std::map<std::string, std::string> conn;   // 槽 → 转换后表达式
    for (const auto& e : graph.edges) {
        if (e.toNode != master.id) continue;
        const ShaderGraphNode* from = byId.at(e.fromNode);
        const GraphNodeSpec* fromSpec = specs.at(from->type);
        GraphPortType fromType = GraphPortType::Float;
        for (const auto& p : fromSpec->outputs)
            if (p.name == e.fromPort) fromType = p.type;
        GraphPortType toType = GraphPortType::Float;
        for (const auto& p : specs.at("Master")->inputs)
            if (p.name == e.toPort) toType = p.type;
        conn[e.toPort] = convertExpr(varName(from->id), fromType, toType);
    }

    std::vector<std::string> code;
    // albedo：连接 → 图表达式；未连接 → 材质 albedo 链（贴图 × 颜色）
    code.push_back(conn.count("albedo")
                       ? "    vec3 albedo = " + conn["albedo"] + ";"
                       : "    vec3 albedo = uUseAlbedoMap != 0 ? texture(uAlbedoMap, vUV).rgb * uAlbedo : uAlbedo;");
    // metallic / roughness：同 blinn_phong（贴图 × 数值 + clamp）
    code.push_back(conn.count("metallic")
                       ? "    float metallic = " + conn["metallic"] + ";"
                       : "    float metallic = uUseMetallicMap != 0 ? texture(uMetallicMap, vUV).r * uMetallic : uMetallic;");
    code.push_back(conn.count("roughness")
                       ? "    float roughness = " + conn["roughness"] + ";"
                       : "    float roughness = uUseRoughnessMap != 0 ? texture(uRoughnessMap, vUV).r * uRoughness : uRoughness;");
    code.push_back("    metallic = clamp(metallic, 0.0, 1.0);");
    code.push_back("    roughness = clamp(roughness, 0.045, 1.0);");
    // 法线：连接 → 图表达式（世界空间法线）；未连接 → 材质法线贴图链（TBN，同 blinn_phong）
    if (conn.count("normal")) {
        code.push_back("    vec3 N = normalize(" + conn["normal"] + ");");
    } else {
        code.push_back("    vec3 N = normalize(vNormal);");
        code.push_back("    if (uUseNormalMap != 0) {");
        code.push_back("        vec3 tn = texture(uNormalMap, vUV).rgb * 2.0 - 1.0;");
        code.push_back("        vec3 T = normalize(vTangent - N * dot(vTangent, N));");
        code.push_back("        vec3 B = cross(N, T);");
        code.push_back("        N = normalize(T * tn.x + B * tn.y + N * tn.z);");
        code.push_back("    }");
    }
    // 光照（与 blinn_phong.frag 131-175 同款；emission 在最后）
    code.push_back("    vec3 L = normalize(uLightDir);");
    code.push_back("    vec3 V = normalize(uCamPos.xyz - vWorldPos);");
    code.push_back("    vec3 H = normalize(L + V);");
    code.push_back("    float NdotL = max(dot(N, L), 0.0);");
    code.push_back("    float NdotV = max(dot(N, V), 1e-4);");
    code.push_back("    float NdotH = max(dot(N, H), 0.0);");
    code.push_back("    float HdotV = max(dot(H, V), 0.0);");
    code.push_back("    vec3 F0 = mix(vec3(0.04), albedo, metallic);");
    code.push_back("    vec3 F = fresnelSchlick(HdotV, F0);");
    code.push_back("    float D = distributionGGX(NdotH, roughness);");
    code.push_back("    float G = geometrySmith(NdotV, NdotL, roughness);");
    code.push_back("    vec3 specular = F * D * G / max(4.0 * NdotV * NdotL, 1e-4);");
    code.push_back("    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);");
    code.push_back("    vec3 direct = (kD * albedo / PI + specular) * uLightColor * NdotL;");
    code.push_back("    float shadow = uShadowEnabled != 0 ? shadowFactor(vWorldPos, N, L) : 1.0;");
    code.push_back("    vec3 ambient = uAmbient * albedo;");
    code.push_back("    vec3 indirect = vec3(0.0);");
    code.push_back("    if (uIblIntensity > 0.0) {");
    code.push_back("        float sr = sin(uIblSkyRotation);");
    code.push_back("        float cr = cos(uIblSkyRotation);");
    code.push_back("        vec3 rotN = vec3(cr * N.x + sr * N.z, N.y, -sr * N.x + cr * N.z);");
    code.push_back("        vec3 R = reflect(-V, N);");
    code.push_back("        vec3 rotR = vec3(cr * R.x + sr * R.z, R.y, -sr * R.x + cr * R.z);");
    code.push_back("        vec3 irradiance = texture(uIrrMap, rotN).rgb;");
    code.push_back("        vec3 prefiltered = textureLod(uPrefMap, rotR, roughness * 4.0).rgb;");
    code.push_back("        vec2 brdf = texture(uBrdfLut, vec2(NdotV, roughness)).rg;");
    code.push_back("        vec3 specularIbl = prefiltered * (F * brdf.x + brdf.y);");
    code.push_back("        indirect = (kD * irradiance * albedo + specularIbl) * uIblIntensity;");
    code.push_back("    }");
    // emission：连接 → 图表达式 × 材质强度；未连接 → 材质发光链
    code.push_back(conn.count("emission")
                       ? "    vec3 emission = " + conn["emission"] + " * uEmissionIntensity;"
                       : "    vec3 emission = (uUseEmissionMap != 0 ? texture(uEmissionMap, vUV).rgb : vec3(1.0)) * uEmissionColor * uEmissionIntensity;");
    code.push_back("    vec3 color = ambient + direct * shadow + indirect + emission;");
    code.push_back("    color = uGamma <= 0.0 ? color : pow(max(color, vec3(0.0)), vec3(uGamma));");
    code.push_back("    fragColor = vec4(color, uAlpha);");
    sb.nodeBlock(master.id, master.type, code);
}

}  // namespace

// ==================== 公开接口 ====================

bool ShaderGraphCodeGen::generate(const ShaderGraphAsset& graph, std::string& outFragmentSource,
                                  std::vector<std::string>& outErrors) {
    outFragmentSource.clear();
    outErrors.clear();
    s_lineMap.clear();
    s_texturePaths.clear();

    // 1. 校验（环/类型/缺失输入/未知类型 → 明确错误文本，直接透传给面板）
    outErrors = graph.validateGraph();
    if (!outErrors.empty()) return false;

    // 2. 索引
    std::map<std::string, const ShaderGraphNode*> byId;
    std::map<std::string, const GraphNodeSpec*> specs;
    for (const auto& n : graph.nodes) {
        byId[n.id] = &n;
        specs[n.type] = ShaderGraphAsset::specOf(n.type);
    }

    // 3. 拓扑序（Master 依赖先；游离节点追加）
    std::vector<const ShaderGraphNode*> order;
    topoOrder(graph, byId, order);

    // 4. 纹理节点 → unit 分配（拓扑序内出现的顺序 = gTex0..；提交路径按同序绑定 unit 9+i）
    std::map<std::string, int> texUnitOf;
    for (const ShaderGraphNode* n : order) {
        if (n->type == "TextureSample") {
            texUnitOf[n->id] = static_cast<int>(s_texturePaths.size());
            s_texturePaths.push_back(paramOf(*n, "path"));
        }
    }

    // 5. 生成源
    SourceBuilder sb;
    sb.append("#version 450 core");
    sb.append("");
    sb.append("// ===== NovaEngine Shader Graph 生成（V1.0-5，§27.4）=====");
    sb.append("// 由 ShaderGraphCodeGen 从 .novashadergraph 生成，请勿手改。");
    sb.append("// uniform 契约与 assets/shaders/blinn_phong.frag 逐名一致（错位 = 花屏，§27.7-3）；");
    sb.append("// PBR 光照函数模板与 blinn_phong.frag 同步维护——改任一处必须同步另一处。");
    sb.append("");
    sb.append("in vec3 vWorldPos;");
    sb.append("in vec3 vNormal;");
    sb.append("in vec2 vUV;");
    sb.append("in vec3 vTangent;");
    sb.append("");
    sb.append("layout(std140, binding = 0) uniform CameraBlock {");
    sb.append("    mat4 uViewProj;");
    sb.append("    vec4 uCamPos;");
    sb.append("};");
    sb.append("");
    sb.append("// ===== 灯光（blinn_phong 同款）=====");
    sb.append("uniform vec3 uLightDir;");
    sb.append("uniform vec3 uLightColor;");
    sb.append("uniform vec3 uAmbient;");
    sb.append("");
    sb.append("// ===== 材质（Master 槽未连接 → 回退这些材质 uniform）=====");
    sb.append("uniform vec3 uAlbedo;");
    sb.append("uniform float uRoughness;");
    sb.append("uniform float uMetallic;");
    sb.append("uniform float uAlpha;");
    sb.append("");
    sb.append("// ===== 贴图开关 + 采样器（unit 总表同 blinn_phong：0 albedo/1 shadow/2 normal/");
    sb.append("// 3 metallic/4 roughness/5 emission）=====");
    sb.append("uniform int uUseAlbedoMap;");
    sb.append("uniform sampler2D uAlbedoMap;     // unit 0");
    sb.append("uniform int uUseNormalMap;");
    sb.append("uniform sampler2D uNormalMap;     // unit 2");
    sb.append("uniform int uUseMetallicMap;");
    sb.append("uniform sampler2D uMetallicMap;   // unit 3");
    sb.append("uniform int uUseRoughnessMap;");
    sb.append("uniform sampler2D uRoughnessMap;  // unit 4");
    sb.append("uniform int uUseEmissionMap;");
    sb.append("uniform sampler2D uEmissionMap;   // unit 5");
    sb.append("");
    sb.append("// ===== 自发光 =====");
    sb.append("uniform vec3 uEmissionColor;");
    sb.append("uniform float uEmissionIntensity;");
    sb.append("");
    sb.append("// ===== 阴影（blinn_phong 同款）=====");
    sb.append("uniform sampler2D uShadowMap;   // unit 1");
    sb.append("uniform mat4 uLightViewProj;");
    sb.append("uniform int uShadowEnabled;");
    sb.append("uniform float uShadowBias;");
    sb.append("uniform float uShadowMapSize;");
    sb.append("");
    sb.append("// ===== IBL 环境（blinn_phong 同款；unit 6/7/8）=====");
    sb.append("uniform samplerCube uIrrMap;    // unit 6");
    sb.append("uniform samplerCube uPrefMap;   // unit 7");
    sb.append("uniform sampler2D uBrdfLut;     // unit 8");
    sb.append("uniform float uIblIntensity;");
    sb.append("uniform float uIblSkyRotation;");
    sb.append("");
    sb.append("// ===== 契约占位（与 blinn_phong.frag 同名）：生成 shader 不写 GBuffer（提交路径恒");
    sb.append("// uGbufferPass=0，自定义 shader 不参与 SSAO/DOF 深度写入，v1 备案）；uView 仅声明 =====");
    sb.append("uniform int uGbufferPass;");
    sb.append("uniform mat4 uView;");
    sb.append("");
    sb.append("// ===== 输出 gamma（同 blinn_phong：HDR 0 直通 / 旧路径 1/2.2）=====");
    sb.append("uniform float uGamma;");
    sb.append("");
    sb.append("// ===== 图节点（Time + 纹理节点）=====");
    sb.append("uniform float uTime;   // Time 节点（提交路径每帧设置 Time::totalTime()）");
    for (size_t t = 0; t < s_texturePaths.size(); ++t)
        sb.append(std::format("uniform sampler2D gTex{};   // 图纹理节点 unit {}（提交路径按"
                              "lastTexturePaths 顺序绑定）", t, 9 + t));
    sb.append("");
    sb.append("out vec4 fragColor;");
    sb.append("");

    // PBR 模板（逐行输出，参与行号计数；与 blinn_phong.frag 同步维护）
    std::istringstream tpl(kPbrTemplate);
    std::string tplLine;
    while (std::getline(tpl, tplLine)) sb.append(tplLine);
    sb.append("");

    sb.append("void main() {");
    sb.append("    // ===== 图节点代码块（每块 #line 标记，编译错误可反查节点 id）=====");
    for (const ShaderGraphNode* n : order) {
        if (n->type == "Master") continue;
        std::string codeLine, error;
        if (!nodeCode(graph, *n, byId, specs, texUnitOf, codeLine, error)) {
            outErrors.push_back(error);
            return false;
        }
        sb.nodeBlock(n->id, n->type, {codeLine});
    }
    // Master 主块
    for (const ShaderGraphNode* n : order) {
        if (n->type == "Master") masterCode(graph, *n, byId, specs, sb);
    }
    sb.append("}");
    outFragmentSource = sb.src;
    return true;
}

const std::vector<std::pair<int, std::string>>& ShaderGraphCodeGen::lastLineMap() {
    return s_lineMap;
}

std::vector<std::string> ShaderGraphCodeGen::lastTexturePaths() {
    return s_texturePaths;
}

bool ShaderGraphCodeGen::compileGenerated(const std::string& fragmentSource,
                                          std::string& outLog) {
    outLog.clear();
    std::string vsSrc;
    if (!AssetStream::readAllText("assets/shaders/blinn_phong.vert", vsSrc)) {
        outLog = "无法读取 assets/shaders/blinn_phong.vert（顶点源码，提交路径依赖）";
        return false;
    }

    // 编译 vs + fs（单遍收集完整 log；不删 log 细节，面板展示）
    auto compileOne = [&](GLenum type, const std::string& src, std::string& log) {
        const uint32_t sh = glCreateShader(type);
        const char* p = src.c_str();
        glShaderSource(sh, 1, &p, nullptr);
        glCompileShader(sh);
        GLint ok = 0;
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        GLint len = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
        if (len > 1) {
            std::string l(static_cast<size_t>(len - 1), '\0');
            glGetShaderInfoLog(sh, len, nullptr, l.data());
            log += l;
        }
        glDeleteShader(sh);
        return ok != 0;
    };
    std::string log;
    const bool vsOk = compileOne(GL_VERTEX_SHADER, vsSrc, log);
    const bool fsOk = compileOne(GL_FRAGMENT_SHADER, fragmentSource, log);
    if (vsOk && fsOk) return true;
    outLog = log;
    return false;
}

std::vector<ShaderGraphCodeGen::GraphNodeError> ShaderGraphCodeGen::mapCompileErrors(
    const std::string& log) {
    std::vector<GraphNodeError> out;
    // 行号反查（±1 容差防驱动 #line 行号偏移）
    const auto nodeForLine = [&](int line) -> const std::string* {
        for (const auto& [l, id] : s_lineMap)
            if (std::abs(l - line) <= 1) return &id;
        return nullptr;
    };
    const auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    std::istringstream ss(log);
    std::string line;
    while (std::getline(ss, line)) {
        if (lower(line).find("error") == std::string::npos) continue;   // 大小写不敏感（NVIDIA
                                                                        // "error C0000" 小写）
        GraphNodeError e;
        e.message = line;
        // 行号提取：优先 "N(line) :"（NVIDIA 形态 "1(124) : error ..."）；
        // 回退 ":line:"（"ERROR: 0:112: ..." 形态）
        const auto paren = line.find('(');
        if (paren != std::string::npos) {
            size_t d = paren + 1;
            while (d < line.size() && std::isdigit(static_cast<unsigned char>(line[d]))) ++d;
            if (d > paren + 1 && d < line.size() && line[d] == ')')
                e.line = std::stoi(line.substr(paren + 1, d - paren - 1));
        }
        if (e.line <= 0) {
            size_t pos = 0;
            while ((pos = line.find(':', pos)) != std::string::npos) {
                size_t d = pos + 1;
                while (d < line.size() && std::isdigit(static_cast<unsigned char>(line[d]))) ++d;
                if (d > pos + 1) {
                    e.line = std::stoi(line.substr(pos + 1, d - pos - 1));
                    break;
                }
                pos = d;
            }
        }
        if (e.line > 0)
            if (const std::string* id = nodeForLine(e.line)) e.nodeId = *id;
        out.push_back(std::move(e));
    }
    return out;
}

}  // namespace NovaEngine
