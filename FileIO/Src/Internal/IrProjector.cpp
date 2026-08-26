#include "IrProjector.h"

#include "Log/SyLogger.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_map>


namespace Fio
{
    namespace
    {
        /// 把 std::string 安全写入固定长度 char 缓冲区（保证以 '\0' 结尾）
        template <std::size_t N>
        void copyFixed(char (&dst)[N], const std::string& src)
        {
            const std::size_t n = std::min(src.size(), N - 1);
            std::memcpy(dst, src.data(), n);
            dst[n] = '\0';
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
        // 1-based：0 保留为「未分配图层」哨兵（与 EntityInfo::layerSourceId 默认值一致）
        li.sourceId = static_cast<uint32_t>(m_layers.size()) + 1u;
        copyFixed(li.name, name);
        li.color = argbColor;
        li.visible = visible;
        li.locked = locked;
        m_layers.push_back(li);
        return li.sourceId;
    }

    uint64_t IrPublisher::addGroup(const std::string& name, uint64_t parentSourceId)
    {
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
        for (const auto& li : m_layers)
        {
            if (name == li.name)
            {
                return li.sourceId;
            }
        }
        return 0u;
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
            /// 投影单个图元的几何数据。扩展数据块布局必须与消费侧
            /// （Main/Src/Import/FioEntityConverter.cpp）的读取顺序严格一致。
            void projectGeometry(const ParsedGeometry& g, EntityInfo& info, IrPublisher& pub)
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
                    copyFixed(info.text.text, g.text.text);
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
                        break;
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
                        break;
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
                        break;
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
                        break;
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
                    SY_WARNF("[IrProjector] SmartLine is not representable in IR yet, sourceId=%llu",
                        static_cast<unsigned long long>(g.sourceId));
                    break;

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
                    SY_WARNF("[IrProjector] Group %llu references missing parent %llu, treated as top-level",
                        static_cast<unsigned long long>(data.groups[i].sourceId),
                        static_cast<unsigned long long>(parent));
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
                while (cur != 0u && hops <= pub.groups().size())
                {
                    cur = pub.groups()[cur - 1u].parentSourceId;
                    ++hops;
                }
                if (hops > pub.groups().size())
                {
                    pub.warnings().emplace_back("Group cycle detected, chain broken at this group");
                    SY_WARNF("[IrProjector] Group cycle detected at group %zu, promoted to top-level", i + 1u);
                    pub.groups()[i].parentSourceId = 0u;
                }
            }

            // ---- 图元 ----
            for (const auto& g : data.geometries)
            {
                const EntityType type = mapType(g.type);
                if (type == EntityType::Unknown)
                {
                    pub.warnings().emplace_back("Unknown geometry type skipped");
                    continue;
                }

                EntityInfo info{};
                info.sourceId = g.sourceId;
                info.type = type;
                copyFixed(info.name, g.name);
                info.lineWidth = g.lineWidth;
                info.visible = g.visible;
                info.locked = g.locked;

                if (const auto it = layerIdMap.find(g.layerSourceId); it != layerIdMap.end())
                {
                    info.layerSourceId = it->second;
                }
                if (const auto it = groupIdMap.find(g.groupSourceId); it != groupIdMap.end())
                {
                    info.groupSourceId = it->second;
                }

                // 闭合折线统一映射为 Polygon：消费侧以 EntityType 判定 bClosed，
                // ParsedGeometry 上的 closed / polyline.closed 两个标记任一为真都算闭合。
                if (type == EntityType::Polyline && (g.closed || g.polyline.closed))
                {
                    info.type = EntityType::Polygon;
                }

                projectGeometry(g, info, pub);
                pub.entities().push_back(info);
            }

            for (const auto& w : data.warnings)
            {
                pub.warnings().push_back(w);
            }

            FioParseResult result = pub.publish(sourceFormat, sourceUnit);
            SY_INFOF("[IrProjector] Projected %s: %u entities, %u layers, %u groups, %zu blob bytes",
                sourceFormat != nullptr ? sourceFormat : "?",
                result.entityCount,
                result.layerCount,
                result.groupCount,
                result.extensionBlob.size);
            return result;
        }
    }  // namespace IrProjector
}  // namespace Fio
