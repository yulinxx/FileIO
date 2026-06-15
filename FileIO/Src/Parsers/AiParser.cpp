#include "FileIO/Parsers/AiParser.h"

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
        return { "ai" };
    }

    bool AiParser::isValidSourceFormat(const std::string& filePath) const
    {
        bool isPdf = PdfToSvgConverter::isPdfFile(filePath);
        bool isPs = PdfToSvgConverter::isPostScriptFile(filePath);
        if (!isPdf && !isPs)
        {
            // 失效格式：不是 PDF 基 AI 也不是 PostScript 基 AI
            return false;
        }
        return true;
    }

    std::string AiParser::extraToolCheckError(const std::string& filePath) const
    {
        if (PdfToSvgConverter::isPostScriptFile(filePath)
            && !PdfToSvgConverter::isGhostscriptAvailable())
        {
            return "Ghostscript not found for PostScript AI format.\n\n"
                + PdfToSvgConverter::getInstallHint();
        }
        return {};  // 检查通过
    }
} // namespace Fio