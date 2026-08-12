#include "FileIO/FileParserFactory.h"
#include "FileIO/IFileParser.h"
#include "FileIO/Parsers/DxfParser.h"
#include "FileIO/Parsers/PltParser.h"
#include "FileIO/Parsers/SvgParser.h"
#include "FileIO/Parsers/UgParser.h"
#include "FileIO/Parsers/StepParser.h"
#include "FileIO/Parsers/PdfParser.h"
#include "FileIO/Parsers/AiParser.h"
#include "FileIO/Parsers/NativeParser.h"
#include "FileIO/Parsers/NativeParser3D.h"
#include "FileIO/Parsers/StlParser.h"
#include "Engine/SyEntity/SyEntity.h"

#include <map>
#include <string>
#include <algorithm>
#include <cctype>

namespace Fio
{
    class FileParserFactory::Impl
    {
    public:
        std::map<FileFormat, CreatorFunc> m_creators;
        std::map<std::string, FileFormat> m_extToFormat;
    };

    FileParserFactory::FileParserFactory()
        : m_impl(new Impl())
    {
    }

    FileParserFactory::~FileParserFactory()
    {
        delete m_impl;
    }

    FileParserFactory& FileParserFactory::instance()
    {
        static FileParserFactory factory;
        return factory;
    }

    void FileParserFactory::registerParser(FileFormat format, CreatorFunc creator)
    {
        m_impl->m_creators[format] = creator;
    }

    void FileParserFactory::registerWithExtensions(FileFormat format, CreatorFunc creator,
        const char* const* extensions, size_t count)
    {
        registerParser(format, creator);
        for (size_t i = 0; i < count; ++i)
            m_impl->m_extToFormat[extensions[i]] = format;
    }

    IFileParser* FileParserFactory::createParser(FileFormat format) const
    {
        auto it = m_impl->m_creators.find(format);
        if (it != m_impl->m_creators.end())
            return it->second();
        return nullptr;
    }

    void FileParserFactory::destroyParser(IFileParser* parser) const
    {
        delete parser;
    }

    bool FileParserFactory::hasParser(FileFormat format) const
    {
        return m_impl->m_creators.find(format) != m_impl->m_creators.end();
    }

    FileFormat FileParserFactory::detectFormat(const char* ext) const
    {
        if (!ext)
            return FileFormat::Unknown;

        std::string lowerExt = ext;
        std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(),
            [](unsigned char c) { return std::tolower(c); });

        auto it = m_impl->m_extToFormat.find(lowerExt);
        if (it != m_impl->m_extToFormat.end())
            return it->second;
        return FileFormat::Unknown;
    }

    void FileParserFactory::forEachSupportedExtension(
        void (*visitor)(const char* ext, void* ctx), void* ctx) const
    {
        if (!visitor)
            return;
        for (const auto& pair : m_impl->m_extToFormat)
            visitor(pair.first.c_str(), ctx);
    }

    void FileParserFactory::initDefaults()
    {
        const char* const dxfExts[] = { "dxf" };
        registerWithExtensions(FileFormat::DXF, []() -> IFileParser* { return new DxfParser(); }, dxfExts, 1);

        const char* const pltExts[] = { "plt", "hpgl" };
        registerWithExtensions(FileFormat::PLT, []() -> IFileParser* { return new PltParser(); }, pltExts, 2);

        const char* const svgExts[] = { "svg", "svgz" };
        registerWithExtensions(FileFormat::SVG, []() -> IFileParser* { return new SvgParser(); }, svgExts, 2);

        const char* const ugExts[] = { "prt", "igs", "iges" };
        registerWithExtensions(FileFormat::UG, []() -> IFileParser* { return new UgParser(); }, ugExts, 3);

        const char* const stepExts[] = { "stp", "step" };
        registerWithExtensions(FileFormat::STEP, []() -> IFileParser* { return new StepParser(); }, stepExts, 2);

        const char* const pdfExts[] = { "pdf" };
        registerWithExtensions(FileFormat::PDF, []() -> IFileParser* { return new PdfParser(); }, pdfExts, 1);

        const char* const aiExts[] = { "ai" };
        registerWithExtensions(FileFormat::AI, []() -> IFileParser* { return new AiParser(); }, aiExts, 1);

        const char* const nativeExts[] = { "sy" };
        registerWithExtensions(FileFormat::Native, []() -> IFileParser* { return new NativeParser(); }, nativeExts, 1);

        const char* const native3DExts[] = { "syx" };
        registerWithExtensions(FileFormat::Native3D, []() -> IFileParser* { return new NativeParser3D(); }, native3DExts, 1);

        const char* const stlExts[] = { "stl" };
        registerWithExtensions(FileFormat::STL, []() -> IFileParser* { return new StlParser(); }, stlExts, 1);
    }
} // namespace Fio