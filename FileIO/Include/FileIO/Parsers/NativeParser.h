#pragma once

#include "FileIO/IFileParser.h"
#include "FileIOInternal.h"

namespace Fio
{
    // ============================================================
    // NativeParser —— .sy 文件读取器
    //
    // 基于 SySerializer 实现 Protobuf 二进制反序列化
    // 兼容现有 IFileParser 接口
    // ============================================================
    class FILEIO_API NativeParser : public IFileParser, public ILegacyParser
    {
    public:
        NativeParser();
        ~NativeParser() override;

        FileFormat format() const override;
        size_t formatName(char* buffer, size_t bufferSize) const override;
        void forEachSupportedExtension(void(*visitor)(const char*, void*), void* ctx) const override;
        ParseResult parse(const char* filePath,
            VecSyEntityPtr& outEntities) override;

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Fio