#include "IrProjector.h"

#include "Log/SyLogger.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>


namespace Fio
{
    namespace
    {
        /// 把 std::string 安全写入固定长度 char 缓冲区（保证以 '\0' 结尾）
        ///
        /// @return true 表示发生了截断。截断不是无害的：图层名被截短后
        ///         IrPublisher::findLayer 按名字比对会失配，于是同一个图层被重复登记；
        ///         图元名被截短则影响上层按名字定位。所以调用方要把截断计入统计。
        template <std::size_t N>
        bool copyFixed(char (&dst)[N], const std::string& src)
        {
            const std::size_t n = std::min(src.size(), N - 1);
            std::memcpy(dst, src.data(), n);
            dst[n] = '\0';
            return n < src.size();
        }

        /// ParsedGeometryType → EntityType（两套枚举一一对应，但刻意显式列出：
        /// 任何一侧新增类型时，编译器会在 switch 缺分支处告警，而不是静默错映射）
        EntityType mapType(ParsedGeometryType t)
        {
            switch (t)
            {
            case ParsedGeometryType::Line:
                return EntityType::Line;
            case ParsedGeometryType::Arc:
                return EntityType::Arc;
            case ParsedGeometryType::Circle:
                return EntityType::Circle;
            case ParsedGeometryType::Ellipse:
                return EntityType::Ellipse;
            case ParsedGeometryType::Polygon:
                return EntityType::Polygon;
            case ParsedGeometryType::Bezier:
                return EntityType::Bezier;
            case ParsedGeometryType::Bezier2:
                return EntityType::Bezier2;
            case ParsedGeometryType::Nurbs:
                return EntityType::Nurbs;
            case ParsedGeometryType::Spline:
                return EntityType::Spline;
            case ParsedGeometryType::Polyline:
                return EntityType::Polyline;
            case ParsedGeometryType::Point:
                return EntityType::Point;
            case ParsedGeometryType::Text:
                return EntityType::Text;
            case ParsedGeometryType::Image:
                return EntityType::Image;
            case ParsedGeometryType::BarCode:
                return EntityType::BarCode;
            case ParsedGeometryType::QRCode:
                return EntityType::QRCode;
            case ParsedGeometryType::SmartLine:
                return EntityType::SmartLine;
            case ParsedGeometryType::Mesh3D:
                return EntityType::Mesh3D;
            case ParsedGeometryType::Unknown:
                break;
            }
            return EntityType::Unknown;
        }

        /// 把点序列打平成 double 序列（x0,y0,x1,y1,...），扩展数据块的多边形/折线布局
        std::vector<double> flattenPoints(const std::vector<ParsedPoint2D>& pts)
        {
            std::vector<double> out;
            out.reserve(pts.size() * 2);
            for (const auto& p : pts)
            {
                out.push_back(p.x);
                out.push_back(p.y);
            }
            return out;
        }
    }  // namespace

    // ===================== IrPublisher =====================

    IrPublisher& IrPublisher::threadLocal()
    {
        // 每线程一份：并发解析（一线程一个文件）互不干扰。
        // 生命周期与线程一致，因此返回给调用方的指针在「下一次同线程解析」前保持有效。
        thread_local IrPublisher s_instance;
        return s_instance;
    }

    void IrPublisher::reset()
    {
        // 只 clear 不 shrink：容量留着复用，连续导入多个文件时避免反复分配
        m_entities.clear();
        m_layers.clear();
        m_groups.clear();
        m_blob.clear();
        m_warnings.clear();
        m_layerIndex.clear();

        // 触顶告警是「每次解析一条」的语义，必须随缓冲区一起复位，
        // 否则第二个文件撞上限时会静默
        m_layerLimitWarned = false;
        m_groupLimitWarned = false;
    }

    uint32_t IrPublisher::appendBlob(const void* data, std::size_t bytes)
    {
        if (data == nullptr || bytes == 0)
        {
            return 0u;
        }

        const std::size_t offset = m_blob.size();
        // EntityInfo::extensionDataOffset/Size 都是 uint32_t，超出即无法表达。
        // 这里必须挡住，否则截断后的偏移会指向别的图元的数据（静默数据错乱）。
        constexpr std::size_t kMax = static_cast<std::size_t>(std::numeric_limits<uint32_t>::max()) - 1u;
        if (offset > kMax || bytes > kMax - offset)
        {
            SY_WARNF("[IrProjector] Extension blob overflow: offset=%zu bytes=%zu, entity data dropped", offset, bytes);
            return kInvalidOffset;
        }

        m_blob.resize(offset + bytes);
        std::memcpy(m_blob.data() + offset, data, bytes);
        return static_cast<uint32_t>(offset);
    }

    uint32_t IrPublisher::addLayer(const std::string& name, uint32_t argbColor, bool visible, bool locked)
    {
        IrLayerInfo li;
        copyFixed(li.name, name);

        // 索引键统一用截断后的名字：DXF 里两个超长图层名可能截断后相同，
        // 若用原名做键就会登记两条 name 完全一致的图层，下游按名字还原时必然错乱
        const std::string key(li.name);

        // 查重：DXF 的 LAYER 表允许重复记录，SVG 按颜色分层时更是天然大量重复调用
        const auto it = m_layerIndex.find(key);
        if (it != m_layerIndex.end())
        {
            return it->second;
        }

        if (m_layers.size() >= kMaxLayers)
        {
            // 返回 0（未分配）让图元落到下游默认图层——图元不能因为图层超限而丢
            if (!m_layerLimitWarned)
            {
                m_layerLimitWarned = true;
                m_warnings.push_back("Layer count reached the limit (" + std::to_string(kMaxLayers) +
                                     "), extra layers are merged into the default layer");
                SY_WARNF("[IrProjector] Layer limit %zu reached, subsequent layers fall back to default: '%s'",
                    kMaxLayers, key.c_str());
            }
            return 0u;
        }

        // 1-based：0 保留为「未分配图层」哨兵（与 EntityInfo::layerSourceId 默认值一致）
        li.sourceId = static_cast<uint32_t>(m_layers.size()) + 1u;
        li.color = argbColor;
        li.visible = visible;
        li.locked = locked;
        m_layers.push_back(li);
        m_layerIndex.emplace(key, li.sourceId);
        return li.sourceId;
    }

    uint64_t IrPublisher::addGroup(const std::string& name, uint64_t parentSourceId)
    {
        if (m_groups.size() >= kMaxGroups)
        {
            // 退回父群组：层级在这里被压平，但图元的归属仍然有效，不会变成孤立图元
            if (!m_groupLimitWarned)
            {
                m_groupLimitWarned = true;
                m_warnings.push_back("Group count reached the limit (" + std::to_string(kMaxGroups) +
                                     "), extra groups are flattened into their parent");
                SY_WARNF("[IrProjector] Group limit %zu reached, subsequent groups are flattened into parent %llu",
                    kMaxGroups, static_cast<unsigned long long>(parentSourceId));
            }
            return parentSourceId;
        }

        IrGroupInfo gi;
        // 1-based：0 保留为「无群组」哨兵（与 EntityInfo::groupSourceId 默认值一致）
        gi.sourceId = static_cast<uint64_t>(m_groups.size()) + 1u;
        gi.parentSourceId = parentSourceId;
        copyFixed(gi.name, name);
        m_groups.push_back(gi);
        return gi.sourceId;
    }

    uint32_t IrPublisher::findLayer(const std::string& name) const
    {
        // 必须与 addLayer 用同一把键：先按 IrLayerInfo::name 的容量截断再查，
        // 否则长名图层「登记得进、查不出来」，每个实体都会触发一次重复登记
        char buf[sizeof(IrLayerInfo::name)];
        copyFixed(buf, name);

        const auto it = m_layerIndex.find(std::string(buf));
        return it != m_layerIndex.end() ? it->second : 0u;
    }

    FioParseResult IrPublisher::publish(const char* sourceFormat, const char* sourceUnit)
    {
        FioParseResult result;
        // 空 vector 的 data() 可能返回 nullptr，配合 count==0 消费方不会解引用，是安全的
        result.entities = m_entities.data();
        result.entityCount = static_cast<uint32_t>(m_entities.size());
        result.layers = m_layers.data();
        result.layerCount = static_cast<uint32_t>(m_layers.size());
        result.groups = m_groups.data();
        result.groupCount = static_cast<uint32_t>(m_groups.size());
        result.extensionBlob.data = m_blob.data();
        result.extensionBlob.size = m_blob.size();
        result.warningCount = static_cast<uint32_t>(m_warnings.size());

        if (sourceFormat != nullptr)
        {
            copyFixed(result.sourceFormat, sourceFormat);
        }
        if (sourceUnit != nullptr)
        {
            copyFixed(result.sourceUnit, sourceUnit);
        }
        return result;
    }

    // ===================== IrProjector =====================

    namespace IrProjector
    {
        namespace
        {
            /// ParsedGeometryType 的稳定英文短名，仅用于日志。
            /// 不裸打整数：现场看到 "type=13" 没法判断是哪种几何，而枚举值会随重构改动。
            const char* parsedTypeName(ParsedGeometryType t)
            {
                switch (t)
                {
                case ParsedGeometryType::Line:
                    return "Line";
                case ParsedGeometryType::Arc:
                    return "Arc";
                case ParsedGeometryType::Circle:
                    return "Circle";
                case ParsedGeometryType::Ellipse:
                    return "Ellipse";
                case ParsedGeometryType::Polygon:
                    return "Polygon";
                case ParsedGeometryType::Bezier:
                    return "Bezier";
                case ParsedGeometryType::Bezier2:
                    return "Bezier2";
                case ParsedGeometryType::Nurbs:
                    return "Nurbs";
                case ParsedGeometryType::Spline:
                    return "Spline";
                case ParsedGeometryType::Polyline:
                    return "Polyline";
                case ParsedGeometryType::Point:
                    return "Point";
                case ParsedGeometryType::Text:
                    return "Text";
                case ParsedGeometryType::Image:
                    return "Image";
                case ParsedGeometryType::BarCode:
                    return "BarCode";
                case ParsedGeometryType::QRCode:
                    return "QRCode";
                case ParsedGeometryType::SmartLine:
                    return "SmartLine";
                case ParsedGeometryType::Mesh3D:
                    return "Mesh3D";
                case ParsedGeometryType::Unknown:
                    return "Unknown";
                }
                return "?";
            }

            /// 一次投影过程中的「静默降级」计数。
            ///
            /// 为什么聚合到结尾一次输出，而不是在循环里逐条打日志：
            /// 一个文件可能有几万个图元，而「引用了不存在的图层」这类畸形文件的问题
            /// 是**每个图元都会触发一次**的，逐条打会直接把日志文件淹掉，反而更难查。
            /// 解析是有明确边界的批处理（不是长期运行的流），所以聚合计数 + 首个样本的
            /// sourceId 是更合适的形态：既给出完整的量，又能拿着 sourceId 回原文件定位。
            ///
            /// 这些计数**同时**会写进 IrPublisher::warnings()，从而反映到
            /// FioParseResult::warningCount 上 —— 否则上层（ImportReaderBase 打的
            /// "Converter dropped N of M"）里的 M 根本不包含这里丢掉的图元，
            /// 「文件里 1000 个图元、界面只出现 800 个」就会变成查不下去的现场。
            struct ProjectionStats
            {
                uint32_t unknownType = 0;      ///< 类型无法映射，整条图元被跳过
                uint32_t smartLineSkipped = 0; ///< SmartLine 目前无 IR 通道
                uint32_t blobOverflow = 0;     ///< 扩展数据溢出，图元退化成无几何的空壳
                uint32_t missingLayerRef = 0;  ///< 引用了不存在的图层，落到「未分配」
                uint32_t missingGroupRef = 0;  ///< 引用了不存在的群组，落到「无群组」
                uint32_t nameTruncated = 0;    ///< 名字超长被截断
                uint32_t groupParentMissing = 0;
                uint32_t groupCycleBroken = 0;

                int firstUnknownTypeValue = -1;
                uint64_t firstUnknownSourceId = 0;
                uint64_t firstBlobOverflowSourceId = 0;
                uint64_t firstMissingLayerSourceId = 0;

                bool anyDegradation() const
                {
                    return unknownType != 0 || smartLineSkipped != 0 || blobOverflow != 0 || missingLayerRef != 0
                        || missingGroupRef != 0 || nameTruncated != 0 || groupParentMissing != 0
                        || groupCycleBroken != 0;
                }
            };

            /// 投影单个图元的几何数据。扩展数据块布局必须与消费侧
            /// （Main/Src/Import/FioEntityConverter.cpp）的读取顺序严格一致。
            ///
            /// 几何没能完整写入时（扩展数据溢出，或该类型暂无 IR 通道）不做特殊返回，
            /// 而是记进 stats：图元本身仍会入表（保留 id/图层归属），消费侧读到的是空几何，
            /// 数量由调用方在结尾汇总成 warning 与 WARN 日志。
            void projectGeometry(const ParsedGeometry& g, EntityInfo& info, IrPublisher& pub, ProjectionStats& stats)
            {
                switch (g.type)
                {
                case ParsedGeometryType::Line:
                    info.line.x1 = g.line.start.x;
                    info.line.y1 = g.line.start.y;
                    info.line.x2 = g.line.end.x;
                    info.line.y2 = g.line.end.y;
                    break;

                case ParsedGeometryType::Arc:
                    info.arc.cx = g.arc.center.x;
                    info.arc.cy = g.arc.center.y;
                    info.arc.r = g.arc.radius;
                    info.arc.sa = g.arc.startAngle;
                    info.arc.ea = g.arc.endAngle;
                    break;

                case ParsedGeometryType::Circle:
                    info.circle.cx = g.circle.center.x;
                    info.circle.cy = g.circle.center.y;
                    info.circle.r = g.circle.radius;
                    break;

                case ParsedGeometryType::Ellipse:
                    info.ellipse.cx = g.ellipse.center.x;
                    info.ellipse.cy = g.ellipse.center.y;
                    info.ellipse.rx = g.ellipse.radiusX;
                    info.ellipse.ry = g.ellipse.radiusY;
                    info.ellipse.rot = g.ellipse.rotation;
                    info.ellipse.sa = g.ellipse.startAngle;
                    info.ellipse.ea = g.ellipse.endAngle;
                    break;

                case ParsedGeometryType::Point:
                    // 点坐标复用 line.x1/y1（消费侧同口径）
                    info.line.x1 = g.line.start.x;
                    info.line.y1 = g.line.start.y;
                    break;

                case ParsedGeometryType::Bezier:
                    // 起点走 line.x1/y1，控制点与终点走 bezier 段（消费侧同口径）
                    info.line.x1 = g.bezier.start.x;
                    info.line.y1 = g.bezier.start.y;
                    info.bezier.c0x = g.bezier.ctrl0.x;
                    info.bezier.c0y = g.bezier.ctrl0.y;
                    info.bezier.c1x = g.bezier.ctrl1.x;
                    info.bezier.c1y = g.bezier.ctrl1.y;
                    info.bezier.ex = g.bezier.end.x;
                    info.bezier.ey = g.bezier.end.y;
                    break;

                case ParsedGeometryType::Bezier2:
                    info.line.x1 = g.bezier2.start.x;
                    info.line.y1 = g.bezier2.start.y;
                    info.bezier2.cx = g.bezier2.ctrl.x;
                    info.bezier2.cy = g.bezier2.ctrl.y;
                    info.bezier2.ex = g.bezier2.end.x;
                    info.bezier2.ey = g.bezier2.end.y;
                    break;

                case ParsedGeometryType::Text:
                case ParsedGeometryType::BarCode:
                case ParsedGeometryType::QRCode:
                    // 三者共用 text 段承载内容；条码/二维码的尺寸另走独立字段
                    info.text.x = g.text.position.x;
                    info.text.y = g.text.position.y;
                    if (copyFixed(info.text.text, g.text.text))
                    {
                        // 文本内容被截断意味着导入后的字面量与原文件不一致 —— 对条码/二维码
                        // 更严重：内容变了，扫出来就是另一个码。必须让它可见。
                        ++stats.nameTruncated;
                    }
                    info.text.h = g.text.height;
                    info.text.a = g.text.angle;
                    break;

                case ParsedGeometryType::Polygon:
                case ParsedGeometryType::Polyline:
                {
                    // 扩展数据布局：double 序列 x0,y0,x1,y1,...
                    // 闭合与否由 EntityType（Polygon/Polyline）表达，不再单独占字段
                    const std::vector<double> flat = flattenPoints(g.polyline.points);
                    const std::size_t bytes = flat.size() * sizeof(double);
                    const uint32_t offset = pub.appendBlob(flat.data(), bytes);
                    if (offset == IrPublisher::kInvalidOffset)
                    {
                        info.vertexCount = 0;
                        ++stats.blobOverflow;
                        if (stats.firstBlobOverflowSourceId == 0)
                        {
                            stats.firstBlobOverflowSourceId = g.sourceId;
                        }
                        return;
                    }
                    info.vertexCount = static_cast<uint32_t>(g.polyline.points.size());
                    info.extensionDataOffset = offset;
                    info.extensionDataSize = static_cast<uint32_t>(bytes);
                    break;
                }

                case ParsedGeometryType::Nurbs:
                case ParsedGeometryType::Spline:
                {
                    // 扩展数据布局：[控制点 n*2 doubles][节点 k doubles][权重 w doubles]
                    // 三段必须一次性连续写入，中途溢出就整条丢弃，避免只写了半段导致
                    // 消费侧把节点当权重读。
                    std::vector<double> packed = flattenPoints(g.nurbs.controlPoints);
                    packed.insert(packed.end(), g.nurbs.knots.begin(), g.nurbs.knots.end());
                    packed.insert(packed.end(), g.nurbs.weights.begin(), g.nurbs.weights.end());

                    const std::size_t bytes = packed.size() * sizeof(double);
                    const uint32_t offset = pub.appendBlob(packed.data(), bytes);
                    if (offset == IrPublisher::kInvalidOffset)
                    {
                        ++stats.blobOverflow;
                        if (stats.firstBlobOverflowSourceId == 0)
                        {
                            stats.firstBlobOverflowSourceId = g.sourceId;
                        }
                        return;
                    }
                    info.nurbsDegree = static_cast<int32_t>(g.nurbs.degree);
                    info.nurbsCtrlPtCount = static_cast<uint32_t>(g.nurbs.controlPoints.size());
                    info.nurbsKnotCount = static_cast<uint32_t>(g.nurbs.knots.size());
                    info.extensionDataOffset = offset;
                    info.extensionDataSize = static_cast<uint32_t>(bytes);
                    break;
                }

                case ParsedGeometryType::Image:
                {
                    // 位图：扩展数据块存**编码后的原始字节**（PNG/JPEG 等），
                    // 由消费侧 SyImage::setPixelData 解码；位置复用 line.x1/y1 作为左上角
                    info.imageWidth = g.image.width;
                    info.imageHeight = g.image.height;
                    info.line.x1 = g.image.position.x;
                    info.line.y1 = g.image.position.y;

                    const uint32_t offset = pub.appendBlob(g.image.data.data(), g.image.data.size());
                    if (offset == IrPublisher::kInvalidOffset)
                    {
                        ++stats.blobOverflow;
                        if (stats.firstBlobOverflowSourceId == 0)
                        {
                            stats.firstBlobOverflowSourceId = g.sourceId;
                        }
                        return;
                    }
                    info.extensionDataOffset = offset;
                    info.extensionDataSize = static_cast<uint32_t>(g.image.data.size());
                    break;
                }

                case ParsedGeometryType::Mesh3D:
                {
                    // 扩展数据布局：[顶点 vertCount*3 float][法线 vertCount*3 float]
                    // 注意是 **float** 而非 double（消费侧按 float 读），且法线数量与顶点对齐；
                    // 解析器未提供法线时补 (0,0,1)，保证两段长度一致，消费侧不必猜。
                    const std::size_t vertCount = g.mesh.vertices.size();
                    std::vector<float> packed;
                    packed.reserve(vertCount * 6);
                    for (const auto& v : g.mesh.vertices)
                    {
                        packed.push_back(static_cast<float>(v.x));
                        packed.push_back(static_cast<float>(v.y));
                        packed.push_back(static_cast<float>(v.z));
                    }
                    for (std::size_t i = 0; i < vertCount; ++i)
                    {
                        if (i < g.mesh.normals.size())
                        {
                            packed.push_back(static_cast<float>(g.mesh.normals[i].x));
                            packed.push_back(static_cast<float>(g.mesh.normals[i].y));
                            packed.push_back(static_cast<float>(g.mesh.normals[i].z));
                        }
                        else
                        {
                            packed.push_back(0.0f);
                            packed.push_back(0.0f);
                            packed.push_back(1.0f);
                        }
                    }

                    const std::size_t bytes = packed.size() * sizeof(float);
                    const uint32_t offset = pub.appendBlob(packed.data(), bytes);
                    if (offset == IrPublisher::kInvalidOffset)
                    {
                        ++stats.blobOverflow;
                        if (stats.firstBlobOverflowSourceId == 0)
                        {
                            stats.firstBlobOverflowSourceId = g.sourceId;
                        }
                        return;
                    }
                    info.meshVertCount = static_cast<uint32_t>(vertCount);
                    info.meshTriCount = static_cast<uint32_t>(g.mesh.indices.size() / 3);
                    info.extensionDataOffset = offset;
                    info.extensionDataSize = static_cast<uint32_t>(bytes);
                    break;
                }

                case ParsedGeometryType::SmartLine:
                    // 智能线由子图元组成，跨 DLL IR 目前没有嵌套通道。
                    // 后续按「智能线 = 一个群组」落地（与 DXF 块引用同一套机制），
                    // 在此之前由调用方在 ParseData 阶段拆平，本层不静默丢数据、只记警告。
                    // 计数而非逐条打日志：含大量智能线的文件会刷出成千条同样的 WARN。
                    ++stats.smartLineSkipped;
                    return;

                case ParsedGeometryType::Unknown:
                    break;
                }
            }
        }  // namespace

        FioParseResult project(const ParseData& data, const char* sourceFormat, const char* sourceUnit)
        {
            if (!data.success)
            {
                SY_ERRORF("[IrProjector] project called on failed ParseData: format=%s error=%s",
                    sourceFormat != nullptr ? sourceFormat : "?",
                    data.errorMessage.c_str());
                return FioParseResult{};
            }

            IrPublisher& pub = IrPublisher::threadLocal();
            pub.reset();

            ProjectionStats stats;

            // ---- 图层 ----
            // 解析器分配的 ParsedLayer::sourceId 未必连续，投影时重新编号成 1-based 稠密序列，
            // 并保留「旧 id → 新 id」映射供图元引用改写。
            std::unordered_map<uint32_t, uint32_t> layerIdMap;
            layerIdMap.reserve(data.layers.size());
            for (const auto& pl : data.layers)
            {
                const uint32_t newId = pub.addLayer(pl.name, pl.color, pl.visible, pl.locked);
                layerIdMap[pl.sourceId] = newId;
            }

            // ---- 群组 ----
            // 同样重新编号；父引用在第二遍改写（父可能出现在子之后）
            std::unordered_map<uint64_t, uint64_t> groupIdMap;
            groupIdMap.reserve(data.groups.size());
            for (const auto& pg : data.groups)
            {
                const uint64_t newId = pub.addGroup(pg.name, 0u);
                groupIdMap[pg.sourceId] = newId;
            }
            for (std::size_t i = 0; i < data.groups.size(); ++i)
            {
                // addGroup 触顶后会拒绝新建，pub.groups() 与 data.groups 的下标就不再一一对应。
                // 此时继续按 i 改写会把父引用写到别的群组上，宁可整段跳过（层级压平但不错乱）。
                if (pub.groups().size() != data.groups.size())
                {
                    break;
                }
                const uint64_t parent = data.groups[i].parentGroupSourceId;
                if (parent == 0u)
                {
                    continue;
                }
                auto it = groupIdMap.find(parent);
                if (it == groupIdMap.end())
                {
                    // 畸形文件：父群组不存在。降级为顶层，不丢图元。
                    pub.warnings().emplace_back("Group parent not found, treated as top-level");
                    ++stats.groupParentMissing;
                    continue;
                }
                pub.groups()[i].parentSourceId = it->second;
            }
            // 环检测：沿 parentSourceId 上溯，超过群组总数即说明有环，把该节点提为顶层断环。
            // 不做这一步的话，消费侧建 SyGroup 树时会无限递归。
            for (std::size_t i = 0; i < pub.groups().size(); ++i)
            {
                std::size_t hops = 0;
                uint64_t cur = pub.groups()[i].parentSourceId;
                bool corrupted = false;
                while (cur != 0u && hops <= pub.groups().size())
                {
                    // parentSourceId 本应是 addGroup 分配的 1-based 稠密 id，理论上必然落在
                    // [1, size] 内。但这是**外部文件驱动**的数据通路，一旦哪个解析器绕过
                    // groupIdMap 直接塞了别的值，下面的 cur - 1 就是越界读 —— 属于会读到
                    // 随机内存的那类崩溃，必须在这里挡住而不是相信上游。
                    if (cur > pub.groups().size())
                    {
                        corrupted = true;
                        break;
                    }
                    cur = pub.groups()[cur - 1u].parentSourceId;
                    ++hops;
                }
                if (corrupted || hops > pub.groups().size())
                {
                    pub.warnings().emplace_back("Group cycle detected, chain broken at this group");
                    ++stats.groupCycleBroken;
                    pub.groups()[i].parentSourceId = 0u;
                }
            }

            // ---- 图元 ----
            for (const auto& g : data.geometries)
            {
                const EntityType type = mapType(g.type);
                if (type == EntityType::Unknown)
                {
                    ++stats.unknownType;
                    if (stats.firstUnknownSourceId == 0)
                    {
                        stats.firstUnknownSourceId = g.sourceId;
                        stats.firstUnknownTypeValue = static_cast<int>(g.type);
                    }
                    continue;
                }

                EntityInfo info{};
                info.sourceId = g.sourceId;
                info.type = type;
                if (copyFixed(info.name, g.name))
                {
                    ++stats.nameTruncated;
                }
                info.lineWidth = g.lineWidth;
                info.visible = g.visible;
                info.locked = g.locked;

                // 引用不存在的图层 / 群组时保持默认哨兵（0 = 未分配 / 无群组）。
                // 这是畸形文件的常见症状，必须计数：否则「导入后图元全挤在默认图层上」
                // 这种现场只能靠肉眼看，日志里一点线索都没有。
                if (const auto it = layerIdMap.find(g.layerSourceId); it != layerIdMap.end())
                {
                    info.layerSourceId = it->second;
                }
                else if (g.layerSourceId != 0u)
                {
                    ++stats.missingLayerRef;
                    if (stats.firstMissingLayerSourceId == 0)
                    {
                        stats.firstMissingLayerSourceId = g.sourceId;
                    }
                }
                if (const auto it = groupIdMap.find(g.groupSourceId); it != groupIdMap.end())
                {
                    info.groupSourceId = it->second;
                }
                else if (g.groupSourceId != 0u)
                {
                    ++stats.missingGroupRef;
                }

                // 闭合折线统一映射为 Polygon：消费侧以 EntityType 判定 bClosed，
                // ParsedGeometry 上的 closed / polyline.closed 两个标记任一为真都算闭合。
                if (type == EntityType::Polyline && (g.closed || g.polyline.closed))
                {
                    info.type = EntityType::Polygon;
                }

                // 几何没写全（扩展数据溢出 / 该类型暂无 IR 通道）时图元仍入表，以保住
                // id 与图层归属；对应的 warning 在循环外按类别汇总一次（见下方），
                // 否则上层只看 entityCount，会把「空壳图元」误判成导入成功。
                projectGeometry(g, info, pub, stats);
                pub.entities().push_back(info);
            }

            // 静默降级按类别各汇总成一条 warning，而不是每个图元一条：
            // 畸形文件里这类问题动辄成千上万条，逐条塞进 warnings 只会把上层的
            // warningCount 和日志一起冲爆，反而看不出还有别的问题。
            if (stats.unknownType != 0)
            {
                pub.warnings().emplace_back(
                    "Geometries skipped due to unmappable type: " + std::to_string(stats.unknownType));
            }
            if (stats.smartLineSkipped != 0)
            {
                pub.warnings().emplace_back("SmartLine geometries imported as empty shells (no IR channel yet): "
                    + std::to_string(stats.smartLineSkipped));
            }
            if (stats.blobOverflow != 0)
            {
                pub.warnings().emplace_back("Entities imported as empty shells due to extension blob overflow: "
                    + std::to_string(stats.blobOverflow));
            }
            if (stats.missingLayerRef != 0)
            {
                pub.warnings().emplace_back(
                    "Dangling layer references, entities fell back to the unassigned layer: "
                    + std::to_string(stats.missingLayerRef));
            }
            if (stats.missingGroupRef != 0)
            {
                pub.warnings().emplace_back(
                    "Dangling group references, entities left ungrouped: " + std::to_string(stats.missingGroupRef));
            }
            if (stats.nameTruncated != 0)
            {
                pub.warnings().emplace_back("Names truncated to fit fixed-size buffers: "
                    + std::to_string(stats.nameTruncated));
            }

            for (const auto& w : data.warnings)
            {
                pub.warnings().push_back(w);
            }


            FioParseResult result = pub.publish(sourceFormat, sourceUnit);
            const char* fmt = sourceFormat != nullptr ? sourceFormat : "?";

            // 守恒口径：解析器产出多少条几何 -> IR 里最终有多少条图元。
            // 上层 ImportReaderBase 打的 "Converter dropped N of M" 里的 M 是 entityCount，
            // 也就是**已经**扣掉这里丢掉的量了，所以这一行是整条链上唯一能看到
            // 「解析出来了但没进 IR」的位置。
            SY_INFOF("[IrProjector] Projected %s: %zu parsed -> %u entities, %u layers, %u groups, "
                     "%zu blob bytes, %u warnings",
                fmt,
                data.geometries.size(),
                result.entityCount,
                result.layerCount,
                result.groupCount,
                result.extensionBlob.size,
                result.warningCount);

            if (stats.anyDegradation())
            {
                SY_WARNF("[IrProjector] Degraded while projecting %s: unknownType=%u smartLineSkipped=%u "
                         "blobOverflow=%u missingLayerRef=%u missingGroupRef=%u nameTruncated=%u "
                         "groupParentMissing=%u groupCycleBroken=%u",
                    fmt,
                    stats.unknownType,
                    stats.smartLineSkipped,
                    stats.blobOverflow,
                    stats.missingLayerRef,
                    stats.missingGroupRef,
                    stats.nameTruncated,
                    stats.groupParentMissing,
                    stats.groupCycleBroken);

                // 首个样本的 sourceId：拿着它可以回原文件（DXF 句柄 / 记录序号）定位到
                // 具体是哪一条数据触发的降级，比只知道「有 37 条出问题」有用得多。
                if (stats.unknownType != 0)
                {
                    SY_WARNF("[IrProjector] First unknown geometry type: raw=%d (%s), sourceId=%llu",
                        stats.firstUnknownTypeValue,
                        parsedTypeName(static_cast<ParsedGeometryType>(stats.firstUnknownTypeValue)),
                        static_cast<unsigned long long>(stats.firstUnknownSourceId));
                }
                if (stats.blobOverflow != 0)
                {
                    SY_WARNF("[IrProjector] First blob overflow at sourceId=%llu (entity became an empty shell)",
                        static_cast<unsigned long long>(stats.firstBlobOverflowSourceId));
                }
                if (stats.missingLayerRef != 0)
                {
                    SY_WARNF("[IrProjector] First dangling layer reference at sourceId=%llu "
                             "(entity fell back to the unassigned layer)",
                        static_cast<unsigned long long>(stats.firstMissingLayerSourceId));
                }
            }
            return result;
        }
    }  // namespace IrProjector
}  // namespace Fio
