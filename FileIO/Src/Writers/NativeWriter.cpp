#include "FileIO/Writers/NativeWriter.h"
#include "FileIO/AprSerializer.h"
#include "FileIO/AprDocument.h"
#include "MetadataFiller.h"

#include "Engine/AprEntity/AprEntity.h"
#include "Log/AprLogger.h"

#include <cstring>

namespace Fio
{
    // ----
    // ---- NativeWriter::Impl —— PIMPL 隐藏实现细节 ----
    //
    // 持有 AprSerializer 实例，按目标格式选择魔数和软件名称。
    // ----
    class NativeWriter::Impl
    {
    public:
        explicit Impl(FileFormat fmt)
            : targetFormat(fmt)
        {
        }

        /// 目标文件格式（决定魔数和元数据）
        FileFormat targetFormat;

        /// 序列化器实例
        AprSerializer serializer;

        /// 进度回调
        SerializeProgressCallback progressCb{ nullptr };
        void* progressCtx{ nullptr };

        /// 根据目标格式返回对应的软件名称
        const char* softwareName() const
        {
            return (targetFormat == FileFormat::Native3D) ? "April CAD 3D" : "April CAD 2D";
        }
    };

    // ----
    // ---- 构造 / 析构 ----
    // ----

    NativeWriter::NativeWriter(FileFormat fmt)
        : m_impl(std::make_unique<Impl>(fmt))
    {
        APR_DEBUGF("[NativeWriter] Created for format=%d (%s)",
            static_cast<int>(fmt),
            (fmt == FileFormat::Native3D) ? "3D" : "2D");
    }

    NativeWriter::~NativeWriter() = default;

    void NativeWriter::setProgressCallback(SerializeProgressCallback cb, void* ctx)
    {
        m_impl->progressCb = cb;
        m_impl->progressCtx = ctx;
    }

    // ----
    // ---- IFileWriter 接口 ----
    // ----

    FileFormat NativeWriter::format() const
    {
        return m_impl->targetFormat;
    }

    size_t NativeWriter::formatName(char* buffer, size_t bufferSize) const
    {
        const char* name =
            (m_impl->targetFormat == FileFormat::Native3D) ? "April 3D Native (Protobuf)" : "April Native (Protobuf)";
        return copyToBuffer(buffer, bufferSize, name);
    }

    size_t NativeWriter::defaultExtension(char* buffer, size_t bufferSize) const
    {
        const char* ext = (m_impl->targetFormat == FileFormat::Native3D) ? "apr3d" : "apr";
        return copyToBuffer(buffer, bufferSize, ext);
    }

    // ----
    // ---- ILegacyWriter 接口 ----
    //
    // 仅处理 2D 图元，适用于不区分 2D/3D 的调用方。
    // 3D 场景使用 writeDocument() 传入完整 AprDocument。
    // ----

    WriteResult NativeWriter::write(const char* filePath, const VecAprEntityPtr& entities)
    {
        APR_DEBUGF("[NativeWriter] write(): path=%s, entities=%zu, format=%d",
            filePath ? filePath : "(null)",
            entities.size(),
            static_cast<int>(m_impl->targetFormat));

        AprDocument doc;

        // 使用共享元数据填充工具
        MetadataFiller::fillMetadata(doc, m_impl->softwareName(), "1.0.0");

        // 借出图元到文档（不克隆）：调用方保证这些指针在 saveToFile 期间存活，
        // 避免为了写入而把每个图元（含图片像素/网格顶点）再深拷贝一份。
        for (const auto& entity : entities)
        {
            if (entity)
            {
                doc.addBorrowedEntity(entity.get());
            }
        }

        return writeDocument(filePath, doc);
    }

    // ----
    // ---- 完整文档写入 ----
    //
    // 支持同时保存 2D 图元和 3D 网格。
    // AprSerializer 根据目标格式自动选择魔数：
    //   - FileFormat::Native  → "APRB"
    //   - FileFormat::Native3D → "AP3D"
    //
    // 注意：本方法不碰元数据。调用方若直接构造 AprDocument，必须先用
    // writeDocumentWithMetadata()，否则溯源字段会整片写空。
    // ----

    WriteResult NativeWriter::writeDocument(const char* filePath, const AprDocument& doc)
    {
        if (!filePath || !*filePath)
        {
            APR_ERRORF("[NativeWriter] writeDocument(): empty file path");
            return WriteResult::fail("Empty file path");
        }

        APR_DEBUGF(
            "[NativeWriter] writeDocument(): path=%s, format=%d", filePath, static_cast<int>(m_impl->targetFormat));

        auto result = m_impl->serializer.saveToFile(
            filePath, doc, false, m_impl->targetFormat, nullptr, nullptr, m_impl->progressCb, m_impl->progressCtx);
        if (!result.success)
        {
            APR_ERRORF("[NativeWriter] writeDocument() failed: %s", result.errorMessage);
            return WriteResult::fail(result.errorMessage);
        }

        APR_DEBUGF("[NativeWriter] writeDocument() succeeded: %s", filePath);
        return WriteResult::ok();
    }

    WriteResult NativeWriter::writeDocumentWithMetadata(const char* filePath, AprDocument& doc)
    {
        // softwareName 是 fillMetadata 一定会写的字段，用它区分「全新文档」与
        // 「读入后修改再保存」。后者只刷新 modifiedTime，避免把 createdTime
        // 和原文件来源信息改成本次保存。
        const char* const existingSoftware = doc.softwareName();
        if (existingSoftware && existingSoftware[0] != '\0')
        {
            MetadataFiller::updateModifiedTime(doc);
        }
        else
        {
            MetadataFiller::fillMetadata(doc, m_impl->softwareName(), "1.0.0");
        }
        return writeDocument(filePath, doc);
    }
}  // namespace Fio