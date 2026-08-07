#include "FileIO/Parsers/AiParser.h"

namespace Fio
{
    FileFormat AiParser::format() const
    {
        return FileFormat::AI;
    }

    size_t AiParser::formatName(char* buffer, size_t bufferSize) const
    {
        const char* name = "Adobe Illustrator";
        const size_t len = std::strlen(name);
        if (buffer != nullptr && bufferSize > len)
            std::strcpy(buffer, name);
        return len;
    }

    void AiParser::forEachSupportedExtension(void(*visitor)(const char*, void*), void* ctx) const
    {
        visitor("ai", ctx);
    }

    bool AiParser::isValidSourceFormat(const char* filePath) const
    {
        std::string fp(filePath);
        bool isPdf = PdfToSvgConverter::isPdfFile(fp);
        bool isPs = PdfToSvgConverter::isPostScriptFile(fp);
        if (!isPdf && !isPs)
        {
            // 失效格式：不是 PDF 基 AI 也不是 PostScript 基 AI
            return false;
        }
        return true;
    }

    std::string AiParser::extraToolCheckError(const char* filePath) const
    {
        std::string fp(filePath);
        if (PdfToSvgConverter::isPostScriptFile(fp)
            && !PdfToSvgConverter::isGhostscriptAvailable())
        {
            return "Ghostscript not found for PostScript AI format.\n\n"
                + PdfToSvgConverter::getInstallHint();
        }
        return {};  // 检查通过
    }
} // namespace Fio