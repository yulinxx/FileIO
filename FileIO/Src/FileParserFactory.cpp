#include "FileIO/FileParserFactory.h"
#include "FileIO/Parsers/DxfParser.h"
#include "FileIO/Parsers/PltParser.h"
#include "FileIO/Parsers/SvgParser.h"
#include "FileIO/Parsers/UgParser.h"
#include "FileIO/Parsers/PdfParser.h"
#include "FileIO/Parsers/AiParser.h"

#include <algorithm>
#include <cctype>

namespace Fio
{
    FileParserFactory& FileParserFactory::instance()
    {
        static FileParserFactory factory;
        return factory;
    }

    void FileParserFactory::registerParser(FileFormat format, CreatorFunc creator)
    {
        m_creators[format] = std::move(creator);
    }

    std::unique_ptr<IFileParser> FileParserFactory::createParser(FileFormat format) const
    {
        auto it = m_creators.find(format);
        if (it != m_creators.end())
            return it->second();
        return nullptr;
    }

    std::unique_ptr<IFileParser> FileParserFactory::createParserByExtension(const std::string& ext) const
    {
        std::string lowerExt = ext;
        std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(),
            [](unsigned char c) { return std::tolower(c); });
        
        auto it = m_extToFormat.find(lowerExt);
        if (it != m_extToFormat.end())
            return createParser(it->second);
        return nullptr;
    }

    bool FileParserFactory::hasParser(FileFormat format) const
    {
        return m_creators.find(format) != m_creators.end();
    }

    FileFormat FileParserFactory::detectFormat(const std::string& ext) const
    {
        std::string lowerExt = ext;
        std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(),
            [](unsigned char c) { return std::tolower(c); });
        
        auto it = m_extToFormat.find(lowerExt);
        if (it != m_extToFormat.end())
            return it->second;
        return FileFormat::Unknown;
    }

    std::vector<FileFormat> FileParserFactory::supportedFormats() const
    {
        std::vector<FileFormat> formats;
        for (const auto& pair : m_creators)
            formats.push_back(pair.first);
        return formats;
    }

    std::vector<std::string> FileParserFactory::allSupportedExtensions() const
    {
        std::vector<std::string> exts;
        for (const auto& pair : m_extToFormat)
            exts.push_back(pair.first);
        return exts;
    }

    void FileParserFactory::initDefaults()
    {
        registerParser(FileFormat::DXF, []() {
            return std::make_unique<DxfParser>();
            });
        m_extToFormat["dxf"] = FileFormat::DXF;

        registerParser(FileFormat::PLT, []() {
            return std::make_unique<PltParser>();
            });
        m_extToFormat["plt"] = FileFormat::PLT;
        m_extToFormat["hpgl"] = FileFormat::PLT;

        registerParser(FileFormat::SVG, []() {
            return std::make_unique<SvgParser>();
            });
        m_extToFormat["svg"] = FileFormat::SVG;
        m_extToFormat["svgz"] = FileFormat::SVG;

        registerParser(FileFormat::UG, []() {
            return std::make_unique<UgParser>();
            });
        m_extToFormat["prt"] = FileFormat::UG;
        m_extToFormat["igs"] = FileFormat::UG;
        m_extToFormat["iges"] = FileFormat::UG;
        m_extToFormat["stp"] = FileFormat::UG;
        m_extToFormat["step"] = FileFormat::UG;

        registerParser(FileFormat::PDF, []() {
            return std::make_unique<PdfParser>();
            });
        m_extToFormat["pdf"] = FileFormat::PDF;

        registerParser(FileFormat::AI, []() {
            return std::make_unique<AiParser>();
            });
        m_extToFormat["ai"] = FileFormat::AI;
    }
} // namespace Fio
