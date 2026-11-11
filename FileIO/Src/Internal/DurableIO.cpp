/**
 * @file DurableIO.cpp
 * @brief 耐久写盘辅助实现
 */
#include "DurableIO.h"

#include <cerrno>
#include <cstring>

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
    }  // namespace Internal
}  // namespace Fio
