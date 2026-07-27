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
#ifdef _WIN32
        const char sep = ';';
#else
        const char sep = ':';
#endif
        std::string::size_type end = pathStr.find(sep);

        while (end != std::string::npos)
        {
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

    std::string PdfToSvgConverter::findToolExe(const std::string& toolName, const std::string& exeName)
    {
        std::string appDir = getExecutableDir();

        // 结构化部署: tools/<toolName>/bin/<exe>
        std::string toolBinDir = (std::filesystem::path(appDir) / "tools" / toolName / "bin").string();
        std::string result = findInDirectory(toolBinDir, exeName);
        if (!result.empty())
            return result;

        // SANYI_TOOLS_DIR 环境变量指定的目录 + 工具名
        const char* toolsDirEnv = std::getenv("SANYI_TOOLS_DIR");
        if (toolsDirEnv)
        {
            std::string envToolBin = (std::filesystem::path(toolsDirEnv) / toolName / "bin").string();
            result = findInDirectory(envToolBin, exeName);
            if (!result.empty())
                return result;
        }

        return "";
    }

    std::string PdfToSvgConverter::findToolLibDir(const std::string& toolName)
    {
        std::string appDir = getExecutableDir();

        // 结构化部署: tools/<toolName>/lib/
        std::string toolLibDir = (std::filesystem::path(appDir) / "tools" / toolName / "lib").string();
        if (std::filesystem::exists(toolLibDir) && std::filesystem::is_directory(toolLibDir))
        {
            return toolLibDir;
        }

        // SANYI_TOOLS_DIR 环境变量
        const char* toolsDirEnv = std::getenv("SANYI_TOOLS_DIR");
        if (toolsDirEnv)
        {
            std::string envToolLib = (std::filesystem::path(toolsDirEnv) / toolName / "lib").string();
            if (std::filesystem::exists(envToolLib) && std::filesystem::is_directory(envToolLib))
            {
                return envToolLib;
            }
        }

        return "";
    }

    std::string PdfToSvgConverter::findPdftocairoPath()
    {
        std::string exeName = "pdftocairo";
#ifdef _WIN32
        exeName += ".exe";
#endif

        // 外部工具发现策略（优先级从高到低）：
        // 1. SANYI_TOOLS_DIR 环境变量（用户显式指定，最高优先级）
        // 2. 应用程序同级目录（Windows 便携部署，CMake 直接复制）
        // 3. 结构化 tools/poppler/bin/ 目录（Linux/macOS 便携部署）
        // 4. 应用程序同级目录的 tools/ 子目录（扁平结构）
        // 5. PATH 环境变量（系统级安装）
        // 6. 常见安装路径（兜底查找）
        const char* toolsDirEnv = std::getenv("SANYI_TOOLS_DIR");
        if (toolsDirEnv)
        {
            std::string result = findInDirectory(toolsDirEnv, exeName);
            if (!result.empty())
                return result;
        }

        std::string appDir = getExecutableDir();
        std::string result = findInDirectory(appDir, exeName);
        if (!result.empty())
            return result;

        // 结构化部署: tools/poppler/bin/ （Linux/macOS）
        result = findToolExe("poppler", exeName);
        if (!result.empty())
            return result;

        std::string toolsSubDir = (std::filesystem::path(appDir) / "tools").string();
        result = findInDirectory(toolsSubDir, exeName);
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
        exeNames = { "gswin64c.exe", "gswin32c.exe", "gswin64.exe", "gswin32.exe" };
#else
        exeNames = { "gs", "ghostscript" };
#endif

        // 外部工具发现策略（优先级从高到低）：
        // 1. SANYI_TOOLS_DIR 环境变量（用户显式指定，最高优先级）
        // 2. 应用程序同级目录（Windows 便携部署，CMake 直接复制）
        // 3. 结构化 tools/ghostscript/bin/ 目录（Linux/macOS 便携部署）
        // 4. 应用程序同级目录的 tools/ 子目录（扁平结构）
        // 5. PATH 环境变量（系统级安装）
        // 6. 常见安装路径（兜底查找）
        const char* toolsDirEnv = std::getenv("SANYI_TOOLS_DIR");
        if (toolsDirEnv)
        {
            for (const std::string& exeName : exeNames)
            {
                std::string result = findInDirectory(toolsDirEnv, exeName);
                if (!result.empty())
                    return result;
            }
        }

        std::string appDir = getExecutableDir();
        for (const std::string& exeName : exeNames)
        {
            std::string result = findInDirectory(appDir, exeName);
            if (!result.empty())
                return result;
        }

        // 结构化部署: tools/ghostscript/bin/ （Linux/macOS）
        for (const std::string& exeName : exeNames)
        {
            std::string result = findToolExe("ghostscript", exeName);
            if (!result.empty())
                return result;
        }

        std::string toolsSubDir = (std::filesystem::path(appDir) / "tools").string();
        for (const std::string& exeName : exeNames)
        {
            std::string result = findInDirectory(toolsSubDir, exeName);
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
        std::filesystem::path fsPath = std::filesystem::u8path(filePath);
        std::ifstream file(fsPath, std::ios::binary);
        if (!file)
            return false;

        // PDF 文件签名：前4字节必须为 "%PDF"
        // 这是 PDF 规范 ISO 32000 定义的标准魔数
        char header[5];
        file.read(header, 5);
        file.close();

        return std::string(header, 5).substr(0, 4) == "%PDF";
    }

    bool PdfToSvgConverter::isPostScriptFile(const std::string& filePath)
    {
        std::filesystem::path fsPath = std::filesystem::u8path(filePath);
        std::ifstream file(fsPath, std::ios::binary);
        if (!file)
            return false;

        // PostScript 文件签名：前4字节为 "%!PS"
        // 旧版 AI (AI 7-) 使用此格式
        char header[5];
        file.read(header, 5);
        file.close();

        return std::string(header, 5).substr(0, 4) == "%!PS";
    }

    /// 跨平台进程执行封装（带可选库路径环境变量）
    /// Windows: CreateProcessA + WaitForSingleObject
    /// Unix:    fork + execvpe + waitpid，设置 LD_LIBRARY_PATH / DYLD_LIBRARY_PATH
    bool PdfToSvgConverter::executeProcessWithEnv(const std::string& program,
        const std::vector<std::string>& args,
        const std::string& libDir)
    {
#ifdef _WIN32
        std::string command = "\"" + program + "\"";
        for (const std::string& arg : args)
        {
            command += " \"" + arg + "\"";
        }

        std::vector<char> cmdBuf(command.begin(), command.end());
        cmdBuf.push_back('\0');

        STARTUPINFOA si = { 0 };
        PROCESS_INFORMATION pi = { 0 };
        si.cb = sizeof(STARTUPINFOA);

        if (!CreateProcessA(nullptr, cmdBuf.data(),
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
        if (pid == 0)
        {
            if (!libDir.empty())
            {
#ifdef __APPLE__
                const char* existing = std::getenv("DYLD_LIBRARY_PATH");
                std::string newValue = libDir;
                if (existing && strlen(existing) > 0)
                {
                    newValue += ":";
                    newValue += existing;
                }
                setenv("DYLD_LIBRARY_PATH", newValue.c_str(), 1);
#else
                const char* existing = std::getenv("LD_LIBRARY_PATH");
                std::string newValue = libDir;
                if (existing && strlen(existing) > 0)
                {
                    newValue += ":";
                    newValue += existing;
                }
                setenv("LD_LIBRARY_PATH", newValue.c_str(), 1);
#endif
            }

            std::vector<const char*> cargs;
            cargs.push_back(program.c_str());
            for (const std::string& arg : args)
            {
                cargs.push_back(arg.c_str());
            }
            cargs.push_back(nullptr);

            execvp(program.c_str(), const_cast<char* const*>(cargs.data()));
            _exit(1);
        }
        else if (pid > 0)
        {
            int status;
            waitpid(pid, &status, 0);
            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }
        return false;
#endif
    }

    /// PostScript → PDF 转换，通过 Ghostscript CLI
    /// 参数含义：-dNOPAUSE（不分页暂停）-dBATCH（结束后退出）-dQUIET（静默模式）
    ///   -sDEVICE=pdfwrite（输出设备为 PDF 写入器）
    ///   -sOutputFile=（输出路径）-f（结束参数，后跟输入文件）
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

        return executeProcessWithEnv(gs, args, findToolLibDir("ghostscript"));
    }

    /// PDF → SVG 转换：调用 pdftocairo 外部工具
    /// pdftocairo 命令格式：pdftocairo -f <page> -l <page> -svg <input> <output>
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

        bool result = executeProcessWithEnv(pdftocairo, args, findToolLibDir("poppler"));

        if (!result)
            return false;

        // pdftocairo 可能在给定文件名后自动追加 .svg 后缀（如 "output.svg" → "output.svg.svg"）
        // 如果发现这种情况，重命名为期望的文件名
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

    /// 将 PDF/AI/PS 文件转为临时 SVG 文件的完整管道：
    /// PostScript(.ps) → Ghostscript → PDF → pdftocairo → SVG
    /// PDF(.pdf) / AI(.ai) → pdftocairo → SVG
    /// 使用 hash 缓存已经转换过的文件，避免重复调用外部工具
    std::string PdfToSvgConverter::convertToTempSvg(const std::string& filePath, int page)
    {
        std::string hash = generateHash(filePath + std::to_string(page));

        std::filesystem::path tempDir = std::filesystem::temp_directory_path();
        std::string tempSvg = (tempDir / ("sanyi_pdf_" + hash + ".svg")).string();

        // 命中缓存：该文件已经转换过，直接返回
        if (std::filesystem::exists(tempSvg))
        {
            return tempSvg;
        }

        std::string actualPdfPath = filePath;

        // PostScript 格式需要两步转换：PS → PDF → SVG
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