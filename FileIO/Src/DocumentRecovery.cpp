/**
 * @file DocumentRecovery.cpp
 * @brief 文档自动保存与崩溃恢复实现
 */
#include "FileIO/DocumentRecovery.h"
#include "DurableIO.h"

#include "Log/AprLogger.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace Fio
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr const char* kManifestSuffix = ".meta";

        int clampIntervalSeconds(int seconds)
        {
            if (seconds < 5)
            {
                return 5;
            }
            // 上限 24 小时，防止误配置导致自动保存实际上永不触发
            if (seconds > 24 * 60 * 60)
            {
                return 24 * 60 * 60;
            }
            return seconds;
        }

        std::string hexHash(const std::string& input)
        {
            // FNV-1a 64：稳定、无依赖，跨平台一致，用于区分同名文档
            uint64_t hash = 1469598103934665603ULL;
            for (unsigned char c : input)
            {
                hash ^= c;
                hash *= 1099511628211ULL;
            }
            char buffer[17] = { 0 };
            std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
            return std::string(buffer);
        }

        std::string sanitizeStem(const std::string& stem)
        {
            std::string out;
            out.reserve(stem.size());
            for (unsigned char c : stem)
            {
                if (std::isalnum(c) != 0 || c == '-' || c == '_')
                {
                    out.push_back(static_cast<char>(c));
                }
            }
            if (out.empty())
            {
                out = "document";
            }
            // 文件名长度收敛，避免超长路径
            if (out.size() > 48)
            {
                out.resize(48);
            }
            return out;
        }

        std::string joinKeyValues(const std::string& originalPath, long long epochSeconds, bool is3D)
        {
            std::string original = originalPath;
            std::replace(original.begin(), original.end(), '\n', ' ');
            std::replace(original.begin(), original.end(), '\r', ' ');

            std::ostringstream out;
            out << "original=" << original << "\n";
            out << "saved=" << epochSeconds << "\n";
            out << "format=" << (is3D ? "3d" : "2d") << "\n";
            return out.str();
        }

        struct ManifestData
        {
            std::string originalPath;
            std::string savedAt;
            bool is3D{ false };
            bool formatKnown{ false };
        };

        ManifestData readManifest(const fs::path& manifestPath)
        {
            ManifestData data;
            std::ifstream in(manifestPath, std::ios::binary);
            if (!in)
            {
                return data;
            }
            std::string line;
            while (std::getline(in, line))
            {
                const auto pos = line.find('=');
                if (pos == std::string::npos)
                {
                    continue;
                }
                const std::string key = line.substr(0, pos);
                const std::string value = line.substr(pos + 1);
                if (key == "original")
                {
                    data.originalPath = value;
                }
                else if (key == "saved")
                {
                    data.savedAt = value;
                }
                else if (key == "format")
                {
                    data.is3D = (value == "3d");
                    data.formatKnown = true;
                }
            }
            return data;
        }

        // 独立于 AprSerializer 的完整性校验：恢复文件必须按自身内容判定，
        // 不能借用 loadFromFile 的 .bak 回退把「主恢复文件损坏」掩盖成可用。
        uint32_t crc32Of(const uint8_t* data, size_t len)
        {
            static uint32_t table[256];
            static std::once_flag flag;
            std::call_once(flag, [] {
                const uint32_t polynomial = 0xEDB88320;
                for (uint32_t i = 0; i < 256; ++i)
                {
                    uint32_t crc = i;
                    for (int j = 0; j < 8; ++j)
                    {
                        crc = (crc & 1) ? ((crc >> 1) ^ polynomial) : (crc >> 1);
                    }
                    table[i] = crc;
                }
            });

            uint32_t crc = 0xFFFFFFFF;
            for (size_t i = 0; i < len; ++i)
            {
                crc = (crc >> 8) ^ table[(crc ^ data[i]) & 0xFF];
            }
            return crc ^ 0xFFFFFFFF;
        }

        /// 校验单个恢复文件的完整性，返回失败原因（空表示通过）
        std::string validateRecoveryBytes(const std::vector<uint8_t>& bytes, bool& is3D)
        {
            constexpr size_t headerSize = AprFileConst::HEADER_SIZE;
            constexpr size_t footerSize = AprFileConst::FOOTER_SIZE;
            if (bytes.size() < headerSize + footerSize)
            {
                return "Recovery file is too small to be valid";
            }

            const bool is2D = std::memcmp(bytes.data(), AprFileConst::MAGIC_APR, 4) == 0;
            const bool is3DMagic = std::memcmp(bytes.data(), AprFileConst::MAGIC_APR3D, 4) == 0;
            if (!is2D && !is3DMagic)
            {
                return "Recovery file has an invalid magic header";
            }
            is3D = is3DMagic;

            const uint32_t dataLen = static_cast<uint32_t>(bytes[12]) | (static_cast<uint32_t>(bytes[13]) << 8) |
                (static_cast<uint32_t>(bytes[14]) << 16) | (static_cast<uint32_t>(bytes[15]) << 24);
            const uint64_t totalNeeded = static_cast<uint64_t>(dataLen) + headerSize + footerSize;
            if (totalNeeded > static_cast<uint64_t>(bytes.size()))
            {
                return "Recovery file is truncated (payload length exceeds file size)";
            }

            const uint32_t expected = crc32Of(bytes.data() + headerSize, dataLen);
            const size_t crcOffset = headerSize + dataLen;
            const uint32_t stored = static_cast<uint32_t>(bytes[crcOffset + 0]) |
                (static_cast<uint32_t>(bytes[crcOffset + 1]) << 8) |
                (static_cast<uint32_t>(bytes[crcOffset + 2]) << 16) |
                (static_cast<uint32_t>(bytes[crcOffset + 3]) << 24);
            if (expected != stored)
            {
                return "Recovery file failed CRC32 verification";
            }
            return {};
        }
    }  // namespace

    struct RecoveryStore::Impl
    {
        std::string recoveryDirectory;
    };

    RecoveryStore::RecoveryStore()
        : m_impl(new Impl())
    {
    }

    RecoveryStore::~RecoveryStore()
    {
        delete m_impl;
        m_impl = nullptr;
    }

    void RecoveryStore::setRecoveryDirectory(const char* directoryUtf8)
    {
        m_impl->recoveryDirectory = (directoryUtf8 != nullptr) ? directoryUtf8 : "";
    }

    size_t RecoveryStore::recoveryDirectory(char* buffer, size_t bufferSize) const
    {
        const std::string& dir = m_impl->recoveryDirectory;
        if (buffer != nullptr && bufferSize > dir.size())
        {
            std::snprintf(buffer, bufferSize, "%s", dir.c_str());
        }
        return dir.size();
    }

    bool RecoveryStore::recoveryPathFor(
        const char* documentKey, bool is3D, char* buffer, size_t bufferSize) const
    {
        if (buffer == nullptr || bufferSize == 0)
        {
            return false;
        }

        const std::string key = (documentKey != nullptr && *documentKey != '\0') ? documentKey : "untitled";
        fs::path keyPath = fs::u8path(key);

        std::string stem = keyPath.stem().u8string();
        if (stem.empty())
        {
            stem = keyPath.filename().u8string();
        }
        if (stem.empty())
        {
            stem = "untitled";
        }

        const std::string fileName = sanitizeStem(stem) + "_" + hexHash(key) + (is3D ? ".recovery.apr3d" : ".recovery.apr");
        fs::path target;
        if (!m_impl->recoveryDirectory.empty())
        {
            target = fs::u8path(m_impl->recoveryDirectory) / fs::u8path(fileName);
        }
        else
        {
            fs::path parent = keyPath.parent_path();
            if (parent.empty())
            {
                parent = fs::path(".");
            }
            target = parent / fs::u8path(fileName);
        }

        const std::string utf8 = target.u8string();
        if (utf8.size() + 1 > bufferSize)
        {
            return false;
        }
        std::memcpy(buffer, utf8.c_str(), utf8.size() + 1);
        return true;
    }

    SerializeResult RecoveryStore::writeRecovery(const char* documentKey, bool is3D, const AprDocument& doc)
    {
        char pathBuffer[1200] = { 0 };
        if (!prepareRecoveryPath(documentKey, is3D, pathBuffer, sizeof(pathBuffer)))
        {
            return SerializeResult::fail("Failed to resolve recovery file path");
        }

        AprSerializer serializer;
        const FileFormat format = is3D ? FileFormat::Native3D : FileFormat::Native;
        const SerializeResult saveResult = serializer.saveToFile(pathBuffer, doc, false, format);
        if (!saveResult.success)
        {
            APR_ERRORF("[RecoveryStore] Failed to write recovery file: %s (%s)", pathBuffer, saveResult.errorMessage);
            return saveResult;
        }

        return writeRecoveryManifest(documentKey, is3D);
    }

    bool RecoveryStore::prepareRecoveryPath(
        const char* documentKey, bool is3D, char* buffer, size_t bufferSize)
    {
        char pathBuffer[1200] = { 0 };
        if (!recoveryPathFor(documentKey, is3D, pathBuffer, sizeof(pathBuffer)))
        {
            return false;
        }

        const fs::path recoveryPath = fs::u8path(pathBuffer);
        std::error_code dirError;
        if (!recoveryPath.parent_path().empty())
        {
            fs::create_directories(recoveryPath.parent_path(), dirError);
            if (dirError)
            {
                APR_ERRORF("[RecoveryStore] Cannot create recovery directory: %s (%s)",
                    recoveryPath.parent_path().u8string().c_str(),
                    dirError.message().c_str());
                return false;
            }
        }

        const std::string utf8 = recoveryPath.u8string();
        if (utf8.size() + 1 > bufferSize)
        {
            return false;
        }
        std::memcpy(buffer, utf8.c_str(), utf8.size() + 1);
        return true;
    }

    SerializeResult RecoveryStore::writeRecoveryManifest(const char* documentKey, bool is3D)
    {
        char pathBuffer[1200] = { 0 };
        if (!prepareRecoveryPath(documentKey, is3D, pathBuffer, sizeof(pathBuffer)))
        {
            return SerializeResult::fail("Failed to resolve recovery file path");
        }

        const long long savedAt = static_cast<long long>(std::time(nullptr));
        const std::string manifest = joinKeyValues((documentKey != nullptr) ? documentKey : "", savedAt, is3D);
        const fs::path manifestPath = fs::path(fs::u8path(pathBuffer)).concat(kManifestSuffix);
        const Internal::DurableResult manifestResult =
            Internal::writeFileDurably(manifestPath, manifest.data(), manifest.size());
        if (!manifestResult.success)
        {
            APR_ERRORF("[RecoveryStore] Failed to write recovery manifest: %s (%s)",
                manifestPath.u8string().c_str(),
                manifestResult.error.c_str());
            return SerializeResult::fail(std::string("Failed to write recovery manifest: ")
                    .append(manifestResult.error)
                    .c_str());
        }

        APR_DEBUGF("[RecoveryStore] Recovery file written: %s", pathBuffer);
        return SerializeResult::ok();
    }

    SerializeResult RecoveryStore::inspectRecovery(const char* recoveryPath, RecoveryFileInfo& info) const
    {
        info = RecoveryFileInfo();

        if (recoveryPath == nullptr || *recoveryPath == '\0')
        {
            return SerializeResult::fail("Empty recovery file path");
        }
        std::snprintf(info.recoveryPath, sizeof(info.recoveryPath), "%s", recoveryPath);

        const fs::path path = fs::u8path(recoveryPath);
        std::error_code existsError;
        info.exists = fs::exists(path, existsError);
        if (existsError || !info.exists)
        {
            std::snprintf(info.errorMessage, sizeof(info.errorMessage), "Recovery file not found");
            return SerializeResult::fail("Recovery file not found");
        }

        const ManifestData manifest = readManifest(fs::path(path).concat(kManifestSuffix));
        if (!manifest.originalPath.empty())
        {
            std::snprintf(info.originalPath, sizeof(info.originalPath), "%s", manifest.originalPath.c_str());
        }
        if (!manifest.savedAt.empty())
        {
            std::snprintf(info.savedAt, sizeof(info.savedAt), "%s", manifest.savedAt.c_str());
        }

        std::vector<uint8_t> bytes;
        std::string readError;
        if (!Internal::readWholeFile(path, bytes, readError))
        {
            info.intact = false;
            std::snprintf(info.errorMessage, sizeof(info.errorMessage), "%s", readError.c_str());
            return SerializeResult::fail(readError.c_str());
        }

        bool detected3D = false;
        const std::string validationError = validateRecoveryBytes(bytes, detected3D);
        info.is3D = manifest.formatKnown ? manifest.is3D : detected3D;
        if (!validationError.empty())
        {
            info.intact = false;
            std::snprintf(info.errorMessage, sizeof(info.errorMessage), "%s", validationError.c_str());
            APR_WARNF("[RecoveryStore] Recovery file is damaged: %s (%s)", recoveryPath, validationError.c_str());
            return SerializeResult::fail(validationError.c_str());
        }

        info.intact = true;
        return SerializeResult::ok();
    }

    SerializeResult RecoveryStore::loadRecovery(const char* recoveryPath, AprDocument& doc) const
    {
        if (recoveryPath == nullptr || *recoveryPath == '\0')
        {
            return SerializeResult::fail("Empty recovery file path");
        }
        AprSerializer serializer;
        return serializer.loadFromFile(recoveryPath, doc);
    }

    bool RecoveryStore::discardRecovery(const char* recoveryPath) const
    {
        if (recoveryPath == nullptr || *recoveryPath == '\0')
        {
            return true;
        }

        const fs::path base = fs::u8path(recoveryPath);
        std::error_code ignored;
        fs::remove(base, ignored);
        fs::remove(fs::path(base).concat(".bak"), ignored);
        fs::remove(fs::path(base).concat(kManifestSuffix), ignored);

        std::error_code existsError;
        if (fs::exists(base, existsError) || fs::exists(fs::path(base).concat(".bak"), existsError) ||
            fs::exists(fs::path(base).concat(kManifestSuffix), existsError))
        {
            APR_WARNF("[RecoveryStore] Failed to remove all recovery artifacts for: %s", recoveryPath);
            return false;
        }
        return true;
    }

    void RecoveryStore::forEachRecovery(
        const char* directoryUtf8, void (*visitor)(const char* path, void* ctx), void* ctx) const
    {
        if (visitor == nullptr)
        {
            return;
        }

        const std::string directory = (directoryUtf8 != nullptr && *directoryUtf8 != '\0')
            ? directoryUtf8
            : m_impl->recoveryDirectory;
        if (directory.empty())
        {
            return;
        }

        std::error_code iterateError;
        fs::directory_iterator it(fs::u8path(directory), iterateError);
        if (iterateError)
        {
            APR_WARNF("[RecoveryStore] Cannot enumerate recovery directory: %s (%s)",
                directory.c_str(),
                iterateError.message().c_str());
            return;
        }

        for (const auto& entry : it)
        {
            std::error_code typeError;
            if (!entry.is_regular_file(typeError) || typeError)
            {
                continue;
            }
            const fs::path path = entry.path();
            const std::string name = path.filename().u8string();
            const bool isApr = name.size() > 4 && name.compare(name.size() - 4, 4, ".apr") == 0;
            const bool isApr3D = name.size() > 6 && name.compare(name.size() - 6, 6, ".apr3d") == 0;
            if (!isApr && !isApr3D)
            {
                continue;
            }

            // 只认带 manifest 的恢复文件，避免把用户正式文档当成残留恢复文件
            std::error_code manifestError;
            if (!fs::exists(fs::path(path).concat(kManifestSuffix), manifestError) || manifestError)
            {
                continue;
            }

            const std::string utf8 = path.u8string();
            visitor(utf8.c_str(), ctx);
        }
    }

    bool RecoveryStore::defaultRecoveryDirectory(char* buffer, size_t bufferSize)
    {
        if (buffer == nullptr || bufferSize == 0)
        {
            return false;
        }

        std::string directory;
#if defined(_WIN32)
        const char* base = std::getenv("LOCALAPPDATA");
        if (base == nullptr)
        {
            base = std::getenv("APPDATA");
        }
        if (base != nullptr)
        {
            directory = std::string(base) + "\\April\\Recovery";
        }
#elif defined(__APPLE__)
        const char* home = std::getenv("HOME");
        if (home != nullptr)
        {
            directory = std::string(home) + "/Library/Application Support/April/Recovery";
        }
#else
        const char* xdg = std::getenv("XDG_DATA_HOME");
        if (xdg != nullptr && *xdg != '\0')
        {
            directory = std::string(xdg) + "/April/Recovery";
        }
        else
        {
            const char* home = std::getenv("HOME");
            if (home != nullptr)
            {
                directory = std::string(home) + "/.local/share/April/Recovery";
            }
        }
#endif
        if (directory.empty() || directory.size() + 1 > bufferSize)
        {
            return false;
        }
        std::memcpy(buffer, directory.c_str(), directory.size() + 1);
        return true;
    }

    // ----
    // ---- AutoSaveScheduler ----
    // ----

    struct AutoSaveScheduler::Impl
    {
        int intervalMilliseconds{ 60 * 1000 };
        bool enabled{ true };
        bool started{ false };
        long long lastTriggerMilliseconds{ 0 };
    };

    AutoSaveScheduler::AutoSaveScheduler()
        : m_impl(new Impl())
    {
    }

    AutoSaveScheduler::~AutoSaveScheduler()
    {
        delete m_impl;
        m_impl = nullptr;
    }

    void AutoSaveScheduler::setIntervalSeconds(int seconds)
    {
        m_impl->intervalMilliseconds = clampIntervalSeconds(seconds) * 1000;
    }

    int AutoSaveScheduler::intervalSeconds() const
    {
        return m_impl->intervalMilliseconds / 1000;
    }

    void AutoSaveScheduler::setEnabled(bool enabled)
    {
        m_impl->enabled = enabled;
    }

    bool AutoSaveScheduler::enabled() const
    {
        return m_impl->enabled;
    }

    bool AutoSaveScheduler::tick(long long nowMilliseconds, bool hasUnsavedChanges)
    {
        if (!m_impl->enabled)
        {
            return false;
        }

        if (!m_impl->started)
        {
            m_impl->started = true;
            m_impl->lastTriggerMilliseconds = nowMilliseconds;
            return false;
        }

        if (nowMilliseconds < m_impl->lastTriggerMilliseconds)
        {
            // 时钟回退：只重设基准，不做触发
            m_impl->lastTriggerMilliseconds = nowMilliseconds;
            return false;
        }

        if (!hasUnsavedChanges)
        {
            // 没有未保存修改时推进基准，保证「变脏后至少经过一个完整间隔」才落盘
            m_impl->lastTriggerMilliseconds = nowMilliseconds;
            return false;
        }

        if (nowMilliseconds - m_impl->lastTriggerMilliseconds < m_impl->intervalMilliseconds)
        {
            return false;
        }

        m_impl->lastTriggerMilliseconds = nowMilliseconds;
        return true;
    }

    void AutoSaveScheduler::reset(long long nowMilliseconds)
    {
        m_impl->started = true;
        m_impl->lastTriggerMilliseconds = nowMilliseconds;
    }
}  // namespace Fio
