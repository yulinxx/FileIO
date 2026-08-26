#include "FileIO/Parsers/ObjParser.h"

#include "IrProjector.h"
#include "Log/SyLogger.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace Fio
{
    namespace
    {
        // 单个文件的三角面上限：OBJ 是文本格式，畸形/超大文件会把内存吃光。
        // 2000 万三角 ≈ 顶点+法线约 1.4GB，已远超工艺场景需要。
        constexpr size_t kMaxTriangles = 20'000'000;
        // 单个面的顶点数上限，防畸形 f 行（正常最多几十）
        constexpr size_t kMaxFaceVerts = 1024;

        struct Vec3
        {
            double x = 0.0, y = 0.0, z = 0.0;
        };

        /// OBJ 索引可以是负数（相对当前已读数量，-1 = 最后一个）。
        /// 返回 0-based 索引；越界返回 -1。
        long long resolveIndex(long long raw, size_t count)
        {
            if (raw > 0)
            {
                const long long idx = raw - 1;
                return (static_cast<size_t>(idx) < count) ? idx : -1;
            }
            if (raw < 0)
            {
                const long long idx = static_cast<long long>(count) + raw;
                return (idx >= 0 && static_cast<size_t>(idx) < count) ? idx : -1;
            }
            return -1;  // OBJ 索引从 1 开始，0 非法
        }

        /// 解析 f 行的一个顶点串："v"、"v/vt"、"v//vn"、"v/vt/vn"
        /// @return false 表示这一段无法解析
        bool parseFaceVertex(const std::string& token, long long& vIdx, long long& vnIdx)
        {
            vIdx = 0;
            vnIdx = 0;

            const size_t first = token.find('/');
            if (first == std::string::npos)
            {
                return std::sscanf(token.c_str(), "%lld", &vIdx) == 1;
            }

            if (std::sscanf(token.substr(0, first).c_str(), "%lld", &vIdx) != 1)
            {
                return false;
            }

            const size_t second = token.find('/', first + 1);
            if (second == std::string::npos)
            {
                return true;  // v/vt，没有法线
            }
            const std::string vnPart = token.substr(second + 1);
            if (!vnPart.empty())
            {
                std::sscanf(vnPart.c_str(), "%lld", &vnIdx);
            }
            return true;
        }

        Vec3 faceNormal(const Vec3& a, const Vec3& b, const Vec3& c)
        {
            const Vec3 u{ b.x - a.x, b.y - a.y, b.z - a.z };
            const Vec3 v{ c.x - a.x, c.y - a.y, c.z - a.z };
            Vec3 n{ u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
            const double len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            if (len < 1e-20)
            {
                return Vec3{ 0.0, 0.0, 1.0 };
            }
            n.x /= len;
            n.y /= len;
            n.z /= len;
            return n;
        }

        /// 解析期的一个网格分块：OBJ 的 o / g / usemtl 任一发生变化就切一块，
        /// 每块最终成为一个 Mesh3D 图元，并挂到对应群组下。
        struct MeshChunk
        {
            std::string objectName;    // 最近的 o
            std::string groupName;     // 最近的 g
            std::string materialName;  // 最近的 usemtl
            std::vector<ParsedPoint3D> vertices;
            std::vector<ParsedPoint3D> normals;
        };

        bool sameChunkKey(const MeshChunk& c, const std::string& obj, const std::string& grp, const std::string& mtl)
        {
            return c.objectName == obj && c.groupName == grp && c.materialName == mtl;
        }
    }  // namespace

    FileFormat ObjParser::format() const
    {
        return FileFormat::OBJ;
    }

    size_t ObjParser::formatName(char* buffer, size_t bufferSize) const
    {
        const char* name = "Wavefront OBJ";
        const size_t len = std::strlen(name);
        if (buffer != nullptr && bufferSize > len)
        {
            std::strcpy(buffer, name);
        }
        return len;
    }

    void ObjParser::forEachSupportedExtension(void (*visitor)(const char*, void*), void* ctx) const
    {
        visitor("obj", ctx);
    }

    FioParseResult ObjParser::parseToIR(const char* filePath)
    {
        if (filePath == nullptr)
        {
            SY_ERRORF("[ObjParser] parseToIR: null file path");
            return FioParseResult{};
        }
        SY_INFOF("[ObjParser] parseToIR START: filePath=%s", filePath);

        // Windows 下窄字符路径按 ANSI 码页解析，中文路径打不开；统一走 u8path（与其余解析器一致）
        std::ifstream in(std::filesystem::u8path(filePath), std::ios::binary);
        if (!in)
        {
            SY_ERRORF("[ObjParser] Cannot open file: %s", filePath);
            return FioParseResult{};
        }

        ParseData data;

        std::vector<Vec3> positions;
        std::vector<Vec3> normals;
        std::vector<MeshChunk> chunks;

        std::string currentObject;
        std::string currentGroup;
        std::string currentMaterial;
        bool warnedTexcoord = false;
        bool warnedFreeform = false;
        size_t triangleCount = 0;
        bool truncated = false;

        std::string line;
        while (std::getline(in, line))
        {
            // 去掉 CR（CRLF 文件在 Unix 风格读取下会留下 \r）与注释
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            const size_t hash = line.find('#');
            if (hash != std::string::npos)
            {
                line.erase(hash);
            }
            if (line.empty())
            {
                continue;
            }

            std::istringstream ls(line);
            std::string tag;
            ls >> tag;

            if (tag == "v")
            {
                Vec3 p;
                ls >> p.x >> p.y >> p.z;
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                {
                    data.warnings.emplace_back("OBJ: non-finite vertex skipped");
                    continue;
                }
                positions.push_back(p);
            }
            else if (tag == "vn")
            {
                Vec3 n;
                ls >> n.x >> n.y >> n.z;
                normals.push_back(n);
            }
            else if (tag == "vt")
            {
                if (!warnedTexcoord)
                {
                    warnedTexcoord = true;
                    data.warnings.emplace_back("OBJ: texture coordinates are ignored");
                }
            }
            else if (tag == "o")
            {
                std::getline(ls, currentObject);
                if (!currentObject.empty() && currentObject.front() == ' ')
                {
                    currentObject.erase(0, 1);
                }
                // 新物体默认清掉组/材质上下文，避免跨物体串味
                currentGroup.clear();
            }
            else if (tag == "g")
            {
                std::getline(ls, currentGroup);
                if (!currentGroup.empty() && currentGroup.front() == ' ')
                {
                    currentGroup.erase(0, 1);
                }
            }
            else if (tag == "usemtl")
            {
                ls >> currentMaterial;
            }
            else if (tag == "mtllib" || tag == "s")
            {
                // 材质库与平滑组不影响几何本身，静默跳过（不产出 warning 以免刷屏）
            }
            else if (tag == "curv" || tag == "surf" || tag == "cstype" || tag == "deg")
            {
                if (!warnedFreeform)
                {
                    warnedFreeform = true;
                    data.warnings.emplace_back("OBJ: free-form surfaces are not supported");
                }
            }
            else if (tag == "f")
            {
                if (truncated)
                {
                    continue;
                }

                std::vector<long long> faceV;
                std::vector<long long> faceVN;
                std::string token;
                while (ls >> token)
                {
                    if (faceV.size() >= kMaxFaceVerts)
                    {
                        data.warnings.emplace_back("OBJ: face with too many vertices truncated");
                        break;
                    }
                    long long vi = 0;
                    long long ni = 0;
                    if (!parseFaceVertex(token, vi, ni))
                    {
                        continue;
                    }
                    faceV.push_back(vi);
                    faceVN.push_back(ni);
                }

                if (faceV.size() < 3)
                {
                    data.warnings.emplace_back("OBJ: face with less than 3 vertices skipped");
                    continue;
                }

                // 找/建当前分块
                if (chunks.empty() || !sameChunkKey(chunks.back(), currentObject, currentGroup, currentMaterial))
                {
                    MeshChunk c;
                    c.objectName = currentObject;
                    c.groupName = currentGroup;
                    c.materialName = currentMaterial;
                    chunks.push_back(std::move(c));
                }
                MeshChunk& chunk = chunks.back();

                // 扇形三角化：(0, i, i+1)。凹多边形会不完美，但 OBJ 里非三角面本就少见，
                // 且工艺链路后续只用三角面，这里保持与 Engine3D 旧实现一致的行为。
                for (size_t i = 1; i + 1 < faceV.size(); ++i)
                {
                    if (triangleCount >= kMaxTriangles)
                    {
                        truncated = true;
                        data.warnings.emplace_back("OBJ: triangle limit reached, remaining faces skipped");
                        SY_WARNF("[ObjParser] Triangle limit %zu reached, truncating: %s", kMaxTriangles, filePath);
                        break;
                    }

                    const size_t corner[3] = { 0, i, i + 1 };
                    Vec3 pos[3];
                    long long nIdx[3];
                    bool ok = true;
                    for (int k = 0; k < 3; ++k)
                    {
                        const long long vi = resolveIndex(faceV[corner[k]], positions.size());
                        if (vi < 0)
                        {
                            ok = false;
                            break;
                        }
                        pos[k] = positions[static_cast<size_t>(vi)];
                        nIdx[k] = resolveIndex(faceVN[corner[k]], normals.size());
                    }
                    if (!ok)
                    {
                        data.warnings.emplace_back("OBJ: face references an out-of-range vertex, triangle skipped");
                        continue;
                    }

                    // 法线缺失时用面法线补齐：消费侧要求法线与顶点一一对应
                    const Vec3 fn = faceNormal(pos[0], pos[1], pos[2]);
                    for (int k = 0; k < 3; ++k)
                    {
                        chunk.vertices.push_back(ParsedPoint3D{ pos[k].x, pos[k].y, pos[k].z });
                        const Vec3 n = (nIdx[k] >= 0) ? normals[static_cast<size_t>(nIdx[k])] : fn;
                        chunk.normals.push_back(ParsedPoint3D{ n.x, n.y, n.z });
                    }
                    ++triangleCount;
                }
            }
        }

        if (chunks.empty())
        {
            SY_ERRORF("[ObjParser] No faces found in file: %s", filePath);
            return FioParseResult{};
        }

        // ---- 分块 → 群组树 + Mesh3D 图元 ----
        // 群组语义：o 物体是顶层群组，g / usemtl 形成的分段是它的子群组。
        // 只有一个匿名分块时不建群组，避免给单体模型套一层没有信息量的壳。
        uint64_t nextGroupId = 1;
        uint64_t nextEntityId = 1;
        std::vector<std::pair<std::string, uint64_t>> objectGroups;  // 物体名 → 群组 id

        const bool needGroups = chunks.size() > 1 || !chunks.front().objectName.empty() ||
            !chunks.front().groupName.empty() || !chunks.front().materialName.empty();

        for (const MeshChunk& chunk : chunks)
        {
            if (chunk.vertices.empty())
            {
                continue;
            }

            uint64_t groupId = 0;
            if (needGroups)
            {
                uint64_t objectGroupId = 0;
                if (!chunk.objectName.empty())
                {
                    // 同名物体复用同一个顶层群组：OBJ 里 o 段可能被 g/usemtl 切成多块
                    for (const auto& og : objectGroups)
                    {
                        if (og.first == chunk.objectName)
                        {
                            objectGroupId = og.second;
                            break;
                        }
                    }
                    if (objectGroupId == 0)
                    {
                        ParsedGroup pg;
                        pg.sourceId = nextGroupId++;
                        pg.name = chunk.objectName;
                        data.groups.push_back(pg);
                        objectGroupId = pg.sourceId;
                        objectGroups.emplace_back(chunk.objectName, objectGroupId);
                    }
                }

                std::string childName = chunk.groupName;
                if (!chunk.materialName.empty())
                {
                    childName += childName.empty() ? chunk.materialName : ("/" + chunk.materialName);
                }

                if (!childName.empty())
                {
                    ParsedGroup pg;
                    pg.sourceId = nextGroupId++;
                    pg.parentGroupSourceId = objectGroupId;
                    pg.name = childName;
                    data.groups.push_back(pg);
                    groupId = pg.sourceId;
                }
                else
                {
                    groupId = objectGroupId;
                }
            }

            ParsedGeometry g;
            g.sourceId = nextEntityId++;
            g.type = ParsedGeometryType::Mesh3D;
            g.groupSourceId = groupId;
            g.name = !chunk.objectName.empty() ? chunk.objectName
                : (!chunk.groupName.empty() ? chunk.groupName : std::string("mesh"));
            g.mesh.vertices = chunk.vertices;
            g.mesh.normals = chunk.normals;
            // 顶点已按三角形逐角展开，索引就是顺序自增；消费侧目前只用顶点+法线，
            // 索引保留给未来做焊接/去重时用。
            g.mesh.indices.reserve(chunk.vertices.size());
            for (size_t i = 0; i < chunk.vertices.size(); ++i)
            {
                g.mesh.indices.push_back(static_cast<uint32_t>(i));
            }
            data.geometries.push_back(std::move(g));
        }

        if (data.geometries.empty())
        {
            SY_ERRORF("[ObjParser] All chunks were empty: %s", filePath);
            return FioParseResult{};
        }

        data.success = true;
        // 与其它 parser 保持同一条 END 文案格式，便于按 "parseToIR END" 一把捞出所有格式的收尾统计
        SY_INFOF("[ObjParser] parseToIR END: %zu mesh chunk(s), %zu triangles, %zu group(s): %s",
            data.geometries.size(),
            triangleCount,
            data.groups.size(),
            filePath);

        // OBJ 规范里没有单位声明，sourceUnit 留空表示「与当前文档同单位」
        return IrProjector::project(data, "OBJ");
    }
}  // namespace Fio
