#include "FileIO/Writers/NativeWriter.h"
#include "FileIO/SySerializer.h"
#include "FileIO/SyDocument.h"
#include "MetadataFiller.h"

#include "Engine/SyEntity/SyEntity.h"

namespace Fio
{
    // ============================================================
    // NativeWriter::Impl - PIMPL 隐藏实现细节
    // ============================================================
    class NativeWriter::Impl
    {
    public:
        SySerializer serializer;
    };

    NativeWriter::NativeWriter()
        : m_impl(std::make_unique<Impl>())
    {
    }

    NativeWriter::~NativeWriter() = default;

    FileFormat NativeWriter::format() const
    {
        return FileFormat::Native;
    }

    size_t NativeWriter::formatName(char* buffer, size_t bufferSize) const
    {
        const char* name = "SanYi Native (Protobuf)";
        const size_t len = std::strlen(name);
        if (buffer != nullptr && bufferSize > len)
        {
            std::strcpy(buffer, name);
        }
        return len;
    }

    size_t NativeWriter::defaultExtension(char* buffer, size_t bufferSize) const
    {
        const char* ext = "sy";
        const size_t len = std::strlen(ext);
        if (buffer != nullptr && bufferSize > len)
        {
            std::strcpy(buffer, ext);
        }
        return len;
    }

    WriteResult NativeWriter::write(const char* filePath, const VecSyEntityPtr& entities)
    {
        SyDocument doc;

        // 使用共享元数据填充工具
        MetadataFiller::fillMetadata(doc, "SanYi CAD 2D", "1.0.0");

        // 克隆图元到 doc
        for (const auto& entity : entities)
        {
            if (entity)
            {
                doc.addEntity(entity->clone());
            }
        }

        // 序列化保存
        auto result = m_impl->serializer.saveToFile(filePath, doc, false);
        if (!result.success)
        {
            return WriteResult::fail(result.errorMessage);
        }

        return WriteResult::ok();
    }
}  // namespace Fio
