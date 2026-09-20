/**
 * @file SvgParser.cpp
 * @brief SVG 格式解析器实现
 */
#include "FileIO/Parsers/SvgParser.h"
#include "FileIOUtils.h"
#include "FileIO/ImageUtils.h"
#include "IrProjector.h"

#include "Log/SyLogger.h"

#include "Ut/Vec.h"

#include <cmath>

#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <array>
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
        // nanosvg 使用 NSVG_RGB 格式: r | (g<<8) | (b<<16)，即低字节是 R、高字节是 B
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

        // ===== SVG CSS <style> 类样式内联（优化版：单次遍历 + string_view 避免拷贝）=====
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

        // 使用无序集合做 O(1) 属性名查找
        static const std::unordered_set<std::string_view> kInlinePropSet(kSvgInlineProps, kSvgInlineProps + 10);

        inline bool isSvgInlineProp(std::string_view name)
        {
            return kInlinePropSet.find(name) != kInlinePropSet.end();
        }

        inline std::string_view trimWhitespace(std::string_view s)
        {
            size_t a = 0, b = s.size();
            while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
                ++a;
            while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
                --b;
            return s.substr(a, b - a);
        }

        // 单次扫描标签提取所有属性：返回 {name -> value} map（string_view 指向原 tag 缓冲）
        static void extractTagAttrs(
            std::string_view tag, std::vector<std::pair<std::string_view, std::string_view>>& outAttrs)
        {
            outAttrs.clear();
            size_t p = 0;
            const size_t n = tag.size();
            while (p < n)
            {
                // 跳过空白
                while (p < n && std::isspace(static_cast<unsigned char>(tag[p])))
                    ++p;
                if (p >= n)
                    break;
                if (tag[p] == '>')
                    break;  // 标签结束
                if (tag[p] == '/')
                {
                    // 传入的 tag 已去掉 '<' 和 '>'，末尾的 '/' 即为自闭合标记
                    if (p + 1 >= n || tag[p + 1] == '>')
                        break;
                    ++p;  // 非末尾的 '/'：跳过，避免 p 不前进导致死循环
                    continue;
                }

                size_t nameStart = p;
                while (p < n && !std::isspace(static_cast<unsigned char>(tag[p])) && tag[p] != '=' && tag[p] != '>' &&
                    tag[p] != '/')
                    ++p;
                std::string_view name = tag.substr(nameStart, p - nameStart);

                // 跳到 =
                while (p < n && std::isspace(static_cast<unsigned char>(tag[p])))
                    ++p;
                if (p >= n || tag[p] != '=')
                    continue;
                ++p;
                while (p < n && std::isspace(static_cast<unsigned char>(tag[p])))
                    ++p;
                if (p >= n)
                    break;
                char quote = tag[p];
                if (quote != '"' && quote != '\'')
                    continue;
                ++p;
                size_t valStart = p;
                while (p < n && tag[p] != quote)
                    ++p;
                std::string_view value = tag.substr(valStart, p - valStart);
                if (p < n)
                    ++p;  // 跳过结束引号

                outAttrs.emplace_back(name, value);
            }
        }

        // 检查标签是否已有某属性
        inline bool hasAttr(
            const std::vector<std::pair<std::string_view, std::string_view>>& attrs, std::string_view name)
        {
            for (const auto& kv : attrs)
                if (kv.first == name)
                    return true;
            return false;
        }

        // 解析 <style> 文本中的 .class 规则 → class 名 → (属性 → 值)
        // 使用 unordered_map + string 存储值（需拥有所有权）
        static std::unordered_map<std::string, std::unordered_map<std::string, std::string>> parseCssRules(
            std::string_view css)
        {
            std::unordered_map<std::string, std::unordered_map<std::string, std::string>> rules;
            size_t i = 0;
            const size_t n = css.size();
            while (i < n)
            {
                size_t brace = css.find('{', i);
                if (brace == std::string_view::npos)
                    break;
                size_t close = css.find('}', brace);
                if (close == std::string_view::npos)
                    break;

                std::string_view selectors = css.substr(i, brace - i);
                std::string_view body = css.substr(brace + 1, close - brace - 1);

                // 解析声明：prop: value;
                std::unordered_map<std::string, std::string> decls;
                size_t pos = 0;
                const size_t pb = body.size();
                while (pos < pb)
                {
                    size_t colon = body.find(':', pos);
                    if (colon == std::string_view::npos)
                        break;
                    size_t semi = body.find(';', colon);
                    if (semi == std::string_view::npos)
                        semi = pb;
                    std::string key = std::string(trimWhitespace(body.substr(pos, colon - pos)));
                    std::string val = std::string(trimWhitespace(body.substr(colon + 1, semi - colon - 1)));
                    if (!key.empty() && !val.empty() && isSvgInlineProp(key))
                    {
                        decls.emplace(std::move(key), std::move(val));
                    }
                    pos = semi + 1;
                }

                // 多个选择器（逗号分隔），只处理 .class
                size_t s = 0;
                while (s < selectors.size())
                {
                    size_t comma = selectors.find(',', s);
                    std::string_view sel = trimWhitespace(
                        selectors.substr(s, comma == std::string_view::npos ? std::string_view::npos : comma - s));
                    if (!sel.empty() && sel[0] == '.')
                    {
                        std::string cls(sel.substr(1));
                        auto& target = rules[cls];
                        for (const auto& kv : decls)
                        {
                            target.emplace(kv.first, kv.second);
                        }
                    }
                    if (comma == std::string_view::npos)
                        break;
                    s = comma + 1;
                }
                i = close + 1;
            }
            return rules;
        }

        // 将 <style> 中的 class 样式内联到引用它的元素上（元素自身属性优先，不被覆盖）
        // 单次遍历 SVG 字符串，用 string_view 避免拷贝，仅在需要插入属性时构建新字符串
        static std::string inlineSvgCss(const std::string& svg)
        {
            // 1) 收集所有 <style> 块内容（使用 string_view 避免拷贝）
            std::string css;
            css.reserve(svg.size() / 10);  // 估算
            size_t pos = 0;
            while (true)
            {
                size_t open = svg.find("<style", pos);
                if (open == std::string::npos)
                    break;
                size_t tagEnd = svg.find('>', open);
                if (tagEnd == std::string::npos)
                    break;
                size_t close = svg.find("</style>", tagEnd);
                if (close == std::string::npos)
                    break;
                css += svg.substr(tagEnd + 1, close - tagEnd - 1);
                pos = close + 8;
            }
            if (css.empty())
                return svg;

            auto rules = parseCssRules(css);
            if (rules.empty())
                return svg;

            // 2) 单次遍历处理元素标签
            std::string out;
            out.reserve(svg.size() + rules.size() * 32);  // 预留插入空间
            size_t i = 0;
            const size_t n = svg.size();

            // 复用属性提取缓冲，避免逐标签分配
            std::vector<std::pair<std::string_view, std::string_view>> tagAttrs;
            tagAttrs.reserve(16);

            while (i < n)
            {
                size_t lt = svg.find('<', i);
                if (lt == std::string::npos)
                {
                    out.append(svg.data() + i, n - i);
                    break;
                }
                out.append(svg.data() + i, lt - i);

                // 闭合/声明/注释标签：原样复制
                if (lt + 1 < n && (svg[lt + 1] == '/' || svg[lt + 1] == '?' || svg[lt + 1] == '!'))
                {
                    size_t gt = svg.find('>', lt);
                    if (gt == std::string::npos)
                    {
                        out.append(svg.data() + lt, n - lt);
                        break;
                    }
                    out.append(svg.data() + lt, gt - lt + 1);
                    i = gt + 1;
                    continue;
                }

                size_t gt = svg.find('>', lt);
                if (gt == std::string::npos)
                {
                    out.append(svg.data() + lt, n - lt);
                    break;
                }

                std::string_view tag(svg.data() + lt, gt - lt + 1);

                // 提取 class 属性
                extractTagAttrs(tag.substr(1, tag.size() - 2), tagAttrs);  // 去掉 < >

                std::string_view classAttr;
                for (const auto& kv : tagAttrs)
                {
                    if (kv.first == "class")
                    {
                        classAttr = kv.second;
                        break;
                    }
                }

                if (!classAttr.empty())
                {
                    // 合并 class 列表中的样式（后者覆盖前者）
                    std::unordered_map<std::string, std::string> merged;
                    size_t c = 0;
                    while (c < classAttr.size())
                    {
                        size_t sp = classAttr.find_first_of(" \t\r\n", c);
                        std::string_view one = trimWhitespace(
                            classAttr.substr(c, sp == std::string_view::npos ? std::string_view::npos : sp - c));
                        if (!one.empty())
                        {
                            auto it = rules.find(std::string(one));
                            if (it != rules.end())
                            {
                                for (const auto& kv : it->second)
                                {
                                    merged.emplace(kv.first, kv.second);
                                }
                            }
                        }
                        if (sp == std::string_view::npos)
                            break;
                        c = sp + 1;
                    }

                    if (!merged.empty())
                    {
                        // 检查已有属性，构建插入字符串
                        std::string insertion;
                        for (const auto& kv : merged)
                        {
                            if (!hasAttr(tagAttrs, kv.first))
                            {
                                insertion += ' ';
                                insertion += kv.first;
                                insertion += "=\"";
                                insertion += kv.second;
                                insertion += '"';
                            }
                        }
                        if (!insertion.empty())
                        {
                            // 在 '>' 或 '/>' 前插入
                            size_t insertAt = tag.size() - 1;  // 指向 '>'
                            if (tag.size() >= 2 && tag[tag.size() - 2] == '/')
                                insertAt = tag.size() - 2;  // '/>' 前

                            out.append(tag.data(), insertAt);
                            out += insertion;
                            out.append(tag.data() + insertAt, tag.size() - insertAt);
                            i = gt + 1;
                            continue;
                        }
                    }
                }

                out.append(tag);
                i = gt + 1;
            }
            return out;
        }

        // 点到线段距离（用于曲线扁平度估计）
        // 注：保留贝塞尔曲线后不再需要把曲线离散为折线，此函数及 computeAdaptiveSegments 已移除。

        // ========================================================================
        // <text> / <image> 元素辅助工具（nanosvg 不解析这两个元素，需自行抽取）
        // ========================================================================

        std::string_view attrValue(const std::vector<std::pair<std::string_view, std::string_view>>& attrs,
            const std::string_view key)
        {
            for (const auto& kv : attrs)
            {
                if (kv.first == key)
                {
                    return kv.second;
                }
            }
            return {};
        }

        bool attrHas(const std::vector<std::pair<std::string_view, std::string_view>>& attrs,
            const std::string_view key)
        {
            for (const auto& kv : attrs)
            {
                if (kv.first == key)
                {
                    return true;
                }
            }
            return false;
        }

        // 从属性值里解析首个子串为 double（属性名已知时用 attrValue + 本函数）。
        double firstNumber(std::string_view s, double def)
        {
            s = trimWhitespace(s);
            if (s.empty())
            {
                return def;
            }
            char* end = nullptr;
            const double v = std::strtod(s.data(), &end);
            if (end == s.data())
            {
                return def;
            }
            return v;
        }

        // 把 "rotate(deg)" 或 "rotate(deg cx cy)" 里的旋转角（度）解析出来。
        bool rotateAngleDeg(std::string_view transform, double& outDeg)
        {
            const auto pos = transform.find("rotate(");
            if (pos == std::string_view::npos)
            {
                return false;
            }
            const std::string_view sub = transform.substr(pos + 7);
            outDeg = firstNumber(sub, 0.0);
            return true;
        }

        // 解析 CSS 颜色：支持 #rrggbb / #rgb / rgb(r,g,b)。不支持/非法返回 false。
        bool cssColorToVec3(const std::string_view sIn, Ut::Vec3f& out)
        {
            std::string_view s = trimWhitespace(sIn);
            if (s.empty() || s == "none")
            {
                return false;
            }
            if (s[0] == '#')
            {
                s = s.substr(1);
                if (s.size() == 3)
                {
                    auto hex = [](char c) -> int {
                        if (c >= '0' && c <= '9')
                            return c - '0';
                        if (c >= 'a' && c <= 'f')
                            return c - 'a' + 10;
                        if (c >= 'A' && c <= 'F')
                            return c - 'A' + 10;
                        return -1;
                    };
                    const int r0 = hex(s[0]), g0 = hex(s[1]), b0 = hex(s[2]);
                    if (r0 < 0 || g0 < 0 || b0 < 0)
                    {
                        return false;
                    }
                    const int r = r0 * 17, g = g0 * 17, b = b0 * 17;
                    out = Ut::Vec3f(r / 255.0f, g / 255.0f, b / 255.0f);
                    return true;
                }
                if (s.size() == 6)
                {
                    auto hex2 = [](const char* p) -> int {
                        int v = 0;
                        for (int k = 0; k < 2; ++k)
                        {
                            v <<= 4;
                            const char c = p[k];
                            if (c >= '0' && c <= '9')
                                v |= (c - '0');
                            else if (c >= 'a' && c <= 'f')
                                v |= (c - 'a' + 10);
                            else if (c >= 'A' && c <= 'F')
                                v |= (c - 'A' + 10);
                            else
                                return -1;
                        }
                        return v;
                    };
                    const int r = hex2(s.data()), g = hex2(s.data() + 2), b = hex2(s.data() + 4);
                    if (r < 0 || g < 0 || b < 0)
                    {
                        return false;
                    }
                    out = Ut::Vec3f(r / 255.0f, g / 255.0f, b / 255.0f);
                    return true;
                }
                return false;
            }
            // rgb( r , g , b )
            if (s.rfind("rgb(", 0) == 0 && s.back() == ')')
            {
                const std::string_view inner = s.substr(4, s.size() - 5);
                double vals[3] = { 0.0, 0.0, 0.0 };
                int got = 0;
                size_t i = 0;
                while (got < 3 && i < inner.size())
                {
                    while (i < inner.size() && (inner[i] == ',' || inner[i] == ' ' || inner[i] == '\t'))
                    {
                        ++i;
                    }
                    const double v = firstNumber(inner.substr(i), -1.0);
                    if (v < 0)
                    {
                        break;
                    }
                    vals[got++] = v;
                    // 跳过被 firstNumber 消费的数字字符，回到分隔符
                    while (i < inner.size() && (inner[i] != ',' && inner[i] != ' ' && inner[i] != '\t'))
                    {
                        ++i;
                    }
                }
                if (got != 3)
                {
                    return false;
                }
                out = Ut::Vec3f(static_cast<float>(vals[0]) / 255.0f,
                    static_cast<float>(vals[1]) / 255.0f,
                    static_cast<float>(vals[2]) / 255.0f);
                return true;
            }
            return false;
        }

        // 简易 base64 解码（标准字母表，忽略空白）。失败返回 false。
        bool base64Decode(const std::string_view in, std::vector<unsigned char>& out)
        {
            static const std::array<signed char, 256> kDec = [] {
                std::array<signed char, 256> d{};
                d.fill(static_cast<signed char>(-1));
                const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                for (int i = 0; i < 64; ++i)
                {
                    d[static_cast<unsigned char>(table[i])] = static_cast<signed char>(i);
                }
                return d;
            }();
            out.clear();
            unsigned int acc = 0;
            int nBits = 0;
            for (char c : in)
            {
                if (c == '\n' || c == '\r' || c == ' ' || c == '\t')
                {
                    continue;
                }
                const signed char v = kDec[static_cast<unsigned char>(c)];
                if (v < 0)
                {
                    // 末尾可能带 '=' 填充；其余非法字符停止
                    break;
                }
                acc = (acc << 6) | static_cast<unsigned int>(v);
                nBits += 6;
                if (nBits >= 8)
                {
                    nBits -= 8;
                    out.push_back(static_cast<unsigned char>((acc >> nBits) & 0xFF));
                }
            }
            return !out.empty();
        }

        // 在编辑中：解码 XML/HTML 常见实体（&amp; &lt; &gt; &quot; &#39; 及数字/十六进制）。
        void htmlEntityDecodeInplace(std::string& s)
        {
            const auto amp = s.find('&');
            if (amp == std::string::npos)
            {
                return;
            }
            std::string out;
            out.reserve(s.size());
            size_t i = 0;
            while (i < s.size())
            {
                if (s[i] != '&')
                {
                    out.push_back(s[i]);
                    ++i;
                    continue;
                }
                const size_t semi = s.find(';', i);
                if (semi == std::string::npos || semi - i > 10)
                {
                    out.push_back(s[i]);
                    ++i;
                    continue;
                }
                const std::string_view ent(s.data() + i, semi - i + 1);
                auto decodeOne = [](const std::string_view e) -> char {
                    if (e == "&amp;")
                        return '&';
                    if (e == "&lt;")
                        return '<';
                    if (e == "&gt;")
                        return '>';
                    if (e == "&quot;")
                        return '"';
                    if (e == "&apos;")
                        return '\'';
                    if (e == "&#39;")
                        return '\'';
                    if (e.size() > 3 && e[1] == '#')
                    {
                        const bool hex = (e[2] == 'x' || e[2] == 'X');
                        std::string_view num = e.substr(hex ? 3 : 2, e.size() - (hex ? 4 : 3));
                        unsigned long v = 0;
                        for (char d : num)
                        {
                            int dv = -1;
                            if (d >= '0' && d <= '9')
                                dv = d - '0';
                            else if (hex && d >= 'a' && d <= 'f')
                                dv = d - 'a' + 10;
                            else if (hex && d >= 'A' && d <= 'F')
                                dv = d - 'A' + 10;
                            if (dv < 0)
                                return 0;
                            v = v * (hex ? 16u : 10u) + static_cast<unsigned long>(dv);
                        }
                        return v <= 0x7F ? static_cast<char>(v) : static_cast<char>(0);
                    }
                    return 0;
                };
                const char decoded = decodeOne(ent);
                if (decoded)
                {
                    out.push_back(decoded);
                    i = semi + 1;
                    continue;
                }
                out.push_back('&');
                ++i;
            }
            s = std::move(out);
        }

        // 渐变做近似纯色：对全部 stop 的 RGB 求平均（nanosvg stop.color 为 0xAARRGGBB，
        // 低 24 位与 NSVG_RGB 一致：低字节 R、次低 G、高 B，与 extractSvgColor 同构）。
        Ut::Vec3f approxGradientColor(const NSVGgradient* gradient)
        {
            double r = 0.0, g = 0.0, b = 0.0;
            int n = 0;
            if (gradient != nullptr)
            {
                for (int i = 0; i < gradient->nstops; ++i)
                {
                    const unsigned int c = gradient->stops[i].color;
                    r += static_cast<double>(c & 0xFF);
                    g += static_cast<double>((c >> 8) & 0xFF);
                    b += static_cast<double>((c >> 16) & 0xFF);
                    ++n;
                }
            }
            if (n <= 0)
            {
                return Ut::Vec3f(0.0f, 0.0f, 0.0f);
            }
            r /= 255.0 * n;
            g /= 255.0 * n;
            b /= 255.0 * n;
            return Ut::Vec3f(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b));
        }
    }  // namespace

    class NsvgInterpreter
    {
    public:
        NsvgInterpreter(std::vector<EntityInfo>& outEntities,
            std::vector<IrLayerInfo>& outLayers,
            std::vector<std::string>& warnings,
            bool importFillAsOutline,
            std::vector<uint8_t>& outBlob,
            ParseProgressCallback onProgress = nullptr,
            void* progressCtx = nullptr)
            : m_outEntities(outEntities)
            , m_outLayers(outLayers)
            , m_warnings(warnings)
            , m_importFillAsOutline(importFillAsOutline)
            , m_success(false)
            , m_onProgress(onProgress)
            , m_progressCtx(progressCtx)
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

            size_t shapeCount = 0;
            size_t strokeCount = 0;
            size_t fillOnlyCount = 0;

            // 进度：shape 迭代即主要工作量。先数一遍总数作为分母，再逐个上报已处理比例。
            size_t totalShapes = 0;
            for (NSVGshape* s = image->shapes; s != nullptr; s = s->next)
            {
                ++totalShapes;
            }
            size_t processedShapes = 0;

            for (NSVGshape* shape = image->shapes; shape != nullptr; shape = shape->next)
            {
                ++processedShapes;
                if (m_onProgress && totalShapes > 0)
                {
                    m_onProgress(
                        static_cast<float>(static_cast<double>(processedShapes) / static_cast<double>(totalShapes)),
                        m_progressCtx);
                }

                bool visible = (shape->flags & NSVG_FLAGS_VISIBLE) != 0;
                bool hasStroke = shape->stroke.type != NSVG_PAINT_NONE;

                if (!visible)
                {
                    continue;
                }

                ++shapeCount;

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
                    shapeColor = resolvePaintColor(shape->fill);
                    ++fillOnlyCount;
                    // 如果 fill 也是 none，尝试用 stroke
                    if (shape->fill.type != NSVG_PAINT_COLOR && shape->stroke.type == NSVG_PAINT_COLOR)
                    {
                        shapeColor = resolvePaintColor(shape->stroke);
                    }
                }
                else
                {
                    // 有 stroke 时，优先用 stroke 的颜色
                    shapeColor = resolvePaintColor(shape->stroke);
                    ++strokeCount;
                    // 如果 stroke 是 none，尝试用 fill
                    if (shape->stroke.type != NSVG_PAINT_COLOR && shape->fill.type == NSVG_PAINT_COLOR)
                    {
                        shapeColor = resolvePaintColor(shape->fill);
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
                // 一个 NSVGpath。语义上「整条子路径 = 一个复合曲线图元」：选中就是整条，
                // 不再是选中其中一段贝塞尔。
                for (NSVGpath* svgPath = shape->paths; svgPath != nullptr; svgPath = svgPath->next)
                {
                    convertPathToCompositeEntity(svgPath, shapeColor, layerSourceId, shape->id, shape->strokeWidth);
                }
            }

            SY_DEBUGF("[SvgParser] Parsed %zu visible shapes: %zu stroked, %zu fill-only",
                shapeCount,
                strokeCount,
                fillOnlyCount);

            // nanosvg 会静默丢弃 <text> 与 <image>，这里从原始（CSS 已内联的）SVG 重新抽取
            parsePendantElements(svgData.data());

            if (m_imageSkipped > 0)
            {
                m_warnings.push_back("SVG: " + std::to_string(m_imageSkipped) +
                    " <image> element(s) skipped (no embedded raster data or unsupported format)");
            }

            if (m_onProgress)
            {
                m_onProgress(1.0f, m_progressCtx);
            }

            m_success = !m_outEntities.empty();
        }

    private:
        std::vector<EntityInfo>& m_outEntities;
        std::vector<IrLayerInfo>& m_outLayers;
        std::vector<std::string>& m_warnings;
        bool m_importFillAsOutline;
        bool m_success;

        /// 解析进度回调与上下文（可空）；在 parseFile 的 shape 循环里按比例上报
        ParseProgressCallback m_onProgress = nullptr;
        void* m_progressCtx = nullptr;

        /// 复合曲线的段几何统一写在这里（EntityInfo 只记偏移与长度）
        std::vector<uint8_t>& m_outBlob;

        /// 单条 path 的段缓冲，逐路径复用容量，避免每条路径各分配一次
        std::vector<double> m_segBuf;

        /// ARGB 颜色 → 图层 sourceId。O(1) 查找，取代原来对 m_outLayers 的线性扫描
        /// （线性版在「每个 shape 一个图层」时是 O(shape²)）。
        std::unordered_map<uint32_t, uint32_t> m_layerByColor;
        bool m_layerLimitWarned = false;

        /// 渐变近似纯色只告警一次，避免逐图元刷屏
        bool m_gradientWarned = false;

        /// <image> 因解码失败/无内嵌数据而被跳过的次数（解析结束时统一汇总告警一次）
        int m_imageSkipped = 0;

        // ========================================================================
        // <text> / <image> 抽取（nanosvg 不解析这两个元素）
        // ========================================================================

        /// 遍历原始（CSS 已内联的）SVG 文本，处理所有 <text>/<image> 元素。
        void parsePendantElements(const char* data)
        {
            if (!data)
            {
                return;
            }
            auto isTag = [](const char* name, const char* tag) -> bool {
                const size_t n = std::strlen(tag);
                if (std::strncmp(name, tag, n) != 0)
                {
                    return false;
                }
                const unsigned char c = static_cast<unsigned char>(name[n]);
                return c == '\0' || std::isspace(c) || c == '>' || c == '/';
            };

            const char* p = data;
            while (*p)
            {
                const char* lt = std::strchr(p, '<');
                if (!lt)
                {
                    break;
                }
                if (std::strncmp(lt, "<!--", 4) == 0)
                {
                    const char* e = std::strstr(lt + 4, "-->");
                    p = e ? e + 3 : lt + 4;
                    continue;
                }
                const char* name = lt + 1;
                if (isTag(name, "text"))
                {
                    const char* gt = std::strchr(lt, '>');
                    if (!gt)
                    {
                        break;
                    }
                    p = parseTextElement(lt, gt);
                }
                else if (isTag(name, "image"))
                {
                    const char* gt = std::strchr(lt, '>');
                    if (!gt)
                    {
                        break;
                    }
                    p = parseImageElement(lt, gt);
                }
                else
                {
                    const char* gt = std::strchr(lt, '>');
                    if (!gt)
                    {
                        break;
                    }
                    p = gt + 1;
                }
            }
        }

        /// 解析 <text ...>…</text>，产出 EntityType::Text（对齐现有 Text 图元逻辑）。
        /// 返回跳过 </text> 之后的位置。
        const char* parseTextElement(const char* openTag, const char* gt)
        {
            std::string tag(openTag + 1, gt);
            std::vector<std::pair<std::string_view, std::string_view>> attrs;
            extractTagAttrs(tag, attrs);

            const char* contentStart = gt + 1;
            const char* close = std::strstr(contentStart, "</text");
            const char* contentEnd = close ? close : contentStart;

            // 收集文本内容，剥掉内部的 <tspan>/<comment> 等标签
            std::string textContent;
            textContent.reserve(static_cast<size_t>(contentEnd - contentStart) + 8);
            for (const char* q = contentStart; q < contentEnd;)
            {
                unsigned char c = static_cast<unsigned char>(*q);
                if (c != '<')
                {
                    textContent.push_back(static_cast<char>(c));
                    ++q;
                    continue;
                }
                if (std::strncmp(q, "<!--", 4) == 0)
                {
                    const char* e = std::strstr(q + 4, "-->");
                    if (!e || e >= contentEnd)
                    {
                        break;
                    }
                    q = e + 3;
                    continue;
                }
                const char* tgt = std::strchr(q, '>');
                if (!tgt || tgt >= contentEnd)
                {
                    break;
                }
                q = tgt + 1;
            }
            htmlEntityDecodeInplace(textContent);

            const char* afterClose = contentStart;
            if (close)
            {
                const char* ce = std::strchr(close, '>');
                afterClose = ce ? ce + 1 : close;
            }

            addTextEntity(attrs, std::move(textContent));
            return afterClose;
        }

        /// 把 <text> 构造成一个 EntityInfo（EntityType::Text）。
        void addTextEntity(
            const std::vector<std::pair<std::string_view, std::string_view>>& attrs, std::string content)
        {
            // 规整文本：去首尾空白、压缩连续空白为单个空格
            {
                const size_t b = content.find_first_not_of(" \t\r\n");
                if (b == std::string::npos)
                {
                    return;
                }
                const size_t e = content.find_last_not_of(" \t\r\n");
                std::string cleaned;
                cleaned.reserve(e - b + 1);
                bool lastSpace = false;
                for (size_t i = b; i <= e; ++i)
                {
                    const char c = content[i];
                    if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
                    {
                        if (!lastSpace)
                        {
                            cleaned.push_back(' ');
                            lastSpace = true;
                        }
                    }
                    else
                    {
                        cleaned.push_back(c);
                        lastSpace = false;
                    }
                }
                content = std::move(cleaned);
                if (content.empty())
                {
                    return;
                }
            }

            // SVGO/Illustrator 常把 fill/font-size 等放在 style 里，做一个小解析器兜底
            std::string_view style;
            if (attrHas(attrs, "style"))
            {
                style = attrValue(attrs, "style");
            }
            auto styleVal = [&style](std::string_view key) -> std::string_view {
                if (style.empty())
                {
                    return {};
                }
                size_t pos = 0;
                while (pos <= style.size())
                {
                    const size_t semi = style.find(';', pos);
                    const size_t pieceEnd = (semi == std::string_view::npos) ? style.size() : semi;
                    const std::string_view piece = style.substr(pos, pieceEnd - pos);
                    const size_t colon = piece.find(':');
                    if (colon != std::string_view::npos)
                    {
                        const std::string_view k = trimWhitespace(piece.substr(0, colon));
                        if (k == key)
                        {
                            return trimWhitespace(piece.substr(colon + 1));
                        }
                    }
                    if (semi == std::string_view::npos)
                    {
                        break;
                    }
                    pos = semi + 1;
                }
                return {};
            };

            const double x = attrHas(attrs, "x") ? firstNumber(attrValue(attrs, "x"), 0.0) : 0.0;
            const double y = attrHas(attrs, "y") ? firstNumber(attrValue(attrs, "y"), 0.0) : 0.0;

            // SVG 默认 font-size = medium = 16px（与应用其它文本高度语义一致）
            double fontSize = 16.0;
            if (attrHas(attrs, "font-size"))
            {
                fontSize = firstNumber(attrValue(attrs, "font-size"), fontSize);
            }
            else
            {
                const auto sv = styleVal("font-size");
                if (!sv.empty())
                {
                    fontSize = firstNumber(sv, fontSize);
                }
            }

            Ut::Vec3f color(0.0f, 0.0f, 0.0f);
            bool haveColor = false;
            std::string_view fill;
            if (attrHas(attrs, "fill"))
            {
                fill = attrValue(attrs, "fill");
            }
            else
            {
                fill = styleVal("fill");
            }
            if (!fill.empty() && cssColorToVec3(fill, color))
            {
                haveColor = true;
            }
            const Ut::Vec3f effColor = haveColor ? color : Ut::Vec3f(0.0f, 0.0f, 0.0f);

            double angleDeg = 0.0;
            std::string_view tf;
            if (attrHas(attrs, "transform"))
            {
                tf = attrValue(attrs, "transform");
            }
            else
            {
                tf = styleVal("transform");
            }
            if (!tf.empty())
            {
                rotateAngleDeg(tf, angleDeg);
            }

            EntityInfo info{};
            info.type = EntityType::Text;
            info.sourceId = static_cast<uint64_t>(m_outEntities.size());
            info.visible = true;
            info.color = packSvgColor(effColor);
            info.colorPolicy = static_cast<uint8_t>(EntityColorPolicy::ByLayer);
            info.text.x = x;
            info.text.y = -y;  // SVG Y 向下 -> 系统 Y 向上，与 path 处理一致
            info.text.h = fontSize;
            info.text.a = angleDeg * 3.14159265358979323846 / 180.0;
            std::strncpy(info.text.text, content.c_str(), sizeof(info.text.text) - 1);
            info.text.text[sizeof(info.text.text) - 1] = '\0';
            std::strncpy(info.name, content.c_str(), sizeof(info.name) - 1);
            info.name[sizeof(info.name) - 1] = '\0';
            info.layerSourceId = getOrCreateLayer(effColor);
            m_outEntities.push_back(info);
        }

        /// 解析 <image ...>（data URI 内嵌位图），解码为 Image 图元。
        /// 返回跳过该元素之后的位置；任何不支持/解不了码的情况统一计一次 skip。
        const char* parseImageElement(const char* openTag, const char* gt)
        {
            std::string tag(openTag + 1, gt);
            std::vector<std::pair<std::string_view, std::string_view>> attrs;
            extractTagAttrs(tag, attrs);

            // 自闭合判断：'>' 前最近的非空白字符是 '/'
            std::string_view tagv(openTag + 1, static_cast<size_t>(gt - (openTag + 1)));
            size_t k = tagv.size();
            while (k > 0 && std::isspace(static_cast<unsigned char>(tagv[k - 1])))
            {
                --k;
            }
            const bool selfClosed = k > 0 && tagv[k - 1] == '/';

            const char* nextP = gt + 1;
            if (!selfClosed)
            {
                const char* close = std::strstr(nextP, "</image");
                if (close)
                {
                    const char* ce = std::strchr(close, '>');
                    nextP = ce ? ce + 1 : close;
                }
            }

            std::string_view href;
            if (attrHas(attrs, "href"))
            {
                href = attrValue(attrs, "href");
            }
            else if (attrHas(attrs, "xlink:href"))
            {
                href = attrValue(attrs, "xlink:href");
            }

            if (href.empty() || href.rfind("data:", 0) != 0 || href.find("base64") == std::string_view::npos)
            {
                // 非内嵌 base64（外部 URL 或非 base64 data URI）不处理，计一次 skip
                ++m_imageSkipped;
                return nextP;
            }

            const size_t comma = href.find(',');
            if (comma == std::string_view::npos)
            {
                ++m_imageSkipped;
                return nextP;
            }

            std::vector<unsigned char> raw;
            if (!base64Decode(href.substr(comma + 1), raw) || raw.empty())
            {
                ++m_imageSkipped;
                return nextP;
            }

            std::vector<unsigned char> rgba;
            int w = 0, h = 0;
            if (!Fio::loadImageToRgbaFromMemory(raw.data(), raw.size(), rgba, w, h) || w <= 0 || h <= 0)
            {
                ++m_imageSkipped;
                return nextP;
            }

            const double ix = attrHas(attrs, "x") ? firstNumber(attrValue(attrs, "x"), 0.0) : 0.0;
            const double iy = attrHas(attrs, "y") ? firstNumber(attrValue(attrs, "y"), 0.0) : 0.0;
            addImageEntity(ix, iy, rgba, w, h);
            return nextP;
        }

        /// 把解码后的 RGBA 位图构造成一个 EntityInfo（EntityType::Image）。
        void addImageEntity(double x, double y, const std::vector<unsigned char>& rgba, int w, int h)
        {
            const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
            if (m_outBlob.size() + bytes > 0xFFFFFFFFull)
            {
                m_warnings.push_back("SVG <image> dropped: extension blob would exceed 4 GiB");
                SY_ERRORF("[SvgParser] Image extension blob overflow, image dropped");
                return;
            }
            EntityInfo info{};
            info.type = EntityType::Image;
            info.sourceId = static_cast<uint64_t>(m_outEntities.size());
            info.visible = true;
            info.imageWidth = w;
            info.imageHeight = h;
            // 锚点取 SVG 的 (x,y)，Y 翻转；世界尺寸由转换层按「1 像素 = 1 单位」展开
            info.line.x1 = x;
            info.line.y1 = -y;
            info.colorPolicy = static_cast<uint8_t>(EntityColorPolicy::ByLayer);
            info.layerSourceId = getOrCreateLayer(Ut::Vec3f(0.0f, 0.0f, 0.0f));
            info.extensionDataOffset = static_cast<uint32_t>(m_outBlob.size());
            info.extensionDataSize = static_cast<uint32_t>(bytes);
            m_outBlob.insert(m_outBlob.end(), rgba.begin(), rgba.end());
            m_outEntities.push_back(info);
        }

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
                    SY_WARNF(
                        "[SvgParser] Color layer limit %zu reached, extra colors merged into layer 1", kMaxSvgLayers);
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

        /// 解析一个 paint 为纯色：
        ///   - COLOR → 直接返回；
        ///   - 渐变 → 近似成 blend 纯色并（首次）告警；
        ///   - 其它（none/未知）→ 黑色。
        Ut::Vec3f resolvePaintColor(const NSVGpaint& paint)
        {
            if (paint.type == NSVG_PAINT_COLOR)
            {
                return extractSvgColor(paint);
            }
            if ((paint.type == NSVG_PAINT_LINEAR_GRADIENT || paint.type == NSVG_PAINT_RADIAL_GRADIENT) &&
                paint.gradient != nullptr)
            {
                if (!m_gradientWarned)
                {
                    m_gradientWarned = true;
                    m_warnings.push_back("SVG gradient/pattern fill approximated to a blended solid color");
                    SY_WARNF("[SvgParser] Gradient fill approximated to blended color");
                }
                return approxGradientColor(paint.gradient);
            }
            return Ut::Vec3f(0.0f, 0.0f, 0.0f);
        }

        /// 把一条子路径（nanosvg 的一个 NSVGpath）聚合成**一个**图元。
        ///
        /// 旧实现是「每段三次贝塞尔一个图元」：一条 10 段的 path 产出 10 个图元，
        /// 语义上选不中整条路径，落地阶段还要为每段各走一遍 clone / R-tree insert /
        /// observer 通知，是导入慢的主要放大器。现在的规则：
        ///   - ≥2 段 → EntityType::SmartLine（复合曲线），几何写进扩展数据块；
        ///   - 单段 → 直接产出 Line 或 Bezier，不套复合曲线容器（少一层间接）。
        void convertPathToCompositeEntity(
            NSVGpath* svgPath, const Ut::Vec3f& shapeColor, uint32_t layerSourceId, const char* sourceName, float strokeWidth)
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

                // NaN/Inf 坐标会导致后续 static_cast<int>(NaN) 产生 UB，
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
            // 保留 stroke-width：渲染现阶段未实现线宽，先记录属性供后续使用/导出
            info.lineWidth = (strokeWidth > 0.0f) ? static_cast<double>(strokeWidth) : 1.0;
            // SVG 按颜色分层，层色就是该 shape 的自身颜色，所以标成随层色（ByLayer）而不是
            // 显式覆盖色：导入后的观感与源文件完全一致（显示色 = 层色 = 原色），而把图元移到
            // 其它图层、或修改图层颜色时颜色会跟着变（与 AutoCAD 的 BYLAYER 语义一致）。
            // color 字段仍记录源色，仅作追溯用——ByLayer 时转换层不会再写覆盖色。
            info.colorPolicy = static_cast<uint8_t>(EntityColorPolicy::ByLayer);
            // 源 SVG 的 id（nanosvg 会把 <g id> 继承给无 id 的子 shape）。
            // 不当图层名用，但保留下来：出问题时能把画布上的图元对回 SVG 里的元素。
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
            // 这里丢弃该路径而不是写入截断偏移——截断偏移会指向别的图元的数据（静默错乱）。
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
        static bool isStraightCubic(const Ut::Vec2d& p0, const Ut::Vec2d& c1, const Ut::Vec2d& c2, const Ut::Vec2d& p1)
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
    // SVG 子路径 → 一个图元（多段为 SmartLine 复合曲线，单段为 Line/Bezier），
    // 不离散为折线；不依赖 Engine2D 类型，跨 DLL 安全
    // ========================================================================
    FioParseResult SvgParser::parseToIR(const char* filePath)
    {
        return parseToIRImpl(filePath, nullptr, nullptr);
    }

    FioParseResult SvgParser::parseToIRWithProgress(
        const char* filePath, ParseProgressCallback onProgress, void* progressCtx)
    {
        return parseToIRImpl(filePath, onProgress, progressCtx);
    }

    FioParseResult SvgParser::parseToIRImpl(const char* filePath, ParseProgressCallback onProgress, void* progressCtx)
    {
        SY_INFOF("[SvgParser] parseToIR START: %s", filePath ? filePath : "");

        // 统一缓冲区管理（与 DxfParser/StlParser 一致）
        IrPublisher& pub = IrPublisher::threadLocal();
        pub.reset();

        if (!filePath)
        {
            SY_ERROR("[SvgParser] parseToIR: null filePath");
            return FioParseResult{};
        }

        try
        {
            NsvgInterpreter interpreter(
                pub.entities(), pub.layers(), pub.warnings(), m_importFillAsOutline, pub.blob(), onProgress, progressCtx);
            interpreter.parseFile(filePath);
            if (!interpreter.succeeded())
            {
                const std::string message =
                    pub.warnings().empty() ? std::string("Failed to parse SVG file: ") + filePath : pub.warnings().back();
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

        if (pub.entities().empty())
        {
            SY_WARNF("[SvgParser] parseToIR: no entities produced: %s", filePath);
            return FioParseResult{};
        }

        SY_INFOF("[SvgParser] parseToIR END: %u entities, %u layers, %u warnings: %s",
            static_cast<uint32_t>(pub.entities().size()),
            static_cast<uint32_t>(pub.layers().size()),
            static_cast<uint32_t>(pub.warnings().size()),
            filePath);
        return pub.publish("SVG");
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
            std::snprintf(buffer, bufferSize, "%s", name);
        }
        return len;
    }

    void SvgParser::forEachSupportedExtension(void (*visitor)(const char*, void*), void* ctx) const
    {
        visitor("svg", ctx);
        visitor("svgz", ctx);
    }
}  // namespace Fio
#include "FileIO/FileParserFactory.h"
namespace Fio {
namespace {
    static struct SvgRegistrar {
        SvgRegistrar() {
            FileParserFactory::instance().registerParser(FileFormat::SVG, []() -> IFileParser* {
                return new SvgParser();
            });
        }
    } s_svgRegistrar;
}

}  // namespace Fio
