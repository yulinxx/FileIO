#include "FileIO/Parsers/PdfParser.h"

namespace Fio
{
    FileFormat PdfParser::format() const
    {
        return FileFormat::PDF;
    }

    std::string PdfParser::formatName() const
    {
        return "PDF";
    }

    std::vector<std::string> PdfParser::supportedExtensions() const
    {
        return { "pdf" };
    }

    bool PdfParser::isValidSourceFormat(const std::string& filePath) const
    {
        return PdfToSvgConverter::isPdfFile(filePath);
    }
} // namespace Fio