#pragma once

#include "FileIO/IFileParser.h"
#include "FileIOInternal.h"

namespace Fio
{
    // 加导出宏的原因：测试与外部工具会直接构造 DxfParser 调用 parseToIR，
    // 不加则链接期报 LNK2001/LNK2019。与 StlParser / UgParser / NativeParser 口径一致。
    class FILEIO_API DxfParser : public IFileParser, public ILegacyParser
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