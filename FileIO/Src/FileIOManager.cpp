#include "FileIO/FileIOManager.h"
#include "FileIO/FileParserFactory.h"
#include "FileIO/FileWriterFactory.h"
#include "FileIO/IFileParser.h"
#include "FileIO/IFileWriter.h"

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
        if (m_importCallback)
            m_importCallback(filePath, true);

        auto& factory = FileParserFactory::instance();
        if (!factory.hasParser(format))
        {
            if (m_importCallback)
                m_importCallback(filePath, false);
            return ParseResult::fail("No parser registered for this format");
        }

        auto parser = factory.createParser(format);
        if (!parser)
        {
            if (m_importCallback)
                m_importCallback(filePath, false);
            return ParseResult::fail("Failed to create parser");
        }

        auto result = parser->parse(filePath, outEntities);
        if (m_importCallback)
            m_importCallback(filePath, result.success);

        return result;
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
        if (m_exportCallback)
            m_exportCallback(filePath, true);

        auto& factory = FileWriterFactory::instance();
        if (!factory.hasWriter(format))
        {
            if (m_exportCallback)
                m_exportCallback(filePath, false);
            return WriteResult::fail("No writer registered for this format");
        }

        auto writer = factory.createWriter(format);
        if (!writer)
        {
            if (m_exportCallback)
                m_exportCallback(filePath, false);
            return WriteResult::fail("Failed to create writer");
        }

        auto result = writer->write(filePath, entities);
        if (m_exportCallback)
            m_exportCallback(filePath, result.success);

        return result;
    }

    FileFormat FileIOManager::detectFormat(const std::string& filePath) const
    {
        std::filesystem::path path(filePath);
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
