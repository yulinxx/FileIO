#include "FileIO/Parsers/PdfToSvgConverter.h"

#include "FileIO/FileIOUtils.h"

#include <fstream>
#include <filesystem>
#include <vector>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <libloaderapi.h>
#include <process.h>
#elif defined(__linux__)
#include <unistd.h>
#include <limits.h>
#include <sys/wait.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#include <sys/wait.h>
#endif

namespace Fio
{
    std::string PdfToSvgConverter::getExecutableDir()
    {
#ifdef _WIN32
        wchar_t pathBuf[MAX_PATH];
        if (GetModuleFileNameW(nullptr, pathBuf, MAX_PATH) > 0)
        {
            std::filesystem::path path(pathBuf);
            return path.parent_path().string();
        }
#elif defined(__linux__)
        char pathBuf[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", pathBuf, sizeof(pathBuf) - 1);
        if (len > 0)
        {
            pathBuf[len] = '\0';
            std::filesystem::path path(pathBuf);
            return path.parent_path().string();
        }
#elif defined(__APPLE__)
        char pathBuf[PATH_MAX];
        uint32_t len = sizeof(pathBuf);
        if (_NSGetExecutablePath(pathBuf, &len) == 0)
        {
            std::filesystem::path path(pathBuf);
            return path.parent_path().string();
        }
#endif
        return "";
    }

    bool PdfToSvgConverter::isPdftocairoAvailable()
    {
        return !findPdftocairoPath().empty();
    }

    std::string PdfToSvgConverter::findInDirectory(const std::string& dir, const std::string& exeName)
    {
        if (dir.empty())
            return "";
        std::filesystem::path fullPath = std::filesystem::path(dir) / exeName;
        if (std::filesystem::exists(fullPath))
            return fullPath.string();
        return "";
    }

    std::string PdfToSvgConverter::findInPathEnv(const std::string& exeName)
    {
        const char* pathEnv = std::getenv("PATH");
        if (!pathEnv)
            return "";

        std::string pathStr(pathEnv);
        std::string::size_type start = 0;
        std::string::size_type end = pathStr.find(';');

#ifdef _WIN32
        char sep = ';';
#else
        char sep = ':';
#endif

        while (end != std::string::npos) {
            std::string dir = pathStr.substr(start, end - start);
            std::string result = findInDirectory(dir, exeName);
            if (!result.empty())
                return result;
            start = end + 1;
            end = pathStr.find(sep, start);
        }

        std::string dir = pathStr.substr(start);
        return findInDirectory(dir, exeName);
    }

    std::string PdfToSvgConverter::findPdftocairoPath()
    {
        std::string exeName = "pdftocairo";
#ifdef _WIN32
        exeName += ".exe";
#endif

        std::string appDir = getExecutableDir();
        std::string result = findInDirectory(appDir, exeName);
        if (!result.empty())
            return result;

        result = findInPathEnv(exeName);
        if (!result.empty())
            return result;

#ifdef _WIN32
        std::vector<std::string> commonPaths = {
            "C:/Program Files/poppler/bin/pdftocairo.exe",
            "C:/Program Files (x86)/poppler/bin/pdftocairo.exe",
            "C:/poppler/bin/pdftocairo.exe",
            "C:/tools/poppler/bin/pdftocairo.exe",
        };
#else
        std::vector<std::string> commonPaths = {
            "/usr/bin/pdftocairo",
            "/usr/local/bin/pdftocairo",
            "/opt/homebrew/bin/pdftocairo",
        };
#endif
        for (const std::string& p : commonPaths)
        {
            if (std::filesystem::exists(p))
                return p;
        }
        return "";
    }

    bool PdfToSvgConverter::isGhostscriptAvailable()
    {
        return !findGhostscriptPath().empty();
    }

    std::string PdfToSvgConverter::findGhostscriptPath()
    {
        std::vector<std::string> exeNames;
#ifdef _WIN32
        exeNames = {"gswin64c.exe", "gswin32c.exe", "gswin64.exe", "gswin32.exe"};
#else
        exeNames = {"gs", "ghostscript"};
#endif

        std::string appDir = getExecutableDir();
        for (const std::string& exeName : exeNames)
        {
            std::string result = findInDirectory(appDir, exeName);
            if (!result.empty())
                return result;
        }

        for (const std::string& exeName : exeNames)
        {
            std::string result = findInPathEnv(exeName);
            if (!result.empty())
                return result;
        }

#ifdef _WIN32
        std::filesystem::path gsDir("C:/Program Files/gs");
        if (std::filesystem::exists(gsDir))
        {
            for (const auto& entry : std::filesystem::directory_iterator(gsDir))
            {
                if (entry.is_directory())
                {
                    for (const std::string& exeName : exeNames)
                    {
                        std::filesystem::path gsPath = entry.path() / "bin" / exeName;
                        if (std::filesystem::exists(gsPath))
                            return gsPath.string();
                    }
                }
            }
        }
#else
        std::vector<std::string> commonPaths = {
            "/usr/bin/gs",
            "/usr/local/bin/gs",
            "/opt/homebrew/bin/gs",
        };
        for (const std::string& p : commonPaths)
        {
            if (std::filesystem::exists(p))
                return p;
        }
#endif
        return "";
    }

    bool PdfToSvgConverter::isPdfFile(const std::string& filePath)
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file)
            return false;

        char header[5];
        file.read(header, 5);
        file.close();

        return std::string(header, 5).substr(0, 4) == "%PDF";
    }

    bool PdfToSvgConverter::isPostScriptFile(const std::string& filePath)
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file)
            return false;

        char header[5];
        file.read(header, 5);
        file.close();

        return std::string(header, 5).substr(0, 4) == "%!PS";
    }

    bool executeProcess(const std::string& program, const std::vector<std::string>& args)
    {
#ifdef _WIN32
        std::string command = program;
        for (const std::string& arg : args) {
            command += " \"" + arg + "\"";
        }

        STARTUPINFO si = {0};
        PROCESS_INFORMATION pi = {0};
        si.cb = sizeof(STARTUPINFO);

        if (!CreateProcessA(nullptr, const_cast<char*>(command.c_str()),
                            nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                            &si, &pi))
        {
            return false;
        }

        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD exitCode;
        GetExitCodeProcess(pi.hProcess, &exitCode);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        return exitCode == 0;
#else
        pid_t pid = fork();
        if (pid == 0) {
            std::vector<const char*> cargs;
            cargs.push_back(program.c_str());
            for (const std::string& arg : args) {
                cargs.push_back(arg.c_str());
            }
            cargs.push_back(nullptr);

            execvp(program.c_str(), const_cast<char* const*>(cargs.data()));
            _exit(1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }
        return false;
#endif
    }

    bool PdfToSvgConverter::convertPsToPdf(const std::string& psPath, const std::string& pdfPath)
    {
        std::string gs = findGhostscriptPath();
        if (gs.empty())
            return false;

        if (!std::filesystem::exists(psPath))
            return false;

        std::vector<std::string> args;
        args.push_back("-dNOPAUSE");
        args.push_back("-dBATCH");
        args.push_back("-dQUIET");
        args.push_back("-sDEVICE=pdfwrite");
        args.push_back("-sOutputFile=" + pdfPath);
        args.push_back("-f");
        args.push_back(psPath);

        return executeProcess(gs, args);
    }

    bool PdfToSvgConverter::convertToSvg(const std::string& pdfPath, const std::string& svgPath, int page)
    {
        std::string pdftocairo = findPdftocairoPath();
        if (pdftocairo.empty())
            return false;

        if (!std::filesystem::exists(pdfPath))
            return false;

        std::vector<std::string> args;
        args.push_back("-f");
        char pageStr[10];
        sprintf(pageStr, "%d", page);
        args.push_back(pageStr);
        args.push_back("-l");
        args.push_back(pageStr);
        args.push_back("-svg");
        args.push_back(pdfPath);
        args.push_back(svgPath);

        bool result = executeProcess(pdftocairo, args);

        if (!result)
            return false;

        if (!std::filesystem::exists(svgPath))
        {
            std::string altPath = svgPath + ".svg";
            if (std::filesystem::exists(altPath))
            {
                std::filesystem::rename(altPath, svgPath);
            }
        }

        return std::filesystem::exists(svgPath);
    }

    std::string PdfToSvgConverter::convertToTempSvg(const std::string& filePath, int page)
    {
        std::string hash = generateHash(filePath + std::to_string(page));

        std::filesystem::path tempDir = std::filesystem::temp_directory_path();
        std::string tempSvg = (tempDir / ("sanyi_pdf_" + hash + ".svg")).string();

        if (std::filesystem::exists(tempSvg))
        {
            return tempSvg;
        }

        std::string actualPdfPath = filePath;

        if (isPostScriptFile(filePath))
        {
            std::string tempPdf = (tempDir / ("sanyi_ps_" + hash + ".pdf")).string();

            if (!std::filesystem::exists(tempPdf))
            {
                if (!convertPsToPdf(filePath, tempPdf))
                {
                    return "";
                }
            }

            actualPdfPath = tempPdf;
        }
        else if (!isPdfFile(filePath))
        {
            return "";
        }

        if (!convertToSvg(actualPdfPath, tempSvg, page))
        {
            return "";
        }

        return tempSvg;
    }

    std::string PdfToSvgConverter::getInstallHint()
    {
#ifdef _WIN32
        return 
            "pdftocairo and Ghostscript are required to import PDF/AI files.\n\n"
            "=== Option 1: Copy to application folder (Recommended) ===\n\n"
            "1. Download pdftocairo (poppler):\n"
            "   https://github.com/oschwartz10612/poppler-windows/releases\n"
            "   Extract all files from Library/bin/ to your app folder.\n\n"
            "2. Download Ghostscript:\n"
            "   https://github.com/ArtifexSoftware/ghostpdl-downloads/releases\n"
            "   Extract gswin64c.exe and gs.dll to your app folder.\n\n"
            "=== Option 2: Install to system ===\n"
            "1. Run installers for pdftocairo and Ghostscript.\n"
            "2. Ensure they are in PATH or Program Files.\n\n"
            "Restart the application after installation.";
#else
        return 
            "pdftocairo and Ghostscript are required to import PDF/AI files.\n\n"
            "=== Option 1: Copy to application folder (Recommended) ===\n\n"
            "1. Linux: sudo apt install poppler-utils ghostscript\n"
            "   macOS: brew install poppler ghostscript\n"
            "   Then copy executables to your app folder:\n"
            "   - /usr/bin/pdftocairo\n"
            "   - /usr/bin/gs\n\n"
            "=== Option 2: Install to system ===\n"
            "Linux: sudo apt install poppler-utils ghostscript\n"
            "macOS: brew install poppler ghostscript\n\n"
            "Restart the application after installation.";
#endif
    }
} // namespace Fio
