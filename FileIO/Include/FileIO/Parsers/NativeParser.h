#pragma once

#include "FileIO/IFileParser.h"

namespace Fio
{
    // ============================================================
    // NativeParser —— .sy 文件读取器
    //
    // 基于 SySerializer 实现 Protobuf 二进制反序列化
    // 兼容现有 IFileParser 接口
    // ============================================================
    class FILEIO_API NativeParser : public IFileParser
    {
    public:
        NativeParser();
        ~NativeParser() override;

        FileFormat format() const override;
        std::string formatName() const override;
        std::vector<std::string> supportedExtensions() const override;
        ParseResult parse(const std::string& filePath,
            VecSyEntityPtr& outEntities) override;

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Fio