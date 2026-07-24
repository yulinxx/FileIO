#include "FileIO/FileIOManager.h"
#include "FileIO/FileParserFactory.h"
#include "FileIO/FileWriterFactory.h"
#include "FileIO/IFileParser.h"
#include "FileIO/IFileWriter.h"
#include "Log/SyLogger.h"

#include <filesystem>

namespace Fio
{
    FileIOManager::FileIOManager()
    {
        FileParserFactory::instance().initDefaults();
        FileWriterFactory::instance().initDefaults();
    }

    FileIOManager::~FileIOManager() = default;

    void FileIOManager::setImportCallback(ImportCallback callback)
    {
        m_importCallback = std::move(callback);
    }

    void FileIOManager::setExportCallback(ExportCallback callback)
    {
        m_exportCallback = std::move(callback);
    }

    ParseResult FileIOManager::importFile(const std::string& filePath, VecSyEntityPtr& outEntities)
    {
        FileFormat fmt = detectFormat(filePath);
        if (fmt == FileFormat::Unknown)
            return ParseResult::fail("Unsupported file format: " + filePath);

        return importFile(filePath, fmt, outEntities);
    }

    ParseResult FileIOManager::importFile(const std::string& filePath, FileFormat format, VecSyEntityPtr& outEntities)
    {
        SY_INFOF("[FileIO] Importing file: %s (format=%d)", filePath.c_str(), static_cast<int>(format));

        if (format == FileFormat::STEP)
            SY_INFOF("[FileIO] STEP/STP import requested: %s", filePath.c_str());

        auto& factory = FileParserFactory::instance();

        return processFile<ParseResult>(
            filePath, m_importCallback, "parser",
            [&]() { return factory.hasParser(format); },
            [&]() { return factory.createParser(format); },
            [&](IFileParser* p) { return p->parse(filePath, outEntities); }
        );
    }

    WriteResult FileIOManager::exportFile(const std::string& filePath, const VecSyEntityPtr& entities)
    {
        FileFormat fmt = detectFormat(filePath);
        if (fmt == FileFormat::Unknown)
            return WriteResult::fail("Cannot determine format from file extension: " + filePath);

        return exportFile(filePath, fmt, entities);
    }

    WriteResult FileIOManager::exportFile(const std::string& filePath, FileFormat format, const VecSyEntityPtr& entities)
    {
        SY_INFOF("[FileIO] Exporting file: %s (format=%d, entities=%zu)",
            filePath.c_str(), static_cast<int>(format), entities.size());
        auto& factory = FileWriterFactory::instance();
        return processFile<WriteResult>(
            filePath, m_exportCallback, "writer",
            [&]() { return factory.hasWriter(format); },
            [&]() { return factory.createWriter(format); },
            [&](IFileWriter* w) { return w->write(filePath, entities); }
        );
    }

    FileFormat FileIOManager::detectFormat(const std::string& filePath) const
    {
        std::filesystem::path path = std::filesystem::u8path(filePath);
        std::string ext = path.extension().string();
        if (!ext.empty() && ext[0] == '.')
            ext = ext.substr(1);

        FileFormat fmt = FileParserFactory::instance().detectFormat(ext);
        if (fmt != FileFormat::Unknown)
            return fmt;

        if (ext == "sy")
            return FileFormat::Native;
        if (ext == "bmp")
            return FileFormat::BMP;
        if (ext == "png")
            return FileFormat::PNG;
        if (ext == "igs" || ext == "iges")
            return FileFormat::UG;
        if (ext == "stp" || ext == "step")
            return FileFormat::STEP;

        return FileFormat::Unknown;
    }

    std::vector<std::string> FileIOManager::supportedImportExtensions() const
    {
        return FileParserFactory::instance().allSupportedExtensions();
    }

    std::vector<std::string> FileIOManager::supportedExportExtensions() const
    {
        return FileWriterFactory::instance().supportedExtensions();
    }

    bool FileIOManager::canImport(const std::string& filePath) const
    {
        return detectFormat(filePath) != FileFormat::Unknown
            && FileParserFactory::instance().hasParser(detectFormat(filePath));
    }

    bool FileIOManager::canExport(FileFormat format) const
    {
        return FileWriterFactory::instance().hasWriter(format);
    }
} // namespace Fio