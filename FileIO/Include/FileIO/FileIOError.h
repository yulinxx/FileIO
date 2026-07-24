#pragma once

#include <string>
#include <vector>
#include <map>

namespace Fio
{
    /// DXF 图层定义信息
    struct DxfLayerInfo
    {
        std::string name;       // 图层名称
        int color{ 7 };           // ACI 颜色索引 (1-255)
        bool visible{ true };     // 是否可见
    };

    struct ParseResult
    {
        bool success = false;
        std::string errorMessage;
        std::vector<std::string> warnings;

        /// DXF 图层定义（仅 DXF 导入时填充）
        std::vector<DxfLayerInfo> dxfLayers;
        /// 实体索引 -> DXF 图层名 映射（仅 DXF 导入时填充）
        std::map<size_t, std::string> entityLayerMap;
        /// 实体索引 -> DXF 颜色索引 映射（仅 DXF 导入时填充）
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

    ////////////////////////////////////////////////////////////////
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
} // namespace Fio
