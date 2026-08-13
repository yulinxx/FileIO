#pragma once

#include "FileIO/IFileWriter.h"
#include "FileIOInternal.h"

namespace Fio
{
    // ============================================================
    // NativeWriter —— .sy 文件写入器
    //
    // 基于 SySerializer 实现 Protobuf 二进制序列化
    // 兼容现有 IFileWriter 接口
    // ============================================================
    class FILEIO_API NativeWriter : public IFileWriter, public ILegacyWriter
    {
    public:
        NativeWriter();
        ~NativeWriter() override;

        FileFormat format() const override;
        size_t formatName(char* buffer, size_t bufferSize) const override;
        size_t defaultExtension(char* buffer, size_t bufferSize) const override;
        WriteResult write(const char* filePath, const VecSyEntityPtr& entities) override;

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };
}  // namespace Fio