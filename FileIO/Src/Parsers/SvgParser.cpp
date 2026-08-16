#include "FileIO/Parsers/SvgParser.h"
#include "FileIO/FileIOUtils.h"

#include "Log/SyLogger.h"

#include "Ut/Vec.h"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"

#include <cmath>
#include <cstring>
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
        Ut::Vec3f extractSvgColor(const NSVGpaint& paint)
        {
            if (paint.type == NSVG_PAINT_COLOR)
            {
                unsigned int color = paint.color;
                float r = ((color >> 16) & 0xFF) / 255.0f;
                float g = ((color >> 8) & 0xFF) / 255.0f;
                float b = (color & 0xFF) / 255.0f;
                return Ut::Vec3f(r, g, b);
            }
            // For gradients or unknown types, return default color
            return Ut::Vec3f(0.0f, 0.0f, 0.0f);
        }

        // Adaptive bezier sampling based on chord error
        // Returns number of segments needed for the given cubic bezier curve
        int computeAdaptiveSegments(
            const Ut::Vec2d& p0, const Ut::Vec2d& c1, const Ut::Vec2d& c2, const Ut::Vec2d& p1, double tolerance = 0.1)
        {
            // Calculate chord length
            double chordLen = (p1 - p0).length();
            if (chordLen < 1e-10)
            {
                return 1;
            }

            // Calculate maximum distance from curve to chord (flatness test)
            // Sample at t=0.5 and measure distance to chord
            double t = 0.5;
            double t1 = 1.0 - t;
            Ut::Vec2d mid = t1 * t1 * t1 * p0 + 3 * t1 * t1 * t * c1 + 3 * t1 * t * t * c2 + t * t * t * p1;
            Ut::Vec2d chordMid = (p0 + p1) * 0.5;
            double flatness = (mid - chordMid).length();

            // Estimate segments needed based on flatness
            if (flatness < tolerance)
            {
                return 1;
            }

            // Use empirical formula: segments = ceil(sqrt(flatness / tolerance))
            int segs = static_cast<int>(std::ceil(std::sqrt(flatness / tolerance)));
            return std::clamp(segs, 2, 32);
        }
    }  // namespace

    class NsvgInterpreter
    {
    public:
        NsvgInterpreter(std::vector<EntityInfo>& outEntities,
            std::vector<uint8_t>& extensionBlob,
            std::vector<IrLayerInfo>& outLayers,
            std::vector<std::string>& warnings)
            : m_outEntities(outEntities)
            , m_extensionBlob(extensionBlob)
            , m_outLayers(outLayers)
            , m_warnings(warnings)
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
                bool hasFill = shape->fill.type != NSVG_PAINT_NONE;
                bool hasStroke = shape->stroke.type != NSVG_PAINT_NONE;

                if (!visible)
                {
                    continue;
                }
                if (!hasFill && !hasStroke)
                {
                    continue;
                }

                // Extract color from shape (prefer stroke color for lines, fill for closed shapes)
                Ut::Vec3f shapeColor = Ut::Vec3f(0.0f, 0.0f, 0.0f);
                if (hasStroke)
                {
                    shapeColor = extractSvgColor(shape->stroke);
                }
                else if (hasFill)
                {
                    shapeColor = extractSvgColor(shape->fill);
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
                    convertPathToEntity(svgPath, shapeColor, layerSourceId);
                }
            }

            m_success = !m_outEntities.empty();
        }

    private:
        std::vector<EntityInfo>& m_outEntities;
        std::vector<uint8_t>& m_extensionBlob;
        std::vector<IrLayerInfo>& m_outLayers;
        std::vector<std::string>& m_warnings;
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

        Ut::Vec2d evalCubicBezier(
            const Ut::Vec2d& p0, const Ut::Vec2d& c1, const Ut::Vec2d& c2, const Ut::Vec2d& p1, double t)
        {
            double t1 = 1.0 - t;
            double t1t1t1 = t1 * t1 * t1;
            double t1t1t = t1 * t1 * t;
            double t1tt = t1 * t * t;
            double ttt = t * t * t;

            return Ut::Vec2d(t1t1t1 * p0.x() + 3.0 * t1t1t * c1.x() + 3.0 * t1tt * c2.x() + ttt * p1.x(),
                t1t1t1 * p0.y() + 3.0 * t1t1t * c1.y() + 3.0 * t1tt * c2.y() + ttt * p1.y());
        }

        void convertPathToEntity(NSVGpath* svgPath, const Ut::Vec3f& /*shapeColor*/, uint32_t layerSourceId)
        {
            if (!svgPath || svgPath->npts < 4)
            {
                return;
            }

            float* pts = svgPath->pts;
            int npts = svgPath->npts;

            std::vector<Ut::Vec2d> points;
            points.reserve(static_cast<size_t>(npts));

            // First point (Y axis flip: SVG Y down -> system Y up)
            points.emplace_back(pts[0], -pts[1]);

            // Process cubic bezier segments
            // nanosvg pts layout: [x0,y0, cpx1,cpy1, cpx2,cpy2, x1,y1, ...]
            // Step through in groups of 3 control points (6 floats per segment)
            for (int i = 0; i + 3 < npts; i += 3)
            {
                Ut::Vec2d p0(pts[i * 2], -pts[i * 2 + 1]);
                Ut::Vec2d c1(pts[i * 2 + 2], -pts[i * 2 + 3]);
                Ut::Vec2d c2(pts[i * 2 + 4], -pts[i * 2 + 5]);
                Ut::Vec2d p1(pts[i * 2 + 6], -pts[i * 2 + 7]);

                // Adaptive sampling based on curve flatness
                int segs = computeAdaptiveSegments(p0, c1, c2, p1);

                for (int s = 1; s <= segs; ++s)
                {
                    double t = static_cast<double>(s) / segs;
                    points.push_back(evalCubicBezier(p0, c1, c2, p1, t));
                }
            }

            // Close path if needed
            if (svgPath->closed && points.size() >= 2)
            {
                double dx = points.front().x() - points.back().x();
                double dy = points.front().y() - points.back().y();
                if (std::hypot(dx, dy) > 1e-6)
                {
                    points.push_back(points.front());
                }
            }

            if (points.size() < 2)
            {
                return;
            }

            // Remove duplicate consecutive points
            std::vector<Ut::Vec2d> cleaned;
            cleaned.reserve(points.size());
            cleaned.push_back(points[0]);
            for (size_t i = 1; i < points.size(); ++i)
            {
                if ((points[i] - cleaned.back()).length() > 1e-6)
                {
                    cleaned.push_back(points[i]);
                }
            }
            if (cleaned.size() < 2)
            {
                return;
            }

            // 收集顶点为 double 序列（x0,y0,x1,y1,...）
            std::vector<double> verts;
            verts.reserve(cleaned.size() * 2);
            for (const auto& pt : cleaned)
            {
                verts.push_back(pt.x());
                verts.push_back(pt.y());
            }

            // 填充 EntityInfo：Polyline 类型，顶点数据存入 extensionBlob
            EntityInfo info{};
            info.type = EntityType::Polyline;
            info.sourceId = static_cast<uint64_t>(m_outEntities.size());
            info.layerSourceId = layerSourceId;
            info.visible = true;
            info.vertexCount = static_cast<uint32_t>(cleaned.size());
            info.extensionDataOffset = static_cast<uint32_t>(m_extensionBlob.size());
            info.extensionDataSize = static_cast<uint32_t>(verts.size() * sizeof(double));
            m_extensionBlob.insert(m_extensionBlob.end(),
                reinterpret_cast<const uint8_t*>(verts.data()),
                reinterpret_cast<const uint8_t*>(verts.data()) + info.extensionDataSize);
            m_outEntities.push_back(info);
        }
    };

    // ========================================================================
    // SvgParser::parseToIR() — 中立 IR 解析路径
    // SVG path → 贝塞尔自适应采样 → Polyline(POD) + 顶点数据存入 extensionBlob
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
            NsvgInterpreter interpreter(s_entities, s_extensionBlob, s_layers, s_warnings);
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