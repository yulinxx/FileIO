#pragma once

#include "FileIO/FileIOAPI.h"
#include "FileIO/FileFormat.h"
#include "FileIO/FileIOError.h"

#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace Fio
{
    class FILEIO_API FileIOManager
    {
    public:
        using ImportCallback = std::function<void(const std::string&, bool)>;
        using ExportCallback = std::function<void(const std::string&, bool)>;

        explicit FileIOManager();
        ~FileIOManager();

        FileIOManager(const FileIOManager&) = delete;
        FileIOManager& operator=(const FileIOManager&) = delete;

        void setImportCallback(ImportCallback callback);
        void setExportCallback(ExportCallback callback);

        ParseResult importFile(const std::string& filePath, VecSyEntityPtr& outEntities);

        ParseResult importFile(const std::string& filePath, FileFormat format, VecSyEntityPtr& outEntities);

        WriteResult exportFile(const std::string& filePath, const VecSyEntityPtr& entities);

        WriteResult exportFile(const std::string& filePath, FileFormat format, const VecSyEntityPtr& entities);

        FileFormat detectFormat(const std::string& filePath) const;

        std::vector<std::string> supportedImportExtensions() const;
        std::vector<std::string> supportedExportExtensions() const;

        bool canImport(const std::string& filePath) const;
        bool canExport(FileFormat format) const;

    private:
        ImportCallback m_importCallback;
        ExportCallback m_exportCallback;
    };
} // namespace Fio
