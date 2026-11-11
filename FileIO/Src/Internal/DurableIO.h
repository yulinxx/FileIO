/**
 * @file DurableIO.h
 * @brief 耐久写盘辅助：把文件与目录项真正刷到物理介质
 *
 * FileIO 内部的实现细节，不对外导出（不带 FILEIO_API）。
 * 原子写只保证「不会看到半份文件」，但不保证掉电后还在 ——
 * 必须对内容（文件）与目录项（rename 结果）分别做持久化同步。
 */
#pragma once

#include <filesystem>
#include <string>

namespace Fio
{
    namespace Internal
    {
        /// 同步结果：失败必须显式上报，禁止静默当成成功
        struct DurableResult
        {
            bool success{ false };
            std::string error;

            static DurableResult ok()
            {
                return { true, {} };
            }

            static DurableResult fail(std::string message)
            {
                return { false, std::move(message) };
            }
        };

        /// 把指定文件的内容刷到物理介质（POSIX: fsync / macOS: F_FULLFSYNC；Windows: FlushFileBuffers）
        /// @param path 已存在且已关闭写句柄的文件
        DurableResult syncFile(const std::filesystem::path& path);

        /// 把目录项（rename/create/remove 的结果）刷到物理介质
        /// @param directory 目标目录；Windows 上目录无通用刷新接口，按成功处理
        DurableResult syncDirectory(const std::filesystem::path& directory);

        /// 同步目标文件所在目录（目标路径的 parent_path；为空时用当前目录）
        DurableResult syncParentDirectory(const std::filesystem::path& filePath);
    }  // namespace Internal
}  // namespace Fio
