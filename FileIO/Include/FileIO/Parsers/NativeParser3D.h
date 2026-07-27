#pragma once

#include "FileIO/IFileParser.h"
#include "FileIO/SyDocument.h"

namespace Fio
{
    // ============================================================
    // NativeParser3D —— .syx 文件读取器
    //
    // 基于 SySerializer 实现 Protobuf 二进制反序列化
    // 支持同时读取 2D 图元和 3D 网格图元
    // 兼容现有 IFileParser 接口，附加 SyDocument 读取能力
    // ============================================================
    class FILEIO_API NativeParser3D : public IFileParser
    {
    public:
        NativeParser3D();
        ~NativeParser3D() override;

        // ---- IFileParser 接口 (仅返回 2D 图元) ----
        FileFormat format() const override;
        std::string formatName() const override;
        std::vector<std::string> supportedExtensions() const override;
        ParseResult parse(const std::string& filePath,
            VecSyEntityPtr& outEntities) override;

        // ---- 3D 完整文档读取 (同时返回 2D 图元和 3D 网格) ----
        ParseResult parseDocument(const std::string& filePath,
            SyDocument& outDoc);

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Fio