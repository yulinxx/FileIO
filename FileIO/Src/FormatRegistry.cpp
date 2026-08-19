#include "FileIO/FormatRegistry.h"

#include <cctype>
#include <cstring>

namespace Fio
{
    namespace
    {
        const char* const kDxfExts[] = { "dxf" };
        const char* const kPltExts[] = { "plt", "hpgl" };
        const char* const kSvgExts[] = { "svg", "svgz" };
        const char* const kUgExts[] = { "prt", "igs", "iges" };
        const char* const kStepExts[] = { "stp", "step" };
        const char* const kPdfExts[] = { "pdf" };
        const char* const kAiExts[] = { "ai" };
        const char* const kNativeExts[] = { "sy" };
        const char* const kNative3DExts[] = { "syx" };
        const char* const kBmpExts[] = { "bmp" };
        const char* const kPngExts[] = { "png" };
        const char* const kObjExts[] = { "obj" };
        const char* const kStlExts[] = { "stl" };

        std::string toLower(const char* s)
        {
            std::string out;
            if (s)
            {
                for (const char* p = s; *p; ++p)
                {
                    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
                }
            }
            return out;
        }
    }  // namespace

    FormatRegistry& FormatRegistry::instance()
    {
        static FormatRegistry s_instance;
        return s_instance;
    }

    FormatRegistry::FormatRegistry()
    {
        registerFormat(FileFormat::DXF, "DXF Files", kDxfExts, 1);
        registerFormat(FileFormat::PLT, "PLT/HPGL Files", kPltExts, 2);
        registerFormat(FileFormat::SVG, "SVG Files", kSvgExts, 2);
        registerFormat(FileFormat::UG, "UG/IGES Files", kUgExts, 3);
        registerFormat(FileFormat::STEP, "STEP Files", kStepExts, 2);
        registerFormat(FileFormat::PDF, "PDF Files", kPdfExts, 1);
        registerFormat(FileFormat::AI, "AI Files", kAiExts, 1);
        registerFormat(FileFormat::Native, "SanYi 2D Files", kNativeExts, 1);
        registerFormat(FileFormat::Native3D, "SanYi 3D Files", kNative3DExts, 1);
        registerFormat(FileFormat::BMP, "BMP Files", kBmpExts, 1);
        registerFormat(FileFormat::PNG, "PNG Files", kPngExts, 1);
        registerFormat(FileFormat::OBJ, "OBJ Files", kObjExts, 1);
        registerFormat(FileFormat::STL, "STL Files", kStlExts, 1);
    }

    void FormatRegistry::registerFormat(FileFormat format, const char* label,
                                        const char* const* extensions, size_t extCount)
    {
        Entry e;
        e.format = format;
        e.label = label;
        e.extensions = extensions;
        e.extCount = extCount;

        std::string wildcard;
        for (size_t i = 0; i < extCount; ++i)
        {
            if (i > 0)
            {
                wildcard += " ";
            }
            wildcard += "*.";
            wildcard += extensions[i];
        }

        e.importFilterStr = std::string(label) + " (" + wildcard + ")";
        e.exportFilterStr = std::string(label) + " (" + wildcard + ")";

        m_entries.push_back(std::move(e));
    }

    const FormatRegistry::Entry* FormatRegistry::find(FileFormat format) const
    {
        for (const auto& e : m_entries)
        {
            if (e.format == format)
            {
                return &e;
            }
        }
        return nullptr;
    }

    FileFormat FormatRegistry::detectFormat(const char* filePath) const
    {
        if (!filePath || filePath[0] == '\0')
        {
            return FileFormat::Unknown;
        }

        // 取最后一个 '.' 之后的扩展名
        const char* dot = std::strrchr(filePath, '.');
        if (!dot || dot[1] == '\0')
        {
            return FileFormat::Unknown;
        }

        std::string ext = toLower(dot + 1);
        for (const auto& e : m_entries)
        {
            for (size_t i = 0; i < e.extCount; ++i)
            {
                if (ext == e.extensions[i])
                {
                    return e.format;
                }
            }
        }
        return FileFormat::Unknown;
    }

    const char* FormatRegistry::importFilter(FileFormat format) const
    {
        const Entry* e = find(format);
        return e ? e->importFilterStr.c_str() : nullptr;
    }

    const char* FormatRegistry::exportFilter(FileFormat format) const
    {
        const Entry* e = find(format);
        return e ? e->exportFilterStr.c_str() : nullptr;
    }

    FileFormat FormatRegistry::detectFormatByExtension(const char* ext) const
    {
        if (!ext || ext[0] == '\0')
        {
            return FileFormat::Unknown;
        }

        std::string extLower = toLower(ext);
        for (const auto& e : m_entries)
        {
            for (size_t i = 0; i < e.extCount; ++i)
            {
                if (extLower == e.extensions[i])
                {
                    return e.format;
                }
            }
        }
        return FileFormat::Unknown;
    }

    void FormatRegistry::forEachImportExtension(void (*visitor)(const char* ext, void* ctx), void* ctx) const
    {
        if (!visitor)
        {
            return;
        }
        for (const auto& e : m_entries)
        {
            for (size_t i = 0; i < e.extCount; ++i)
            {
                visitor(e.extensions[i], ctx);
            }
        }
    }
}  // namespace Fio
