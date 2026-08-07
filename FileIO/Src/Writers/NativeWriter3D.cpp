#include "FileIO/Writers/NativeWriter3D.h"
#include "FileIO/SySerializer.h"

#include "Engine/SyEntity/SyEntity.h"
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace Fio
{
    namespace
    {
        void fillMetadata(SyDocument& doc)
        {
            doc.setMetadataVersion(SyFileConst::FILE_VERSION);
            doc.setMetadataFileVersion(1);
            doc.setSoftwareName("SanYi CAD 3D");
            doc.setSoftwareVersion("1.0.0");

            const auto now = std::chrono::system_clock::now();
            const auto time = std::chrono::system_clock::to_time_t(now);
            std::ostringstream oss;
            oss << std::put_time(std::localtime(&time), "%Y-%m-%dT%H:%M:%S");
            doc.setCreatedTime(oss.str().c_str());
            doc.setModifiedTime(oss.str().c_str());

#ifdef _WIN32
            doc.setOperatingSystem("Windows");
#elif defined(__linux__)
            doc.setOperatingSystem("Linux");
#elif defined(__APPLE__)
            doc.setOperatingSystem("macOS");
#else
            doc.setOperatingSystem("Unknown");
#endif
        }
    } // anonymous namespace

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

    WriteResult NativeWriter3D::write(const char* filePath,
        const VecSyEntityPtr& entities)
    {
        // 构建文档并克隆 2D 图元
        SyDocument doc;
        fillMetadata(doc);
        for (const auto& entity : entities)
        {
            if (entity)
                doc.addEntity(entity->clone());
        }
        return writeDocument(filePath, doc);
    }

    // ---- 3D 完整文档写入 ----

    WriteResult NativeWriter3D::writeDocument(const char* filePath,
        const SyDocument& doc)
    {
        auto result = m_impl->serializer.saveToFile(filePath, doc, false);
        if (!result.success)
        {
            return WriteResult::fail(result.errorMessage);
        }
        return WriteResult::ok();
    }
} // namespace Fio