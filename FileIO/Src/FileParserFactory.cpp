#include "FileIO/FileParserFactory.h"
#include "FileIO/Parsers/DxfParser.h"
#include "FileIO/Parsers/PltParser.h"
#include "FileIO/Parsers/SvgParser.h"
#include "FileIO/Parsers/UgParser.h"
#include "FileIO/Parsers/PdfParser.h"
#include "FileIO/Parsers/AiParser.h"
#include "FileIO/Parsers/NativeParser.h"
#include "FileIO/Parsers/NativeParser3D.h"

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
        registerWithExtensions(FileFormat::DXF, []() { return std::make_unique<DxfParser>(); },
            { "dxf" });

        registerWithExtensions(FileFormat::PLT, []() { return std::make_unique<PltParser>(); },
            { "plt", "hpgl" });

        registerWithExtensions(FileFormat::SVG, []() { return std::make_unique<SvgParser>(); },
            { "svg", "svgz" });

        registerWithExtensions(FileFormat::UG, []() { return std::make_unique<UgParser>(); },
            { "prt", "igs", "iges", "stp", "step" });

        registerWithExtensions(FileFormat::PDF, []() { return std::make_unique<PdfParser>(); },
            { "pdf" });

        registerWithExtensions(FileFormat::AI, []() { return std::make_unique<AiParser>(); },
            { "ai" });

        registerWithExtensions(FileFormat::Native, []() { return std::make_unique<NativeParser>(); },
            { "sy" });

        registerWithExtensions(FileFormat::Native3D, []() { return std::make_unique<NativeParser3D>(); },
            { "syx" });
    }
} // namespace Fio