#pragma once

#include "FileIO/FileIOAPI.h"
#include "FileIO/FileFormat.h"
#include "FileIO/FioTypes.h"

#include <memory>
#include <vector>

namespace Eg
{
    struct SyEntity;
}

namespace Fio
{
    // 兼容别名：Main 侧 IImportReader / IExportWriter 等仍使用 Fio::VecSyEntityPtr。
    // 仅为类型别名（using），无 ABI 影响；旧版纯虚 parse()/write() 已随
    // ParseResult/WriteResult 一并内迁至 Src/Internal（ILegacyParser/ILegacyWriter），
    // 不再经导出接口携带 STL 跨越 DLL 边界。
    using VecSyEntityPtr = std::vector<std::unique_ptr<Eg::SyEntity>>;

    class FILEIO_API IFileParser
    {
    public:
        // 析构定义移至 .cpp 避免 STL 跨 DLL 边界
        virtual ~IFileParser();

        virtual FileFormat format() const = 0;

        /// 遍历支持的扩展名（回调模式替代 std::vector<std::string> 返回）
        virtual void forEachSupportedExtension(void (*visitor)(const char*, void*), void* ctx) const = 0;

        /// 格式名称（buffer 模式替代 std::string 返回）
        virtual size_t formatName(char* buffer, size_t bufferSize) const = 0;

        // ---- 新版 API（推荐） ----
        // 输出中立 IR（FioParseResult），纯 POD 类型，跨 DLL 安全
        // 调用方通过 FioEntityConverter 将 IR 转换为 Engine 领域对象
        virtual FioParseResult parseToIR(const char* filePath);

        /// 带进度上报的 IR 解析（可选覆写）。
        ///
        /// 默认实现忽略进度回调、直接转调 parseToIR()，因此尚未接入进度的解析器
        /// 无需改动。支持进度的解析器（PLT / DXF / SVG）覆写本方法，在长循环里按
        /// [0,1] 调用 onProgress（onProgress 可能为空，需判空）。
        virtual FioParseResult parseToIRWithProgress(
            const char* filePath, ParseProgressCallback onProgress, void* progressCtx);

        /// 解析器选项设置（key-value 字符串对）。
        ///
        /// 默认实现为空操作（no-op），解析器按需覆写处理各自支持的选项。
        /// Facade 在创建解析器后、调用 parseToIR 之前统一调用此方法，
        /// 避免对具体解析器类型做 dynamic_cast。
        ///
        /// 已知选项：
        ///   "importFillAsOutline" — "true"/"false"，是否把纯填充色块导入为轮廓线
        virtual void setOption(const char* /*key*/, const char* /*value*/) {}
    };
}  // namespace Fio
