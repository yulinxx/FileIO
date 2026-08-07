#pragma once

// FileIO 内部公共类型（仅在 FileIO.dll 内部使用）
// std::vector/std::string 可自由使用，不跨 DLL 边界
//
// 说明（2026-07-31 C4 收口）：
//   - 旧版解析/写入结果（ParseResult/WriteResult/DxfLayerInfo）与旧版
//     纯虚接口（ILegacyParser::parse / ILegacyWriter::write）原属
//     IFileParser.h / IFileWriter.h / FileIOError.h 的导出面，携带 STL
//     跨越 DLL 边界。现已整体内迁至此，仅 FileIO.dll 内部消费。
//   - VecSyEntityPtr 兼容别名仍由公开头 IFileParser.h 提供（Main 侧使用），
//     此处直接复用，避免重定义。

#include "FileIO/IFileParser.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Eg { struct SyEntity; }

namespace Fio {

/// DXF 图层定义信息（旧版内部 API）
struct DxfLayerInfo
{
    std::string name;       // 图层名称
    int color{ 7 };           // ACI 颜色索引 (1-255)
    bool visible{ true };     // 是否可见
};

/// 旧版解析结果结构体（仅 DLL 内部使用）
struct ParseResult
{
    bool success = false;
    std::string errorMessage;
    std::vector<std::string> warnings;

    /// DXF 图层定义（仅 DXF 导入时填充）
    std::vector<DxfLayerInfo> dxfLayers;
    /// 图元索引 -> DXF 图层名 映射（仅 DXF 导入时填充）
    std::map<size_t, std::string> entityLayerMap;
    /// 图元索引 -> DXF 颜色索引 映射（仅 DXF 导入时填充）
    std::map<size_t, int> entityColorMap;

    static ParseResult ok()
    {
        return { true, {}, {}, {}, {}, {} };
    }

    static ParseResult ok(const std::vector<std::string>& warns)
    {
        return { true, {}, warns, {}, {}, {} };
    }

    static ParseResult fail(const std::string& msg)
    {
        return { false, msg, {}, {}, {}, {} };
    }

    static ParseResult fail(const std::string& msg, const std::vector<std::string>& warns)
    {
        return { false, msg, warns, {}, {}, {} };
    }
};

/// 旧版写入结果结构体（仅 DLL 内部使用）
struct WriteResult
{
    bool success = false;
    std::string errorMessage;

    static WriteResult ok()
    {
        return { true, {} };
    }

    static WriteResult fail(const std::string& msg)
    {
        return { false, msg };
    }
};

/// 旧版解析接口（仅 DLL 内部使用，不再作为导出接口跨 DLL）
class ILegacyParser
{
public:
    virtual ~ILegacyParser() = default;
    virtual ParseResult parse(const char* filePath, VecSyEntityPtr& outEntities) = 0;
};

/// 旧版写入接口（仅 DLL 内部使用，不再作为导出接口跨 DLL）
class ILegacyWriter
{
public:
    virtual ~ILegacyWriter() = default;
    virtual WriteResult write(const char* filePath, const VecSyEntityPtr& entities) = 0;
};

} // namespace Fio
