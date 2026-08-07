#include "FileIO/FileWriterFactory.h"
#include "FileIO/IFileWriter.h"
#include "FileIO/Writers/DxfWriter.h"
#include "FileIO/Writers/SvgWriter.h"
#include "FileIO/Writers/PltWriter.h"
#include "FileIO/Writers/NativeWriter.h"
#include "FileIO/Writers/NativeWriter3D.h"
#include "FileIO/Writers/UgWriter.h"
#include "Engine/SyEntity/SyEntity.h"

#include <map>
#include <string>

namespace Fio
{
    class FileWriterFactory::Impl
    {
    public:
        std::map<FileFormat, CreatorFunc> m_creators;
        std::map<FileFormat, std::string> m_formatToExt;
    };

    FileWriterFactory::FileWriterFactory()
        : m_impl(new Impl())
    {
    }

    FileWriterFactory::~FileWriterFactory()
    {
        delete m_impl;
    }

    FileWriterFactory& FileWriterFactory::instance()
    {
        static FileWriterFactory factory;
        return factory;
    }

    void FileWriterFactory::registerWriter(FileFormat format, CreatorFunc creator,
        const char* defaultExtension)
    {
        m_impl->m_creators[format] = creator;
        if (defaultExtension && defaultExtension[0] != '\0')
            m_impl->m_formatToExt[format] = defaultExtension;
    }

    IFileWriter* FileWriterFactory::createWriter(FileFormat format) const
    {
        auto it = m_impl->m_creators.find(format);
        if (it != m_impl->m_creators.end())
            return it->second();
        return nullptr;
    }

    void FileWriterFactory::destroyWriter(IFileWriter* writer) const
    {
        delete writer;
    }

    bool FileWriterFactory::hasWriter(FileFormat format) const
    {
        return m_impl->m_creators.find(format) != m_impl->m_creators.end();
    }

    void FileWriterFactory::forEachSupportedExtension(
        void (*visitor)(const char* ext, void* ctx), void* ctx) const
    {
        if (!visitor)
            return;
        for (const auto& pair : m_impl->m_formatToExt)
            visitor(pair.second.c_str(), ctx);
    }

    void FileWriterFactory::initDefaults()
    {
        registerWriter(FileFormat::DXF, []() -> IFileWriter* { return new DxfWriter(); }, "dxf");
        registerWriter(FileFormat::SVG, []() -> IFileWriter* { return new SvgWriter(); }, "svg");
        registerWriter(FileFormat::PLT, []() -> IFileWriter* { return new PltWriter(); }, "plt");
        registerWriter(FileFormat::UG, []() -> IFileWriter* { return new UgWriter(); }, "igs");
        registerWriter(FileFormat::Native, []() -> IFileWriter* { return new NativeWriter(); }, "sy");
        registerWriter(FileFormat::Native3D, []() -> IFileWriter* { return new NativeWriter3D(); }, "syx");
    }
} // namespace Fio