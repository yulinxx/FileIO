#pragma once

#include "FileIO/IFileWriter.h"
#include "FileIO/SyDocument.h"
#include "FileIOInternal.h"

namespace Fio
{
    // ============================================================
    // NativeWriter3D —— .syx 文件写入器
    //
    // 基于 SySerializer 实现 Protobuf 二进制序列化
    // 支持同时保存 2D 图元和 3D 网格图元
    // 兼容现有 IFileWriter 接口，附加 SyDocument 写入能力
    // ============================================================
    class FILEIO_API NativeWriter3D : public IFileWriter, public ILegacyWriter
    {
    public:
        NativeWriter3D();
        ~NativeWriter3D() override;

        // ---- IFileWriter 接口 (仅处理 2D 图元) ----
        FileFormat format() const override;
        size_t formatName(char* buffer, size_t bufferSize) const override;
        size_t defaultExtension(char* buffer, size_t bufferSize) const override;
        WriteResult write(const char* filePath, const VecSyEntityPtr& entities) override;

        // ---- 3D 完整文档写入 (同时保存 2D 图元和 3D 网格) ----
        WriteResult writeDocument(const char* filePath, const SyDocument& doc);

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };
}  // namespace Fio