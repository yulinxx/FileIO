/**
 * @file DurableIO.h
 * @brief 耐久写盘辅助：把文件与目录项真正刷到物理介质
 *
 * FileIO 内部的实现细节，不对外导出（不带 FILEIO_API）。
 * 原子写只保证「不会看到半份文件」，但不保证掉电后还在 ——
 * 必须对内容（文件）与目录项（rename 结果）分别做持久化同步。
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

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

        /// 以「临时文件 + 刷盘 + 原子替换 + 目录刷盘」写整块数据。
        /// 供恢复文件 / manifest 等非文档格式的小文件复用，保证与正式保存同等耐久性。
        DurableResult writeFileDurably(const std::filesystem::path& path, const void* data, size_t size);

        /// 读取整个文件；失败时返回 false 并填充 error
        bool readWholeFile(const std::filesystem::path& path, std::vector<uint8_t>& out, std::string& error);
    }  // namespace Internal
}  // namespace Fio

