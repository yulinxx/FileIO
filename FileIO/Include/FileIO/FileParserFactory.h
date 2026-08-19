#pragma once

#include "FileIO/FileIOAPI.h"
#include "FileIO/FileFormat.h"

#include <cstddef>

namespace Fio
{
    class IFileParser;

    // 解析器工厂（C2 已收口）：
    // - 注册回调改 C 函数指针（CreatorFunc），不再导出 std::function
    // - createParser 返回裸指针（所有权转移给调用方，用后调用 destroyParser 释放）
    // - 扩展名枚举改回调模式，字符串参数改 const char*
    // - std::map/std::vector 等 STL 成员隐藏到 PIMPL（Impl）
    class FILEIO_API FileParserFactory
    {
    public:
        using CreatorFunc = IFileParser* (*)();

        static FileParserFactory& instance();

        void registerParser(FileFormat format, CreatorFunc creator);

        /// 创建解析器；所有权转移给调用方，用后调用 destroyParser 释放
        IFileParser* createParser(FileFormat format) const;

        void destroyParser(IFileParser* parser) const;

        bool hasParser(FileFormat format) const;

        /// 根据扩展名检测格式（委托给 FormatRegistry 唯一入口）
        FileFormat detectFormat(const char* ext) const;

        /// 遍历所有支持的导入扩展名（回调模式，委托给 FormatRegistry 唯一入口）
        void forEachSupportedExtension(void (*visitor)(const char* ext, void* ctx), void* ctx) const;

        void initDefaults();

    private:
        FileParserFactory();
        ~FileParserFactory();
        FileParserFactory(const FileParserFactory&) = delete;
        FileParserFactory& operator=(const FileParserFactory&) = delete;

        class Impl;
        Impl* m_impl;
    };
}  // namespace Fio
