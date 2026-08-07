#pragma once

#include "FileIO/FileIOAPI.h"
#include "FileIO/FileFormat.h"

#include <cstddef>

namespace Fio
{
    class IFileWriter;

    // 写出器工厂（C2 已收口）：
    // - 注册回调改 C 函数指针（CreatorFunc），不再导出 std::function
    // - createWriter 返回裸指针（所有权转移给调用方，用后调用 destroyWriter 释放）
    // - 扩展名枚举改回调模式，字符串参数改 const char*
    // - std::map/std::vector 等 STL 成员隐藏到 PIMPL（Impl）
    class FILEIO_API FileWriterFactory
    {
    public:
        using CreatorFunc = IFileWriter * (*)();

        static FileWriterFactory& instance();

        void registerWriter(FileFormat format, CreatorFunc creator, const char* defaultExtension = nullptr);

        /// 创建写出器；所有权转移给调用方，用后调用 destroyWriter 释放
        IFileWriter* createWriter(FileFormat format) const;

        void destroyWriter(IFileWriter* writer) const;

        bool hasWriter(FileFormat format) const;

        /// 遍历所有支持的扩展名（回调模式，替代 std::vector<std::string> 返回）
        void forEachSupportedExtension(void (*visitor)(const char* ext, void* ctx), void* ctx) const;

        void initDefaults();

    private:
        FileWriterFactory();
        ~FileWriterFactory();
        FileWriterFactory(const FileWriterFactory&) = delete;
        FileWriterFactory& operator=(const FileWriterFactory&) = delete;

        class Impl;
        Impl* m_impl;
    };
} // namespace Fio
