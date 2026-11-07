#pragma once

// NativeWriter —— .sy / .syx 统一写入器
//
// 基于 SySerializer 实现 Protobuf 二进制序列化。
// 通过构造时传入的 FileFormat 区分 2D (.sy) 和 3D (.sx) 行为：
//   - FileFormat::Native  → 魔数 "SYPB", 软件名 "SanYi CAD 2D"
//   - FileFormat::Native3D → 魔数 "SXPB", 软件名 "SanYi CAD 3D"
//
// 设计说明:
//   统一 NativeWriter 的序列化逻辑，消除代码重复。
//   FileWriterFactory 对两种格式分别注册同一个 writer 类（不同构造参数）。
//   ILegacyWriter::write() 接口保留兼容性，新代码优先使用 writeDocument()。

#include "FileIO/IFileWriter.h"
#include "FileIO/SyDocument.h"
#include "FileIO/ILegacyTypes.h"

namespace Fio
{
    // 前向声明（定义在 SySerializer.h）
    typedef void (*SerializeProgressCallback)(float progress, const char* stage, void* ctx);

    class FILEIO_API NativeWriter : public IFileWriter, public ILegacyWriter
    {
    public:
        /// 构造统一写入器
        /// @param fmt 目标格式：FileFormat::Native (2D) 或 FileFormat::Native3D (3D)
        explicit NativeWriter(FileFormat fmt = FileFormat::Native);
        ~NativeWriter() override;

        /// 设置进度回调
        void setProgressCallback(SerializeProgressCallback cb, void* ctx);

        // ---- IFileWriter 接口 ----
        FileFormat format() const override;
        size_t formatName(char* buffer, size_t bufferSize) const override;
        size_t defaultExtension(char* buffer, size_t bufferSize) const override;

        // ---- ILegacyWriter 接口（兼容旧路径，仅处理 2D 图元）----
        WriteResult write(const char* filePath, const VecSyEntityPtr& entities) override;

        /// 完整文档写入（同时保存 2D 图元和 3D 网格）
        ///
        /// **不填充元数据**：本方法假定调用方已经自行填好 SyDocument 的
        /// software/version/时间戳等溯源字段。直接 new 一份 SyDocument 再调用
        /// 它，会写出元数据全空的 .sy/.syx —— 文件能打开，但来源与创建时间丢失。
        /// 这类调用方请改用 writeDocumentWithMetadata()。
        WriteResult writeDocument(const char* filePath, const SyDocument& doc);

        /// 填充标准元数据后写入完整文档（生产导出路径应使用本方法）。
        ///
        /// 新构造的文档走 MetadataFiller::fillMetadata（软件名/版本/创建时间/
        /// OS 溯源信息）；已带元数据的文档（读入后修改再保存）只刷新
        /// modifiedTime，不覆盖 createdTime 与原始来源信息。
        /// 与 write() 共享同一套填充逻辑，避免降级路径与借用路径写出不同元数据。
        WriteResult writeDocumentWithMetadata(const char* filePath, SyDocument& doc);

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };
}  // namespace Fio
