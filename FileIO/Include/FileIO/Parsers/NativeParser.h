#pragma once

// NativeParser —— .sy / .syx 统一读取器
//
// 基于 SySerializer 实现 Protobuf 二进制反序列化。
// 通过构造时传入的 FileFormat 区分 2D (.sy) 和 3D (.syx) 行为。
//
// 设计说明:
//   统一 NativeParser / NativeParser3D 的反序列化逻辑，消除代码重复。
//   FileParserFactory 对两种格式分别注册同一个 parser 类（不同构造参数）。
//   ILegacyParser::parse() 接口保留兼容性，新代码优先使用 parseDocument()。

#include "FileIO/IFileParser.h"
#include "FileIO/SyDocument.h"
#include "FileIOInternal.h"

namespace Fio
{
    class FILEIO_API NativeParser : public IFileParser, public ILegacyParser
    {
    public:
        /// 构造统一读取器
        /// @param fmt 目标格式：FileFormat::Native (2D) 或 FileFormat::Native3D (3D)
        explicit NativeParser(FileFormat fmt = FileFormat::Native);
        ~NativeParser() override;

        // ---- IFileParser 接口 ----
        FileFormat format() const override;
        size_t formatName(char* buffer, size_t bufferSize) const override;
        void forEachSupportedExtension(void (*visitor)(const char*, void*), void* ctx) const override;

        // ---- ILegacyParser 接口（兼容旧路径，仅返回 2D 图元）----
        ParseResult parse(const char* filePath, VecSyEntityPtr& outEntities) override;

        /// 完整文档读取（同时返回 2D 图元和 3D 网格，推荐使用）
        ParseResult parseDocument(const char* filePath, SyDocument& outDoc);

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };
}  // namespace Fio
