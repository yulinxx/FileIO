#include "FileIO/Parsers/SvgParser.h"
#include "FileIO/FileIOUtils.h"

#include "Log/SyLogger.h"

#include "Ut/Vec.h"

#include <cmath>

#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
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
        // nanosvg 使用 0xAABBGGRR 格式（ABGR）
        Ut::Vec3f extractSvgColor(const NSVGpaint& paint)
        {
            if (paint.type == NSVG_PAINT_COLOR)
            {
                unsigned int color = paint.color;
                // ABGR 格式：color & 0xFF = B, (>>8) & 0xFF = G, (>>16) & 0xFF = R
                float r = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
                float g = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
                float b = static_cast<float>(color & 0xFF) / 255.0f;
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
        static const char* kSvgInlineProps[] = { "fill",
            "stroke",
            "stroke-width",
            "fill-opacity",
            "stroke-opacity",
            "opacity",
            "stroke-linecap",
            "stroke-linejoin",
            "stroke-dasharray",
            "stroke-dashoffset",
            nullptr };

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
                if (found == 0 || tag[found - 1] == ' ' || tag[found - 1] == '\t' || tag[found - 1] == '\n' ||
                    tag[found - 1] == '\r' || tag[found - 1] == '<')
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
                    std::string sel =
                        trimWhitespace(selectors.substr(s, comma == std::string::npos ? std::string::npos : comma - s));
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
                        std::string one =
                            trimWhitespace(cls.substr(c, sp == std::string::npos ? std::string::npos : sp - c));
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
            bool importFillAsOutline,
            std::vector<uint8_t>& outBlob)
            : m_outEntities(outEntities)
            , m_outLayers(outLayers)
            , m_warnings(warnings)
            , m_importFillAsOutline(importFillAsOutline)
            , m_success(false)
            , m_outBlob(outBlob)
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
                    // 没有 stroke 时，优先用 fill 的颜色
                    shapeColor = extractSvgColor(shape->fill);
                    SY_INFOF("[SvgParser] shape stroke=none, fill.type=%d color=(%f,%f,%f)",
                        shape->fill.type, shapeColor.x(), shapeColor.y(), shapeColor.z());
                    // 如果 fill 也是 none，尝试用 stroke
                    if (shape->fill.type != NSVG_PAINT_COLOR && shape->stroke.type == NSVG_PAINT_COLOR)
                    {
                        shapeColor = extractSvgColor(shape->stroke);
                    }
                }
                else
                {
                    // 有 stroke 时，优先用 stroke 的颜色
                    shapeColor = extractSvgColor(shape->stroke);
                    SY_INFOF("[SvgParser] shape hasStroke stroke.type=%d color=(%f,%f,%f)",
                        shape->stroke.type, shapeColor.x(), shapeColor.y(), shapeColor.z());
                    // 如果 stroke 是 none，尝试用 fill
                    if (shape->stroke.type != NSVG_PAINT_COLOR && shape->fill.type == NSVG_PAINT_COLOR)
                    {
                        shapeColor = extractSvgColor(shape->fill);
                    }
                }

                // 图层按**颜色**归并，不按 shape id。
                // 原因：nanosvg 只把 <g id> 继承给无 id 的子 shape，Figma / SVGO 导出的文件
                // 每个元素都带唯一 id，按 id 分层会变成「1 个图形 = 1 个图层」，
                // 1000 个 shape 直接撞穿 LayerManager::kMaxLayerCount(1024)。
                // 激光加工的实际用法本来就是按颜色区分工艺，颜色才是有意义的分层维度。
                // 原始 id 降级写入 EntityInfo::name，源对象仍可追溯。
                uint32_t layerSourceId = getOrCreateLayer(shapeColor);

                // 一条 <path> 里可能有多个子路径（M ... M ...），nanosvg 把每个子路径拆成
                // 一个 NSVGpath。语义上「整条子路径 = 一个复合曲线实体」：选中就是整条，
                // 不再是选中其中一段贝塞尔。
                for (NSVGpath* svgPath = shape->paths; svgPath != nullptr; svgPath = svgPath->next)
                {
                    convertPathToCompositeEntity(svgPath, shapeColor, layerSourceId, shape->id);
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

        /// 复合曲线的段几何统一写在这里（EntityInfo 只记偏移与长度）
        std::vector<uint8_t>& m_outBlob;

        /// 单条 path 的段缓冲，逐路径复用容量，避免每条路径各分配一次
        std::vector<double> m_segBuf;


        /// ARGB 颜色 → 图层 sourceId。O(1) 查找，取代原来对 m_outLayers 的线性扫描
        /// （线性版在「每个 shape 一个图层」时是 O(shape²)）。
        std::unordered_map<uint32_t, uint32_t> m_layerByColor;
        bool m_layerLimitWarned = false;

        /// 图层数上限。与 Engine 侧 LayerManager::kMaxLayerCount(1024) 留足余量：
        /// 按颜色分层时 256 种已经远超任何真实激光工艺需求，
        /// 超出部分统一并入第一个图层，避免畸形文件（如逐像素渐变描边）撑爆图层表。
        static constexpr std::size_t kMaxSvgLayers = 256;

        // 按颜色查找或创建图层，返回图层 sourceId。
        // SVG 无显式图层定义；颜色是唯一有实际工艺含义的分层维度（激光按颜色区分功率/速度）。
        uint32_t getOrCreateLayer(const Ut::Vec3f& color)
        {
            const uint32_t argb = packSvgColor(color);

            const auto it = m_layerByColor.find(argb);
            if (it != m_layerByColor.end())
            {
                return it->second;
            }

            if (m_outLayers.size() >= kMaxSvgLayers)
            {
                // 并入第一个图层而不是返回 0：0 是「未分配」哨兵，会让图元落到下游默认图层，
                // 颜色信息虽然还在 EntityInfo::color 上，但图层归属会显得凭空消失
                if (!m_layerLimitWarned)
                {
                    m_layerLimitWarned = true;
                    m_warnings.push_back("SVG color count exceeds " + std::to_string(kMaxSvgLayers) +
                        ", extra colors are merged into the first layer");
                    SY_WARNF("[SvgParser] Color layer limit %zu reached, extra colors merged into layer 1",
                        kMaxSvgLayers);
                }
                return 1u;
            }

            IrLayerInfo layer;
            // 1-based sourceId：0 保留为「未分配图层」哨兵
            layer.sourceId = static_cast<uint32_t>(m_outLayers.size()) + 1;
            // 图层名用 #RRGGBB：导入后在图层面板里能直接看出这层是什么颜色
            char nameBuf[8] = {};
            std::snprintf(nameBuf, sizeof(nameBuf), "#%06X", static_cast<unsigned>(argb & 0x00FFFFFFu));
            std::strncpy(layer.name, nameBuf, sizeof(layer.name) - 1);
            layer.name[sizeof(layer.name) - 1] = '\0';
            layer.color = argb;
            layer.visible = true;
            m_outLayers.push_back(layer);
            m_layerByColor.emplace(argb, layer.sourceId);
            return layer.sourceId;
        }

        /// 把一条子路径（nanosvg 的一个 NSVGpath）聚合成**一个**实体。
        ///
        /// 旧实现是「每段三次贝塞尔一个实体」：一条 10 段的 path 产出 10 个图元，
        /// 语义上选不中整条路径，落地阶段还要为每段各走一遍 clone / R-tree insert /
        /// observer 通知，是导入慢的主要放大器。现在的规则：
        ///   - ≥2 段 → EntityType::SmartLine（复合曲线），几何写进扩展数据块；
        ///   - 单段 → 直接产出 Line 或 Bezier，不套复合曲线容器（少一层间接）。
        void convertPathToCompositeEntity(
            NSVGpath* svgPath, const Ut::Vec3f& shapeColor, uint32_t layerSourceId, const char* sourceName)
        {
            if (!svgPath || svgPath->npts < 4)
            {
                return;
            }

            const float* pts = svgPath->pts;
            const int npts = svgPath->npts;

            // 段缓冲复用（每条 path 清空但不释放容量），避免逐路径反复分配
            m_segBuf.clear();
            m_segBuf.reserve(static_cast<std::size_t>(npts / 3) * kSmartSegStride);

            // nanosvg pts 布局: [x0,y0, cpx1,cpy1, cpx2,cpy2, x1,y1, ...]
            // 每段三次贝塞尔 = 一个起点 + 两个控制点 + 一个终点（相对起点增加 3 个点）。
            // 注意：闭合路径时 nanosvg 已在 addPath 中补上一条回到起点的闭合段，
            // 所以闭合性由几何本身携带，不需要额外的闭合标记跨 DLL 传递。
            for (int i = 0; i + 3 < npts; i += 3)
            {
                // Y 轴翻转：SVG Y 向下 -> 系统 Y 向上
                Ut::Vec2d p0(pts[i * 2], -pts[i * 2 + 1]);
                Ut::Vec2d c1(pts[i * 2 + 2], -pts[i * 2 + 3]);
                Ut::Vec2d c2(pts[i * 2 + 4], -pts[i * 2 + 5]);
                Ut::Vec2d p1(pts[i * 2 + 6], -pts[i * 2 + 7]);

                // [F2-P0 修复] NaN/Inf 坐标会导致后续 static_cast<int>(NaN) 产生 UB，
                // 以及越界数组索引。此处对所有坐标做 finite 校验，非法点跳过并告警。
                if (!std::isfinite(p0.x()) || !std::isfinite(p0.y()) || !std::isfinite(c1.x()) ||
                    !std::isfinite(c1.y()) || !std::isfinite(c2.x()) || !std::isfinite(c2.y()) ||
                    !std::isfinite(p1.x()) || !std::isfinite(p1.y()))
                {
                    SY_WARNF("[SvgParser] NaN/Inf coordinate detected at path segment %d, skipping", i);
                    continue;
                }

                // 跳过退化段：所有点几乎重合（零长度/退化贝塞尔）
                double extent = std::max({ std::fabs(p0.x() - c1.x()),
                    std::fabs(p0.y() - c1.y()),
                    std::fabs(p0.x() - c2.x()),
                    std::fabs(p0.y() - c2.y()),
                    std::fabs(p0.x() - p1.x()),
                    std::fabs(p0.y() - p1.y()) });
                if (extent < 1e-9)
                {
                    continue;
                }

                appendSegment(p0, c1, c2, p1);
            }

            if (m_segBuf.empty())
            {
                return;
            }

            EntityInfo info{};
            info.sourceId = static_cast<uint64_t>(m_outEntities.size());
            info.layerSourceId = layerSourceId;
            info.visible = true;
            info.color = packSvgColor(shapeColor);
            // 源 SVG 的 id（nanosvg 会把 <g id> 继承给无 id 的子 shape）。
            // 不当图层名用，但保留下来：出问题时能把画布上的实体对回 SVG 里的元素。
            if (sourceName != nullptr && sourceName[0] != '\0')
            {
                std::strncpy(info.name, sourceName, sizeof(info.name) - 1);
                info.name[sizeof(info.name) - 1] = '\0';
            }

            const uint32_t segCount = static_cast<uint32_t>(m_segBuf.size() / kSmartSegStride);
            if (segCount == 1)
            {
                fillSingleSegment(info, m_segBuf.data());
                m_outEntities.push_back(info);
                return;
            }

            const std::size_t bytes = m_segBuf.size() * sizeof(double);
            // extensionDataOffset/Size 都是 uint32_t，越界就无法表达。
            // 这里丢弃该路径而不是写入截断偏移——截断偏移会指向别的实体的数据（静默错乱）。
            if (m_outBlob.size() + bytes > 0xFFFFFFFFull)
            {
                m_warnings.push_back("SVG path dropped: extension blob would exceed 4 GiB");
                SY_ERRORF("[SvgParser] Extension blob overflow at %zu bytes, path dropped", m_outBlob.size());
                return;
            }

            info.type = EntityType::SmartLine;
            info.vertexCount = segCount;
            // 起点 = 第一段的 p0（段记录里下标 1、2 就是 p0.x / p0.y）
            info.line.x1 = m_segBuf[1];
            info.line.y1 = m_segBuf[2];
            info.extensionDataOffset = static_cast<uint32_t>(m_outBlob.size());
            info.extensionDataSize = static_cast<uint32_t>(bytes);

            const auto* raw = reinterpret_cast<const uint8_t*>(m_segBuf.data());
            m_outBlob.insert(m_outBlob.end(), raw, raw + bytes);
            m_outEntities.push_back(info);
        }

        /// 追加一段到段缓冲，按 kSmartSegStride 定长排布：[标签][p0][p1][p2][p3]
        void appendSegment(const Ut::Vec2d& p0, const Ut::Vec2d& c1, const Ut::Vec2d& c2, const Ut::Vec2d& p1)
        {
            // nanosvg 把直线也升阶成三次贝塞尔（nsvg__lineTo 把两个控制点放在 1/3、2/3 处），
            // 识别回直线是有实际收益的：渲染侧不必再细分这一段。
            // 折线型 SVG（多边形、矩形轮廓）几乎全部命中，顶点量能少一个量级。
            if (isStraightCubic(p0, c1, c2, p1))
            {
                m_segBuf.push_back(static_cast<double>(SmartSegKind::Line));
                m_segBuf.push_back(p0.x());
                m_segBuf.push_back(p0.y());
                m_segBuf.push_back(p1.x());
                m_segBuf.push_back(p1.y());
                m_segBuf.push_back(0.0);
                m_segBuf.push_back(0.0);
                m_segBuf.push_back(0.0);
                m_segBuf.push_back(0.0);
                return;
            }

            m_segBuf.push_back(static_cast<double>(SmartSegKind::Bezier));
            m_segBuf.push_back(p0.x());
            m_segBuf.push_back(p0.y());
            m_segBuf.push_back(c1.x());
            m_segBuf.push_back(c1.y());
            m_segBuf.push_back(c2.x());
            m_segBuf.push_back(c2.y());
            m_segBuf.push_back(p1.x());
            m_segBuf.push_back(p1.y());
        }

        /// 单段路径：不套复合曲线容器，直接填成 Line / Bezier
        static void fillSingleSegment(EntityInfo& info, const double* seg)
        {
            const auto kind = static_cast<SmartSegKind>(static_cast<int>(seg[0]));
            if (kind == SmartSegKind::Line)
            {
                info.type = EntityType::Line;
                info.line.x1 = seg[1];
                info.line.y1 = seg[2];
                info.line.x2 = seg[3];
                info.line.y2 = seg[4];
                return;
            }

            info.type = EntityType::Bezier;
            // 转换层约定：Bezier 的起点借用 line.x1/y1
            info.line.x1 = seg[1];
            info.line.y1 = seg[2];
            info.bezier.c0x = seg[3];
            info.bezier.c0y = seg[4];
            info.bezier.c1x = seg[5];
            info.bezier.c1y = seg[6];
            info.bezier.ex = seg[7];
            info.bezier.ey = seg[8];
        }

        /// 判断一段三次贝塞尔是否等价于直线：两个控制点都落在 p0→p1 线段上
        static bool isStraightCubic(
            const Ut::Vec2d& p0, const Ut::Vec2d& c1, const Ut::Vec2d& c2, const Ut::Vec2d& p1)
        {
            const double dx = p1.x() - p0.x();
            const double dy = p1.y() - p0.y();
            const double len2 = dx * dx + dy * dy;
            if (len2 < 1e-24)
            {
                // 首尾重合：这是个环形段，压成零长直线会丢掉整段几何，交给贝塞尔分支
                return false;
            }
            const double len = std::sqrt(len2);
            const double tol = 1e-6 * len;  // 相对容差：跟着图纸尺度缩放

            // 点到直线距离 = |叉积| / |方向|
            const double d1 = std::fabs((c1.x() - p0.x()) * dy - (c1.y() - p0.y()) * dx) / len;
            const double d2 = std::fabs((c2.x() - p0.x()) * dy - (c2.y() - p0.y()) * dx) / len;
            if (d1 > tol || d2 > tol)
            {
                return false;
            }

            // 还要求控制点在 [p0,p1] 之间：共线但落在延长线上的控制点会让曲线折返，
            // 那不是直线（会先冲出去再回来）
            const double t1 = ((c1.x() - p0.x()) * dx + (c1.y() - p0.y()) * dy) / len2;
            const double t2 = ((c2.x() - p0.x()) * dx + (c2.y() - p0.y()) * dy) / len2;
            return t1 >= -1e-9 && t1 <= 1.0 + 1e-9 && t2 >= -1e-9 && t2 <= 1.0 + 1e-9;
        }
    };

    // ========================================================================
    // SvgParser::parseToIR() — 中立 IR 解析路径
    // SVG 子路径 → 一个实体（多段为 SmartLine 复合曲线，单段为 Line/Bezier），
    // 不离散为折线；不依赖 Engine2D 类型，跨 DLL 安全
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
            NsvgInterpreter interpreter(s_entities, s_layers, s_warnings, m_importFillAsOutline, s_extensionBlob);
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

        SY_INFOF("[SvgParser] parseToIR END: %u entities, %u layers, %u warnings: %s",
            result.entityCount,
            result.layerCount,
            result.warningCount,
            filePath);
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