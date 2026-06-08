#include "FileIO/Parsers/AiParser.h"
#include "FileIO/Parsers/PdfToSvgConverter.h"
#include "FileIO/Parsers/SvgParser.h"

#include <filesystem>
#include <iostream>
#include <chrono>

namespace Fio
{
    FileFormat AiParser::format() const
    {
        return FileFormat::AI;
    }

    std::string AiParser::formatName() const
    {
        return "Adobe Illustrator";
    }

    std::vector<std::string> AiParser::supportedExtensions() const
    {
        return {"ai"};
    }

    ParseResult AiParser::parse(const std::string& filePath, VecSyEntityPtr& outEntities)
    {
        auto t0 = std::chrono::steady_clock::now();

        if (!std::filesystem::exists(filePath))
        {
            return ParseResult::fail("AI file not found: " + filePath);
        }

        bool isPdf = PdfToSvgConverter::isPdfFile(filePath);
        bool isPs = PdfToSvgConverter::isPostScriptFile(filePath);

        if (!isPdf && !isPs)
        {
            return ParseResult::fail(
                "Unknown AI format.\n"
                "File must be PDF-based AI (AI 8+) or PostScript-based AI (AI 7).\n"
                "File: " + filePath);
        }

        if (!PdfToSvgConverter::isPdftocairoAvailable())
        {
            std::string hint = PdfToSvgConverter::getInstallHint();
            return ParseResult::fail("pdftocairo not found.\n\n" + hint);
        }

        if (isPs && !PdfToSvgConverter::isGhostscriptAvailable())
        {
            std::string hint = PdfToSvgConverter::getInstallHint();
            return ParseResult::fail("Ghostscript not found for PostScript AI format.\n\n" + hint);
        }

        std::string tempSvg = PdfToSvgConverter::convertToTempSvg(filePath, 1);
        if (tempSvg.empty())
        {
            return ParseResult::fail("Failed to convert AI to SVG: " + filePath);
        }

        SvgParser svgParser;
        ParseResult result = svgParser.parse(tempSvg, outEntities);

        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        return result;
    }
} // namespace Fio
