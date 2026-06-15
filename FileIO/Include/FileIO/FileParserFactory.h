#pragma once

#include "FileIO/FileIOAPI.h"
#include "FileIO/FileFormat.h"
#include "FileIO/IFileParser.h"

#include <memory>
#include <map>
#include <functional>
#include <string>
#include <vector>

namespace Fio
{
    class FILEIO_API FileParserFactory
    {
    public:
        using CreatorFunc = std::function<std::unique_ptr<IFileParser>()>;

        static FileParserFactory& instance();

        void registerParser(FileFormat format, CreatorFunc creator);

        std::unique_ptr<IFileParser> createParser(FileFormat format) const;

        std::unique_ptr<IFileParser> createParserByExtension(const std::string& ext) const;

        bool hasParser(FileFormat format) const;

        FileFormat detectFormat(const std::string& ext) const;

        std::vector<FileFormat> supportedFormats() const;

        std::vector<std::string> allSupportedExtensions() const;

        void initDefaults();

    private:
        FileParserFactory() = default;

        /// 批量注册一个解析器及其扩展名映射，消除 initDefaults 中的重复样板
        void registerWithExtensions(FileFormat format, CreatorFunc creator,
            std::vector<std::string> extensions)
        {
            registerParser(format, std::move(creator));
            for (auto& ext : extensions)
                m_extToFormat[std::move(ext)] = format;
        }

    private:
        std::map<FileFormat, CreatorFunc> m_creators;
        std::map<std::string, FileFormat> m_extToFormat;
    };
} // namespace Fio
