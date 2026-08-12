#pragma once

#include <cstddef>

#include "FileIO/FileIOAPI.h"
#include "FileIO/FileFormat.h"

#include <cstddef>

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
    };
} // namespace Fio
