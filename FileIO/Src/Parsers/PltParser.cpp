// 确保在任何标准头文件之前定义，使 PltHpglInterpreter.h 中的 M_PI 可用
#ifndef _USE_MATH_DEFINES
    #define _USE_MATH_DEFINES
#endif

#include "FileIO/Parsers/PltParser.h"
#include "FileIO/Parsers/PltHpglInterpreter.h"
#include "IrProjector.h"

#include "Log/SyLogger.h"

#include <fstream>
#include <sstream>
#include <regex>
#include <cmath>
#include <algorithm>
#include <memory>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <system_error>
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
            std::strncpy(buffer, name, bufferSize - 1);
            buffer[bufferSize - 1] = '\0';
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
    // HPGL 文本 → PltHpglInterpreter → EntityInfo(POD) + extensionBlob
    // 不依赖 Engine2D 类型，跨 DLL 安全
    // PLT 折线（Polyline/Polygon）的顶点数据存放在 extensionBlob 中
    // ========================================================================
    FioParseResult PltParser::parseToIR(const char* filePath)
    {
        return parseToIRImpl(filePath, nullptr, nullptr);
    }

    FioParseResult PltParser::parseToIRWithProgress(
        const char* filePath, ParseProgressCallback onProgress, void* progressCtx)
    {
        return parseToIRImpl(filePath, onProgress, progressCtx);
    }

    FioParseResult PltParser::parseToIRImpl(const char* filePath, ParseProgressCallback onProgress, void* progressCtx)
    {
        SY_INFOF("[PltParser] parseToIR START: %s", filePath ? filePath : "");

        // 统一缓冲区管理（与 DxfParser/StlParser 一致）
        IrPublisher& pub = IrPublisher::threadLocal();
        pub.reset();

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

        // 进度：PLT 逐行顺序读取，流位置即可靠的单调进度信号（总行数事先不可知，
        // 但文件字节数是确定的）。每 kProgressStride 行取一次 tellg，避免逐行系统调用。
        std::error_code sizeEc;
        const std::uintmax_t totalBytes = std::filesystem::file_size(fsPath, sizeEc);
        constexpr int kProgressStride = 2048;
        const bool reportProgress = (onProgress != nullptr) && !sizeEc && totalBytes > 0;

        try
        {
            PltHpglInterpreter interpreter(pub.entities(), pub.warnings(), pub.blob());

            std::string line;
            std::string pending;  // 跨行拼接缓冲：上一行末尾无分号时暂存
            int lineIdx = 0;

            while (std::getline(file, line))
            {
                if (pub.entities().size() >= static_cast<size_t>(MAX_ENTITIES))
                {
                    pub.warnings().push_back("Entity limit (" + std::to_string(MAX_ENTITIES) + ") reached, stopping parse.");
                    break;
                }

                line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());

                // 跨行拼接：若缓冲非空则拼接；否则检查本行是否末尾无分号
                if (!pending.empty())
                {
                    pending += line;
                    // 检查拼接后是否末尾仍有未闭合命令（无分号结尾）
                    if (!line.empty() && line.back() != ';')
                    {
                        continue;  // 继续读下一行
                    }
                    interpreter.processLine(pending, lineIdx);
                    pending.clear();
                }
                else if (!line.empty() && line.back() != ';')
                {
                    // 本行末尾无分号：可能命令被截断，暂存等下一行
                    pending = line;
                    continue;
                }
                else
                {
                    interpreter.processLine(line, lineIdx);
                }
                ++lineIdx;

                if (reportProgress && (lineIdx % kProgressStride == 0))
                {
                    const std::streamoff pos = static_cast<std::streamoff>(file.tellg());
                    const float p = (pos < 0)
                        ? 0.0f
                        : std::min(1.0f, static_cast<float>(static_cast<double>(pos) / static_cast<double>(totalBytes)));
                    onProgress(p, progressCtx);
                }
            }

            // 处理最后剩余的未闭合行
            if (!pending.empty())
            {
                interpreter.processLine(pending, lineIdx);
            }

            interpreter.finalize();

            if (onProgress)
            {
                onProgress(1.0f, progressCtx);
            }
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

        if (pub.entities().empty())
        {
            SY_WARNF("[PltParser] parseToIR: no entities produced: %s", filePath);
            return FioParseResult{};
        }

        SY_INFOF(
            "[PltParser] parseToIR END: %u entities, %u warnings: %s",
            static_cast<uint32_t>(pub.entities().size()),
            static_cast<uint32_t>(pub.warnings().size()),
            filePath);
        return pub.publish("PLT");
    }
}  // namespace Fio