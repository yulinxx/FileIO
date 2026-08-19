#include "FileIO/FormatRegistry.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Fio
{
    namespace
    {
        std::string toLower(const std::string& s)
        {
            std::string out = s;
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }
    }  // namespace

    class FormatRegistry::Impl
    {
    public:
        struct FormatInfo
        {
            std::vector<std::string> extensions;
            std::string defaultExt;
            std::string importFilter;
            std::string exportFilter;
            bool importSupported = false;
            bool exportSupported = false;
        };

        std::unordered_map<std::string, FileFormat> extToFormat;
        std::unordered_map<FileFormat, FormatInfo> formatInfo;
    };

    FormatRegistry& FormatRegistry::instance()
    {
        static FormatRegistry registry;
        return registry;
    }

    FormatRegistry::FormatRegistry()
        : m_impl(new Impl())
    {
        registerDefaults();
    }

    FormatRegistry::~FormatRegistry()
    {
        delete m_impl;
    }

    void FormatRegistry::registerFormat(FileFormat format,
                                        const char* const* extensions,
                                        size_t extCount,
                                        const char* defaultExt,
                                        const char* importFilter,
                                        const char* exportFilter,
                                        bool importSupported,
                                        bool exportSupported)
    {
        Impl::FormatInfo info;
        info.importSupported = importSupported;
        info.exportSupported = exportSupported;

        if (defaultExt)
        {
            info.defaultExt = toLower(defaultExt);
        }
        if (importFilter)
        {
            info.importFilter = importFilter;
        }
        if (exportFilter)
        {
            info.exportFilter = exportFilter;
        }

        for (size_t i = 0; i < extCount; ++i)
        {
            std::string ext = toLower(extensions[i]);
            info.extensions.push_back(ext);
            m_impl->extToFormat[ext] = format;
        }

        if (info.defaultExt.empty() && !info.extensions.empty())
        {
            info.defaultExt = info.extensions.front();
        }

        m_impl->formatInfo[format] = std::move(info);
    }

    void FormatRegistry::registerDefaults()
    {
        // 与 FileParserFactory.initDefaults / ImportDispatcher / ExportDispatcher / FileDialogService 对齐
        const char* dxf[] = { "dxf" };
        registerFormat(FileFormat::DXF, dxf, 1, "dxf",
                       "DXF Files (*.dxf);;All Files (*.*)",
                       "DXF Files (*.dxf);;All Files (*.*)", true, true);

        const char* plt[] = { "plt", "hpgl" };
        registerFormat(FileFormat::PLT, plt, 2, "plt",
                       "PLT Files (*.plt *.hpgl);;All Files (*.*)",
                       "PLT Files (*.plt);;All Files (*.*)", true, true);

        const char* svg[] = { "svg", "svgz" };
        registerFormat(FileFormat::SVG, svg, 2, "svg",
                       "SVG Files (*.svg);;All Files (*.*)",
                       "SVG Files (*.svg);;All Files (*.*)", true, true);

        const char* ug[] = { "prt", "igs", "iges" };
        registerFormat(FileFormat::UG, ug, 3, "igs",
                       "IGES Files (*.igs *.iges);;All Files (*.*)",
                       "IGES Files (*.igs *.iges);;All Files (*.*)", true, true);

        const char* step[] = { "stp", "step" };
        registerFormat(FileFormat::STEP, step, 2, "stp",
                       "STEP Files (*.stp *.step);;All Files (*.*)",
                       nullptr, true, false);

        const char* pdf[] = { "pdf" };
        registerFormat(FileFormat::PDF, pdf, 1, "pdf",
                       "PDF Files (*.pdf);;All Files (*.*)",
                       nullptr, true, false);

        const char* ai[] = { "ai" };
        registerFormat(FileFormat::AI, ai, 1, "ai",
                       "Adobe Illustrator (*.ai);;All Files (*.*)",
                       nullptr, true, false);

        const char* sy[] = { "sy" };
        registerFormat(FileFormat::Native, sy, 1, "sy",
                       "SanYi 2D File (*.sy);;All Files (*.*)",
                       "SanYi Files (*.sy);;All Files (*.*)", true, true);

        const char* syx[] = { "syx" };
        registerFormat(FileFormat::Native3D, syx, 1, "syx",
                       nullptr,
                       "SanYi 3D File (*.syx);;All Files (*.*)", true, true);

        const char* stl[] = { "stl" };
        registerFormat(FileFormat::STL, stl, 1, "stl",
                       "STL Files (*.stl);;All Files (*.*)",
                       nullptr, true, false);

        const char* obj[] = { "obj" };
        registerFormat(FileFormat::OBJ, obj, 1, "obj",
                       "OBJ Files (*.obj);;All Files (*.*)",
                       nullptr, true, false);

        const char* bmp[] = { "bmp" };
        registerFormat(FileFormat::BMP, bmp, 1, "bmp",
                       nullptr,
                       "BMP Files (*.bmp);;All Files (*.*)", false, true);

        const char* png[] = { "png" };
        registerFormat(FileFormat::PNG, png, 1, "png",
                       nullptr,
                       "PNG Files (*.png);;All Files (*.*)", false, true);
    }

    FileFormat FormatRegistry::detectFormat(const char* filePath) const
    {
        if (!filePath || !*filePath)
        {
            return FileFormat::Unknown;
        }

        std::filesystem::path path = std::filesystem::u8path(filePath);
        std::string ext = path.extension().string();
        if (!ext.empty() && ext[0] == '.')
        {
            ext = ext.substr(1);
        }
        return detectFormatByExtension(ext.c_str());
    }

    FileFormat FormatRegistry::detectFormatByExtension(const char* ext) const
    {
        if (!ext || !*ext)
        {
            return FileFormat::Unknown;
        }

        std::string lowerExt = toLower(ext);
        auto it = m_impl->extToFormat.find(lowerExt);
        return (it != m_impl->extToFormat.end()) ? it->second : FileFormat::Unknown;
    }

    const char* FormatRegistry::defaultExtension(FileFormat format) const
    {
        auto it = m_impl->formatInfo.find(format);
        if (it != m_impl->formatInfo.end() && !it->second.defaultExt.empty())
        {
            return it->second.defaultExt.c_str();
        }
        return nullptr;
    }

    bool FormatRegistry::isImportSupported(FileFormat format) const
    {
        auto it = m_impl->formatInfo.find(format);
        return (it != m_impl->formatInfo.end() && it->second.importSupported);
    }

    bool FormatRegistry::isExportSupported(FileFormat format) const
    {
        auto it = m_impl->formatInfo.find(format);
        return (it != m_impl->formatInfo.end() && it->second.exportSupported);
    }

    void FormatRegistry::forEachImportExtension(void (*visitor)(const char* ext, void* ctx), void* ctx) const
    {
        if (!visitor)
        {
            return;
        }
        for (const auto& pair : m_impl->formatInfo)
        {
            if (!pair.second.importSupported)
            {
                continue;
            }
            for (const auto& ext : pair.second.extensions)
            {
                visitor(ext.c_str(), ctx);
            }
        }
    }

    void FormatRegistry::forEachExportExtension(void (*visitor)(const char* ext, void* ctx), void* ctx) const
    {
        if (!visitor)
        {
            return;
        }
        for (const auto& pair : m_impl->formatInfo)
        {
            if (!pair.second.exportSupported)
            {
                continue;
            }
            for (const auto& ext : pair.second.extensions)
            {
                visitor(ext.c_str(), ctx);
            }
        }
    }

    const char* FormatRegistry::importFilter(FileFormat format) const
    {
        auto it = m_impl->formatInfo.find(format);
        if (it != m_impl->formatInfo.end() && !it->second.importFilter.empty())
        {
            return it->second.importFilter.c_str();
        }
        return nullptr;
    }

    const char* FormatRegistry::exportFilter(FileFormat format) const
    {
        auto it = m_impl->formatInfo.find(format);
        if (it != m_impl->formatInfo.end() && !it->second.exportFilter.empty())
        {
            return it->second.exportFilter.c_str();
        }
        return nullptr;
    }
}  // namespace Fio
