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
            std::strncpy(buffer, name, bufferSize - 1);
            buffer[bufferSize - 1] = '\0';
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
#include "FileIO/FileParserFactory.h"
namespace Fio {
namespace {
    static struct PdfRegistrar {
        PdfRegistrar() {
            FileParserFactory::instance().registerParser(FileFormat::PDF, []() -> IFileParser* {
                return new PdfParser();
            });
        }
    } s_pdfRegistrar;
}

}  // namespace Fio
