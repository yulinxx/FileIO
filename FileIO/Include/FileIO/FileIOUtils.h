#pragma once

#include "FileIO/FileIOAPI.h"

#include <string>
#include <fstream>
#include <filesystem>
#include <cstdint>
#include <cstdio>

namespace Fio
{
    inline std::string generateHash(const std::string& str)
    {
        uint32_t hash = 0x811c9dc5;
        for (char c : str)
        {
            hash ^= static_cast<uint8_t>(c);
            hash *= 0x01000193;
        }
        char buffer[9];
        std::snprintf(buffer, sizeof(buffer), "%08x", hash);
        return std::string(buffer);
    }

    ////////////////////////////////////////////////////////////////
    /// 临时文件副本工具类，用于处理中文路径文件读取兼容性问题
    /// ABI 说明：本类含 std::string 成员且为 header-only 内联实现，
    /// 仅限同编译器/同 CRT 体系内部使用（FileIO 内部 C++ DLL）。
    class FILEIO_API TempFileCopy
    {
    public:
        TempFileCopy(const std::string& originalPath, const std::string& tempPrefix)
            : m_originalPath(originalPath)
        {
            // 使用 std::filesystem::path 处理 UTF-8 中文路径（Windows 下 std::ifstream(const char*) 按 ANSI 码页解析）
            std::filesystem::path fsPath = std::filesystem::u8path(originalPath);
            std::ifstream inFile(fsPath, std::ios::binary);
            if (!inFile)
            {
                m_error = "Cannot open file: " + originalPath;
                return;
            }

            std::string content((std::istreambuf_iterator<char>(inFile)),
                std::istreambuf_iterator<char>());
            inFile.close();

            std::string hash = generateHash(originalPath);
            std::string ext = fsPath.extension().string();
            std::filesystem::path tempPath = std::filesystem::temp_directory_path()
                / ("sanyi_" + tempPrefix + "_" + hash + ext);
            m_tempPath = tempPath.string();

            std::ofstream outFile(tempPath, std::ios::binary | std::ios::trunc);
            if (!outFile)
            {
                m_error = "Cannot create temp file: " + tempPath.string();
                m_tempPath.clear();
                return;
            }
            outFile.write(content.data(), content.size());
            outFile.close();
        }

        ~TempFileCopy()
        {
            if (!m_tempPath.empty())
                std::filesystem::remove(m_tempPath);
        }

        TempFileCopy(const TempFileCopy&) = delete;
        TempFileCopy& operator=(const TempFileCopy&) = delete;

        bool isValid() const
        {
            return !m_tempPath.empty();
        }
        const std::string& error() const
        {
            return m_error;
        }
        const std::string& path() const
        {
            return m_tempPath.empty() ? m_originalPath : m_tempPath;
        }

    private:
        std::string m_originalPath;
        std::string m_tempPath;
        std::string m_error;
    };
} // namespace Fio
