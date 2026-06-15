#pragma once

#include "FileIO/FileIOAPI.h"
#include "FileIO/FileFormat.h"
#include "FileIO/FileIOError.h"

#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace Fio
{
    class FILEIO_API FileIOManager
    {
    public:
        using ImportCallback = std::function<void(const std::string&, bool)>;
        using ExportCallback = std::function<void(const std::string&, bool)>;

        explicit FileIOManager();
        ~FileIOManager();

        FileIOManager(const FileIOManager&) = delete;
        FileIOManager& operator=(const FileIOManager&) = delete;

        void setImportCallback(ImportCallback callback);
        void setExportCallback(ExportCallback callback);

        ParseResult importFile(const std::string& filePath, VecSyEntityPtr& outEntities);

        ParseResult importFile(const std::string& filePath, FileFormat format, VecSyEntityPtr& outEntities);

        WriteResult exportFile(const std::string& filePath, const VecSyEntityPtr& entities);

        WriteResult exportFile(const std::string& filePath, FileFormat format, const VecSyEntityPtr& entities);

        FileFormat detectFormat(const std::string& filePath) const;

        std::vector<std::string> supportedImportExtensions() const;
        std::vector<std::string> supportedExportExtensions() const;

        bool canImport(const std::string& filePath) const;
        bool canExport(FileFormat format) const;

    private:
        ImportCallback m_importCallback;
        ExportCallback m_exportCallback;

        /// 通用文件操作管道模板 ——
        /// 封装 import/export 共有的 "回调通知→工厂校验→创建→执行→回调反馈" 流程，
        /// 通过策略函数参数消除两个几乎相同方法的重复代码。
        ///
        /// @param filePath   文件路径
        /// @param callback   操作回调（import 用 m_importCallback，export 用 m_exportCallback）
        /// @param typeLabel  处理器类型标签（如 "parser" / "writer"），用于错误消息
        /// @param hasCreator  检查工厂是否支持该格式
        /// @param create      创建处理器
        /// @param execute     执行处理逻辑，返回 ResultT
        template<typename ResultT, typename HasCreator, typename Create, typename Execute>
        ResultT processFile(const std::string& filePath,
            const ImportCallback& callback,
            const char* typeLabel,
            HasCreator&& hasCreator,
            Create&& create,
            Execute&& execute)
        {
            if (callback)
                callback(filePath, true);

            if (!hasCreator())
            {
                if (callback)
                    callback(filePath, false);
                return ResultT::fail(
                    std::string("No ") + typeLabel + " registered for this format");
            }

            auto processor = create();
            if (!processor)
            {
                if (callback)
                    callback(filePath, false);
                return ResultT::fail(
                    std::string("Failed to create ") + typeLabel);
            }

            auto result = execute(processor.get());
            if (callback)
                callback(filePath, result.success);

            return result;
        }
    };
} // namespace Fio
