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
#include "FileIO/Parsers/StlParser.h"
#include "FileIO/FormatRegistry.h"
#include "Engine/SyEntity/SyEntity.h"

#include <map>
#include <string>

namespace Fio
{
    class FileParserFactory::Impl
    {
    public:
        std::map<FileFormat, CreatorFunc> m_creators;
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

    IFileParser* FileParserFactory::createParser(FileFormat format) const
    {
        auto it = m_impl->m_creators.find(format);
        if (it != m_impl->m_creators.end())
        {
            return it->second();
        }
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
        return FormatRegistry::instance().detectFormatByExtension(ext);
    }

    void FileParserFactory::forEachSupportedExtension(void (*visitor)(const char* ext, void* ctx), void* ctx) const
    {
        FormatRegistry::instance().forEachImportExtension(visitor, ctx);
    }

    void FileParserFactory::initDefaults()
    {
        registerParser(FileFormat::DXF, []() -> IFileParser* {
            return new DxfParser();
        });

        registerParser(FileFormat::PLT, []() -> IFileParser* {
            return new PltParser();
        });

        registerParser(FileFormat::SVG, []() -> IFileParser* {
            return new SvgParser();
        });

        registerParser(FileFormat::UG, []() -> IFileParser* {
            return new UgParser();
        });

        registerParser(FileFormat::STEP, []() -> IFileParser* {
            return new StepParser();
        });

        registerParser(FileFormat::PDF, []() -> IFileParser* {
            return new PdfParser();
        });

        registerParser(FileFormat::AI, []() -> IFileParser* {
            return new AiParser();
        });

        registerParser(FileFormat::Native, []() -> IFileParser* {
            return new NativeParser(FileFormat::Native);
        });

        registerParser(FileFormat::Native3D, []() -> IFileParser* {
            return new NativeParser(FileFormat::Native3D);
        });

        registerParser(FileFormat::STL, []() -> IFileParser* {
            return new StlParser();
        });
    }
}  // namespace Fio