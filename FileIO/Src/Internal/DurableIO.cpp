/**
 * @file DurableIO.cpp
 * @brief 耐久写盘辅助实现
 */
#include "DurableIO.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <fstream>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
#endif

namespace Fio
{
    namespace Internal
    {
        namespace
        {
            std::string errnoMessage(const char* what, int code)
            {
                std::string message(what);
                message.append(": ");
                message.append(std::strerror(code));
                return message;
            }
        }  // namespace

#if defined(_WIN32)

        DurableResult syncFile(const std::filesystem::path& path)
        {
            // FlushFileBuffers 要求写权限；文件此刻已由 ofstream 关闭，可以安全重开。
            HANDLE handle = ::CreateFileW(path.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                return DurableResult::fail(errnoMessage("Cannot open file to flush", static_cast<int>(::GetLastError())));
            }

            const BOOL flushed = ::FlushFileBuffers(handle);
            const DWORD flushError = flushed ? ERROR_SUCCESS : ::GetLastError();
            ::CloseHandle(handle);

            if (!flushed)
            {
                return DurableResult::fail(errnoMessage("FlushFileBuffers failed", static_cast<int>(flushError)));
            }
            return DurableResult::ok();
        }

        DurableResult syncDirectory(const std::filesystem::path& /*directory*/)
        {
            // Windows 不支持对目录句柄调用 FlushFileBuffers（需要备份/还原特权），
            // NTFS 的 rename 元数据由文件系统日志保证，这里按成功处理。
            return DurableResult::ok();
        }

#else

        DurableResult syncFile(const std::filesystem::path& path)
        {
            const int fd = ::open(path.c_str(), O_RDONLY);
            if (fd < 0)
            {
                return DurableResult::fail(errnoMessage("Cannot open file to flush", errno));
            }

            int result = 0;
    #if defined(__APPLE__)
            // macOS 上 fsync 只落到驱动缓存，F_FULLFSYNC 才真正刷到介质。
            // 个别文件系统不支持该命令，此时回退到 fsync。
            result = ::fcntl(fd, F_FULLFSYNC);
            if (result != 0 && (errno == EINVAL || errno == ENOTSUP || errno == ENOTTY))
            {
                result = ::fsync(fd);
            }
    #else
            result = ::fsync(fd);
    #endif
            const int syncError = (result == 0) ? 0 : errno;
            ::close(fd);

            if (result != 0)
            {
                return DurableResult::fail(errnoMessage("Failed to flush file to disk", syncError));
            }
            return DurableResult::ok();
        }

        DurableResult syncDirectory(const std::filesystem::path& directory)
        {
            int flags = O_RDONLY;
    #if defined(O_DIRECTORY)
            flags |= O_DIRECTORY;
    #endif
            const int fd = ::open(directory.c_str(), flags);
            if (fd < 0)
            {
                return DurableResult::fail(errnoMessage("Cannot open directory to flush", errno));
            }

            const int result = ::fsync(fd);
            const int syncError = (result == 0) ? 0 : errno;
            ::close(fd);

            if (result != 0)
            {
                return DurableResult::fail(errnoMessage("Failed to flush directory to disk", syncError));
            }
            return DurableResult::ok();
        }

#endif

        DurableResult syncParentDirectory(const std::filesystem::path& filePath)
        {
            std::filesystem::path directory = filePath.parent_path();
            if (directory.empty())
            {
                directory = std::filesystem::path(".");
            }
            return syncDirectory(directory);
        }

        DurableResult writeFileDurably(const std::filesystem::path& path, const void* data, size_t size)
        {
            static std::atomic<unsigned long long> tempSequence{ 0 };
            std::filesystem::path tempPath = path;
            tempPath += ".tmp" + std::to_string(tempSequence.fetch_add(1, std::memory_order_relaxed));

            auto discardTemp = [&tempPath]() {
                std::error_code ignored;
                std::filesystem::remove(tempPath, ignored);
            };

            {
                std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
                if (!out)
                {
                    return DurableResult::fail(std::string("Cannot open temp file for writing: ")
                            .append(tempPath.u8string()));
                }
                if (data != nullptr && size > 0)
                {
                    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
                }
                out.flush();
                if (!out.good())
                {
                    out.close();
                    discardTemp();
                    return DurableResult::fail(std::string("Failed to write temp file: ").append(tempPath.u8string()));
                }
                out.close();
                if (out.fail())
                {
                    discardTemp();
                    return DurableResult::fail(std::string("Failed to close temp file: ").append(tempPath.u8string()));
                }
            }

            const DurableResult contentSync = syncFile(tempPath);
            if (!contentSync.success)
            {
                discardTemp();
                return contentSync;
            }

            std::error_code renameError;
            std::filesystem::rename(tempPath, path, renameError);
            if (renameError)
            {
                discardTemp();
                return DurableResult::fail(std::string("Failed to replace file: ")
                        .append(path.u8string())
                        .append(" (")
                        .append(renameError.message())
                        .append(")"));
            }

            return syncParentDirectory(path);
        }

        bool readWholeFile(const std::filesystem::path& path, std::vector<uint8_t>& out, std::string& error)
        {
            std::ifstream in(path, std::ios::binary | std::ios::ate);
            if (!in)
            {
                error = std::string("Cannot open file: ").append(path.u8string());
                return false;
            }
            const auto size = static_cast<size_t>(in.tellg());
            in.seekg(0);
            out.resize(size);
            if (size > 0 && !in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size)))
            {
                error = std::string("Failed to read file: ").append(path.u8string());
                return false;
            }
            return true;
        }
    }  // namespace Internal
}  // namespace Fio
