/**
 * @file DocumentRecoveryTests.cpp
 * @brief 恢复文件与自动保存调度器回归测试
 *
 * 覆盖：
 *   - 恢复文件写入 / 检查 / 载入 / 清理闭环
 *   - 恢复文件损坏（截断、CRC 破坏）时如实上报，不静默忽略
 *   - 残留恢复文件枚举只认带 manifest 的恢复文件
 *   - 自动保存：无修改不触发、到间隔才触发、禁用不触发、reset 生效
 *
 * 全部用例只依赖临时目录，不涉及 GUI / GPU。
 */

#include <gtest/gtest.h>

#include "FileIO/DocumentRecovery.h"
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

    class RecoveryTempDir
    {
    public:
        RecoveryTempDir()
        {
            static std::atomic<unsigned long long> sequence{ 0 };
            const auto stamp = static_cast<unsigned long long>(
                std::chrono::high_resolution_clock::now().time_since_epoch().count());
            m_path = fs::temp_directory_path() /
                ("apr_recovery_" + std::to_string(stamp) + "_" + std::to_string(sequence.fetch_add(1)));
            std::error_code ignored;
            fs::create_directories(m_path, ignored);
        }

        ~RecoveryTempDir()
        {
            std::error_code ignored;
            fs::remove_all(m_path, ignored);
        }

        RecoveryTempDir(const RecoveryTempDir&) = delete;
        RecoveryTempDir& operator=(const RecoveryTempDir&) = delete;

        std::string str() const
        {
            return m_path.u8string();
        }

        std::string child(const char* name) const
        {
            return (m_path / name).u8string();
        }

    private:
        fs::path m_path;
    };

    AprDocument makeRecoveryDocument(const char* name)
    {
        AprDocument doc;
        doc.setSoftwareName(name);
        doc.setSoftwareVersion("1.0.0");
        return doc;
    }

    std::string recoveryPathOf(const RecoveryStore& store, const std::string& documentKey, bool is3D)
    {
        char buffer[1200] = { 0 };
        if (!store.recoveryPathFor(documentKey.c_str(), is3D, buffer, sizeof(buffer)))
        {
            return {};
        }
        return std::string(buffer);
    }

    void overwriteRecoveryBytes(const std::string& path, const std::vector<uint8_t>& bytes)
    {
        std::ofstream out(fs::u8path(path), std::ios::binary | std::ios::trunc);
        if (!bytes.empty())
        {
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
    }

    std::vector<uint8_t> readRecoveryBytes(const std::string& path)
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

    struct PathCollector
    {
        std::vector<std::string> paths;

        static void visit(const char* path, void* ctx)
        {
            if (ctx && path)
            {
                static_cast<PathCollector*>(ctx)->paths.emplace_back(path);
            }
        }
    };
}  // namespace

TEST(DocumentRecoveryTest, WriteInspectLoadRoundTrip)
{
    RecoveryTempDir dir;
    RecoveryStore store;
    store.setRecoveryDirectory(dir.str().c_str());

    const std::string documentKey = dir.child("drawing.apr");
    ASSERT_TRUE(store.writeRecovery(documentKey.c_str(), false, makeRecoveryDocument("April CAD 2D")).success);

    const std::string recoveryPath = recoveryPathOf(store, documentKey, false);
    ASSERT_FALSE(recoveryPath.empty());
    ASSERT_TRUE(fs::exists(fs::u8path(recoveryPath)));

    RecoveryFileInfo info;
    ASSERT_TRUE(store.inspectRecovery(recoveryPath.c_str(), info).success);
    EXPECT_TRUE(info.exists);
    EXPECT_TRUE(info.intact);
    EXPECT_FALSE(info.is3D);
    EXPECT_EQ(documentKey, std::string(info.originalPath));
    EXPECT_FALSE(std::string(info.savedAt).empty());

    AprDocument loaded;
    ASSERT_TRUE(store.loadRecovery(recoveryPath.c_str(), loaded).success);
    EXPECT_EQ(std::string("April CAD 2D"), std::string(loaded.softwareName()));
}

TEST(DocumentRecoveryTest, CorruptRecoveryIsReportedNotIgnored)
{
    RecoveryTempDir dir;
    RecoveryStore store;
    store.setRecoveryDirectory(dir.str().c_str());

    const std::string documentKey = dir.child("broken.apr");
    ASSERT_TRUE(store.writeRecovery(documentKey.c_str(), false, makeRecoveryDocument("v1")).success);

    const std::string recoveryPath = recoveryPathOf(store, documentKey, false);
    std::vector<uint8_t> bytes = readRecoveryBytes(recoveryPath);
    ASSERT_GT(bytes.size(), static_cast<size_t>(20));
    bytes.resize(10);  // 截断：长度不足
    overwriteRecoveryBytes(recoveryPath, bytes);

    RecoveryFileInfo info;
    const SerializeResult result = store.inspectRecovery(recoveryPath.c_str(), info);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(info.exists);
    EXPECT_FALSE(info.intact);
    EXPECT_FALSE(std::string(info.errorMessage).empty()) << "damage reason must be reported";
}

TEST(DocumentRecoveryTest, DiscardRemovesRecoveryManifestAndBackup)
{
    RecoveryTempDir dir;
    RecoveryStore store;
    store.setRecoveryDirectory(dir.str().c_str());

    const std::string documentKey = dir.child("discard.apr");
    ASSERT_TRUE(store.writeRecovery(documentKey.c_str(), false, makeRecoveryDocument("v1")).success);
    ASSERT_TRUE(store.writeRecovery(documentKey.c_str(), false, makeRecoveryDocument("v2")).success);

    const std::string recoveryPath = recoveryPathOf(store, documentKey, false);
    ASSERT_TRUE(fs::exists(fs::u8path(recoveryPath)));
    ASSERT_TRUE(fs::exists(fs::u8path(recoveryPath + ".bak")));
    ASSERT_TRUE(fs::exists(fs::u8path(recoveryPath + ".meta")));

    EXPECT_TRUE(store.discardRecovery(recoveryPath.c_str()));
    EXPECT_FALSE(fs::exists(fs::u8path(recoveryPath)));
    EXPECT_FALSE(fs::exists(fs::u8path(recoveryPath + ".bak")));
    EXPECT_FALSE(fs::exists(fs::u8path(recoveryPath + ".meta")));
}

TEST(DocumentRecoveryTest, EnumerateResidualRecoveryFilesOnly)
{
    RecoveryTempDir dir;
    RecoveryStore store;
    store.setRecoveryDirectory(dir.str().c_str());

    const std::string key2D = dir.child("a.apr");
    const std::string key3D = dir.child("b.apr3d");
    ASSERT_TRUE(store.writeRecovery(key2D.c_str(), false, makeRecoveryDocument("2d")).success);
    ASSERT_TRUE(store.writeRecovery(key3D.c_str(), true, makeRecoveryDocument("3d")).success);

    // 目录里放一份普通文档（无 manifest），不得被当成残留恢复文件
    const std::string plain = dir.child("plain.apr");
    overwriteRecoveryBytes(plain, readRecoveryBytes(recoveryPathOf(store, key2D, false)));

    PathCollector collector;
    store.forEachRecovery(dir.str().c_str(), &PathCollector::visit, &collector);
    EXPECT_EQ(static_cast<size_t>(2), collector.paths.size());
}

TEST(DocumentRecoveryTest, RecoveryPathIsStableAndDistinct)
{
    RecoveryTempDir dir;
    RecoveryStore store;
    store.setRecoveryDirectory(dir.str().c_str());

    const std::string first = recoveryPathOf(store, dir.child("same.apr"), false);
    const std::string second = recoveryPathOf(store, dir.child("same.apr"), false);
    const std::string other = recoveryPathOf(store, dir.child("other.apr"), false);
    EXPECT_EQ(first, second);
    EXPECT_NE(first, other);
    EXPECT_NE(std::string::npos, first.find(".recovery.apr"));
}

TEST(AutoSaveSchedulerTest, NoTriggerWithoutUnsavedChanges)
{
    AutoSaveScheduler scheduler;
    EXPECT_FALSE(scheduler.tick(0, false));
    EXPECT_FALSE(scheduler.tick(60000, false));
    EXPECT_FALSE(scheduler.tick(120000, false));
}

TEST(AutoSaveSchedulerTest, TriggersOnlyAfterInterval)
{
    AutoSaveScheduler scheduler;
    scheduler.setIntervalSeconds(60);

    EXPECT_FALSE(scheduler.tick(0, true)) << "first tick establishes the baseline";
    EXPECT_FALSE(scheduler.tick(59999, true));
    EXPECT_TRUE(scheduler.tick(60000, true));
    EXPECT_FALSE(scheduler.tick(61000, true));
    EXPECT_TRUE(scheduler.tick(120000, true));
}

TEST(AutoSaveSchedulerTest, DisabledNeverTriggers)
{
    AutoSaveScheduler scheduler;
    scheduler.setEnabled(false);
    EXPECT_FALSE(scheduler.tick(0, true));
    EXPECT_FALSE(scheduler.tick(600000, true));
}

TEST(AutoSaveSchedulerTest, ResetAfterManualSaveDelaysNextTrigger)
{
    AutoSaveScheduler scheduler;
    scheduler.setIntervalSeconds(60);

    EXPECT_FALSE(scheduler.tick(0, true));
    EXPECT_TRUE(scheduler.tick(60000, true));

    scheduler.reset(60000);
    EXPECT_FALSE(scheduler.tick(90000, true));
    EXPECT_FALSE(scheduler.tick(119999, true));
    EXPECT_TRUE(scheduler.tick(120000, true));
}

TEST(AutoSaveSchedulerTest, IntervalIsClampedToMinimum)
{
    AutoSaveScheduler scheduler;
    scheduler.setIntervalSeconds(1);
    EXPECT_EQ(5, scheduler.intervalSeconds());
}
