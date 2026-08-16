#pragma once

#include "FileIO/IFileParser.h"
#include "FileIOInternal.h"

#include <cstring>

namespace Fio
{
    /// Siemens NX / Unigraphics (UG) 解析器
    ///
    /// 通过 IGES（.igs/.iges）中性交换格式读取几何。
    /// NX 原生 .prt 为私有二进制格式，需 NX Open API（商业许可）才能解析，
    /// 本解析器仅覆盖 IGES 交换格式中的常见 2D 图元
    /// （110 直线、100 圆弧、116 点、106 折线等），经中立 IR 输出。
    class UgParser : public IFileParser, public ILegacyParser
    {
    public:
        UgParser() = default;
        ~UgParser() override = default;

    public:
        FileFormat format() const override
        {
            return FileFormat::UG;
        }

        size_t formatName(char* buffer, size_t bufferSize) const override
        {
            const char* name = "Siemens NX / Unigraphics (IGES)";
            const size_t len = std::strlen(name);
            if (buffer != nullptr && bufferSize > len)
            {
                std::strcpy(buffer, name);
            }
            return len;
        }

        void forEachSupportedExtension(void (*visitor)(const char*, void*), void* ctx) const override
        {
            visitor("igs", ctx);
            visitor("iges", ctx);
        }

        ParseResult parse(const char* filePath, VecSyEntityPtr& outEntities) override;

        /// 输出中立 IR（跨 DLL 安全）：解析 IGES 常见图元到 FioParseResult
        FioParseResult parseToIR(const char* filePath) override;
    };
}  // namespace Fio
