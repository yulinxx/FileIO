#pragma once

#include <cstring>
#include <cstddef>

#include "FileIO/FileIOAPI.h"
#include "FileIO/FileFormat.h"

namespace Fio
{
    // 旧版纯虚 write() 已随 WriteResult 内迁至 Src/Internal（ILegacyWriter），
    // 不再经导出接口携带 STL 跨越 DLL 边界。
    class FILEIO_API IFileWriter
    {
    public:
        // 析构定义移至 .cpp 避免 STL 跨 DLL 边界
        virtual ~IFileWriter();

        virtual FileFormat format() const = 0;

        /// 格式名称（buffer 模式替代 std::string 返回）
        virtual size_t formatName(char* buffer, size_t bufferSize) const = 0;

        /// 默认扩展名（buffer 模式替代 std::string 返回）
        virtual size_t defaultExtension(char* buffer, size_t bufferSize) const = 0;

    protected:
        /// 将字符串安全复制到 buffer（统一实现，避免各 Writer 重复模板代码）
        static size_t copyToBuffer(char* buffer, size_t bufferSize, const char* str)
        {
            const size_t len = std::strlen(str);
            if (buffer != nullptr && bufferSize > len)
            {
                std::strncpy(buffer, str, bufferSize - 1);
                buffer[bufferSize - 1] = '\0';
            }
            return len;
        }
    };
}  // namespace Fio
