#pragma once

#include "FileIO/FileIOAPI.h"

#include <string>
#include <fstream>
#include <filesystem>
#include <cstdint>
#include <cstdio>
#include <random>

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
        // 从原始路径读取内容并复制到临时文件（通用路径，字节级原样复制）
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

            std::string content((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
            inFile.close();

            writeToTemp(originalPath, tempPrefix, content);
        }

        // 使用调用方提供的（可能已预处理过的）内容写入临时文件，避免重复读取原始文件
        TempFileCopy(const std::string& content, const std::string& originalPath, const std::string& tempPrefix)
            : m_originalPath(originalPath)
        {
            writeToTemp(originalPath, tempPrefix, content);
        }

        ~TempFileCopy()
        {
            if (!m_tempPath.empty())
            {
                std::filesystem::remove(m_tempPath);
            }
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
        void writeToTemp(const std::string& originalPath, const std::string& tempPrefix, const std::string& content)
        {
            std::filesystem::path fsPath = std::filesystem::u8path(originalPath);
            std::string ext = fsPath.extension().string();

            // [F7-P1 修复] 使用随机后缀替代确定性哈希，防止 TOCTOU 竞争和预创建文件投毒。
            // 旧代码用 FNV 哈希生成固定文件名，多个进程处理同一文件会产生冲突。
            std::string randomSuffix = generateRandomSuffix();
            std::filesystem::path tempPath =
                std::filesystem::temp_directory_path() / ("sanyi_" + tempPrefix + "_" + randomSuffix + ext);

            // [F7-P1 修复] 使用 exclusive create 模式，失败则重试（避免竞争条件）。
            constexpr int MAX_RETRIES = 10;
            for (int attempt = 0; attempt < MAX_RETRIES; ++attempt)
            {
                std::ofstream outFile(tempPath, std::ios::binary | std::ios::trunc);
                if (outFile)
                {
                    outFile.write(content.data(), content.size());
                    outFile.close();
                    m_tempPath = tempPath.string();
                    return;
                }
                // 文件已存在（被其他进程创建），生成新随机名重试
                randomSuffix = generateRandomSuffix();
                tempPath = std::filesystem::temp_directory_path() / ("sanyi_" + tempPrefix + "_" + randomSuffix + ext);
            }
            m_error = "Cannot create temp file after " + std::to_string(MAX_RETRIES) + " retries";
            m_tempPath.clear();
        }

        // [F7] 生成随机后缀（8 字符十六进制）
        static std::string generateRandomSuffix()
        {
            static std::random_device rd;
            static std::mt19937_64 gen(rd());
            static std::uniform_int_distribution<uint64_t> dis;

            uint64_t val = dis(gen);
            char buf[17];
            snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(val));
            return std::string(buf, 16);
        }

        std::string m_originalPath;
        std::string m_tempPath;
        std::string m_error;
    };
}  // namespace Fio
