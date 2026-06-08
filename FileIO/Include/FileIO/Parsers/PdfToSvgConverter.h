#pragma once

#include <string>
#include <vector>

namespace Fio
{
    class PdfToSvgConverter
    {
    public:
        static bool isPdftocairoAvailable();
        static std::string findPdftocairoPath();
        static bool isGhostscriptAvailable();
        static std::string findGhostscriptPath();
        static bool convertToSvg(const std::string& pdfPath, const std::string& svgPath, int page = 1);
        static std::string convertToTempSvg(const std::string& pdfPath, int page = 1);
        static bool isPdfFile(const std::string& filePath);
        static bool isPostScriptFile(const std::string& filePath);
        static std::string getInstallHint();

    private:
        static bool convertPsToPdf(const std::string& psPath, const std::string& pdfPath);
        static std::string getExecutableDir();
        static std::string findInDirectory(const std::string& dir, const std::string& exeName);
        static std::string findInPathEnv(const std::string& exeName);
    };
} // namespace Fio
