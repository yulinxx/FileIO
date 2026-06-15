#include "FileIO/Writers/NativeWriter.h"
#include "FileIO/SySerializer.h"
#include "FileIO/SyDocument.h"

#include "Engine/SyEntity/SyEntity.h"
#include <chrono>
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

    std::string NativeWriter::formatName() const
    {
        return "SanYi Native (Protobuf)";
    }

    std::string NativeWriter::defaultExtension() const
    {
        return "sy";
    }

    WriteResult NativeWriter::write(const std::string& filePath,
        const VecSyEntityPtr& entities)
    {
        // 构建 SyDocument
        SyDocument doc;

        // 填充元信息默认值
        doc.metadata.version = SyFileConst::FILE_VERSION;
        doc.metadata.fileVersion = 1;
        doc.metadata.softwareName = "SanYi CAD";
        doc.metadata.softwareVersion = "1.0.0";

        {
            const auto now = std::chrono::system_clock::now();
            const auto time = std::chrono::system_clock::to_time_t(now);
            std::ostringstream oss;
            oss << std::put_time(std::localtime(&time), "%Y-%m-%dT%H:%M:%S");
            doc.metadata.createdTime = oss.str();
            doc.metadata.modifiedTime = oss.str();
        }

#ifdef _WIN32
        doc.metadata.operatingSystem = "Windows";
#elif defined(__linux__)
        doc.metadata.operatingSystem = "Linux";
#elif defined(__APPLE__)
        doc.metadata.operatingSystem = "macOS";
#else
        doc.metadata.operatingSystem = "Unknown";
#endif

        // 克隆图元到 doc
        doc.entities.reserve(entities.size());
        for (const auto& entity : entities)
        {
            if (entity)
            {
                doc.entities.push_back(entity->clone());
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