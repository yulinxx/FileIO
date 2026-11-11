/**
 * @file DurableSaveTests.cpp
 * @brief 持久化耐久性与 .bak 回退回归测试
 *
 * 覆盖：
 *   - 首次保存不产生 .bak
 *   - 覆盖保存把上一版轮转为 .bak，且 .bak 内容等于上一版
 *   - 主文件损坏（截断）时自动回退到 .bak，并经警告回调明确告知
 *   - 主文件与 .bak 都损坏时如实失败
 *   - 保存后目标文件内容完整可再次读取
 *
 * 全部用例只依赖临时目录与 AprSerializer，不涉及 GUI / GPU。
 */

#include <gtest/gtest.h>

#include "FileIO/AprSerializer.h"
#include "FileIO/AprDocument.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Fio;

namespace
{
    namespace fs = std::filesystem;

    /// 每个用例独立的临时目录，析构自动清理
    class ScopedTempDir
    {
    public:
        ScopedTempDir()
        {
            static std::atomic<unsigned long long> sequence{ 0 };
            const auto stamp = static_cast<unsigned long long>(
                std::chrono::high_resolution_clock::now().time_since_epoch().count());
            m_path = fs::temp_directory_path() /
                ("apr_durable_" + std::to_string(stamp) + "_" + std::to_string(sequence.fetch_add(1)));
            std::error_code ignored;
            fs::create_directories(m_path, ignored);
        }

        ~ScopedTempDir()
        {
            std::error_code ignored;
            fs::remove_all(m_path, ignored);
        }

        ScopedTempDir(const ScopedTempDir&) = delete;
        ScopedTempDir& operator=(const ScopedTempDir&) = delete;

        std::string filePath(const char* name) const
        {
            return (m_path / name).u8string();
        }

    private:
        fs::path m_path;
    };

    AprDocument makeDocument(const char* description)
    {
        AprDocument doc;
        doc.setSoftwareName("April CAD Test");
        doc.setSoftwareVersion("1.0.0");
        doc.setDescription(description);
        return doc;
    }

    std::vector<uint8_t> readAllBytes(const std::string& path)
    {
        std::ifstream in(fs::u8path(path), std::ios::binary | std::ios::ate);
        if (!in)
        {
            return {};
        }
        const auto size = static_cast<size_t>(in.tellg());
        in.seekg(0);
        std::vector<uint8_t> bytes(size);
        if (size > 0)
        {
            in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
        }
        return bytes;
    }

    void writeRawBytes(const std::string& path, const std::vector<uint8_t>& bytes)
    {
        std::ofstream out(fs::u8path(path), std::ios::binary | std::ios::trunc);
        if (!bytes.empty())
        {
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
    }

    /// 警告收集上下文（C 回调，不能用 std::function）
    struct WarningSink
    {
        std::vector<std::string> messages;

        static void collect(const char* msg, void* ctx)
        {
            if (ctx && msg)
            {
                static_cast<WarningSink*>(ctx)->messages.emplace_back(msg);
            }
        }
    };
}  // namespace

TEST(DurableSaveTest, FirstSaveCreatesNoBackup)
{
    ScopedTempDir dir;
    const std::string path = dir.filePath("first.apr");

    AprSerializer serializer;
    const AprDocument doc = makeDocument("first save");
    ASSERT_TRUE(serializer.saveToFile(path.c_str(), doc).success);

    EXPECT_TRUE(fs::exists(fs::u8path(path)));
    EXPECT_FALSE(fs::exists(fs::u8path(path + ".bak")));
}

TEST(DurableSaveTest, OverwriteRotatesPreviousVersionToBackup)
{
    ScopedTempDir dir;
    const std::string path = dir.filePath("rotate.apr");

    AprSerializer serializer;
    const AprDocument first = makeDocument("version one");
    ASSERT_TRUE(serializer.saveToFile(path.c_str(), first).success);
    const std::vector<uint8_t> firstBytes = readAllBytes(path);
    ASSERT_FALSE(firstBytes.empty());

    // 覆盖保存：上一版必须完整出现在 .bak 中
    const AprDocument second = makeDocument("version two");
    ASSERT_TRUE(serializer.saveToFile(path.c_str(), second).success);

    const std::vector<uint8_t> backupBytes = readAllBytes(path + ".bak");
    ASSERT_FALSE(backupBytes.empty()) << ".bak must be created when overwriting an existing file";
    EXPECT_EQ(backupBytes, firstBytes) << ".bak must hold the previous version byte-for-byte";

    // 主文件仍是可读的新版本
    AprDocument reloaded;
    ASSERT_TRUE(serializer.loadFromFile(path.c_str(), reloaded).success);
}

TEST(DurableSaveTest, CorruptPrimaryFallsBackToBackupAndWarns)
{
    ScopedTempDir dir;
    const std::string path = dir.filePath("fallback.apr");

    AprSerializer serializer;
    const AprDocument first = makeDocument("good version");
    ASSERT_TRUE(serializer.saveToFile(path.c_str(), first).success);

    const AprDocument second = makeDocument("newer version");
    ASSERT_TRUE(serializer.saveToFile(path.c_str(), second).success);
    ASSERT_TRUE(fs::exists(fs::u8path(path + ".bak")));

    // 截断主文件，制造可检测的损坏（长度不足 -> 头部校验失败）
    std::vector<uint8_t> truncated = readAllBytes(path);
    ASSERT_GT(truncated.size(), static_cast<size_t>(8));
    truncated.resize(8);
    writeRawBytes(path, truncated);

    WarningSink warnings;
    AprDocument loaded;
    const SerializeResult result =
        serializer.loadFromFile(path.c_str(), loaded, &WarningSink::collect, &warnings);

    ASSERT_TRUE(result.success) << "load must fall back to .bak when the primary file is damaged";
    EXPECT_FALSE(warnings.messages.empty()) << "backup fallback must be reported, not silent";
    EXPECT_NE(std::string::npos, warnings.messages.front().find("backup"));
}

TEST(DurableSaveTest, CorruptPrimaryAndBackupFailsHonestly)
{
    ScopedTempDir dir;
    const std::string path = dir.filePath("both_broken.apr");

    AprSerializer serializer;
    ASSERT_TRUE(serializer.saveToFile(path.c_str(), makeDocument("v1")).success);
    ASSERT_TRUE(serializer.saveToFile(path.c_str(), makeDocument("v2")).success);

    const std::vector<uint8_t> broken{ 'B', 'A', 'D', '!' };
    writeRawBytes(path, broken);
    writeRawBytes(path + ".bak", broken);

    AprDocument loaded;
    const SerializeResult result = serializer.loadFromFile(path.c_str(), loaded);
    EXPECT_FALSE(result.success);
    // 必须给出可定位的失败原因，不能空消息
    EXPECT_GT(std::string(result.errorMessage).size(), static_cast<size_t>(0));
}

TEST(DurableSaveTest, MissingPrimaryWithoutBackupFails)
{
    ScopedTempDir dir;
    const std::string path = dir.filePath("missing.apr");

    AprSerializer serializer;
    AprDocument loaded;
    const SerializeResult result = serializer.loadFromFile(path.c_str(), loaded);
    EXPECT_FALSE(result.success);
}
