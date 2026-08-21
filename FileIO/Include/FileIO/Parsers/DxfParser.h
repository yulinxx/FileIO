#pragma once

#include "FileIO/IFileParser.h"
#include "FileIOInternal.h"

namespace Fio
{
    class DxfParser : public IFileParser, public ILegacyParser
    {
    public:
        DxfParser() = default;
        ~DxfParser() override = default;

    public:
        FileFormat format() const override;
        size_t formatName(char* buffer, size_t bufferSize) const override;
        void forEachSupportedExtension(void (*visitor)(const char*, void*), void* ctx) const override;

        // 旧版 API（直接输出 Engine 类型，deprecated）
        ParseResult parse(const char* filePath, VecSyEntityPtr& outEntities) override;

        // 新版 API（输出中立 IR，跨 DLL 安全）
        FioParseResult parseToIR(const char* filePath) override;
    };
}  // namespace Fio