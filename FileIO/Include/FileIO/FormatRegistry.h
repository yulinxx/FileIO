#pragma once

#include "FileIO/FileIOAPI.h"
#include "FileIO/FileFormat.h"

#include <string>
#include <vector>

namespace Fio
{
    /**
     * @brief 文件格式注册表（P1-11 收敛后的唯一入口）
     *
     * 统一管理各 FileFormat 的扩展名、描述以及导入/导出对话框过滤器字符串。
     * UI 层 (FileDialogService 等) 只应通过本类获取过滤器，避免各处硬编码。
     *
     * 扩展名映射与 FileParserFactory / FileWriterFactory 保持一致。
     */
    class FILEIO_API FormatRegistry
    {
    public:
        static FormatRegistry& instance();

        /// 根据文件路径（含扩展名）检测格式；无法识别时返回 FileFormat::Unknown
        FileFormat detectFormat(const char* filePath) const;

        /// 仅根据扩展名（如 "dxf") 检测格式；无法识别时返回 FileFormat::Unknown
        FileFormat detectFormatByExtension(const char* ext) const;

        /// 导入对话框过滤器字符串（如 "DXF Files (*.dxf)"）；未知格式返回 nullptr
        const char* importFilter(FileFormat format) const;

        /// 导出对话框过滤器字符串；未知格式返回 nullptr
        const char* exportFilter(FileFormat format) const;

        /// 遍历所有导入扩展名，调用 visitor(ext, ctx)
        void forEachImportExtension(void (*visitor)(const char* ext, void* ctx), void* ctx) const;

    private:
        FormatRegistry();
        ~FormatRegistry() = default;

        FormatRegistry(const FormatRegistry&) = delete;
        FormatRegistry& operator=(const FormatRegistry&) = delete;

        struct Entry
        {
            FileFormat format = FileFormat::Unknown;
            const char* label = "";
            const char* const* extensions = nullptr;
            size_t extCount = 0;
            std::string importFilterStr;
            std::string exportFilterStr;
        };

        void registerFormat(FileFormat format, const char* label,
                            const char* const* extensions, size_t extCount);

        const Entry* find(FileFormat format) const;

        std::vector<Entry> m_entries;
    };
}  // namespace Fio
