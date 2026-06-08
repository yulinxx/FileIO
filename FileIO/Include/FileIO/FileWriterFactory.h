#pragma once

#include "FileIO/FileIOAPI.h"
#include "FileIO/FileFormat.h"
#include "FileIO/IFileWriter.h"

#include <memory>
#include <map>
#include <functional>
#include <string>
#include <vector>

namespace Fio
{
    class FILEIO_API FileWriterFactory
    {
    public:
        using CreatorFunc = std::function<std::unique_ptr<IFileWriter>()>;

        static FileWriterFactory& instance();

        void registerWriter(FileFormat format, CreatorFunc creator, const std::string& defaultExtension = "");

        std::unique_ptr<IFileWriter> createWriter(FileFormat format) const;

        bool hasWriter(FileFormat format) const;

        std::vector<FileFormat> supportedFormats() const;

        std::vector<std::string> supportedExtensions() const;

        void initDefaults();

    private:
        FileWriterFactory() = default;

        std::map<FileFormat, CreatorFunc> m_creators;
        std::map<FileFormat, std::string> m_formatToExt;
    };
} // namespace Fio
