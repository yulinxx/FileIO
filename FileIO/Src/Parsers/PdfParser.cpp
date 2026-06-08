#include "FileIO/Parsers/PdfParser.h"
#include "FileIO/Parsers/PdfToSvgConverter.h"
#include "FileIO/Parsers/SvgParser.h"

#include <filesystem>
#include <iostream>
#include <chrono>

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
        return {"pdf"};
    }

    ParseResult PdfParser::parse(const std::string& filePath, VecSyEntityPtr& outEntities)
    {
        auto t0 = std::chrono::steady_clock::now();

        if (!std::filesystem::exists(filePath))
        {
            return ParseResult::fail("PDF file not found: " + filePath);
        }

        if (!PdfToSvgConverter::isPdfFile(filePath))
        {
            return ParseResult::fail("File is not a valid PDF: " + filePath);
        }

        if (!PdfToSvgConverter::isPdftocairoAvailable())
        {
            std::string hint = PdfToSvgConverter::getInstallHint();
            return ParseResult::fail("pdftocairo not found.\n\n" + hint);
        }

        std::string tempSvg = PdfToSvgConverter::convertToTempSvg(filePath, 1);
        if (tempSvg.empty())
        {
            return ParseResult::fail("Failed to convert PDF to SVG: " + filePath);
        }

        SvgParser svgParser;
        ParseResult result = svgParser.parse(tempSvg, outEntities);

        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        return result;
    }
} // namespace Fio
