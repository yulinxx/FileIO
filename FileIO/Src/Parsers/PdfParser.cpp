#include "FileIO/Parsers/PdfParser.h"

namespace Fio
{
    FileFormat PdfParser::format() const
    {
        return FileFormat::PDF;
    }

    size_t PdfParser::formatName(char* buffer, size_t bufferSize) const
    {
        const char* name = "PDF";
        const size_t len = std::strlen(name);
        if (buffer != nullptr && bufferSize > len)
        {
            std::strcpy(buffer, name);
        }
        return len;
    }

    void PdfParser::forEachSupportedExtension(void (*visitor)(const char*, void*), void* ctx) const
    {
        visitor("pdf", ctx);
    }

    bool PdfParser::isValidSourceFormat(const char* filePath) const
    {
        return PdfToSvgConverter::isPdfFile(filePath);
    }
}  // namespace Fio