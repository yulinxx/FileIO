#include "FileIO/Writers/NativeWriter3D.h"
#include "FileIO/SySerializer.h"
#include "MetadataFiller.h"

#include "Engine/SyEntity/SyEntity.h"

namespace Fio
{
    class NativeWriter3D::Impl
    {
    public:
        SySerializer serializer;
    };

    NativeWriter3D::NativeWriter3D()
        : m_impl(std::make_unique<Impl>())
    {
    }

    NativeWriter3D::~NativeWriter3D() = default;

    // ---- IFileWriter 接口 ----

    FileFormat NativeWriter3D::format() const
    {
        return FileFormat::Native3D;
    }

    size_t NativeWriter3D::formatName(char* buffer, size_t bufferSize) const
    {
        const char* name = "SanYi 3D Native (Protobuf)";
        const size_t len = std::strlen(name);
        if (buffer != nullptr && bufferSize > len)
        {
            std::strcpy(buffer, name);
        }
        return len;
    }

    size_t NativeWriter3D::defaultExtension(char* buffer, size_t bufferSize) const
    {
        const char* ext = "syx";
        const size_t len = std::strlen(ext);
        if (buffer != nullptr && bufferSize > len)
        {
            std::strcpy(buffer, ext);
        }
        return len;
    }

    WriteResult NativeWriter3D::write(const char* filePath, const VecSyEntityPtr& entities)
    {
        SyDocument doc;

        // 使用共享元数据填充工具
        MetadataFiller::fillMetadata(doc, "SanYi CAD 3D", "1.0.0");

        for (const auto& entity : entities)
        {
            if (entity)
            {
                doc.addEntity(entity->clone());
            }
        }
        return writeDocument(filePath, doc);
    }

    // ---- 3D 完整文档写入 ----

    WriteResult NativeWriter3D::writeDocument(const char* filePath, const SyDocument& doc)
    {
        auto result = m_impl->serializer.saveToFile(filePath, doc, false);
        if (!result.success)
        {
            return WriteResult::fail(result.errorMessage);
        }
        return WriteResult::ok();
    }
}  // namespace Fio