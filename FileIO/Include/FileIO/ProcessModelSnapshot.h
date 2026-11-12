#pragma once

/**
 * @file ProcessModelSnapshot.h
 * @brief 作业级工艺模型的序列化快照接口（供 JobRecord 使用）。
 *
 * 取舍：JobRecord 推荐存**快照**而不是只存引用。文档保存后参数仍可能被改，
 * 只存「文档 id + 工艺 id + 版本」无法复现当时实际下发的工艺参数。
 * 快照字节自包含，配合 version + contentHash 可检测损坏/替换。
 *
 * 注意：本接口仅用于同进程/同 CRT 体系；跨模块传递请传 bytes 而非 C++ 对象。
 */

#include "FileIO/FileIOAPI.h"
#include "Engine/Process/ProcessModel.h"

#include <cstdint>
#include <string>

namespace Fio
{
    class AprDocument;

    struct ProcessModelSnapshotResult
    {
        bool ok = false;
        uint32_t version = 0;
        uint64_t contentHash = 0;  ///< FNV-1a 64，对 bytes 计算
        std::string bytes;         ///< ProcessModelData 序列化字节
        std::string error;         ///< 失败原因（英文）
    };

    /// 把内存模型序列化为快照字节；失败时 result.ok=false 且 error 非空。
    FILEIO_API ProcessModelSnapshotResult SerializeProcessModel(const Eg::ProcessModel& model);

    /// 从快照字节恢复模型；不校验版本合法性，调用方随后应调用 validateProcessModel。
    FILEIO_API bool DeserializeProcessModel(const std::string& bytes, Eg::ProcessModel& model,
        std::string& error);

    /// 取已加载文档里的工艺模型（原生 .apr 的 ProcessModelData）。
    /// 旧档案/外来格式没有该块时返回 false，out 不被改写。
    FILEIO_API bool ReadProcessModelFromDocument(const AprDocument& document, Eg::ProcessModel& out);

}  // namespace Fio
