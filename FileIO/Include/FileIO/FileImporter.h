#pragma once

#include "FileIO/FileIOAPI.h"
#include "FileIO/FioTypes.h"

namespace Fio
{
    /// FileImporter — PIMPL 外观类
    /// 将 FileIO.dll 的解析能力以纯 POD 接口对外暴露，
    /// std::vector/std::string 完全隐藏在内部实现中。
    class FILEIO_API FileImporter
    {
    public:
        FileImporter();
        ~FileImporter();

        FileImporter(const FileImporter&) = delete;
        FileImporter& operator=(const FileImporter&) = delete;

        /// 打开并解析文件
        /// @param path 文件路径（UTF-8）
        /// @return 解析是否成功
        bool Open(const char* path);

        // ---- 逐条查询（POD 方式） ----

        /// 图层数量
        int LayerCount() const;
        /// 获取图层信息（索引越界返回 false）
        bool GetLayer(int index, IrLayerInfo* out) const;

        /// 图元数量
        int EntityCount() const;
        /// 获取图元信息（索引越界返回 false）
        bool GetEntity(int index, EntityInfo* out) const;

        // ---- 批量导出（Blob 方式） ----

        /// 将所有解析结果导出为二进制 blob
        /// 调用者需通过 FreeBlob() 释放
        BinaryBlob ExportBlob() const;

        /// 释放由 ExportBlob() 返回的 blob
        static void FreeBlob(BinaryBlob* blob);

        /// 获取错误消息（最后操作失败时可用）
        const char* LastError() const;

    private:
        struct Impl;
        Impl* pImpl;
    };
}  // namespace Fio