#include "FileIO/FileParserFactory.h"
#include "FileIO/IFileParser.h"
#include "FileIO/FormatRegistry.h"
#include "Log/SyLogger.h"

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

        SY_WARNF("[FileParserFactory] No parser registered for format=%d (%zu format(s) registered)",
            static_cast<int>(format),
            m_impl->m_creators.size());
        return nullptr;
    }

    void FileParserFactory::destroyParser(IFileParser* parser) const
    {
        if (!parser)
        {
            SY_DEBUG("[FileParserFactory] destroyParser called with nullptr");
            return;
        }
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
}  // namespace Fio
