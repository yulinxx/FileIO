#include "FileIO/Parsers/StlParser.h"

#include <fstream>
#include <sstream>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace Fio
{
    void StlParser::forEachSupportedExtension(void(*visitor)(const char*, void*), void* ctx) const
    {
        visitor("stl", ctx);
    }

    size_t StlParser::formatName(char* buffer, size_t bufferSize) const
    {
        const char* name = "STL (Stereolithography)";
        const size_t len = std::strlen(name);
        if (buffer != nullptr && bufferSize > len)
            std::strcpy(buffer, name);
        return len;
    }

    FioParseResult StlParser::parseToIR(const char* filePath)
    {
        FioParseResult result;
        result.sourceFormat[0] = '\0';
        std::strncpy(result.sourceFormat, "STL", sizeof(result.sourceFormat) - 1);

        std::filesystem::path fsPath = std::filesystem::u8path(filePath);
        std::ifstream file(fsPath, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            return result;

        std::streamsize fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        if (fileSize < 15)
            return result;

        std::vector<uint8_t> data(static_cast<size_t>(fileSize));
        if (!file.read(reinterpret_cast<char*>(data.data()), fileSize))
            return result;

        // 检测格式并解析
        uint32_t triangleCount = 0;
        std::vector<float> vertices;
        std::vector<float> normals;

        if (data.size() >= 84)
        {
            uint32_t count = 0;
            std::memcpy(&count, data.data() + 80, 4);
            size_t expectedSize = 84 + static_cast<size_t>(count) * 50;
            if (expectedSize == data.size())
            {
                triangleCount = count;
                vertices.reserve(triangleCount * 9);
                normals.reserve(triangleCount * 9);

                const uint8_t* ptr = data.data() + 84;
                for (uint32_t i = 0; i < triangleCount; ++i)
                {
                    float nx, ny, nz;
                    std::memcpy(&nx, ptr, 4); ptr += 4;
                    std::memcpy(&ny, ptr, 4); ptr += 4;
                    std::memcpy(&nz, ptr, 4); ptr += 4;

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
                        if (len > 0.0001f) { nx /= len; ny /= len; nz /= len; }
                        else { nx = 0; ny = 0; nz = 1; }

                        vertices.push_back(v[0]); vertices.push_back(v[1]); vertices.push_back(v[2]);
                        vertices.push_back(v[3]); vertices.push_back(v[4]); vertices.push_back(v[5]);
                        vertices.push_back(v[6]); vertices.push_back(v[7]); vertices.push_back(v[8]);
                    }
                    else
                    {
                        float v[9];
                        std::memcpy(v, ptr, 36);
                        ptr += 36;
                        vertices.push_back(v[0]); vertices.push_back(v[1]); vertices.push_back(v[2]);
                        vertices.push_back(v[3]); vertices.push_back(v[4]); vertices.push_back(v[5]);
                        vertices.push_back(v[6]); vertices.push_back(v[7]); vertices.push_back(v[8]);
                    }

                    // 每个顶点使用相同法线
                    for (int j = 0; j < 3; ++j)
                    {
                        normals.push_back(nx);
                        normals.push_back(ny);
                        normals.push_back(nz);
                    }

                    ptr += 2; // 跳过 attribute byte count
                }
            }
        }

        // 如果 binary 解析失败，尝试 ASCII
        if (triangleCount == 0)
        {
            std::string header(reinterpret_cast<const char*>(data.data()),
                std::min(data.size(), size_t(256)));
            std::string lowerHeader = header;
            std::transform(lowerHeader.begin(), lowerHeader.end(), lowerHeader.begin(),
                [](unsigned char c) { return std::tolower(c); });

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
                    if (start == std::string::npos) continue;
                    auto end = line.find_last_not_of(" \t\r\n");
                    std::string trimmed = line.substr(start, end - start + 1);

                    std::string lower = trimmed;
                    std::transform(lower.begin(), lower.end(), lower.begin(),
                        [](unsigned char c) { return std::tolower(c); });

                    if (lower.find("facet normal") != std::string::npos)
                    {
                        std::istringstream ls(trimmed);
                        std::string token;
                        ls >> token >> token;
                        if (ls >> nx >> ny >> nz) { }
                        vertRead = 0;
                    }
                    else if (lower.find("vertex") != std::string::npos)
                    {
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
                                if (len > 0.0001f) { nx /= len; ny /= len; nz /= len; }
                                else { nx = 0; ny = 0; nz = 1; }
                            }

                            for (int j = 0; j < 9; ++j)
                                vertices.push_back(v[j]);
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
            return result;

        // 填充 IR
        thread_local EntityInfo s_info;
        s_info = {};
        s_info.type = EntityType::Mesh3D;
        s_info.meshVertCount = triangleCount * 3;
        s_info.meshTriCount = triangleCount;

        // 文件名作为图元名称
        std::filesystem::path p(filePath);
        std::string stem = p.stem().string();
        std::strncpy(s_info.name, stem.c_str(), sizeof(s_info.name) - 1);

        // 扩展数据布局: [顶点: meshVertCount*3*sizeof(float)] [法线: meshVertCount*3*sizeof(float)]
        size_t vertBytes = vertices.size() * sizeof(float);
        size_t normBytes = normals.size() * sizeof(float);
        size_t totalBytes = vertBytes + normBytes;

        thread_local std::vector<uint8_t> s_blob;
        s_blob.resize(totalBytes);
        std::memcpy(s_blob.data(), vertices.data(), vertBytes);
        std::memcpy(s_blob.data() + vertBytes, normals.data(), normBytes);

        s_info.extensionDataOffset = 0;
        s_info.extensionDataSize = static_cast<uint32_t>(totalBytes);

        result.entities = &s_info;
        result.entityCount = 1;
        result.extensionBlob.data = s_blob.data();
        result.extensionBlob.size = totalBytes;

        return result;
    }

} // namespace Fio
