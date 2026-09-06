#include "FileIO/Parsers/DxfParser.h"
#include "FileIO/FileIOUtils.h"

#include "IrProjector.h"
#include "IrTransform.h"

#include "Log/SyLogger.h"

#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyEllipse.h"
#include "Engine2D/SyEntity/SyPoint.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Engine2D/SyEntity/SyNurbs.h"
#include "Engine2D/SyEntity/SyText.h"
#include "Ut/Vec.h"

#include "drw_interface.h"
#include "libdxfrw.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <iostream>
#include <limits>
#include <sstream>

namespace Fio
{
    FileFormat DxfParser::format() const
    {
        return FileFormat::DXF;
    }

    size_t DxfParser::formatName(char* buffer, size_t bufferSize) const
    {
        const char* name = "AutoCAD DXF";
        const size_t len = std::strlen(name);

        if (buffer != nullptr && bufferSize > len)
        {
            std::strcpy(buffer, name);
        }

        return len;
    }

    void DxfParser::forEachSupportedExtension(void (*visitor)(const char*, void*), void* ctx) const
    {
        visitor("dxf", ctx);
    }

    static bool isFinite2(const Ut::Vec2d& p)
    {
        return std::isfinite(p.x()) && std::isfinite(p.y());
    }

    static bool isFiniteScalar(double v)
    {
        return std::isfinite(v);
    }

    static bool isPositiveFinite(double v)
    {
        return std::isfinite(v) && v > 0.0;
    }

    static std::string makeWarning(const char* entityName, const char* reason)
    {
        std::ostringstream oss;
        oss << "[DxfParser] Skip " << entityName << ": " << reason;
        return oss.str();
    }

    // 将 0-255 RGB 打包为 0xAARRGGBB（与 Ut::Color / EntityInfo.color 约定一致）
    static uint32_t packRgb255(uint8_t r, uint8_t g, uint8_t b)
    {
        return 0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) |
            static_cast<uint32_t>(b);
    }

    // DXF 真彩色（组码 420 / DRW_*::color24，值域 0x00RRGGBB）转 0xAARRGGBB。
    // 调用前须自行判断 color24 >= 0，负值表示「未提供真彩色」。
    static uint32_t truecolorToArgb(int color24)
    {
        return 0xFF000000u | (static_cast<uint32_t>(color24) & 0x00FFFFFFu);
    }

    // ACI 颜色索引（组码 62，有效值 1-255）查 DXF 标准调色板，转 0xAARRGGBB。
    // 返回 0 表示索引无效（越界，或 0=BYBLOCK、256=BYLAYER 这类特殊值），由调用方决定回退策略。
    //
    // 入参用 long long 而非 int：DRW_Layer::color 由 dxfReader::getInt32() 直读、未做范围归一，
    // 畸形文件可能给出 INT_MIN，在 int 上取负是有符号溢出（UB）。
    static uint32_t aciToArgb(long long aci)
    {
        const long long index = aci < 0 ? -aci : aci;
        if (index >= 1 && index <= 255)
        {
            const auto& c = DRW::dxfColors[static_cast<size_t>(index)];
            return packRgb255(c[0], c[1], c[2]);
        }
        return 0u;
    }

    // 解析 DXF 图层颜色，返回 0xAARRGGBB；0 表示无法解析（未指定）。
    //
    // 返回 0 而不是兜底黑色，是为了与 resolveDxfColor 的「0=未指定」口径统一：
    // 图层色表用于 BYLAYER 实体查色，一旦把兜底黑塞进色表，色号非法的图层上的
    // BYLAYER 图元就会被强制打上不透明黑覆盖色（表现为整图变黑），而正确行为是
    // 不设覆盖色、交由渲染层回退默认色。需要展示用兜底色的地方（如 IrLayerInfo.color）
    // 由调用方自行补，不要混进色表。
    //
    // DRW_Layer::color 为负是 DXF 约定的「图层关闭(off)」标记，取绝对值后仍是有效索引，
    // 可见性另由 layerIsVisible() 判定，两者刻意拆开。
    static uint32_t resolveLayerColor(const DRW_Layer& layer)
    {
        if (layer.color24 >= 0)
        {
            return truecolorToArgb(layer.color24);
        }
        return aciToArgb(layer.color);
    }

    // 图层可见性：flags 第 0 位为冻结(frozen)，color 为负表示图层关闭(off)。
    static bool layerIsVisible(const DRW_Layer& layer)
    {
        return (layer.flags & 1) == 0 && layer.color >= 0;
    }

    // 解析 DXF 实体颜色，返回 0xAARRGGBB；0 表示未指定（渲染回退到图层/默认色）。
    // 优先级：真彩色(420/color24) > ACI 索引(62/color) > BYLAYER/BYBLOCK 用图层颜色。
    static uint32_t resolveDxfColor(const DRW_Entity& e, const std::map<std::string, uint32_t>& layerColors)
    {
        if (e.color24 >= 0)
        {
            return truecolorToArgb(e.color24);
        }

        // BYLAYER(256) / BYBLOCK(0)：用图层颜色；未知图层回退为未指定
        if (e.color == 256 || e.color == 0)
        {
            auto it = layerColors.find(e.layer);
            return it != layerColors.end() ? it->second : 0u;
        }
        return aciToArgb(e.color);
    }

    // DXF 的 $INSUNITS（组码 70）单位编码 → 单位名。返回 nullptr 表示未定义/未指定，
    // 此时 FioParseResult::sourceUnit 留空，由上层按「与当前文档同单位」处理。
    static const char* insUnitsToName(int code)
    {
        switch (code)
        {
        case 1:
            return "inch";
        case 2:
            return "ft";
        case 4:
            return "mm";
        case 5:
            return "cm";
        case 6:
            return "m";
        case 8:
            return "mil";
        case 9:
            return "um";
        case 10:
            return "yd";
        case 11:
            return "angstrom";
        case 12:
            return "nm";
        case 13:
            return "micron";
        case 14:
            return "dm";
        case 15:
            return "dam";
        case 16:
            return "hm";
        case 17:
            return "km";
        default:
            // 0 = Unitless，其余为天文/英制细分单位，2D 工艺场景用不到
            return nullptr;
        }
    }

    // OCS → WCS 变换。
    //
    // DXF 里 CIRCLE / ARC / LWPOLYLINE / 2D POLYLINE / TEXT / SOLID 等实体的坐标记在
    // 「对象坐标系(OCS)」中，OCS 由挤出方向（extrusion，组码 210/220/230）确定。
    // 最常见的情形是挤出方向为 (0,0,-1)——AutoCAD 中镜像过的图元，此时 OCS 的 X 轴反向；
    // 不做换算就会导入成左右颠倒，而且圆弧起止角也是反的。早先本解析器完全没读挤出方向。
    //
    // 这里按 DXF 规范的 Arbitrary Axis Algorithm 求出 OCS 的 X/Y 轴，取它们的 XY 分量
    // 组成 2D 仿射矩阵。挤出方向为一般空间向量时，这等价于把 OCS 平面正投影到 WCS 的
    // XY 平面——对 2D 工艺软件是可接受的近似（真正的 3D 需要走 Mesh 通道）。
    static IrXform ocsToWcsXform(const DRW_Coord& ext)
    {
        const double len = std::sqrt(ext.x * ext.x + ext.y * ext.y + ext.z * ext.z);
        if (!std::isfinite(len) || len < 1e-12)
        {
            return IrXform{};  // 挤出方向缺失或退化：按标准 XY 平面处理
        }

        const double nx = ext.x / len;
        const double ny = ext.y / len;
        const double nz = ext.z / len;

        // 已经是标准 XY 平面，省掉一次无意义的矩阵乘
        if (std::fabs(nx) < 1e-12 && std::fabs(ny) < 1e-12 && nz > 0.0)
        {
            return IrXform{};
        }

        // 规范定义的阈值 1/64：接近 Z 轴时改用 Wy 叉乘，避免叉积退化
        double axx = 0.0, axy = 0.0, axz = 0.0;
        if (std::fabs(nx) < (1.0 / 64.0) && std::fabs(ny) < (1.0 / 64.0))
        {
            // Ax = Wy × N
            axx = 1.0 * nz - 0.0 * ny;
            axy = 0.0 * nx - 0.0 * nz;
            axz = 0.0 * ny - 1.0 * nx;
        }
        else
        {
            // Ax = Wz × N
            axx = 0.0 * nz - 1.0 * ny;
            axy = 1.0 * nx - 0.0 * nz;
            axz = 0.0 * ny - 0.0 * nx;
        }
        const double axLen = std::sqrt(axx * axx + axy * axy + axz * axz);
        if (axLen < 1e-12)
        {
            return IrXform{};
        }
        axx /= axLen;
        axy /= axLen;
        axz /= axLen;

        // Ay = N × Ax
        const double ayx = ny * axz - nz * axy;
        const double ayy = nz * axx - nx * axz;

        IrXform xf;
        xf.a = axx;
        xf.b = axy;
        xf.c = ayx;
        xf.d = ayy;
        return xf;
    }

    // bulge（组码 42）= tan(包角/4)，表示这一段折线其实是圆弧。
    // Engine 的多边形只有顶点表、承载不了圆弧参数，所以按弦高容差把圆弧细分成折线段。
    // 这是形状保真与既有数据结构之间的取舍：容差取弦长的 0.2%，角步长上限 6°，
    // 单段细分点数上限 256（防畸形 bulge 造成海量顶点）。
    // 只追加中间点，起点/终点由调用方负责，避免重复顶点。
    static void appendBulgeArcPoints(std::vector<double>& out, double x0, double y0, double x1, double y1, double bulge)
    {
        const double chordX = x1 - x0;
        const double chordY = y1 - y0;
        const double chord = std::sqrt(chordX * chordX + chordY * chordY);
        if (!std::isfinite(bulge) || std::fabs(bulge) < 1e-12 || chord < 1e-12)
        {
            return;
        }

        const double included = 4.0 * std::atan(bulge);  // 包角，带符号（正=逆时针）
        const double radius = chord / (2.0 * std::sin(std::fabs(included) / 2.0));
        if (!std::isfinite(radius) || radius < 1e-12)
        {
            return;
        }

        // 圆心在弦中点沿弦左法线偏移 |r*cos(θ/2)|：包角 ≤180° 时偏向 bulge 符号一侧，
        // >180° 时偏向反侧（劣弧/优弧的区别）。符号弄反会让圆弧朝相反方向鼓出。
        const double midX = (x0 + x1) * 0.5;
        const double midY = (y0 + y1) * 0.5;
        const double h = std::sqrt(std::fmax(radius * radius - (chord * 0.5) * (chord * 0.5), 0.0));
        const double sign = (included > 0.0) ? 1.0 : -1.0;
        const double dirX = -chordY / chord;
        const double dirY = chordX / chord;
        const double offset = (std::fabs(included) > M_PI) ? -h : h;
        const double cx = midX + sign * offset * dirX;
        const double cy = midY + sign * offset * dirY;

        const double a0 = std::atan2(y0 - cy, x0 - cx);
        const double tol = std::fmax(chord * 0.002, 1e-9);
        // 弦高容差换算出的最大角步长；再与 6° 取小
        const double maxStepByTol = 2.0 * std::acos(std::fmax(1.0 - tol / radius, -1.0));
        const double maxStep = std::fmin(std::fmax(maxStepByTol, 1e-3), 6.0 * M_PI / 180.0);
        int steps = static_cast<int>(std::ceil(std::fabs(included) / maxStep));
        steps = std::clamp(steps, 1, 256);

        for (int i = 1; i < steps; ++i)
        {
            const double a = a0 + included * (static_cast<double>(i) / steps);
            out.push_back(cx + radius * std::cos(a));
            out.push_back(cy + radius * std::sin(a));
        }
    }



    class DxfConverter : public DRW_Interface
    {
    public:
        DxfConverter(VecSyEntityPtr& outEntities, std::vector<std::string>& warnings)
            : m_outEntities(outEntities)
            , m_warnings(warnings)
        {
        }

        void addHeader(const DRW_Header*) override {}

        void addLType(const DRW_LType&) override {}

        void addDimStyle(const DRW_Dimstyle&) override {}

        void addVport(const DRW_Vport&) override {}

        void addTextStyle(const DRW_Textstyle&) override {}

        void addAppId(const DRW_AppId&) override {}

        void addBlock(const DRW_Block&) override {}

        void setBlock(const int) override {}

        void endBlock() override {}

        void addRay(const DRW_Ray&) override {}

        void addXline(const DRW_Xline&) override {}

        void addLWPolyline(const DRW_LWPolyline& data) override
        {
            if (data.vertlist.empty())
            {
                warnSkip("LWPOLYLINE", "empty vertex list");
                return;
            }

            auto syLine = std::make_unique<Eg::SyLine>();
            for (const auto& vert : data.vertlist)
            {
                if (!vert)
                {
                    continue;
                }

                Ut::Vec2d p(vert->x, vert->y);

                if (!isFinite2(p))
                {
                    warnSkip("LWPOLYLINE", "non-finite vertex");
                    return;
                }

                syLine->addPoint(p);
            }

            if (syLine->pointRef().size() < 2)
            {
                warnSkip("LWPOLYLINE", "less than 2 valid vertices");
                return;
            }

            syLine->basePoint = syLine->pointRef().front();
            syLine->bClosed = (data.flags & 1) != 0;
            applyEntityStyle(syLine.get(), data);
            m_outEntities.push_back(std::move(syLine));
        }

        void addSpline(const DRW_Spline* data) override
        {
            if (!data || data->controllist.empty())
            {
                warnSkip("SPLINE", "empty control points");
                return;
            }

            auto sySpline = std::make_unique<Eg::SyNurbs>();
            sySpline->nDegree = data->degree;
            sySpline->setKnotVector(data->knotslist);
            sySpline->setWeightVector(data->weightlist);

            for (const auto& cp : data->controllist)
            {
                if (!cp)
                {
                    continue;
                }

                Ut::Vec2d p(cp->x, cp->y);
                if (!isFinite2(p))
                {
                    warnSkip("SPLINE", "non-finite control point");
                    return;
                }

                sySpline->addControlPoint(p);
            }

            if (sySpline->controlPointCount() == 0)
            {
                warnSkip("SPLINE", "no valid control points");
                return;
            }

            for (double k : sySpline->knotRef())
            {
                if (!isFiniteScalar(k))
                {
                    warnSkip("SPLINE", "non-finite knot value");
                    return;
                }
            }

            for (double w : sySpline->weightRef())
            {
                if (!isFiniteScalar(w) || w <= 0.0)
                {
                    warnSkip("SPLINE", "invalid weight value");
                    return;
                }
            }

            sySpline->basePoint = sySpline->controlPointAt(0);
            applyEntityStyle(sySpline.get(), *data);
            m_outEntities.push_back(std::move(sySpline));
        }

        void addKnot(const DRW_Entity&) override {}

        void addInsert(const DRW_Insert&) override {}

        void addTrace(const DRW_Trace&) override {}

        void add3dFace(const DRW_3Dface&) override {}

        void addSolid(const DRW_Solid&) override {}

        void addMText(const DRW_MText& data) override
        {
            Ut::Vec2d p(data.basePoint.x, data.basePoint.y);

            if (!isFinite2(p) || !isFiniteScalar(data.height))
            {
                warnSkip("MTEXT", "invalid position or height");
                return;
            }

            auto syText = std::make_unique<Eg::SyText>();
            syText->basePoint = p;
            syText->dHeight = data.height;
            syText->setText(data.text.c_str());
            syText->dRotation = data.angle * M_PI / 180.0;
            applyEntityStyle(syText.get(), data);
            m_outEntities.push_back(std::move(syText));
        }

        void addDimAlign(const DRW_DimAligned*) override {}

        void addDimLinear(const DRW_DimLinear*) override {}

        void addDimRadial(const DRW_DimRadial*) override {}

        void addDimDiametric(const DRW_DimDiametric*) override {}

        void addDimAngular(const DRW_DimAngular*) override {}

        void addDimAngular3P(const DRW_DimAngular3p*) override {}

        void addDimOrdinate(const DRW_DimOrdinate*) override {}

        void addLeader(const DRW_Leader*) override {}

        void addHatch(const DRW_Hatch*) override {}

        void addViewport(const DRW_Viewport&) override {}

        void addImage(const DRW_Image*) override {}

        void linkImage(const DRW_ImageDef*) override {}

        void addComment(const char*) override {}

        void addPlotSettings(const DRW_PlotSettings*) override {}

        void writeHeader(DRW_Header&) override {}

        void writeBlocks() override {}

        void writeBlockRecords() override {}

        void writeEntities() override {}

        void writeLTypes() override {}

        void writeLayers() override {}

        void writeTextstyles() override {}

        void writeVports() override {}

        void writeDimstyles() override {}

        void writeObjects() override {}

        void writeAppId() override {}

        void addLayer(const DRW_Layer& layer) override
        {
            m_layerDefs.push_back(layer);

            // 记录图层颜色，供 BYLAYER 实体解析（与 parseToIR 中 IrLayerInfo 的颜色口径共用同一函数）。
            // 只在解析成功时写入：色号非法的图层不进色表，resolveDxfColor 查不到即返回
            // 0（未指定），交由渲染层回退默认色，避免把兜底色当成图层真实颜色。
            if (const uint32_t color = resolveLayerColor(layer); color != 0u)
            {
                m_layerColorMap[layer.name] = color;
            }
        }

        void addPoint(const DRW_Point& point) override
        {
            Ut::Vec2d p(point.basePoint.x, point.basePoint.y);

            if (!isFinite2(p))
            {
                warnSkip("POINT", "non-finite position");
                return;
            }

            auto syPoint = std::make_unique<Eg::SyPoint>();
            syPoint->basePoint = p;
            applyEntityStyle(syPoint.get(), point);
            m_outEntities.push_back(std::move(syPoint));
        }

        void addLine(const DRW_Line& line) override
        {
            Ut::Vec2d p0(line.basePoint.x, line.basePoint.y);
            Ut::Vec2d p1(line.secPoint.x, line.secPoint.y);

            if (!isFinite2(p0) || !isFinite2(p1))
            {
                warnSkip("LINE", "non-finite endpoint");
                return;
            }

            auto syLine = std::make_unique<Eg::SyLine>();
            syLine->addPoint(p0);
            syLine->addPoint(p1);
            syLine->basePoint = syLine->pointRef().front();
            applyEntityStyle(syLine.get(), line);
            m_outEntities.push_back(std::move(syLine));
        }

        void addCircle(const DRW_Circle& circle) override
        {
            Ut::Vec2d c(circle.basePoint.x, circle.basePoint.y);
            double r = circle.radious;

            if (!isFinite2(c) || !isPositiveFinite(r))
            {
                warnSkip("CIRCLE", "invalid center or radius");
                return;
            }

            auto syCircle = std::make_unique<Eg::SyCircle>();
            syCircle->basePoint = c;
            syCircle->dRadius = r;
            applyEntityStyle(syCircle.get(), circle);
            m_outEntities.push_back(std::move(syCircle));
        }

        void addArc(const DRW_Arc& arc) override
        {
            Ut::Vec2d c(arc.basePoint.x, arc.basePoint.y);
            double r = arc.radious;

            if (!isFinite2(c) || !isPositiveFinite(r) || !isFiniteScalar(arc.staangle) || !isFiniteScalar(arc.endangle))
            {
                warnSkip("ARC", "invalid center, radius, or angle");
                return;
            }

            auto syArc = std::make_unique<Eg::SyArc>();
            syArc->basePoint = c;
            syArc->dRadius = r;
            syArc->dStartAngle = arc.staangle;
            syArc->dEndAngle = arc.endangle;
            applyEntityStyle(syArc.get(), arc);
            m_outEntities.push_back(std::move(syArc));
        }

        void addEllipse(const DRW_Ellipse& ellipse) override
        {
            Ut::Vec2d c(ellipse.basePoint.x, ellipse.basePoint.y);

            double majorLen =
                std::sqrt(ellipse.secPoint.x * ellipse.secPoint.x + ellipse.secPoint.y * ellipse.secPoint.y);
            double ratio = ellipse.ratio;
            double rotation = std::atan2(ellipse.secPoint.y, ellipse.secPoint.x);

            if (!isFinite2(c) || !isFiniteScalar(ellipse.secPoint.x) || !isFiniteScalar(ellipse.secPoint.y) ||
                !isPositiveFinite(majorLen) || !isFiniteScalar(ratio) || ratio <= 0.0 ||
                !isFiniteScalar(ellipse.staparam) || !isFiniteScalar(ellipse.endparam) || !isFiniteScalar(rotation))
            {
                warnSkip("ELLIPSE", "invalid geometry");
                return;
            }

            auto syEllipse = std::make_unique<Eg::SyEllipse>();
            syEllipse->basePoint = c;
            syEllipse->dRadiusX = majorLen;
            syEllipse->dRadiusY = majorLen * ratio;
            syEllipse->dRotation = rotation;
            syEllipse->dStartAngle = ellipse.staparam;
            syEllipse->dEndAngle = ellipse.endparam;
            applyEntityStyle(syEllipse.get(), ellipse);
            m_outEntities.push_back(std::move(syEllipse));
        }

        void addPolyline(const DRW_Polyline& polyline) override
        {
            if (polyline.vertlist.empty())
            {
                warnSkip("POLYLINE", "empty vertex list");
                return;
            }

            // 跳过 3D 多边形网格 / 多面网格（flags 的 16/64 位）：它们不是 2D 轮廓，
            // 若按顶点顺序连成一条线会产生大量多余线段。
            if ((polyline.flags & 16) != 0 || (polyline.flags & 64) != 0)
            {
                warnSkip("POLYLINE", "3D mesh / polyface mesh, not drawn as 2D line");
                return;
            }

            auto syLine = std::make_unique<Eg::SyLine>();
            for (const auto& vert : polyline.vertlist)
            {
                if (!vert)
                {
                    continue;
                }

                Ut::Vec2d p(vert->basePoint.x, vert->basePoint.y);
                if (!isFinite2(p))
                {
                    warnSkip("POLYLINE", "non-finite vertex");
                    return;
                }

                syLine->addPoint(p);
            }

            if (syLine->pointRef().size() < 2)
            {
                warnSkip("POLYLINE", "less than 2 valid vertices");
                return;
            }

            syLine->basePoint = syLine->pointRef().front();
            syLine->bClosed = (polyline.flags & 1) != 0;
            applyEntityStyle(syLine.get(), polyline);
            m_outEntities.push_back(std::move(syLine));
        }

        void addText(const DRW_Text& text) override
        {
            Ut::Vec2d p(text.basePoint.x, text.basePoint.y);

            if (!isFinite2(p) || !isFiniteScalar(text.height))
            {
                warnSkip("TEXT", "invalid position or height");
                return;
            }

            auto syText = std::make_unique<Eg::SyText>();
            syText->basePoint = p;
            syText->dHeight = text.height;
            syText->setText(text.text.c_str());
            syText->dRotation = text.angle * M_PI / 180.0;
            applyEntityStyle(syText.get(), text);
            m_outEntities.push_back(std::move(syText));
        }

        const std::vector<DRW_Layer>& getLayerDefs() const
        {
            return m_layerDefs;
        }

        const std::map<size_t, std::string>& getEntityLayerMap() const
        {
            return m_entityLayerMap;
        }

        const std::map<size_t, int>& getEntityColorMap() const
        {
            return m_entityColorMap;
        }

    private:
        void warnSkip(const char* entityName, const char* reason)
        {
            m_warnings.push_back(makeWarning(entityName, reason));
        }

        void applyEntityStyle(Eg::SyEntity* entity, const DRW_Entity& drwEntity)
        {
            (void)entity;
            size_t idx = m_outEntities.size();

            if (!drwEntity.layer.empty())
            {
                m_entityLayerMap[idx] = drwEntity.layer;
            }

            if (drwEntity.color >= 0 && drwEntity.color != 256)
            {
                m_entityColorMap[idx] = drwEntity.color;
            }
        }

    private:
        VecSyEntityPtr& m_outEntities;
        std::vector<std::string>& m_warnings;
        std::vector<DRW_Layer> m_layerDefs;
        std::map<std::string, uint32_t> m_layerColorMap;
        std::map<size_t, std::string> m_entityLayerMap;
        std::map<size_t, int> m_entityColorMap;
    };

    class DxfIrConverter : public DRW_Interface
    {
    public:
        // 嵌套块引用的展开深度上限：畸形文件可能出现 A 引用 B、B 又引用 A 的环，
        // 或者深达上百层的引用链，两者都会把内存吃光，必须有硬上限。
        static constexpr int kMaxBlockDepth = 16;
        // 单个文件展开后的图元总数上限：阵列引用（colcount*rowcount）叠加嵌套后
        // 增长是乘性的，1 个 INSERT 可能膨胀成千万级图元。
        static constexpr size_t kMaxExpandedEntities = 2'000'000;
        // 单次引用的阵列行/列上限
        static constexpr int kMaxArrayCount = 4096;


        explicit DxfIrConverter(IrPublisher& pub, std::vector<std::string>& warnings)
            : m_pub(pub)
            , m_warnings(warnings)
        {
        }

        void addHeader(const DRW_Header* header) override
        {
            // $INSUNITS 是图纸自身声明的单位。早先本回调是空实现，FioParseResult::sourceUnit
            // 永远为空串，英制图纸导入后尺寸差 25.4 倍且无从察觉。
            if (header == nullptr)
            {
                return;
            }
            int units = 0;
            // DRW_Header::getInt 是 private，只能走公开的 vars 表。
            // $INSUNITS 是组码 70 的整型变量；类型不符时按未声明处理，不硬转。
            const auto it = header->vars.find("$INSUNITS");
            if (it == header->vars.end() || it->second == nullptr ||
                it->second->type() != DRW_Variant::INTEGER)
            {
                return;
            }
            units = static_cast<int>(it->second->content.i);

            if (const char* name = insUnitsToName(units); name != nullptr)
            {
                m_sourceUnit = name;
                SY_INFOF("[DxfParser] Header $INSUNITS=%d resolved to unit '%s'", units, name);
            }
            else
            {
                SY_INFOF("[DxfParser] Header $INSUNITS=%d is unitless or unsupported, unit left empty", units);
            }
        }


        /// 图纸单位（来自 $INSUNITS）；空串表示文件未声明
        const std::string& sourceUnit() const
        {
            return m_sourceUnit;
        }



        void addLType(const DRW_LType&) override {}

        void addDimStyle(const DRW_Dimstyle&) override {}

        void addVport(const DRW_Vport&) override {}

        void addTextStyle(const DRW_Textstyle&) override {}

        void addAppId(const DRW_AppId&) override {}

        // libdxfrw 的回调时序：BLOCKS 段里每个块先回调 addBlock()，随后该块内部的图元
        // 照常走 addLine/addCircle/... 回调，最后回调 endBlock()。
        // 早先 addBlock/endBlock 都是空实现，于是**块定义里的图元被当成模型空间图元直接产出**：
        // 未被引用的块也会画出来，被引用的块画两遍（定义一遍、引用一遍还没实现所以少一遍）。
        // 这里改为在块定义期间把图元改道到块自己的缓冲区，只有 INSERT 才实例化。
        void addBlock(const DRW_Block& block) override
        {
            BlockDef& def = m_blocks[block.name];
            def.name = block.name;
            def.baseX = block.basePoint.x;
            def.baseY = block.basePoint.y;
            m_currentBlock = &def;
        }

        // DWG 路径下用块句柄切换当前块；DXF 路径不会用到，保持空实现。
        void setBlock(const int) override {}

        void endBlock() override
        {
            m_currentBlock = nullptr;
        }

        void addRay(const DRW_Ray&) override
        {
            warnSkip("RAY", "infinite construction line is not importable");
        }

        void addXline(const DRW_Xline&) override
        {
            warnSkip("XLINE", "infinite construction line is not importable");
        }

        void addKnot(const DRW_Entity&) override {}

        void addInsert(const DRW_Insert& insert) override
        {
            InsertRef ref;
            ref.blockName = insert.name;
            ref.x = insert.basePoint.x;
            ref.y = insert.basePoint.y;
            ref.sx = insert.xscale;
            ref.sy = insert.yscale;
            ref.angle = insert.angle;  // libdxfrw 已转成弧度
            ref.colcount = insert.colcount;
            ref.rowcount = insert.rowcount;
            ref.colspace = insert.colspace;
            ref.rowspace = insert.rowspace;
            ref.layerSourceId = m_pub.findLayer(insert.layer);
            ref.color = resolveDxfColor(insert, m_layerColorMap);

            if (m_currentBlock != nullptr)
            {
                // 块定义内部的嵌套引用：被引用的块可能还没读到（BLOCKS 段里顺序不保证），
                // 因此只登记引用，等模型空间实例化时再递归展开。
                m_currentBlock->inserts.push_back(std::move(ref));
                return;
            }

            expandInsert(ref, IrXform{}, 0u, 0);
        }


        void addTrace(const DRW_Trace& trace) override
        {
            emitQuad(trace, trace.extPoint, "TRACE");
        }

        void add3dFace(const DRW_3Dface& face) override
        {
            // 3DFACE 是空间四边形；2D 工艺场景按其 XY 投影当闭合轮廓处理，
            // 与 SOLID/TRACE 共用同一套顶点顺序修正。
            emitQuad(face, face.extPoint, "3DFACE");
        }

        void addSolid(const DRW_Solid& solid) override
        {
            emitQuad(solid, solid.extPoint, "SOLID");
        }

        // 标注类实体（DIMENSION 的 7 种变体）都是由标注样式驱动、运行期生成几何的复合体，
        // 逐一还原成图元既不现实也无意义（切割不需要标注）。统一发 warning 而不是静默丢弃：
        // 早先这些回调是空实现，用户完全看不出图纸里有内容没被导入。
        void addDimAlign(const DRW_DimAligned*) override
        {
            warnSkip("DIMENSION", "aligned dimension is not imported");
        }

        void addDimLinear(const DRW_DimLinear*) override
        {
            warnSkip("DIMENSION", "linear dimension is not imported");
        }

        void addDimRadial(const DRW_DimRadial*) override
        {
            warnSkip("DIMENSION", "radial dimension is not imported");
        }

        void addDimDiametric(const DRW_DimDiametric*) override
        {
            warnSkip("DIMENSION", "diametric dimension is not imported");
        }

        void addDimAngular(const DRW_DimAngular*) override
        {
            warnSkip("DIMENSION", "angular dimension is not imported");
        }

        void addDimAngular3P(const DRW_DimAngular3p*) override
        {
            warnSkip("DIMENSION", "3-point angular dimension is not imported");
        }

        void addDimOrdinate(const DRW_DimOrdinate*) override
        {
            warnSkip("DIMENSION", "ordinate dimension is not imported");
        }

        void addLeader(const DRW_Leader*) override
        {
            warnSkip("LEADER", "leader annotation is not imported");
        }

        void addHatch(const DRW_Hatch*) override
        {
            // 填充的边界路径本身是可导入的几何，但填充图案不是；先明确告知，
            // 待边界路径通道（HATCH → 群组 + 若干闭合轮廓）实现后再补。
            warnSkip("HATCH", "hatch fill is not imported");
        }

        void addViewport(const DRW_Viewport&) override
        {
            // 视口是布局元素，不是图形内容，静默跳过是正确行为
        }

        void addImage(const DRW_Image*) override
        {
            // DXF 的 IMAGE 只存外部文件引用（IMAGEDEF），像素数据不在 DXF 里；
            // 要支持得先按 IMAGEDEF 的路径去加载外部图片，属于独立能力。
            warnSkip("IMAGE", "external image reference is not resolved");
        }

        void linkImage(const DRW_ImageDef*) override {}

        void addComment(const char*) override {}

        void addPlotSettings(const DRW_PlotSettings*) override {}


        void writeHeader(DRW_Header&) override {}

        void writeBlocks() override {}

        void writeBlockRecords() override {}

        void writeEntities() override {}

        void writeLTypes() override {}

        void writeLayers() override {}

        void writeTextstyles() override {}

        void writeVports() override {}

        void writeDimstyles() override {}

        void writeObjects() override {}

        void writeAppId() override {}

        void addLayer(const DRW_Layer& layer) override
        {
            // 记录图层颜色，供 BYLAYER 实体解析（与 IrLayerInfo 的颜色口径共用同一函数）。
            // 只在解析成功时写入：色号非法的图层不进色表，resolveDxfColor 查不到即返回
            // 0（未指定），交由渲染层回退默认色，避免把兜底色当成图层真实颜色。
            const uint32_t color = resolveLayerColor(layer);
            if (color != 0u)
            {
                m_layerColorMap[layer.name] = color;
            }

            // DXF 的 TABLES 段一定在 BLOCKS/ENTITIES 之前，所以图层在图元之前就全部登记完毕，
            // 可以在图元产出的当场解析 layerSourceId，不再需要「先记图层名、读完再回填」的二次遍历。
            // resolveLayerColor 失败时补不透明黑：IrLayerInfo.color 是展示用颜色，需要确定值。
            m_pub.addLayer(layer.name, color != 0u ? color : 0xFF000000u, layerIsVisible(layer));
        }

        void addPoint(const DRW_Point& point) override
        {
            Ut::Vec2d p(point.basePoint.x, point.basePoint.y);
            if (!isFinite2(p))
            {
                warnSkip("POINT", "non-finite position");
                return;
            }

            EntityInfo info;
            info.type = EntityType::Point;
            info.line.x1 = p.x();
            info.line.y1 = p.y();
            emit(info, point);
        }


        void addLine(const DRW_Line& line) override
        {
            Ut::Vec2d p0(line.basePoint.x, line.basePoint.y);
            Ut::Vec2d p1(line.secPoint.x, line.secPoint.y);
            if (!isFinite2(p0) || !isFinite2(p1))
            {
                warnSkip("LINE", "non-finite endpoint");
                return;
            }

            EntityInfo info;
            info.type = EntityType::Line;
            info.line.x1 = p0.x();
            info.line.y1 = p0.y();
            info.line.x2 = p1.x();
            info.line.y2 = p1.y();
            emit(info, line);
        }

        void addCircle(const DRW_Circle& circle) override
        {
            Ut::Vec2d c(circle.basePoint.x, circle.basePoint.y);
            double r = circle.radious;
            if (!isFinite2(c) || !isPositiveFinite(r))
            {
                warnSkip("CIRCLE", "invalid center or radius");
                return;
            }

            EntityInfo info;
            info.type = EntityType::Circle;
            info.circle.cx = c.x();
            info.circle.cy = c.y();
            info.circle.r = r;
            emit(info, circle, &circle.extPoint);
        }

        void addArc(const DRW_Arc& arc) override
        {
            Ut::Vec2d c(arc.basePoint.x, arc.basePoint.y);
            double r = arc.radious;
            if (!isFinite2(c) || !isPositiveFinite(r) || !isFiniteScalar(arc.staangle) || !isFiniteScalar(arc.endangle))
            {
                warnSkip("ARC", "invalid center, radius, or angle");
                return;
            }

            EntityInfo info;
            info.type = EntityType::Arc;
            info.arc.cx = c.x();
            info.arc.cy = c.y();
            info.arc.r = r;
            info.arc.sa = arc.staangle;
            info.arc.ea = arc.endangle;
            emit(info, arc, &arc.extPoint);
        }

        void addEllipse(const DRW_Ellipse& ellipse) override
        {
            Ut::Vec2d c(ellipse.basePoint.x, ellipse.basePoint.y);
            double majorLen =
                std::sqrt(ellipse.secPoint.x * ellipse.secPoint.x + ellipse.secPoint.y * ellipse.secPoint.y);
            double ratio = ellipse.ratio;
            double rotation = std::atan2(ellipse.secPoint.y, ellipse.secPoint.x);

            if (!isFinite2(c) || !isFiniteScalar(ellipse.secPoint.x) || !isFiniteScalar(ellipse.secPoint.y) ||
                !isPositiveFinite(majorLen) || !isFiniteScalar(ratio) || ratio <= 0.0 ||
                !isFiniteScalar(ellipse.staparam) || !isFiniteScalar(ellipse.endparam) || !isFiniteScalar(rotation))
            {
                warnSkip("ELLIPSE", "invalid geometry");
                return;
            }

            EntityInfo info;
            info.type = EntityType::Ellipse;
            info.ellipse.cx = c.x();
            info.ellipse.cy = c.y();
            info.ellipse.rx = majorLen;
            info.ellipse.ry = majorLen * ratio;
            info.ellipse.rot = rotation;
            info.ellipse.sa = ellipse.staparam;
            info.ellipse.ea = ellipse.endparam;
            emit(info, ellipse, &ellipse.extPoint);
        }

        void addText(const DRW_Text& text) override
        {
            Ut::Vec2d p(text.basePoint.x, text.basePoint.y);
            if (!isFinite2(p) || !isFiniteScalar(text.height))
            {
                warnSkip("TEXT", "invalid position or height");
                return;
            }

            EntityInfo info;
            info.type = EntityType::Text;
            info.text.x = p.x();
            info.text.y = p.y();
            info.text.h = text.height;
            info.text.a = text.angle * M_PI / 180.0;

            std::strncpy(info.text.text, text.text.c_str(), sizeof(info.text.text) - 1);
            info.text.text[sizeof(info.text.text) - 1] = '\0';
            emit(info, text, &text.extPoint);
        }

        void addMText(const DRW_MText& data) override
        {
            Ut::Vec2d p(data.basePoint.x, data.basePoint.y);
            if (!isFinite2(p) || !isFiniteScalar(data.height))
            {
                warnSkip("MTEXT", "invalid position or height");
                return;
            }

            EntityInfo info;
            info.type = EntityType::Text;
            info.text.x = p.x();
            info.text.y = p.y();
            info.text.h = data.height;
            info.text.a = data.angle * M_PI / 180.0;
            std::strncpy(info.text.text, data.text.c_str(), sizeof(info.text.text) - 1);
            info.text.text[sizeof(info.text.text) - 1] = '\0';
            emit(info, data, &data.extPoint);
        }

        void addLWPolyline(const DRW_LWPolyline& data) override
        {
            if (data.vertlist.empty())
            {
                warnSkip("LWPOLYLINE", "empty vertex list");
                return;
            }

            std::vector<double> verts;
            verts.reserve(data.vertlist.size() * 2);
            for (size_t i = 0; i < data.vertlist.size(); ++i)
            {
                const auto& vert = data.vertlist[i];
                if (!vert)
                {
                    continue;
                }
                Ut::Vec2d p(vert->x, vert->y);
                if (!isFinite2(p))
                {
                    warnSkip("LWPOLYLINE", "non-finite vertex");
                    return;
                }
                verts.push_back(p.x());
                verts.push_back(p.y());

                // bulge 记在「本段起点顶点」上，描述本顶点到下一顶点之间是圆弧。
                // 早先这个字段被完全忽略，带圆角的轮廓会被拉成直线弦，切割件直接报废。
                const size_t next = i + 1;
                const bool hasNext = next < data.vertlist.size();
                const bool closed = (data.flags & 1) != 0;
                if (std::fabs(vert->bulge) > 1e-12 && (hasNext || closed))
                {
                    const auto& nv = hasNext ? data.vertlist[next] : data.vertlist.front();
                    if (nv)
                    {
                        appendBulgeArcPoints(verts, vert->x, vert->y, nv->x, nv->y, vert->bulge);
                    }
                }
            }

            // flags 位 0 = 闭合。闭合与否早先没读，闭合轮廓会缺最后一段。
            emitPolylineVerts(verts, (data.flags & 1) != 0, data, data.extPoint, "LWPOLYLINE");
        }


        void addPolyline(const DRW_Polyline& polyline) override
        {
            if (polyline.vertlist.empty())
            {
                warnSkip("POLYLINE", "empty vertex list");
                return;
            }

            // 跳过 3D 多边形网格 / 多面网格（flags 的 16/64 位）：它们不是 2D 轮廓，
            // 若按顶点顺序连成一条线会产生大量多余线段。
            if ((polyline.flags & 16) != 0 || (polyline.flags & 64) != 0)
            {
                warnSkip("POLYLINE", "3D mesh / polyface mesh, not drawn as 2D line");
                return;
            }

            std::vector<double> verts;
            verts.reserve(polyline.vertlist.size() * 2);
            for (size_t i = 0; i < polyline.vertlist.size(); ++i)
            {
                const auto& vert = polyline.vertlist[i];
                if (!vert)
                {
                    continue;
                }
                Ut::Vec2d p(vert->basePoint.x, vert->basePoint.y);
                if (!isFinite2(p))
                {
                    warnSkip("POLYLINE", "non-finite vertex");
                    return;
                }
                verts.push_back(p.x());
                verts.push_back(p.y());

                // 与 LWPOLYLINE 同一套 bulge 语义（旧式 POLYLINE 的 bulge 记在 VERTEX 上）
                const size_t next = i + 1;
                const bool hasNext = next < polyline.vertlist.size();
                const bool closed = (polyline.flags & 1) != 0;
                if (std::fabs(vert->bulge) > 1e-12 && (hasNext || closed))
                {
                    const auto& nv = hasNext ? polyline.vertlist[next] : polyline.vertlist.front();
                    if (nv)
                    {
                        appendBulgeArcPoints(
                            verts, vert->basePoint.x, vert->basePoint.y, nv->basePoint.x, nv->basePoint.y, vert->bulge);
                    }
                }
            }

            emitPolylineVerts(verts, (polyline.flags & 1) != 0, polyline, polyline.extPoint, "POLYLINE");
        }


        void addSpline(const DRW_Spline* data) override
        {
            if (data == nullptr)
            {
                warnSkip("SPLINE", "null spline data");
                return;
            }

            if (data->controllist.empty())
            {
                // SPLINE 有两种写法：控制点式（含节点/权重）与拟合点式（只给曲线要过的点）。
                // 早先只认控制点式，拟合点式的样条被整条丢弃。这里降级为过拟合点的折线：
                // 精确还原需要做插值反解控制点，属于后续能力，先保证形状与数量不丢。
                std::vector<double> verts;
                verts.reserve(data->fitlist.size() * 2);
                for (const auto& fp : data->fitlist)
                {
                    if (!fp)
                    {
                        continue;
                    }
                    Ut::Vec2d p(fp->x, fp->y);
                    if (!isFinite2(p))
                    {
                        warnSkip("SPLINE", "non-finite fit point");
                        return;
                    }
                    verts.push_back(p.x());
                    verts.push_back(p.y());
                }

                if (verts.size() < 4)
                {
                    warnSkip("SPLINE", "empty control points");
                    return;
                }

                m_warnings.push_back(makeWarning("SPLINE", "fit-point spline imported as polyline"));
                // SPLINE 的坐标是 WCS，没有挤出方向字段，用零向量表示「无需 OCS 换算」
                emitPolylineVerts(verts, (data->flags & 1) != 0, *data, DRW_Coord(), "SPLINE");
                return;
            }

            // 扩展数据布局: [控制点(double*N*2)] [节点(double*K)] [权重(double*W)]
            uint32_t degree = static_cast<uint32_t>(data->degree);
            uint32_t cpCount = 0;
            std::vector<double> cpCoords;

            for (const auto& cp : data->controllist)
            {
                if (!cp)
                {
                    continue;
                }
                Ut::Vec2d p(cp->x, cp->y);
                if (!isFinite2(p))
                {
                    warnSkip("SPLINE", "non-finite control point");
                    return;
                }
                cpCoords.push_back(p.x());
                cpCoords.push_back(p.y());
                cpCount++;
            }

            if (cpCount == 0)
            {
                warnSkip("SPLINE", "no valid control points");
                return;
            }

            // 校验节点值
            for (double k : data->knotslist)
            {
                if (!isFiniteScalar(k))
                {
                    warnSkip("SPLINE", "non-finite knot value");
                    return;
                }
            }
            // 校验权重值，权重必须为有限正数
            for (double w : data->weightlist)
            {
                if (!isFiniteScalar(w) || w <= 0.0)
                {
                    warnSkip("SPLINE", "invalid weight value");
                    return;
                }
            }

            const uint32_t knotCount = static_cast<uint32_t>(data->knotslist.size());
            const size_t byteSize = cpCoords.size() * sizeof(double) + data->knotslist.size() * sizeof(double) +
                data->weightlist.size() * sizeof(double);

            EntityInfo info;
            info.type = EntityType::Nurbs;
            info.nurbsDegree = static_cast<int32_t>(degree);
            info.nurbsCtrlPtCount = cpCount;
            info.nurbsKnotCount = knotCount;
            info.extensionDataOffset = static_cast<uint32_t>(blobTarget().size());
            info.extensionDataSize = static_cast<uint32_t>(byteSize);
            appendExtensionData(cpCoords.data(), cpCoords.size() * sizeof(double));
            if (!data->knotslist.empty())
            {
                appendExtensionData(data->knotslist.data(), data->knotslist.size() * sizeof(double));
            }
            if (!data->weightlist.empty())
            {
                appendExtensionData(data->weightlist.data(), data->weightlist.size() * sizeof(double));
            }
            emit(info, *data);
        }

        /// 读取结束后调用：报告块展开统计，便于定位「图元数量对不上」类问题
        void logBlockSummary() const
        {
            SY_INFOF("[DxfParser] Block expansion: %zu block definitions, %zu instances, %zu expanded entities",
                m_blocks.size(),
                m_instanceCount,
                m_expandedCount);
        }

    private:
        /// 块定义里的一个图元。坐标处于块坐标系（尚未应用插入变换）。
        struct BlockEntity
        {
            EntityInfo info;
            // DXF 规则：块内图元若在图层 "0"，实例化时改随 INSERT 所在图层；
            // 颜色为 BYBLOCK(0) 时改随 INSERT 的颜色。这两条是块能"套用样式"的关键。
            bool layerFromInsert = false;
            bool colorFromInsert = false;
        };

        /// 一次块引用（INSERT）。刻意只存原始参数而不是算好的矩阵：
        /// 变换需要用到被引用块的基点，而嵌套引用登记时那个块可能还没读到。
        struct InsertRef
        {
            std::string blockName;
            double x = 0.0, y = 0.0;
            double sx = 1.0, sy = 1.0;
            double angle = 0.0;  // 弧度
            int colcount = 1, rowcount = 1;
            double colspace = 0.0, rowspace = 0.0;
            uint32_t layerSourceId = 0;
            uint32_t color = 0;
        };

        struct BlockDef
        {
            std::string name;
            double baseX = 0.0, baseY = 0.0;
            std::vector<BlockEntity> entities;
            std::vector<uint8_t> blob;  // 块自己的扩展数据缓冲区，偏移相对本缓冲区
            std::vector<InsertRef> inserts;
        };

        void warnSkip(const char* entityName, const char* reason)
        {
            m_warnings.push_back(makeWarning(entityName, reason));
        }

        /// 扩展数据的写入目标：块定义期间写进块自己的缓冲区，否则写进最终 IR 缓冲区。
        /// 分开存是必须的——块定义会被实例化多次，每次都要重新变换顶点并追加新数据，
        /// 若共用一个缓冲区，块定义那份原始数据就成了永远不被引用的垃圾。
        std::vector<uint8_t>& blobTarget()
        {
            return m_currentBlock != nullptr ? m_currentBlock->blob : m_pub.blob();
        }

        void appendExtensionData(const void* data, size_t byteSize)
        {
            const auto* p = static_cast<const uint8_t*>(data);
            auto& target = blobTarget();
            target.insert(target.end(), p, p + byteSize);
        }

        void applyEntityMeta(EntityInfo& info, const DRW_Entity& drwEntity)
        {
            info.sourceId = m_nextSourceId++;
            info.layerSourceId = m_pub.findLayer(drwEntity.layer);

            // 解析实体自身颜色（真彩色 > ACI > BYLAYER 图层色），以覆盖色形式随 IR 带回，
            // 渲染时优先于图层颜色，避免导入后整图变黑。0 表示未指定。
            info.color = resolveDxfColor(drwEntity, m_layerColorMap);
        }

        /// 图元产出的唯一出口：按当前是否处于块定义中，决定进块缓冲区还是模型空间
        ///
        /// @param ocsExtrusion 非空且挤出方向不是 +Z 时，把坐标从 OCS 换算到 WCS。
        ///        只适用于**没有扩展数据**的图元（圆/弧/文字等）；折线类在打包顶点前
        ///        自行换算（见 emitPolylineVerts），避免把未换算的旧顶点留在缓冲区里。
        void emit(EntityInfo& info, const DRW_Entity& drwEntity, const DRW_Coord* ocsExtrusion = nullptr)
        {
            applyEntityMeta(info, drwEntity);

            if (ocsExtrusion != nullptr)
            {
                const IrXform ocs = ocsToWcsXform(*ocsExtrusion);
                if (!ocs.isIdentity())
                {
                    if (!transformEntity(info, nullptr, 0, ocs, m_pub, m_warnings))
                    {
                        return;
                    }
                }
            }

            if (m_currentBlock == nullptr)
            {
                m_pub.entities().push_back(info);
                return;
            }

            BlockEntity be;
            be.info = info;
            be.layerFromInsert = (drwEntity.layer == "0" || drwEntity.layer.empty());
            be.colorFromInsert = (drwEntity.color == 0 && drwEntity.color24 < 0);
            m_currentBlock->entities.push_back(be);
        }

        /// SOLID / TRACE / 3DFACE 的统一出口：三者在 libdxfrw 里都是 DRW_Trace，
        /// 都由 4 个角点描述，导入成一条闭合轮廓。
        ///
        /// **顶点顺序有坑**：DXF 里这三种实体的角点顺序是 1-2-4-3（第 3、4 点是交叉的），
        /// 按 1-2-3-4 连线会得到一个自交的「蝴蝶结」而不是四边形。
        /// 第 3、4 点重合时退化为三角形，去重后只留 3 个顶点。
        void emitQuad(const DRW_Trace& quad, const DRW_Coord& extrusion, const char* entityName)
        {
            const DRW_Coord corners[4] = { quad.basePoint, quad.secPoint, quad.fourPoint, quad.thirdPoint };

            std::vector<double> verts;
            verts.reserve(8);
            for (const DRW_Coord& c : corners)
            {
                Ut::Vec2d p(c.x, c.y);
                if (!isFinite2(p))
                {
                    warnSkip(entityName, "non-finite corner");
                    return;
                }
                // 与上一个顶点重合则跳过（三角形写法会让两个角点相同）
                const size_t n = verts.size();
                if (n >= 2 && std::fabs(verts[n - 2] - p.x()) < 1e-12 && std::fabs(verts[n - 1] - p.y()) < 1e-12)
                {
                    continue;
                }
                verts.push_back(p.x());
                verts.push_back(p.y());
            }

            emitPolylineVerts(verts, true, quad, extrusion, entityName);
        }


        /// SPLINE 拟合点降级）。顶点先做 OCS→WCS 换算，再打包进扩展数据块。
        /// 闭合折线用 EntityType::Polygon 表达，消费侧据此设置 bClosed。
        ///
        /// 挤出方向要显式传入：libdxfrw 的 extPoint 分别定义在 DRW_Point / DRW_LWPolyline 上，
        /// 并不在 DRW_Entity 基类，无法从 drwEntity 统一取到。
        void emitPolylineVerts(std::vector<double>& verts,
            bool closed,
            const DRW_Entity& drwEntity,
            const DRW_Coord& extrusion,
            const char* entityName)
        {
            if (verts.size() < 4)
            {
                warnSkip(entityName, "less than 2 valid vertices");
                return;
            }

            const IrXform ocs = ocsToWcsXform(extrusion);
            if (!ocs.isIdentity())
            {
                for (size_t i = 0; i + 1 < verts.size(); i += 2)
                {
                    ocs.applyPoint(verts[i], verts[i + 1]);
                }
            }

            EntityInfo info;
            info.type = closed ? EntityType::Polygon : EntityType::Polyline;
            info.vertexCount = static_cast<uint32_t>(verts.size() / 2);
            info.extensionDataOffset = static_cast<uint32_t>(blobTarget().size());
            info.extensionDataSize = static_cast<uint32_t>(verts.size() * sizeof(double));
            appendExtensionData(verts.data(), info.extensionDataSize);
            emit(info, drwEntity);
        }


        /// 把一次块引用实例化成真实图元，并用一个群组表达「这些图元来自同一个块引用」。
        /// 嵌套引用递归展开，每层再套一层子群组，于是 IR 里的群组树就是 DXF 的块嵌套结构。
        ///
        /// @param parentXf 父块坐标系到模型空间的累积变换；顶层引用传单位矩阵
        void expandInsert(const InsertRef& ref, const IrXform& parentXf, uint64_t parentGroupId, int depth)
        {
            if (depth > kMaxBlockDepth)
            {
                warnSkip("INSERT", "block nesting too deep, expansion stopped");
                SY_WARNF("[DxfParser] Block nesting exceeded depth %d at block '%s', stopped",
                    kMaxBlockDepth,
                    ref.blockName.c_str());
                return;
            }

            const auto it = m_blocks.find(ref.blockName);
            if (it == m_blocks.end())
            {
                // 引用了未定义的块：AutoCAD 会当作空块，这里同样跳过但必须留痕
                warnSkip("INSERT", "references an undefined block");
                SY_WARNF("[DxfParser] INSERT references undefined block '%s'", ref.blockName.c_str());
                return;
            }
            const BlockDef& def = it->second;

            if (!isFiniteScalar(ref.x) || !isFiniteScalar(ref.y) || !isFiniteScalar(ref.angle) ||
                !isFiniteScalar(ref.sx) || !isFiniteScalar(ref.sy) || ref.sx == 0.0 || ref.sy == 0.0)
            {
                warnSkip("INSERT", "invalid insert point, scale, or rotation");
                return;
            }

            // 阵列上限：colcount/rowcount 来自文件，畸形值（如 0x7FFFFFFF）会直接打爆内存
            const int cols = std::clamp(ref.colcount, 1, kMaxArrayCount);
            const int rows = std::clamp(ref.rowcount, 1, kMaxArrayCount);
            if (cols != ref.colcount || rows != ref.rowcount)
            {
                warnSkip("INSERT", "array count clamped");
            }

            for (int row = 0; row < rows; ++row)
            {
                for (int col = 0; col < cols; ++col)
                {
                    // DXF 语义：块内坐标先减基点，再缩放、再旋转，最后平移到插入点；
                    // 阵列间距沿 INSERT 自身的旋转坐标轴度量，所以要先旋转再叠加偏移。
                    double ox = col * ref.colspace;
                    double oy = row * ref.rowspace;
                    const IrXform rot = IrXform::rotate(ref.angle);
                    rot.applyVector(ox, oy);

                    const IrXform local = IrXform::translate(-def.baseX, -def.baseY)
                                              .then(IrXform::scale(ref.sx, ref.sy))
                                              .then(rot)
                                              .then(IrXform::translate(ref.x + ox, ref.y + oy));

                    // 嵌套时插入点写在父块坐标系里，因此本层变换要先做、再叠父层变换
                    instantiateBlock(def, ref, local.then(parentXf), parentGroupId, depth);
                }
            }
        }

        void instantiateBlock(
            const BlockDef& def, const InsertRef& ref, const IrXform& xf, uint64_t parentGroupId, int depth)
        {
            if (m_expandedCount >= kMaxExpandedEntities)
            {
                return;
            }

            // 一次引用 = 一个群组。名字带上块名，导入后在群组面板里可辨识来源。
            const uint64_t groupId = m_pub.addGroup("Block:" + def.name, parentGroupId);
            ++m_instanceCount;

            for (const BlockEntity& be : def.entities)
            {
                if (m_expandedCount >= kMaxExpandedEntities)
                {
                    warnSkip("INSERT", "expanded entity limit reached, remaining instances skipped");
                    SY_WARNF("[DxfParser] Expanded entity limit %zu reached, stopping block expansion",
                        kMaxExpandedEntities);
                    return;
                }

                EntityInfo copy = be.info;
                if (!transformEntity(copy, def.blob.data(), def.blob.size(), xf, m_pub, m_warnings))
                {
                    continue;
                }

                copy.sourceId = m_nextSourceId++;
                copy.groupSourceId = groupId;
                // 块内图层 "0" / 颜色 BYBLOCK 的图元随引用方走，这是块能"套用样式"的关键规则
                if (be.layerFromInsert && ref.layerSourceId != 0u)
                {
                    copy.layerSourceId = ref.layerSourceId;
                }
                if (be.colorFromInsert && ref.color != 0u)
                {
                    copy.color = ref.color;
                }

                m_pub.entities().push_back(copy);
                ++m_expandedCount;
            }

            for (const InsertRef& nested : def.inserts)
            {
                expandInsert(nested, xf, groupId, depth + 1);
            }
        }


    private:
        IrPublisher& m_pub;
        std::vector<std::string>& m_warnings;
        std::map<std::string, uint32_t> m_layerColorMap;
        std::string m_sourceUnit;  // 来自 $INSUNITS，空串 = 文件未声明



        // 块状态。用 std::map 而非 unordered_map：节点地址稳定，m_currentBlock 指针在
        // 后续插入其他块时不会失效。
        std::map<std::string, BlockDef> m_blocks;
        BlockDef* m_currentBlock = nullptr;

        uint64_t m_nextSourceId = 1;  // 1-based：0 在 IR 里是「无」的哨兵
        size_t m_instanceCount = 0;
        size_t m_expandedCount = 0;
    };


    // libdxfrw 的 ASCII 读取器用 std::getline 按行切分，无法识别多字节编码（如 GBK/ANSI_936）
    // 中第二字节恰好为 0x0A 的字符，会把这个字节误判为换行，导致整份文件记录错位、解析失败。
    // 在交给 libdxfrw 前，把这类"嵌入字符串内部的 0x0A"替换为空格，从而避免错位，无需修改第三方库。
    // 仅当 0x0A 的前一个字节 >= 0x80（即它是多字节字符的延续字节）时才处理，因此不会破坏
    // CRLF 换行，也不会破坏 Unix 风格 LF-only 换行的文件。
    static void sanitizeDxfBytes(std::string& content)
    {
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(content.data());
        const size_t n = content.size();
        for (size_t i = 1; i < n; ++i)
        {
            if (bytes[i] == 0x0A && bytes[i - 1] >= 0x80)
            {
                content[i] = ' ';
            }
        }
    }

    static bool readFileBytes(const std::string& filePath, std::string& content)
    {
        // Windows 下 std::ifstream(const char*) 按 ANSI 码页解析路径，无法打开 UTF-8 中文路径；
        // 统一用 std::filesystem::u8path 构造宽字符路径（与 FileIO 其余解析器一致）
        std::ifstream in(std::filesystem::u8path(filePath), std::ios::binary);
        if (!in)
        {
            return false;
        }
        content.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return true;
    }

    FioParseResult DxfParser::parseToIR(const char* filePath)
    {
        SY_INFOF("[DxfParser] parseToIR START: filePath=%s", filePath ? filePath : "(null path)");
        const auto startTime = std::chrono::steady_clock::now();

        // 图元 / 图层 / 群组 / 扩展数据的缓冲区统一由 IrPublisher 持有（每线程一份），
        // 解析器不再各自声明 thread_local 向量，跨 DLL 内存契约只在一处定义。
        IrPublisher& pub = IrPublisher::threadLocal();
        pub.reset();

        std::vector<std::string> warnings;

        std::string content;
        if (!readFileBytes(filePath, content))
        {
            SY_ERRORF("[DxfParser] parseToIR: Cannot open file: %s", filePath);
            return FioParseResult{};
        }
        sanitizeDxfBytes(content);

        TempFileCopy tempCopy(content, filePath, "dxf");
        if (!tempCopy.isValid())
        {
            SY_ERRORF("[DxfParser] parseToIR: Temp file copy failed: %s", tempCopy.error().c_str());
            return FioParseResult{};
        }

        try
        {
            dxfRW reader(tempCopy.path().c_str());
            DxfIrConverter converter(pub, warnings);
            bool readResult = reader.read(&converter, false);

            if (!readResult)
            {
                SY_ERRORF("[DxfParser] parseToIR: libdxfrw read failed: %s", filePath);
                return FioParseResult{};
            }

            converter.logBlockSummary();

            // 图层在 addLayer 回调里就已登记进 pub，图元的 layerSourceId 也在产出当场解析完成，
            // 因此这里不再需要「按图层名回填」的二次遍历。
            for (const auto& w : warnings)
            {
                pub.warnings().push_back(w);
            }

            const FioParseResult result =
                pub.publish("DXF", converter.sourceUnit().empty() ? nullptr : converter.sourceUnit().c_str());
            const auto elapsedMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime)
                    .count();
            SY_INFOF("[DxfParser] parseToIR END: %u entities, %u layers, %u groups, %u warnings, unit='%s', %lld ms",
                result.entityCount,
                result.layerCount,
                result.groupCount,
                result.warningCount,
                result.sourceUnit,
                static_cast<long long>(elapsedMs));
            return result;
        }
        catch (const std::exception& ex)
        {
            SY_CRITICALF("[DxfParser] parseToIR exception: %s - %s", filePath, ex.what());
            return FioParseResult{};
        }
        catch (...)
        {
            SY_CRITICALF("[DxfParser] parseToIR unknown exception: %s", filePath);
            return FioParseResult{};
        }
    }


    ParseResult DxfParser::parse(const char* filePath, VecSyEntityPtr& outEntities)
    {
        SY_INFOF("[DxfParser] parse START: filePath=%s", filePath);
        std::vector<std::string> warnings;

        std::string content;
        if (!readFileBytes(filePath, content))
        {
            SY_ERRORF("[DxfParser] parse: Cannot open file: %s", filePath);
            return ParseResult::fail(std::string("Cannot open file: ") + filePath);
        }
        sanitizeDxfBytes(content);

        TempFileCopy tempCopy(content, filePath, "dxf");

        if (!tempCopy.isValid())
        {
            SY_ERRORF("[DxfParser] Temp file copy failed: %s", tempCopy.error().c_str());
            return ParseResult::fail(tempCopy.error());
        }

        try
        {
            dxfRW reader(tempCopy.path().c_str());

            DxfConverter converter(outEntities, warnings);

            bool readResult = reader.read(&converter, false);

            if (!readResult)
            {
                SY_ERRORF("[DxfParser] libdxfrw read failed: %s", filePath);
                return ParseResult::fail(std::string("Failed to read DXF file: ") + filePath);
            }

            size_t entityCount = outEntities.size();
            size_t layerCount = converter.getLayerDefs().size();

            ParseResult result = ParseResult::ok();
            result.warnings = warnings;

            for (const auto& dl : converter.getLayerDefs())
            {
                DxfLayerInfo info;
                info.name = dl.name;
                // 与 IR 路径完全同口径：ACI/真彩色的换算在解析器内完成，对外一律 ARGB；
                // 解析失败时补不透明黑，保证消费方（FileImporter → ParsedLayer）拿到确定值。
                const uint32_t legacyLayerColor = resolveLayerColor(dl);
                info.color = legacyLayerColor != 0u ? legacyLayerColor : 0xFF000000u;
                info.visible = layerIsVisible(dl);
                result.dxfLayers.push_back(info);
            }

            result.entityLayerMap = converter.getEntityLayerMap();
            result.entityColorMap = converter.getEntityColorMap();

            SY_INFOF("[DxfParser] parse END: success, entities=%zu, layers=%zu", entityCount, layerCount);
            return result;
        }
        catch (const std::exception& ex)
        {
            SY_CRITICALF("[DxfParser] Parse exception: %s - %s", filePath, ex.what());
            return ParseResult::fail(std::string("Exception during DXF parsing: ") + ex.what(), warnings);
        }
        catch (...)
        {
            SY_CRITICALF("[DxfParser] Parse unknown exception: %s", filePath);
            return ParseResult::fail(std::string("Unknown exception during DXF parsing"), warnings);
        }
    }
}  // namespace Fio