#include "FileIO/FileWriterFactory.h"
#include "FileIO/Writers/DxfWriter.h"
#include "FileIO/Writers/SvgWriter.h"
#include "FileIO/Writers/PltWriter.h"
#include "FileIO/Writers/NativeWriter.h"
#include "FileIO/Writers/NativeWriter3D.h"
#include "FileIO/Writers/UgWriter.h"

namespace Fio
{
    FileWriterFactory& FileWriterFactory::instance()
    {
        static FileWriterFactory factory;
        return factory;
    }

    void FileWriterFactory::registerWriter(FileFormat format, CreatorFunc creator, const std::string& defaultExtension)
    {
        m_creators[format] = std::move(creator);
        if (!defaultExtension.empty())
            m_formatToExt[format] = defaultExtension;
    }

    std::unique_ptr<IFileWriter> FileWriterFactory::createWriter(FileFormat format) const
    {
        auto it = m_creators.find(format);
        if (it != m_creators.end())
            return it->second();
        return nullptr;
    }

    bool FileWriterFactory::hasWriter(FileFormat format) const
    {
        return m_creators.find(format) != m_creators.end();
    }

    std::vector<FileFormat> FileWriterFactory::supportedFormats() const
    {
        std::vector<FileFormat> formats;
        for (const auto& pair : m_creators)
            formats.push_back(pair.first);
        return formats;
    }

    std::vector<std::string> FileWriterFactory::supportedExtensions() const
    {
        std::vector<std::string> exts;
        for (const auto& pair : m_formatToExt)
            exts.push_back(pair.second);
        return exts;
    }

    void FileWriterFactory::initDefaults()
    {
        registerWriter(FileFormat::DXF, []() { return std::make_unique<DxfWriter>(); }, "dxf");
        registerWriter(FileFormat::SVG, []() { return std::make_unique<SvgWriter>(); }, "svg");
        registerWriter(FileFormat::PLT, []() { return std::make_unique<PltWriter>(); }, "plt");
        registerWriter(FileFormat::UG, []() { return std::make_unique<UgWriter>(); }, "igs");
        registerWriter(FileFormat::Native, []() { return std::make_unique<NativeWriter>(); }, "sy");
        registerWriter(FileFormat::Native3D, []() { return std::make_unique<NativeWriter3D>(); }, "syx");
    }
} // namespace Fio