#include "FileIO/Writers/NativeWriter3D.h"
#include "FileIO/SySerializer.h"

#include "Engine/SyEntity/SyEntity.h"
#include <chrono>
#include <iomanip>
#include <sstream>

namespace Fio
{
    namespace
    {
        void fillMetadata(SyDocument& doc)
        {
            doc.metadata.version = SyFileConst::FILE_VERSION;
            doc.metadata.fileVersion = 1;
            doc.metadata.softwareName = "SanYi CAD 3D";
            doc.metadata.softwareVersion = "1.0.0";

            const auto now = std::chrono::system_clock::now();
            const auto time = std::chrono::system_clock::to_time_t(now);
            std::ostringstream oss;
            oss << std::put_time(std::localtime(&time), "%Y-%m-%dT%H:%M:%S");
            doc.metadata.createdTime = oss.str();
            doc.metadata.modifiedTime = oss.str();

#ifdef _WIN32
            doc.metadata.operatingSystem = "Windows";
#elif defined(__linux__)
            doc.metadata.operatingSystem = "Linux";
#elif defined(__APPLE__)
            doc.metadata.operatingSystem = "macOS";
#else
            doc.metadata.operatingSystem = "Unknown";
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

    std::string NativeWriter3D::formatName() const
    {
        return "SanYi 3D Native (Protobuf)";
    }

    std::string NativeWriter3D::defaultExtension() const
    {
        return "syx";
    }

    WriteResult NativeWriter3D::write(const std::string& filePath,
        const VecSyEntityPtr& entities)
    {
        // 构建文档并克隆 2D 实体
        SyDocument doc;
        fillMetadata(doc);
        doc.entities.reserve(entities.size());
        for (const auto& entity : entities)
        {
            if (entity)
                doc.entities.push_back(entity->clone());
        }
        return writeDocument(filePath, doc);
    }

    // ---- 3D 完整文档写入 ----

    WriteResult NativeWriter3D::writeDocument(const std::string& filePath,
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