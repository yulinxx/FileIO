// 确保在任何标准头文件之前定义，使 PltHpglInterpreter.h 中的 M_PI 可用
#ifndef _USE_MATH_DEFINES
    #define _USE_MATH_DEFINES
#endif

#include "FileIO/Parsers/PltParser.h"
#include "FileIO/Parsers/PltHpglInterpreter.h"

#include "Log/SyLogger.h"

#include <fstream>
#include <sstream>
#include <regex>
#include <cmath>
#include <algorithm>
#include <memory>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <vector>

namespace Fio
{
    FileFormat PltParser::format() const
    {
        return FileFormat::PLT;
    }

    size_t PltParser::formatName(char* buffer, size_t bufferSize) const
    {
        const char* name = "HPGL Plot (PLT)";
        const size_t len = std::strlen(name);
        if (buffer != nullptr && bufferSize > len)
        {
            std::strcpy(buffer, name);
        }
        return len;
    }

    void PltParser::forEachSupportedExtension(void (*visitor)(const char*, void*), void* ctx) const
    {
        visitor("plt", ctx);
        visitor("hpgl", ctx);
    }

    // ========================================================================
    // PltParser::parseToIR() — 中立 IR 解析路径
    // HPGL 文本 → PltHpglInterpreter → EntityInfo(POD)
    // 不依赖 Engine2D 类型，跨 DLL 安全
    // PLT 几何（Line/Arc/Circle）全部用 EntityInfo 内联字段承载，无需 extensionBlob
    // ========================================================================
    FioParseResult PltParser::parseToIR(const char* filePath)
    {
        SY_INFOF("[PltParser] parseToIR START: %s", filePath ? filePath : "");

        // thread_local 缓冲区管理生命周期（与 DxfParser/StepParser 一致）
        thread_local std::vector<EntityInfo> s_entities;
        thread_local std::vector<std::string> s_warnings;
        s_entities.clear();
        s_warnings.clear();

        if (!filePath)
        {
            SY_ERROR("[PltParser] parseToIR: null filePath");
            return FioParseResult{};
        }

        std::filesystem::path fsPath = std::filesystem::u8path(filePath);
        std::ifstream file(fsPath, std::ios::binary);
        if (!file)
        {
            SY_ERRORF("[PltParser] parseToIR: cannot open PLT file: %s", filePath);
            return FioParseResult{};
        }

        // 二进制格式快速检测：读取头部 4KB，若非打印字符占比过高则拒绝
        {
            std::vector<char> headerBuf(4096);
            file.read(headerBuf.data(), static_cast<std::streamsize>(headerBuf.size()));
            std::streamsize bytesRead = file.gcount();

            if (bytesRead == 0)
            {
                SY_ERRORF("[PltParser] parseToIR: PLT file is empty: %s", filePath);
                return FioParseResult{};
            }

            int nonPrintable = 0;
            for (std::streamsize i = 0; i < bytesRead; ++i)
            {
                unsigned char c = static_cast<unsigned char>(headerBuf[i]);
                if (c < 32 && c != '\t' && c != '\n' && c != '\r')
                {
                    nonPrintable++;
                }
            }
            bool likelyBinary = (bytesRead > 256 && nonPrintable > bytesRead / 10);

            if (likelyBinary)
            {
                SY_ERRORF("[PltParser] parseToIR: binary DMPL not supported: %s", filePath);
                return FioParseResult{};
            }

            file.clear();
            file.seekg(0, std::ios::beg);
        }

        const int MAX_ENTITIES = 1000000;

        try
        {
            PltHpglInterpreter interpreter(s_entities, s_warnings);

            std::string line;
            int lineIdx = 0;

            while (std::getline(file, line))
            {
                if (s_entities.size() >= static_cast<size_t>(MAX_ENTITIES))
                {
                    s_warnings.push_back("Entity limit (" + std::to_string(MAX_ENTITIES) + ") reached, stopping parse.");
                    break;
                }

                line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
                interpreter.processLine(line, lineIdx);
                ++lineIdx;
            }

            interpreter.finalize();
        }
        catch (const std::exception& ex)
        {
            SY_CRITICALF("[PltParser] parseToIR exception: %s - %s", filePath, ex.what());
            return FioParseResult{};
        }
        catch (...)
        {
            SY_CRITICALF("[PltParser] parseToIR unknown exception: %s", filePath);
            return FioParseResult{};
        }

        if (s_entities.empty())
        {
            SY_WARNF("[PltParser] parseToIR: no entities produced: %s", filePath);
            return FioParseResult{};
        }

        FioParseResult result;
        result.entities = s_entities.data();
        result.entityCount = static_cast<uint32_t>(s_entities.size());
        result.layers = nullptr;
        result.layerCount = 0;
        result.extensionBlob.data = nullptr;
        result.extensionBlob.size = 0;
        std::strncpy(result.sourceFormat, "PLT", sizeof(result.sourceFormat) - 1);
        result.warningCount = static_cast<uint32_t>(s_warnings.size());

        SY_INFOF(
            "[PltParser] parseToIR END: %u entities, %u warnings: %s", result.entityCount, result.warningCount, filePath);
        return result;
    }
}  // namespace Fio