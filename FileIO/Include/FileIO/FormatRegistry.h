#pragma once

#include "FileIO/FileIOAPI.h"
#include "FileIO/FileFormat.h"

#include <cstddef>

namespace Fio
{
    // 格式注册表（P1-11 收敛后的唯一入口）：
    // - 统一维护 扩展名→格式 映射、导入/导出支持、默认扩展名与过滤器字符串
    // - 所有 detectFormat 与过滤器生成均收敛到此单例，消除各模块重复实现
    // - ABI 安全：字符串用 const char*（UTF-8），枚举用回调模式，STL 成员隐藏到 PIMPL
    class FILEIO_API FormatRegistry
    {
    public:
        static FormatRegistry& instance();

        // 根据完整文件路径检测格式（不区分大小写）
        FileFormat detectFormat(const char* filePath) const;

        // 根据扩展名检测格式（不含点，小写）
        FileFormat detectFormatByExtension(const char* ext) const;

        // 获取默认扩展名（不含点，小写）；无则返回 nullptr
        const char* defaultExtension(FileFormat format) const;

        // 导入/导出支持查询
        bool isImportSupported(FileFormat format) const;
        bool isExportSupported(FileFormat format) const;

        // 遍历所有支持的导入/导出扩展名（回调模式，ABI 安全）
        void forEachImportExtension(void (*visitor)(const char* ext, void* ctx), void* ctx) const;
        void forEachExportExtension(void (*visitor)(const char* ext, void* ctx), void* ctx) const;

        // 获取导入/导出过滤器字符串（UTF-8）；无则返回 nullptr
        const char* importFilter(FileFormat format) const;
        const char* exportFilter(FileFormat format) const;

    private:
        FormatRegistry();
        ~FormatRegistry();
        FormatRegistry(const FormatRegistry&) = delete;
        FormatRegistry& operator=(const FormatRegistry&) = delete;

        void registerDefaults();
        void registerFormat(FileFormat format,
                            const char* const* extensions,
                            size_t extCount,
                            const char* defaultExt,
                            const char* importFilter,
                            const char* exportFilter,
                            bool importSupported,
                            bool exportSupported);

        class Impl;
        Impl* m_impl;
    };
}  // namespace Fio
