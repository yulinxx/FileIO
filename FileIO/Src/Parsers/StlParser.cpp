#include "FileIO/Parsers/StlParser.h"

#include "IrProjector.h"

#include "Log/SyLogger.h"

#include <fstream>
#include <sstream>
#include <chrono>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace Fio
{
    void StlParser::forEachSupportedExtension(void (*visitor)(const char*, void*), void* ctx) const
    {
        visitor("stl", ctx);
    }

    size_t StlParser::formatName(char* buffer, size_t bufferSize) const
    {
        const char* name = "STL (Stereolithography)";
        const size_t len = std::strlen(name);
        if (buffer != nullptr && bufferSize > len)
        {
            std::strcpy(buffer, name);
        }
        return len;
    }

    FioParseResult StlParser::parseToIR(const char* filePath)
    {
        FioParseResult result;
        result.sourceFormat[0] = '\0';
        std::strncpy(result.sourceFormat, "STL", sizeof(result.sourceFormat) - 1);

        if (!filePath || !*filePath)
        {
            SY_ERROR("[StlParser] parseToIR: null or empty file path");
            return result;
        }

        SY_INFOF("[StlParser] parseToIR START: filePath=%s", filePath);
        const auto startTime = std::chrono::steady_clock::now();

        std::filesystem::path fsPath = std::filesystem::u8path(filePath);
        std::ifstream file(fsPath, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            SY_ERRORF("[StlParser] parseToIR: cannot open file: %s", filePath);
            return result;
        }

        std::streamsize fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        if (fileSize < 15)
        {
            SY_ERRORF("[StlParser] parseToIR: file too small to be STL (%lld bytes): %s",
                static_cast<long long>(fileSize),
                filePath);
            return result;
        }

        std::vector<uint8_t> data(static_cast<size_t>(fileSize));
        if (!file.read(reinterpret_cast<char*>(data.data()), fileSize))
        {
            SY_ERRORF("[StlParser] parseToIR: read failed at %lld bytes: %s",
                static_cast<long long>(fileSize),
                filePath);
            return result;
        }

        // 检测格式并解析
        uint32_t triangleCount = 0;
        std::vector<float> vertices;
        std::vector<float> normals;

        // [F3-P1 防护] STL 二进制格式的三角形数量来自文件头，恶意文件可能声明极大值导致 OOM。
        // 设置上限：最大 5000 万三角形（约 5.7GB 顶点数据），超出视为格式错误。
        constexpr uint32_t MAX_STL_TRIANGLES = 50'000'000;

        if (data.size() >= 84)
        {
            uint32_t count = 0;
            std::memcpy(&count, data.data() + 80, 4);

            // 先用「84 + count*50 == 文件长度」判定是否真的是二进制 STL，再谈数量上限。
            // 顺序不能反：ASCII STL 的第 80~84 字节是普通文本，按小端读出来往往是个天文数字，
            // 早先在这里直接按上限拒绝，导致**合法的 ASCII STL（长度 > 84 字节）被整份拒收**，
            // 而不是落到下面的 ASCII 分支。
            const bool sizeMatchesBinary =
                (count <= MAX_STL_TRIANGLES) && (84 + static_cast<size_t>(count) * 50 == data.size());

            if (!sizeMatchesBinary && count > MAX_STL_TRIANGLES && data.size() >= 84 + 50)
            {
                // 声明数量超限且长度也对不上：可能是被截断的超大二进制文件，留个告警便于排查。
                // 不在此 return——仍给 ASCII 分支一个机会，由它决定是否有 facet 可解析。
                SY_WARNF("[StlParser] Binary STL header claims %u triangles (limit %u), falling back to ASCII probe",
                    count,
                    MAX_STL_TRIANGLES);
            }

            if (sizeMatchesBinary)
            {
                triangleCount = count;
                vertices.reserve(triangleCount * 9);
                normals.reserve(triangleCount * 9);


                const uint8_t* ptr = data.data() + 84;
                for (uint32_t i = 0; i < triangleCount; ++i)
                {
                    float nx, ny, nz;
                    std::memcpy(&nx, ptr, 4);
                    ptr += 4;
                    std::memcpy(&ny, ptr, 4);
                    ptr += 4;
                    std::memcpy(&nz, ptr, 4);
                    ptr += 4;

                    // 如果法线为零向量，稍后重新计算
                    if (std::abs(nx) < 0.0001f && std::abs(ny) < 0.0001f && std::abs(nz) < 0.0001f)
                    {
                        // 先读取顶点
                        float v[9];
                        std::memcpy(v, ptr, 36);
                        ptr += 36;
                        // 计算面法线
                        float ax = v[3] - v[0], ay = v[4] - v[1], az = v[5] - v[2];
                        float bx = v[6] - v[0], by = v[7] - v[1], bz = v[8] - v[2];
                        nx = ay * bz - az * by;
                        ny = az * bx - ax * bz;
                        nz = ax * by - ay * bx;
                        float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                        if (len > 0.0001f)
                        {
                            nx /= len;
                            ny /= len;
                            nz /= len;
                        }
                        else
                        {
                            nx = 0;
                            ny = 0;
                            nz = 1;
                        }

                        vertices.push_back(v[0]);
                        vertices.push_back(v[1]);
                        vertices.push_back(v[2]);
                        vertices.push_back(v[3]);
                        vertices.push_back(v[4]);
                        vertices.push_back(v[5]);
                        vertices.push_back(v[6]);
                        vertices.push_back(v[7]);
                        vertices.push_back(v[8]);
                    }
                    else
                    {
                        float v[9];
                        std::memcpy(v, ptr, 36);
                        ptr += 36;
                        vertices.push_back(v[0]);
                        vertices.push_back(v[1]);
                        vertices.push_back(v[2]);
                        vertices.push_back(v[3]);
                        vertices.push_back(v[4]);
                        vertices.push_back(v[5]);
                        vertices.push_back(v[6]);
                        vertices.push_back(v[7]);
                        vertices.push_back(v[8]);
                    }

                    // 每个顶点使用相同法线
                    for (int j = 0; j < 3; ++j)
                    {
                        normals.push_back(nx);
                        normals.push_back(ny);
                        normals.push_back(nz);
                    }

                    ptr += 2;  // 跳过 attribute byte count
                }
            }
        }

        // 如果 binary 解析失败，尝试 ASCII
        if (triangleCount == 0)
        {
            std::string header(reinterpret_cast<const char*>(data.data()), std::min(data.size(), size_t(256)));
            std::string lowerHeader = header;
            std::transform(lowerHeader.begin(), lowerHeader.end(), lowerHeader.begin(), [](unsigned char c) {
                return std::tolower(c);
            });

            if (lowerHeader.find("solid") != std::string::npos)
            {
                std::string content(reinterpret_cast<const char*>(data.data()), data.size());
                std::istringstream stream(content);

                float nx = 0, ny = 0, nz = 1;
                int vertRead = 0;
                float v[9];

                std::string line;
                while (std::getline(stream, line))
                {
                    auto start = line.find_first_not_of(" \t\r\n");
                    if (start == std::string::npos)
                    {
                        continue;
                    }
                    auto end = line.find_last_not_of(" \t\r\n");
                    std::string trimmed = line.substr(start, end - start + 1);

                    std::string lower = trimmed;
                    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                        return std::tolower(c);
                    });

                    if (lower.find("facet normal") != std::string::npos)
                    {
                        std::istringstream ls(trimmed);
                        std::string token;
                        ls >> token >> token;
                        if (ls >> nx >> ny >> nz)
                        {
                        }
                        vertRead = 0;
                    }
                    else if (lower.find("vertex") != std::string::npos)
                    {
                        // [F1-P0 修复] 恶意 STL 文件可能在单个 facet 内写入超过 3 个 vertex 行，
                        // 导致 vertRead >= 3 时 v[vertRead*3] 越界写栈缓冲区。
                        // 此处加守卫：超出 3 个顶点时跳过，防止栈溢出。
                        if (vertRead >= 3)
                        {
                            SY_WARNF("[StlParser] ASCII STL: facet vertex count exceeds 3 (vertRead=%d), skipping "
                                     "extra vertex",
                                vertRead);
                            continue;
                        }
                        std::istringstream ls(trimmed);
                        std::string token;
                        ls >> token;
                        float x, y, z;
                        if (ls >> x >> y >> z)
                        {
                            v[vertRead * 3] = x;
                            v[vertRead * 3 + 1] = y;
                            v[vertRead * 3 + 2] = z;
                            ++vertRead;
                        }
                    }
                    else if (lower.find("endfacet") != std::string::npos)
                    {
                        if (vertRead == 3)
                        {
                            // 如果法线为零，重新计算
                            if (std::abs(nx) < 0.0001f && std::abs(ny) < 0.0001f && std::abs(nz) < 0.0001f)
                            {
                                float ax = v[3] - v[0], ay = v[4] - v[1], az = v[5] - v[2];
                                float bx = v[6] - v[0], by = v[7] - v[1], bz = v[8] - v[2];
                                nx = ay * bz - az * by;
                                ny = az * bx - ax * bz;
                                nz = ax * by - ay * bx;
                                float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                                if (len > 0.0001f)
                                {
                                    nx /= len;
                                    ny /= len;
                                    nz /= len;
                                }
                                else
                                {
                                    nx = 0;
                                    ny = 0;
                                    nz = 1;
                                }
                            }

                            for (int j = 0; j < 9; ++j)
                            {
                                vertices.push_back(v[j]);
                            }
                            for (int j = 0; j < 3; ++j)
                            {
                                normals.push_back(nx);
                                normals.push_back(ny);
                                normals.push_back(nz);
                            }
                            triangleCount++;
                        }
                    }
                    else if (lower.find("endsolid") != std::string::npos)
                    {
                        break;
                    }
                }
            }
        }

        if (triangleCount == 0)
        {
            // 二进制探测与 ASCII 探测都没拿到 facet：文件既不是合法二进制 STL，也没有可解析的 ASCII facet
            SY_ERRORF("[StlParser] parseToIR: no triangle parsed (neither binary nor ASCII layout matched, %lld "
                      "bytes): %s",
                static_cast<long long>(fileSize),
                filePath);
            return result;
        }

        // ---- 填充 IR ----
        // 缓冲区统一由 IrPublisher 持有：此前这里自己声明 thread_local EntityInfo /
        // vector<uint8_t>，与 DXF、SVG、PLT 各写一套，同一个跨 DLL 内存契约被复制了多份。
        IrPublisher& pub = IrPublisher::threadLocal();
        pub.reset();

        EntityInfo info{};
        info.sourceId = 1;  // 1-based，0 在 IR 里是「无」的哨兵
        info.type = EntityType::Mesh3D;
        info.meshVertCount = triangleCount * 3;
        info.meshTriCount = triangleCount;

        // 文件名作为图元名称
        std::filesystem::path p(std::filesystem::u8path(filePath));
        const std::string stem = p.stem().string();
        std::strncpy(info.name, stem.c_str(), sizeof(info.name) - 1);
        info.name[sizeof(info.name) - 1] = '\0';

        // 扩展数据布局: [顶点: meshVertCount*3 float] [法线: meshVertCount*3 float]
        // 与 IrProjector 投影 Mesh3D 时的布局严格一致（OBJ 走投影层、STL 走这里，
        // 两条路产出的字节布局必须相同，消费侧才能只有一份读取代码）。
        const size_t vertBytes = vertices.size() * sizeof(float);
        const size_t normBytes = normals.size() * sizeof(float);

        const uint32_t offset = pub.appendBlob(vertices.data(), vertBytes);
        if (offset == IrPublisher::kInvalidOffset)
        {
            SY_ERRORF("[StlParser] Mesh data too large for IR extension blob: %s", filePath);
            return FioParseResult{};
        }
        if (pub.appendBlob(normals.data(), normBytes) == IrPublisher::kInvalidOffset)
        {
            SY_ERRORF("[StlParser] Normal data too large for IR extension blob: %s", filePath);
            return FioParseResult{};
        }
        info.extensionDataOffset = offset;
        info.extensionDataSize = static_cast<uint32_t>(vertBytes + normBytes);

        pub.entities().push_back(info);

        const auto elapsedMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();
        SY_INFOF("[StlParser] parseToIR END: 1 mesh entity, %u triangles, %u vertices, %zu blob bytes, %lld ms: %s",
            triangleCount,
            info.meshVertCount,
            static_cast<size_t>(info.extensionDataSize),
            static_cast<long long>(elapsedMs),
            filePath);

        // STL 规范不带单位信息，sourceUnit 留空表示「与当前文档同单位」
        return pub.publish("STL");
    }
}  // namespace Fio
