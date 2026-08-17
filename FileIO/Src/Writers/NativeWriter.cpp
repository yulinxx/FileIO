#include "FileIO/Writers/NativeWriter.h"
#include "FileIO/SySerializer.h"
#include "FileIO/SyDocument.h"
#include "MetadataFiller.h"

#include "Engine/SyEntity/SyEntity.h"
#include "Log/SyLogger.h"

#include <cstring>

namespace Fio
{
    // ============================================================
    // NativeWriter::Impl —— PIMPL 隐藏实现细节
    //
    // 持有 SySerializer 实例，按目标格式选择魔数和软件名称。
    // ============================================================
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
        SySerializer serializer;

        /// 根据目标格式返回对应的软件名称
        const char* softwareName() const
        {
            return (targetFormat == FileFormat::Native3D) ? "SanYi CAD 3D" : "SanYi CAD 2D";
        }
    };

    // ============================================================
    // 构造 / 析构
    // ============================================================

    NativeWriter::NativeWriter(FileFormat fmt)
        : m_impl(std::make_unique<Impl>(fmt))
    {
        SY_INFOF("[NativeWriter] Created for format=%d (%s)",
            static_cast<int>(fmt),
            (fmt == FileFormat::Native3D) ? "3D" : "2D");
    }

    NativeWriter::~NativeWriter() = default;

    // ============================================================
    // IFileWriter 接口
    // ============================================================

    FileFormat NativeWriter::format() const
    {
        return m_impl->targetFormat;
    }

    size_t NativeWriter::formatName(char* buffer, size_t bufferSize) const
    {
        const char* name = (m_impl->targetFormat == FileFormat::Native3D)
            ? "SanYi 3D Native (Protobuf)"
            : "SanYi Native (Protobuf)";
        const size_t len = std::strlen(name);
        if (buffer != nullptr && bufferSize > len)
        {
            std::strcpy(buffer, name);
        }
        return len;
    }

    size_t NativeWriter::defaultExtension(char* buffer, size_t bufferSize) const
    {
        const char* ext = (m_impl->targetFormat == FileFormat::Native3D) ? "syx" : "sy";
        const size_t len = std::strlen(ext);
        if (buffer != nullptr && bufferSize > len)
        {
            std::strcpy(buffer, ext);
        }
        return len;
    }

    // ============================================================
    // ILegacyWriter 接口（兼容旧路径）
    //
    // 仅处理 2D 图元，适用于不区分 2D/3D 的调用方。
    // 3D 场景推荐使用 writeDocument() 传入完整 SyDocument。
    // ============================================================

    WriteResult NativeWriter::write(const char* filePath, const VecSyEntityPtr& entities)
    {
        SY_INFOF("[NativeWriter] write(): path=%s, entities=%zu, format=%d",
            filePath ? filePath : "(null)",
            entities.size(),
            static_cast<int>(m_impl->targetFormat));

        SyDocument doc;

        // 使用共享元数据填充工具
        MetadataFiller::fillMetadata(doc, m_impl->softwareName(), "1.0.0");

        // 克隆图元到文档
        for (const auto& entity : entities)
        {
            if (entity)
            {
                doc.addEntity(entity->clone());
            }
        }

        return writeDocument(filePath, doc);
    }

    // ============================================================
    // 完整文档写入（推荐路径）
    //
    // 支持同时保存 2D 图元和 3D 网格。
    // SySerializer 根据目标格式自动选择魔数：
    //   - FileFormat::Native  → "SYPB"
    //   - FileFormat::Native3D → "SXPB"
    // ============================================================

    WriteResult NativeWriter::writeDocument(const char* filePath, const SyDocument& doc)
    {
        if (!filePath || !*filePath)
        {
            SY_ERRORF("[NativeWriter] writeDocument(): empty file path");
            return WriteResult::fail("Empty file path");
        }

        SY_INFOF("[NativeWriter] writeDocument(): path=%s, format=%d",
            filePath,
            static_cast<int>(m_impl->targetFormat));

        auto result = m_impl->serializer.saveToFile(filePath, doc, false, m_impl->targetFormat);
        if (!result.success)
        {
            SY_ERRORF("[NativeWriter] writeDocument() failed: %s", result.errorMessage);
            return WriteResult::fail(result.errorMessage);
        }

        SY_INFOF("[NativeWriter] writeDocument() succeeded: %s", filePath);
        return WriteResult::ok();
    }
}  // namespace Fio
