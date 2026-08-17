#include "FileIO/Parsers/SvgParser.h"
#include "FileIO/FileIOUtils.h"

#include "Log/SyLogger.h"

#include "Ut/Vec.h"

#include <cmath>

#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"

#include <cmath>
#include <cstring>
#include <cctype>
#include <map>
#include <memory>
#include <vector>
#include <algorithm>
#include <numeric>
#include <fstream>

#ifdef FILEIO_HAS_ZLIB
    #include <zlib.h>
#endif

namespace Fio
{
    namespace
    {
        // RAII wrapper for NSVGimage to ensure exception safety
        struct NsvgImageDeleter
        {
            void operator()(NSVGimage* image) const
            {
                if (image)
                {
                    nsvgDelete(image);
                }
            }
        };

        using NsvgImagePtr = std::unique_ptr<NSVGimage, NsvgImageDeleter>;

#ifdef FILEIO_HAS_ZLIB
        // Decompress gzip data (for .svgz files)
        std::vector<char> decompressGzip(const std::vector<char>& compressedData)
        {
            std::vector<char> decompressed;

            z_stream strm = {};
            strm.zalloc = Z_NULL;
            strm.zfree = Z_NULL;
            strm.opaque = Z_NULL;
            strm.avail_in = static_cast<uInt>(compressedData.size());
            strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressedData.data()));

            // 15 + 16 = gzip decoding
            int ret = inflateInit2(&strm, 15 + 16);
            if (ret != Z_OK)
            {
                return decompressed;
            }

            constexpr size_t CHUNK_SIZE = 16384;
            char outBuffer[CHUNK_SIZE];

            do
            {
                strm.avail_out = CHUNK_SIZE;
                strm.next_out = reinterpret_cast<Bytef*>(outBuffer);

                ret = inflate(&strm, Z_NO_FLUSH);

                if (ret != Z_OK && ret != Z_STREAM_END)
                {
                    inflateEnd(&strm);
                    return {};
                }

                size_t have = CHUNK_SIZE - strm.avail_out;
                decompressed.insert(decompressed.end(), outBuffer, outBuffer + have);
            } while (ret != Z_STREAM_END);

            inflateEnd(&strm);
            return decompressed;
        }
#endif  // FILEIO_HAS_ZLIB

        // Check if data starts with gzip magic number (0x1f, 0x8b)
        bool isGzipData(const std::vector<char>& data)
        {
            return data.size() >= 2 && static_cast<unsigned char>(data[0]) == 0x1f &&
                static_cast<unsigned char>(data[1]) == 0x8b;
        }

        // Read file into memory
        std::vector<char> readFileContent(const std::filesystem::path& filePath)
        {
            std::ifstream file(filePath, std::ios::binary | std::ios::ate);
            if (!file)
            {
                return {};
            }

            std::streamsize size = file.tellg();
            if (size <= 0)
            {
                return {};
            }

            file.seekg(0, std::ios::beg);
            std::vector<char> buffer(static_cast<size_t>(size));

            if (!file.read(buffer.data(), size))
            {
                return {};
            }

            return buffer;
        }

        // Extract SVG color from nanosvg paint, returns normalized RGB (0-1)
        // 注意：nanosvg 的颜色格式为 0xAABBGGRR（见 NSVG_RGB: r | g<<8 | b<<16，alpha 在高字节），
        // 与常见的 0xAARRGGBB 不同，直接按高位取会红蓝互换。
        Ut::Vec3f extractSvgColor(const NSVGpaint& paint)
        {
            if (paint.type == NSVG_PAINT_COLOR)
            {
                unsigned int color = paint.color;
                float r = static_cast<float>(color & 0xFF) / 255.0f;
                float g = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
                float b = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
                return Ut::Vec3f(r, g, b);
            }
            // For gradients or unknown types, return default color
            return Ut::Vec3f(0.0f, 0.0f, 0.0f);
        }

        // 将 0-1 RGB 打包为 0xAARRGGBB（与 EntityInfo.color / Ut::Color 约定一致）
        uint32_t packSvgColor(const Ut::Vec3f& c)
        {
            auto toByte = [](float v) {
                int i = static_cast<int>(std::lround(v * 255.0f));
                return static_cast<uint8_t>(std::clamp(i, 0, 255));
            };
            return 0xFF000000u | (static_cast<uint32_t>(toByte(c.x())) << 16) |
                (static_cast<uint32_t>(toByte(c.y())) << 8) | static_cast<uint32_t>(toByte(c.z()));
        }

        // ===== SVG CSS <style> 类样式内联 =====
        // nanosvg 不解析 <style> 中的 .class 规则，只认元素内联属性。大量（尤其 Illustrator
        // 导出）SVG 把颜色/描边放在 class 里，导致 nanosvg 看不到颜色 → 整图变黑。
        // 这里在交给 nanosvg 前，把 class 引用的样式内联成元素属性。

        // nanosvg 实际可消费的表现属性白名单（其余 class 属性内联后会被解析器忽略，无需加入）
        static const char* kSvgInlineProps[] = {
            "fill", "stroke", "stroke-width", "fill-opacity", "stroke-opacity", "opacity",
            "stroke-linecap", "stroke-linejoin", "stroke-dasharray", "stroke-dashoffset", nullptr};

        static bool isSvgInlineProp(const std::string& name)
        {
            for (int i = 0; kSvgInlineProps[i] != nullptr; ++i)
            {
                if (name == kSvgInlineProps[i])
                {
                    return true;
                }
            }
            return false;
        }

        static std::string trimWhitespace(const std::string& s)
        {
            size_t a = 0, b = s.size();
            while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
                ++a;
            while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
                --b;
            return s.substr(a, b - a);
        }

        // 读取标签中某个属性的值（name="..." 或 name='...'），找不到返回空串
        static std::string getSvgAttr(const std::string& tag, const std::string& name)
        {
            size_t p = 0;
            while (p < tag.size())
            {
                size_t found = tag.find(name, p);
                if (found == std::string::npos)
                {
                    break;
                }
                // 属性名前必须是空白或 '<'，避免匹配到属性值里的子串
                if (found == 0 || tag[found - 1] == ' ' || tag[found - 1] == '\t' ||
                    tag[found - 1] == '\n' || tag[found - 1] == '\r' || tag[found - 1] == '<')
                {
                    size_t after = found + name.size();
                    while (after < tag.size() && (tag[after] == ' ' || tag[after] == '\t'))
                        ++after;
                    if (after < tag.size() && tag[after] == '=')
                    {
                        ++after;
                        while (after < tag.size() && (tag[after] == ' ' || tag[after] == '\t'))
                            ++after;
                        if (after < tag.size() && (tag[after] == '"' || tag[after] == '\''))
                        {
                            char q = tag[after];
                            size_t start = after + 1;
                            size_t end = tag.find(q, start);
                            if (end != std::string::npos)
                            {
                                return tag.substr(start, end - start);
                            }
                        }
                    }
                }
                p = found + name.size();
            }
            return "";
        }

        static bool hasSvgAttr(const std::string& tag, const std::string& name)
        {
            return !getSvgAttr(tag, name).empty();
        }

        // 解析 <style> 文本中的 .class 规则 → class 名 → (属性 → 值)
        static std::map<std::string, std::map<std::string, std::string>> parseCssRules(const std::string& css)
        {
            std::map<std::string, std::map<std::string, std::string>> rules;
            size_t i = 0;
            const size_t n = css.size();
            while (i < n)
            {
                size_t brace = css.find('{', i);
                if (brace == std::string::npos)
                {
                    break;
                }
                size_t close = css.find('}', brace);
                if (close == std::string::npos)
                {
                    break;
                }
                std::string selectors = css.substr(i, brace - i);
                std::string body = css.substr(brace + 1, close - brace - 1);

                // 解析声明：prop: value;
                std::map<std::string, std::string> decls;
                size_t pos = 0;
                const size_t pb = body.size();
                while (pos < pb)
                {
                    size_t colon = body.find(':', pos);
                    if (colon == std::string::npos)
                    {
                        break;
                    }
                    size_t semi = body.find(';', colon);
                    if (semi == std::string::npos)
                    {
                        semi = pb;
                    }
                    std::string key = trimWhitespace(body.substr(pos, colon - pos));
                    std::string val = trimWhitespace(body.substr(colon + 1, semi - colon - 1));
                    if (!key.empty() && !val.empty())
                    {
                        decls[key] = val;
                    }
                    pos = semi + 1;
                }

                // 多个选择器（逗号分隔），只处理 .class
                size_t s = 0;
                while (s < selectors.size())
                {
                    size_t comma = selectors.find(',', s);
                    std::string sel = trimWhitespace(
                        selectors.substr(s, comma == std::string::npos ? std::string::npos : comma - s));
                    if (!sel.empty() && sel[0] == '.')
                    {
                        std::string cls = sel.substr(1);
                        for (const auto& kv : decls)
                        {
                            if (isSvgInlineProp(kv.first))
                            {
                                rules[cls][kv.first] = kv.second;
                            }
                        }
                    }
                    if (comma == std::string::npos)
                    {
                        break;
                    }
                    s = comma + 1;
                }

                i = close + 1;
            }
            return rules;
        }

        // 将 <style> 中的 class 样式内联到引用它的元素上（元素自身属性优先，不被覆盖）
        static std::string inlineSvgCss(const std::string& svg)
        {
            // 1) 收集所有 <style> 块内容
            std::string css;
            size_t pos = 0;
            while (true)
            {
                size_t open = svg.find("<style", pos);
                if (open == std::string::npos)
                {
                    break;
                }
                size_t tagEnd = svg.find('>', open);
                if (tagEnd == std::string::npos)
                {
                    break;
                }
                size_t close = svg.find("</style>", tagEnd);
                if (close == std::string::npos)
                {
                    break;
                }
                css += svg.substr(tagEnd + 1, close - tagEnd - 1);
                pos = close + 8;
            }
            if (css.empty())
            {
                return svg;
            }

            auto rules = parseCssRules(css);
            if (rules.empty())
            {
                return svg;
            }

            // 2) 扫描每个元素标签，内联 class 样式
            std::string out;
            out.reserve(svg.size());
            size_t i = 0;
            const size_t n = svg.size();
            while (i < n)
            {
                size_t lt = svg.find('<', i);
                if (lt == std::string::npos)
                {
                    out += svg.substr(i);
                    break;
                }
                out += svg.substr(i, lt - i);

                // 闭合/声明/注释标签：原样复制
                if (lt + 1 < n && (svg[lt + 1] == '/' || svg[lt + 1] == '?' || svg[lt + 1] == '!'))
                {
                    size_t gt = svg.find('>', lt);
                    if (gt == std::string::npos)
                    {
                        out += svg.substr(lt);
                        break;
                    }
                    out += svg.substr(lt, gt - lt + 1);
                    i = gt + 1;
                    continue;
                }

                size_t gt = svg.find('>', lt);
                if (gt == std::string::npos)
                {
                    out += svg.substr(lt);
                    break;
                }
                std::string tag = svg.substr(lt, gt - lt + 1);

                std::string cls = getSvgAttr(tag, "class");
                if (!cls.empty())
                {
                    // 合并 class 列表中的样式（后者覆盖前者）
                    std::map<std::string, std::string> merged;
                    size_t c = 0;
                    while (c < cls.size())
                    {
                        size_t sp = cls.find_first_of(" \t\r\n", c);
                        std::string one = trimWhitespace(cls.substr(c, sp == std::string::npos ? std::string::npos : sp - c));
                        if (!one.empty())
                        {
                            auto it = rules.find(one);
                            if (it != rules.end())
                            {
                                for (const auto& kv : it->second)
                                {
                                    merged[kv.first] = kv.second;
                                }
                            }
                        }
                        if (sp == std::string::npos)
                        {
                            break;
                        }
                        c = sp + 1;
                    }

                    if (!merged.empty())
                    {
                        std::string insertion;
                        for (const auto& kv : merged)
                        {
                            if (!hasSvgAttr(tag, kv.first))
                            {
                                insertion += " " + kv.first + "=\"" + kv.second + "\"";
                            }
                        }
                        if (!insertion.empty())
                        {
                            size_t endPos = tag.size() - 1;  // 指向 '>'
                            size_t insertAt = endPos;
                            if (tag[endPos - 1] == '/')
                            {
                                insertAt = endPos - 1;  // 写在 '/>' 的 '/' 之前
                            }
                            tag.insert(insertAt, insertion);
                        }
                    }
                }

                out += tag;
                i = gt + 1;
            }
            return out;
        }

        // 点到线段距离（用于曲线扁平度估计）
        // 注：保留贝塞尔曲线后不再需要把曲线离散为折线，此函数及 computeAdaptiveSegments 已移除。

        }  // namespace

    class NsvgInterpreter
    {
    public:
        NsvgInterpreter(std::vector<EntityInfo>& outEntities,
            std::vector<IrLayerInfo>& outLayers,
            std::vector<std::string>& warnings,
            bool importFillAsOutline)
            : m_outEntities(outEntities)
            , m_outLayers(outLayers)
            , m_warnings(warnings)
            , m_importFillAsOutline(importFillAsOutline)
            , m_success(false)
        {
        }

        bool succeeded() const
        {
            return m_success;
        }

        void parseFile(const std::string& filePath)
        {
            // Read file content
            std::filesystem::path fsPath = std::filesystem::u8path(filePath);
            std::vector<char> fileContent = readFileContent(fsPath);

            if (fileContent.empty())
            {
                m_warnings.push_back("Cannot read file: " + filePath);
                return;
            }

            // Check if it's gzip compressed (.svgz)
            std::vector<char> svgData;
            if (isGzipData(fileContent))
            {
#ifdef FILEIO_HAS_ZLIB
                svgData = decompressGzip(fileContent);
                if (svgData.empty())
                {
                    m_warnings.push_back("Failed to decompress SVGZ file: " + filePath);
                    return;
                }
#else
                m_warnings.push_back("SVGZ file detected but zlib not available. "
                                     "Please install zlib to support .svgz files: " +
                    filePath);
                return;
#endif
            }
            else
            {
                svgData = std::move(fileContent);
            }

            // 内联 <style> 中的 class 样式，使 nanosvg 能识别颜色/描边（否则整图变黑）
            {
                std::string svgStr(svgData.data(), svgData.size());
                std::string inlined = inlineSvgCss(svgStr);
                svgData.assign(inlined.begin(), inlined.end());
            }

            // Parse SVG from memory (null-terminated string)
            svgData.push_back('\0');

            NSVGimage* rawImage = nsvgParse(svgData.data(), "px", 96.0f);
            if (!rawImage)
            {
                m_warnings.push_back("Failed to parse SVG file: " + filePath);
                return;
            }

            // Wrap in RAII pointer for exception safety
            NsvgImagePtr image(rawImage);

            for (NSVGshape* shape = image->shapes; shape != nullptr; shape = shape->next)
            {
                bool visible = (shape->flags & NSVG_FLAGS_VISIBLE) != 0;
                bool hasStroke = shape->stroke.type != NSVG_PAINT_NONE;

                if (!visible)
                {
                    continue;
                }

                // 默认“只保留描边线条”：丢弃纯填充（fill-only）图形。
                // nanosvg 会把填充区域自动闭合并给出轮廓多边形，若直接绘成线条就会多出
                // 一条把首尾连起来的“封闭线”，与浏览器中实心填充的观感不符。
                // 可通过 setImportFillAsOutline(true) 开启：把纯填充色块也导入为闭合轮廓线。
                Ut::Vec3f shapeColor;
                if (!hasStroke)
                {
                    if (!m_importFillAsOutline)
                    {
                        continue;
                    }
                    shapeColor = extractSvgColor(shape->fill);
                }
                else
                {
                    shapeColor = extractSvgColor(shape->stroke);
                }

                // Extract layer name from shape id (nanosvg propagates the <g> group id down to child shapes)
                std::string layerName;
                if (shape->id[0] != '\0')
                {
                    layerName = shape->id;
                }

                uint32_t layerSourceId = getOrCreateLayer(layerName, shapeColor);

                for (NSVGpath* svgPath = shape->paths; svgPath != nullptr; svgPath = svgPath->next)
                {
                    convertPathToBezierEntities(svgPath, shapeColor, layerSourceId);
                }
            }

            m_success = !m_outEntities.empty();
        }

    private:
        std::vector<EntityInfo>& m_outEntities;
        std::vector<IrLayerInfo>& m_outLayers;
        std::vector<std::string>& m_warnings;
        bool m_importFillAsOutline;
        bool m_success;

        // 按图层名查找或创建图层，返回图层 sourceId。
        // SVG 无显式图层定义，以 shape/group 的 id 作为图层名。
        uint32_t getOrCreateLayer(const std::string& name, const Ut::Vec3f& color)
        {
            const std::string layerName = name.empty() ? std::string("SVG") : name;

            for (const auto& layer : m_outLayers)
            {
                if (layerName == layer.name)
                {
                    return layer.sourceId;
                }
            }

            IrLayerInfo layer;
            // 1-based sourceId：0 保留为「未分配图层」哨兵
            layer.sourceId = static_cast<uint32_t>(m_outLayers.size()) + 1;
            std::strncpy(layer.name, layerName.c_str(), sizeof(layer.name) - 1);
            layer.name[sizeof(layer.name) - 1] = '\0';
            layer.color = 0xFF000000 |
                (static_cast<uint8_t>(color.x() * 255.0f) << 16) |
                (static_cast<uint8_t>(color.y() * 255.0f) << 8) |
                static_cast<uint8_t>(color.z() * 255.0f);
            layer.visible = true;
            m_outLayers.push_back(layer);
            return layer.sourceId;
        }

        void convertPathToBezierEntities(NSVGpath* svgPath, const Ut::Vec3f& shapeColor, uint32_t layerSourceId)
        {
            if (!svgPath || svgPath->npts < 4)
            {
                return;
            }

            float* pts = svgPath->pts;
            int npts = svgPath->npts;

            // nanosvg pts 布局: [x0,y0, cpx1,cpy1, cpx2,cpy2, x1,y1, ...]
            // 每段三次贝塞尔 = 一个起点 + 两个控制点 + 一个终点（相对起点增加 3 个点）。
            // 保留贝塞尔曲线本身（而非离散成折线），每段生成一条 Bezier 实体。
            // 注意：闭合路径时 nanosvg 已在 addPath 中补上一条回到起点的闭合线段（直线贝塞尔），
            // 因此这里无需再额外处理闭合，直接逐段生成即可。
            for (int i = 0; i + 3 < npts; i += 3)
            {
                // Y 轴翻转：SVG Y 向下 -> 系统 Y 向上
                Ut::Vec2d p0(pts[i * 2], -pts[i * 2 + 1]);
                Ut::Vec2d c1(pts[i * 2 + 2], -pts[i * 2 + 3]);
                Ut::Vec2d c2(pts[i * 2 + 4], -pts[i * 2 + 5]);
                Ut::Vec2d p1(pts[i * 2 + 6], -pts[i * 2 + 7]);

                // [F2-P0 修复] NaN/Inf 坐标会导致后续 static_cast<int>(NaN) 产生 UB，
                // 以及越界数组索引。此处对所有坐标做 finite 校验，非法点跳过并告警。
                if (!std::isfinite(p0.x()) || !std::isfinite(p0.y()) ||
                    !std::isfinite(c1.x()) || !std::isfinite(c1.y()) ||
                    !std::isfinite(c2.x()) || !std::isfinite(c2.y()) ||
                    !std::isfinite(p1.x()) || !std::isfinite(p1.y()))
                {
                    SY_WARNF("[SvgParser] NaN/Inf coordinate detected at path segment %d, skipping", i);
                    continue;
                }

                // 跳过退化段：所有点几乎重合（零长度/退化贝塞尔）
                double extent = std::max({ std::fabs(p0.x() - c1.x()), std::fabs(p0.y() - c1.y()),
                    std::fabs(p0.x() - c2.x()), std::fabs(p0.y() - c2.y()),
                    std::fabs(p0.x() - p1.x()), std::fabs(p0.y() - p1.y()) });
                if (extent < 1e-9)
                {
                    continue;
                }

                // 填充 EntityInfo：Bezier 类型（起点在 line.x1/y1，控制点/终点在 bezier 字段）
                EntityInfo info{};
                info.type = EntityType::Bezier;
                info.sourceId = static_cast<uint64_t>(m_outEntities.size());
                info.layerSourceId = layerSourceId;
                info.visible = true;
                info.color = packSvgColor(shapeColor);
                info.line.x1 = p0.x();
                info.line.y1 = p0.y();
                info.bezier.c0x = c1.x();
                info.bezier.c0y = c1.y();
                info.bezier.c1x = c2.x();
                info.bezier.c1y = c2.y();
                info.bezier.ex = p1.x();
                info.bezier.ey = p1.y();
                m_outEntities.push_back(info);
            }
        }
    };

    // ========================================================================
    // SvgParser::parseToIR() — 中立 IR 解析路径
    // SVG path → 保留三次贝塞尔曲线 → 每段生成一条 Bezier 实体（不离散为折线）
    // 不依赖 Engine2D 类型，跨 DLL 安全
    // ========================================================================
    FioParseResult SvgParser::parseToIR(const char* filePath)
    {
        SY_INFOF("[SvgParser] parseToIR START: %s", filePath ? filePath : "");

        // thread_local 缓冲区管理生命周期（与 StepParser/PltParser 一致）
        thread_local std::vector<EntityInfo> s_entities;
        thread_local std::vector<uint8_t> s_extensionBlob;
        thread_local std::vector<IrLayerInfo> s_layers;
        thread_local std::vector<std::string> s_warnings;
        s_entities.clear();
        s_extensionBlob.clear();
        s_layers.clear();
        s_warnings.clear();

        if (!filePath)
        {
            SY_ERROR("[SvgParser] parseToIR: null filePath");
            return FioParseResult{};
        }

        try
        {
            NsvgInterpreter interpreter(s_entities, s_layers, s_warnings, m_importFillAsOutline);
            interpreter.parseFile(filePath);
            if (!interpreter.succeeded())
            {
                const std::string message =
                    s_warnings.empty() ? std::string("Failed to parse SVG file: ") + filePath : s_warnings.back();
                SY_ERRORF("[SvgParser] parseToIR: %s", message.c_str());
                return FioParseResult{};
            }
        }
        catch (const std::exception& ex)
        {
            SY_CRITICALF("[SvgParser] parseToIR exception: %s - %s", filePath, ex.what());
            return FioParseResult{};
        }
        catch (...)
        {
            SY_CRITICALF("[SvgParser] parseToIR unknown exception: %s", filePath);
            return FioParseResult{};
        }

        if (s_entities.empty())
        {
            SY_WARNF("[SvgParser] parseToIR: no entities produced: %s", filePath);
            return FioParseResult{};
        }

        // 填充 FioParseResult
        FioParseResult result;
        result.entities = s_entities.data();
        result.entityCount = static_cast<uint32_t>(s_entities.size());
        result.layers = s_layers.data();
        result.layerCount = static_cast<uint32_t>(s_layers.size());
        result.extensionBlob.data = s_extensionBlob.data();
        result.extensionBlob.size = s_extensionBlob.size();
        std::strncpy(result.sourceFormat, "SVG", sizeof(result.sourceFormat) - 1);
        result.warningCount = static_cast<uint32_t>(s_warnings.size());

        SY_INFOF(
            "[SvgParser] parseToIR END: %u entities, %u layers, %u warnings: %s", result.entityCount, result.layerCount, result.warningCount, filePath);
        return result;
    }

    FileFormat SvgParser::format() const
    {
        return FileFormat::SVG;
    }

    size_t SvgParser::formatName(char* buffer, size_t bufferSize) const
    {
        const char* name = "SVG";
        const size_t len = std::strlen(name);
        if (buffer != nullptr && bufferSize > len)
        {
            std::strcpy(buffer, name);
        }
        return len;
    }

    void SvgParser::forEachSupportedExtension(void (*visitor)(const char*, void*), void* ctx) const
    {
        visitor("svg", ctx);
        visitor("svgz", ctx);
    }
}  // namespace Fio