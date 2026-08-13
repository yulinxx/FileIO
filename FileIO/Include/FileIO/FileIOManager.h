#pragma once

#include "FileIO/FileIOAPI.h"
#include "FileIO/FileFormat.h"
#include "FileIO/FioTypes.h"

#include <cstddef>

namespace Eg
{
    struct SyEntity;
}

namespace Fio
{
    /**
     * @brief 统一导入/导出管理器（外观模式）
     *
     * ABI 安全接口：
     * - 字符串统一使用 const char*（UTF-8）
     * - 图元集合使用裸指针数组 + 计数（Eg::SyEntity* const* / Eg::SyEntity**）
     * - 错误消息写入调用方缓冲区；警告经 C 函数指针回调逐条输出
     * - 回调均为 C 函数指针 + void* ctx（不使用 std::function）
     *
     * 所有权约定：
     * - importFile() 成功时 *outEntities 指向 FileIO 新建的裸指针数组，
     *   调用方取得图元所有权，用 deleteEntities() 释放整个数组；
     *   若调用方已自行接管图元（如包装进 unique_ptr），仅用 freeEntityArray() 释放数组本身。
     * - exportFile() 以裸指针数组传入，借用不转移所有权。
     */
    class FILEIO_API FileIOManager
    {
    public:
        using ImportCallback = void (*)(const char* filePath, bool success, void* ctx);
        using ExportCallback = void (*)(const char* filePath, bool success, void* ctx);
        using WarningCallback = void (*)(const char* warning, void* ctx);

        explicit FileIOManager();
        ~FileIOManager();

        FileIOManager(const FileIOManager&) = delete;
        FileIOManager& operator=(const FileIOManager&) = delete;

        void setImportCallback(ImportCallback callback, void* ctx = nullptr);
        void setExportCallback(ExportCallback callback, void* ctx = nullptr);

        // ==================== 导入 ====================
        // 成功返回 true，*outEntities 指向 FileIO 分配的图元数组，*outCount 为个数。
        // 错误消息写入 errorBuffer（尽力截断 + '\0'）。
        bool importFile(const char* filePath,
            FileFormat format,
            Eg::SyEntity*** outEntities,
            size_t* outCount,
            char* errorBuffer,
            size_t errorBufferSize);

        // 带警告回调与 DXF 图层数输出的导入
        bool importFile(const char* filePath,
            FileFormat format,
            Eg::SyEntity*** outEntities,
            size_t* outCount,
            char* errorBuffer,
            size_t errorBufferSize,
            WarningCallback warningCb,
            void* warningCtx,
            size_t* outLayerCount);

        // 自动检测格式导入
        bool importFile(const char* filePath,
            Eg::SyEntity*** outEntities,
            size_t* outCount,
            char* errorBuffer,
            size_t errorBufferSize);

        // ==================== IR 导入（中立 POD 路径） ====================
        // 新版导入：输出中立 IR（FioParseResult），不实例化 Engine 对象。
        // 调用方通过 FioEntityConverter（Main 层）将 IR 转换为领域对象。
        //
        // 注意：FioParseResult 内的指针指向解析器内部缓冲区，仅在本函数返回后
        // 到下一次同线程解析调用前有效，调用方需立即消费（如立即转换）。
        // 若解析失败返回 false，错误消息写入 errorBuffer。
        bool importToIR(
            const char* filePath, FileFormat format, FioParseResult* outResult, char* errorBuffer, size_t errorBufferSize);

        // 释放 importFile() 产生的图元数组（删除每个图元 + 数组本身）
        static void deleteEntities(Eg::SyEntity** entities, size_t count);

        // 仅释放数组本身（图元所有权已由调用方接管时使用）
        static void freeEntityArray(Eg::SyEntity** entities);

        // ==================== 导出 ====================
        // 图元以裸指针数组传入（借用，不转移所有权）。
        bool exportFile(const char* filePath,
            FileFormat format,
            const Eg::SyEntity* const* entities,
            size_t entityCount,
            char* errorBuffer,
            size_t errorBufferSize);

        bool exportFile(const char* filePath,
            const Eg::SyEntity* const* entities,
            size_t entityCount,
            char* errorBuffer,
            size_t errorBufferSize);

        // ==================== 查询 ====================
        FileFormat detectFormat(const char* filePath) const;

        // 扩展名列表写入 buffer（空格分隔），返回所需长度（不含结尾 '\0'）
        size_t supportedImportExtensions(char* buffer, size_t bufferSize) const;
        size_t supportedExportExtensions(char* buffer, size_t bufferSize) const;

        bool canImport(const char* filePath) const;
        bool canExport(FileFormat format) const;

    private:
        ImportCallback m_importCallback = nullptr;
        void* m_importCtx = nullptr;
        ExportCallback m_exportCallback = nullptr;
        void* m_exportCtx = nullptr;
    };
}  // namespace Fio
