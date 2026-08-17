#pragma once

// NativeWriter —— .sy / .syx 统一写入器
//
// 基于 SySerializer 实现 Protobuf 二进制序列化。
// 通过构造时传入的 FileFormat 区分 2D (.sy) 和 3D (.syx) 行为：
//   - FileFormat::Native  → 魔数 "SYPB", 软件名 "SanYi CAD 2D"
//   - FileFormat::Native3D → 魔数 "SXPB", 软件名 "SanYi CAD 3D"
//
// 设计说明:
//   统一 NativeWriter / NativeWriter3D 的序列化逻辑，消除代码重复。
//   FileWriterFactory 对两种格式分别注册同一个 writer 类（不同构造参数）。
//   ILegacyWriter::write() 接口保留兼容性，新代码优先使用 writeDocument()。

#include "FileIO/IFileWriter.h"
#include "FileIO/SyDocument.h"
#include "FileIOInternal.h"

namespace Fio
{
    class FILEIO_API NativeWriter : public IFileWriter, public ILegacyWriter
    {
    public:
        /// 构造统一写入器
        /// @param fmt 目标格式：FileFormat::Native (2D) 或 FileFormat::Native3D (3D)
        explicit NativeWriter(FileFormat fmt = FileFormat::Native);
        ~NativeWriter() override;

        // ---- IFileWriter 接口 ----
        FileFormat format() const override;
        size_t formatName(char* buffer, size_t bufferSize) const override;
        size_t defaultExtension(char* buffer, size_t bufferSize) const override;

        // ---- ILegacyWriter 接口（兼容旧路径，仅处理 2D 图元）----
        WriteResult write(const char* filePath, const VecSyEntityPtr& entities) override;

        /// 完整文档写入（同时保存 2D 图元和 3D 网格，推荐使用）
        WriteResult writeDocument(const char* filePath, const SyDocument& doc);

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };
}  // namespace Fio
