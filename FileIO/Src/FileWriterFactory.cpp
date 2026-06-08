#include "FileIO/FileWriterFactory.h"

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
        // TODO: 文件导出功能尚未实现，后续需要注册以下 Writer:
        //   registerWriter(FileFormat::DXF,  []() { return std::make_unique<DxfWriter>(); }, "dxf");
        //   registerWriter(FileFormat::SVG,  []() { return std::make_unique<SvgWriter>(); }, "svg");
        //   registerWriter(FileFormat::Native, []() { return std::make_unique<NativeWriter>(); }, "sy");
        //   registerWriter(FileFormat::BMP,    []() { return std::make_unique<BmpWriter>(); }, "bmp");
        //   registerWriter(FileFormat::PNG,    []() { return std::make_unique<PngWriter>(); }, "png");
    }
} // namespace Fio
