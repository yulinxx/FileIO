#include "FileIO/Parsers/UgParser.h"

#include "Log/SyLogger.h"

#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    // IGES 行号：固定宽度格式下，行号位于每行末尾 8 列。
    // 段标记（S/G/D/P/T）位于第 73 列（1-based）。
    constexpr std::size_t kIgesSectionCol = 72;
    constexpr std::size_t kIgesSeqNumCol = 73;

    // 常用 IGES 实体类型号
    constexpr int kEntityCircularArc = 100;
    constexpr int kEntityCompositeCurve = 102;
    constexpr int kEntityCopiousData = 106;
    constexpr int kEntityLine = 110;
    constexpr int kEntityPoint = 116;

    struct IgesEntity
    {
        int type = 0;
        int paramLineStart = 0;  // 参数数据区起始行号（1-based）
    };

    // 解析固定 8 列字段中的数值
    double parseField(const std::string& field)
    {
        if (field.empty())
        {
            return 0.0;
        }
        try
        {
            std::string trimmed = field;
            while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())))
            {
                trimmed.erase(trimmed.begin());
            }
            while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
            {
                trimmed.pop_back();
            }
            return std::stod(trimmed);
        }
        catch (...)
        {
            return 0.0;
        }
    }

    // 逐 token 解析 IGES 参数数据行（逗号分隔，分号结束），跳过参数名首 token
    std::vector<double> parseParamTokens(const std::string& line)
    {
        std::vector<double> tokens;
        std::string token;
        bool first = true;  // 参数行第一个字段是参数名（如 "110," 或 "100,0,0" 的实体引用），忽略

        std::istringstream stream(line);
        while (std::getline(stream, token, ','))
        {
            // 去掉行尾分号
            if (!token.empty() && token.back() == ';')
            {
                token.pop_back();
            }
            if (first)
            {
                first = false;
                continue;
            }
            tokens.push_back(parseField(token));
        }
        return tokens;
    }
}  // namespace

namespace Fio
{
    ParseResult UgParser::parse(const char* /*filePath*/, VecSyEntityPtr& /*outEntities*/)
    {
        return ParseResult::fail(
            "Use parseToIR() for IGES import. Legacy parse() path is not supported for UG.");
    }

    FioParseResult UgParser::parseToIR(const char* filePath)
    {
        SY_INFOF("[UgParser] parseToIR START: filePath=%s", filePath ? filePath : "");

        thread_local std::vector<EntityInfo> s_entities;
        thread_local std::vector<uint8_t> s_extensionBlob;
        thread_local std::vector<IrLayerInfo> s_layers;
        s_entities.clear();
        s_extensionBlob.clear();
        s_layers.clear();

        std::vector<std::string> warnings;

        if (!filePath)
        {
            SY_ERROR("[UgParser] parseToIR: null filePath");
            return FioParseResult{};
        }

        // ---- 1. 读取文件到内存 ----
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            SY_ERRORF("[UgParser] parseToIR: Cannot open file: %s", filePath);
            return FioParseResult{};
        }
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(file, line))
        {
            // 去除行尾回车（Windows CRLF）
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            lines.push_back(line);
        }

        // ---- 2. 收集目录段 (D) 的实体类型与参数起始行 ----
        // IGES 目录段每个实体占 2 行：第一行含实体类型(1-8列)与指针信息，
        // 第二行含参数数据起始行号(41-48列)。两行均以 'D' 为段字母(index 72)。
        std::vector<IgesEntity> entities;
        std::vector<std::pair<std::string, std::string>> directoryRows;  // 收集 D 段行（按出现顺序）
        {
            bool inDirectory = false;
            for (std::size_t i = 0; i < lines.size(); ++i)
            {
                const std::string& l = lines[i];
                if (l.size() < kIgesSectionCol)
                {
                    continue;
                }
                const char section = l[kIgesSectionCol];
                if (section == 'S' || section == 'G')
                {
                    inDirectory = false;
                    continue;
                }
                if (section == 'D')
                {
                    inDirectory = true;
                }
                else if (section == 'P' || section == 'T')
                {
                    inDirectory = false;  // 目录段结束
                }
                if (inDirectory)
                {
                    // 每行记录两类信息：实体类型(1-8列) 与 参数数据指针(9-16列)
                    directoryRows.emplace_back(l.substr(0, 8), l.substr(8, 8));
                }
            }
        }

        // 每 2 行构成一个实体目录条目：第一行的类型与参数指针即可（第二行多为颜色/权重等外观属性）
        for (std::size_t i = 0; i + 1 < directoryRows.size(); i += 2)
        {
            IgesEntity ent;
            ent.type = static_cast<int>(parseField(directoryRows[i].first));      // 第一行 1-8 列：实体类型
            ent.paramLineStart = static_cast<int>(parseField(directoryRows[i].second));  // 第一行 9-16 列：参数数据指针(行号)
            if (ent.type > 0)
            {
                entities.push_back(ent);
            }
        }

        // ---- 3. 预建「参数行号 → 参数文本」映射（参数段 P） ----
        std::vector<std::string> paramLines;  // index = 行号 - 1
        bool inParam = false;
        for (const std::string& l : lines)
        {
            if (l.size() < kIgesSectionCol)
            {
                continue;
            }
            const char section = l[kIgesSectionCol];
            if (section == 'P')
            {
                inParam = true;
            }
            else if (section == 'T')
            {
                inParam = false;
                break;
            }
            if (inParam)
            {
                paramLines.push_back(l);
            }
        }

        // ---- 4. 解析各实体为 IR ----
        for (const IgesEntity& ent : entities)
        {
            const int paramLine = ent.paramLineStart;
            if (paramLine <= 0 || paramLine > static_cast<int>(paramLines.size()))
            {
                warnings.emplace_back("IGES entity has invalid param line: type=" + std::to_string(ent.type));
                continue;
            }
            const std::vector<double> p = parseParamTokens(paramLines[paramLine - 1]);

            EntityInfo info;
            info.type = EntityType::Unknown;
            info.sourceId = static_cast<uint64_t>(s_entities.size()) + 1;

            switch (ent.type)
            {
            case kEntityLine: {
                // 110 直线参数：Z(1) X1(2) Y1(3) Z1(4) X2(5) Y2(6) Z2(7)
                if (p.size() < 6)
                {
                    break;
                }
                info.type = EntityType::Line;
                info.line.x1 = p[1];
                info.line.y1 = p[2];
                info.line.x2 = p[4];
                info.line.y2 = p[5];
                break;
            }
            case kEntityCircularArc: {
                // 100 圆弧参数：ZT(1) X1(2) Y1(3) X2(4) Y2(5) X3(6) Y3(7) XC(8) YC(9)
                // 其中 X1..Y3 为端点与中点（用于定圆心），XC/YC 为圆心，半径=sqrt((X1-XC)^2+(Y1-YC)^2)
                if (p.size() < 9)
                {
                    break;
                }
                const double cx = p[7];
                const double cy = p[8];
                const double x1 = p[1];
                const double y1 = p[2];
                const double r = std::sqrt((x1 - cx) * (x1 - cx) + (y1 - cy) * (y1 - cy));
                const double startAngle = std::atan2(y1 - cy, x1 - cx);
                const double endAngle = std::atan2(p[3] - cy, p[4] - cx);
                info.type = EntityType::Arc;
                info.arc.cx = cx;
                info.arc.cy = cy;
                info.arc.r = r;
                info.arc.sa = startAngle;
                info.arc.ea = endAngle;
                break;
            }
            case kEntityPoint: {
                // 116 点参数：Z(1) X(2) Y(3) Z(4)
                if (p.size() < 3)
                {
                    break;
                }
                info.type = EntityType::Point;
                info.line.x1 = p[1];  // 复用 line 字段承载坐标
                info.line.y1 = p[2];
                info.line.x2 = p[1];
                info.line.y2 = p[2];
                break;
            }
            case kEntityCopiousData: {
                // 106 折线（点序列）：Z(1) ... IP 类型(2) ... X(3) Y(4) ... 由 N(某字段) 决定点数
                // 简化处理：收集从第 4 个 token 起的所有成对坐标作为折线顶点。
                if (p.size() < 4)
                {
                    break;
                }
                std::vector<double> pts;
                for (std::size_t i = 3; i + 1 < p.size(); i += 2)
                {
                    pts.push_back(p[i]);
                    pts.push_back(p[i + 1]);
                }
                if (pts.size() < 4)
                {
                    break;
                }
                info.type = EntityType::Polyline;
                info.vertexCount = static_cast<uint32_t>(pts.size() / 2);
                // 顶点序列存入扩展数据块
                const std::size_t offset = s_extensionBlob.size();
                const std::size_t bytes = pts.size() * sizeof(double);
                s_extensionBlob.resize(offset + bytes);
                std::memcpy(s_extensionBlob.data() + offset, pts.data(), bytes);
                info.extensionDataOffset = static_cast<uint32_t>(offset);
                info.extensionDataSize = static_cast<uint32_t>(bytes);
                break;
            }
            default:
                // 其他类型（124 变换矩阵、102 复合曲线等）暂不支持，跳过并提示
                warnings.emplace_back("Unsupported IGES entity type: " + std::to_string(ent.type));
                break;
            }

            if (info.type != EntityType::Unknown)
            {
                s_entities.push_back(info);
            }
        }

        if (s_entities.empty())
        {
            SY_WARNF("[UgParser] parseToIR: no supported entities in IGES file: %s", filePath);
            return FioParseResult{};
        }

        // ---- 5. 组装 FioParseResult ----
        FioParseResult result;
        result.entities = s_entities.data();
        result.entityCount = static_cast<uint32_t>(s_entities.size());
        result.layers = s_layers.data();
        result.layerCount = static_cast<uint32_t>(s_layers.size());
        result.extensionBlob.data = s_extensionBlob.data();
        result.extensionBlob.size = s_extensionBlob.size();
        std::strncpy(result.sourceFormat, "UG/IGES", sizeof(result.sourceFormat) - 1);
        result.warningCount = static_cast<uint32_t>(warnings.size());

        SY_INFOF("[UgParser] parseToIR END: %u entities, %zu warnings: %s",
            result.entityCount,
            warnings.size(),
            filePath);
        return result;
    }
}  // namespace Fio
