#include "FileIO/Writers/NativeWriter.h"
#include "FileIO/SySerializer.h"
#include "FileIO/SyDocument.h"

#include "Engine/SyEntity/SyEntity.h"
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>

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

    WriteResult NativeWriter::write(const char* filePath,
        const VecSyEntityPtr& entities)
    {
        // 构建 SyDocument
        SyDocument doc;

        // 填充元信息默认值
        doc.setMetadataVersion(SyFileConst::FILE_VERSION);
        doc.setMetadataFileVersion(1);
        doc.setSoftwareName("SanYi CAD 2D");
        doc.setSoftwareVersion("1.0.0");

        {
            const auto now = std::chrono::system_clock::now();
            const auto time = std::chrono::system_clock::to_time_t(now);
            std::ostringstream oss;
            oss << std::put_time(std::localtime(&time), "%Y-%m-%dT%H:%M:%S");
            doc.setCreatedTime(oss.str().c_str());
            doc.setModifiedTime(oss.str().c_str());
        }

#ifdef _WIN32
        doc.setOperatingSystem("Windows");
#elif defined(__linux__)
        doc.setOperatingSystem("Linux");
#elif defined(__APPLE__)
        doc.setOperatingSystem("macOS");
#else
        doc.setOperatingSystem("Unknown");
#endif

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
} // namespace Fio