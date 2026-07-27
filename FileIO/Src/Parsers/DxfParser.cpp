#include "FileIO/Parsers/DxfParser.h"
#include "FileIO/FileIOUtils.h"

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

#include <cmath>
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

    std::string DxfParser::formatName() const
    {
        return "AutoCAD DXF";
    }

    std::vector<std::string> DxfParser::supportedExtensions() const
    {
        return { "dxf" };
    }

    static bool isFinite2(const Ut::Vec2d& p)
    {
        return std::isfinite(p.x()) && std::isfinite(p.y());
    }

    static bool isFinite3(const Ut::Vec3f& p)
    {
        return std::isfinite(p.x()) && std::isfinite(p.y()) && std::isfinite(p.z());
    }

    static bool isFiniteScalar(double v)
    {
        return std::isfinite(v);
    }

    // 有限且为正数的标量校验，用于半径/长度等必须为正的几何量
    static bool isPositiveFinite(double v)
    {
        return std::isfinite(v) && v > 0.0;
    }

    // 统一构造跳过图元时的 warning 文本
    static std::string makeWarning(const char* entityName, const char* reason)
    {
        std::ostringstream oss;
        oss << "[DxfParser] Skip " << entityName << ": " << reason;
        return oss.str();
    }

    static Ut::Vec3f aciToRgb(int aci)
    {
        static const float table[][3] = {
            {0,0,0},       // 0 = black
            {1,0,0},       // 1 = red
            {1,1,0},       // 2 = yellow
            {0,1,0},       // 3 = green
            {0,1,1},       // 4 = cyan
            {0,0,1},       // 5 = blue
            {1,0,1},       // 6 = magenta
            {1,1,1},       // 7 = white
        };

        int idx = aci;
        if (idx < 0) idx = 7;
        if (idx == 256) idx = 7;
        idx = idx % 256;

        if (idx < 8)
            return Ut::Vec3f(table[idx][0], table[idx][1], table[idx][2]);

        int group = (idx - 8) / 10;
        int sub = (idx - 8) % 10;
        float hue = (group * 30.0f);
        float val = (sub < 5) ? 1.0f : 0.5f + sub * 0.05f;

        float h = hue / 60.0f;
        int hi = static_cast<int>(h) % 6;
        float f = h - static_cast<int>(h);
        float p = val * 0.5f;
        float q = val * (1.0f - 0.5f * f);
        float t = val * (1.0f - 0.5f * (1.0f - f));

        switch (hi)
        {
            case 0: return Ut::Vec3f(val, t, p);
            case 1: return Ut::Vec3f(q, val, p);
            case 2: return Ut::Vec3f(p, val, t);
            case 3: return Ut::Vec3f(p, q, val);
            case 4: return Ut::Vec3f(t, p, val);
            case 5: return Ut::Vec3f(val, p, q);
        }
        return Ut::Vec3f(1, 1, 1);
    }

    class DxfConverter : public DRW_Interface
    {
    public:
        DxfConverter(VecSyEntityPtr& outEntities, std::vector<std::string>& warnings)
            : m_outEntities(outEntities), m_warnings(warnings)
        {
        }

        void addHeader(const DRW_Header* data) override
        {
        }
        void addLType(const DRW_LType& data) override
        {
        }
        void addDimStyle(const DRW_Dimstyle& data) override
        {
        }
        void addVport(const DRW_Vport& data) override
        {
        }
        void addTextStyle(const DRW_Textstyle& data) override
        {
        }
        void addAppId(const DRW_AppId& data) override
        {
        }
        void addBlock(const DRW_Block& data) override
        {
        }
        void setBlock(const int handle) override
        {
        }
        void endBlock() override
        {
        }
        void addRay(const DRW_Ray& data) override
        {
        }
        void addXline(const DRW_Xline& data) override
        {
        }

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
                    continue;

                Ut::Vec2d p(vert->x, vert->y);
                // 任一顶点非法即放弃整条多段线，避免部分顶点导致渲染异常
                if (!isFinite2(p))
                {
                    warnSkip("LWPOLYLINE", "non-finite vertex");
                    return;
                }

                syLine->vPoints.push_back(p);
            }

            // 有效顶点少于 2 个无法构成线段，跳过
            if (syLine->vPoints.size() < 2)
            {
                warnSkip("LWPOLYLINE", "less than 2 valid vertices");
                return;
            }

            syLine->basePoint = syLine->vPoints.front();
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
            sySpline->vKnots = data->knotslist;
            sySpline->vWeights = data->weightlist;

            for (const auto& cp : data->controllist)
            {
                if (!cp)
                    continue;

                Ut::Vec2d p(cp->x, cp->y);
                // 控制点非法即放弃整条样条，避免曲线退化
                if (!isFinite2(p))
                {
                    warnSkip("SPLINE", "non-finite control point");
                    return;
                }

                sySpline->vControlPoints.push_back(p);
            }

            // 没有任何有效控制点时跳过
            if (sySpline->vControlPoints.empty())
            {
                warnSkip("SPLINE", "no valid control points");
                return;
            }

            // 校验节点值
            for (double k : sySpline->vKnots)
            {
                if (!isFiniteScalar(k))
                {
                    warnSkip("SPLINE", "non-finite knot value");
                    return;
                }
            }

            // 校验权重值，权重必须为有限正数
            for (double w : sySpline->vWeights)
            {
                if (!isFiniteScalar(w) || w <= 0.0)
                {
                    warnSkip("SPLINE", "invalid weight value");
                    return;
                }
            }

            sySpline->basePoint = sySpline->vControlPoints.front();
            applyEntityStyle(sySpline.get(), *data);
            m_outEntities.push_back(std::move(sySpline));
        }

        void addKnot(const DRW_Entity& data) override
        {
        }
        void addInsert(const DRW_Insert& data) override
        {
        }
        void addTrace(const DRW_Trace& data) override
        {
        }
        void add3dFace(const DRW_3Dface& data) override
        {
        }
        void addSolid(const DRW_Solid& data) override
        {
        }

        void addMText(const DRW_MText& data) override
        {
            Ut::Vec2d p(data.basePoint.x, data.basePoint.y);

            // 位置或字高非法时跳过
            if (!isFinite2(p) || !isFiniteScalar(data.height))
            {
                warnSkip("MTEXT", "invalid position or height");
                return;
            }

            auto syText = std::make_unique<Eg::SyText>();
            syText->basePoint = p;
            syText->dHeight = data.height;
            syText->strText = data.text;
            syText->dRotation = data.angle * M_PI / 180.0;
            applyEntityStyle(syText.get(), data);
            m_outEntities.push_back(std::move(syText));
        }

        void addDimAlign(const DRW_DimAligned* data) override
        {
        }
        void addDimLinear(const DRW_DimLinear* data) override
        {
        }
        void addDimRadial(const DRW_DimRadial* data) override
        {
        }
        void addDimDiametric(const DRW_DimDiametric* data) override
        {
        }
        void addDimAngular(const DRW_DimAngular* data) override
        {
        }
        void addDimAngular3P(const DRW_DimAngular3p* data) override
        {
        }
        void addDimOrdinate(const DRW_DimOrdinate* data) override
        {
        }
        void addLeader(const DRW_Leader* data) override
        {
        }
        void addHatch(const DRW_Hatch* data) override
        {
        }
        void addViewport(const DRW_Viewport& data) override
        {
        }
        void addImage(const DRW_Image* data) override
        {
        }
        void linkImage(const DRW_ImageDef* data) override
        {
        }
        void addComment(const char* comment) override
        {
        }
        void addPlotSettings(const DRW_PlotSettings* data) override
        {
        }
        void writeHeader(DRW_Header& data) override
        {
        }
        void writeBlocks() override
        {
        }
        void writeBlockRecords() override
        {
        }
        void writeEntities() override
        {
        }
        void writeLTypes() override
        {
        }
        void writeLayers() override
        {
        }
        void writeTextstyles() override
        {
        }
        void writeVports() override
        {
        }
        void writeDimstyles() override
        {
        }
        void writeObjects() override
        {
        }
        void writeAppId() override
        {
        }

        void addLayer(const DRW_Layer& layer) override
        {
            m_layerDefs.push_back(layer);
        }

        void addPoint(const DRW_Point& point) override
        {
            Ut::Vec2d p(point.basePoint.x, point.basePoint.y);

            // 位置非有限值时跳过
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

            // 端点存在非有限值时直接跳过，避免脏数据进入场景
            if (!isFinite2(p0) || !isFinite2(p1))
            {
                warnSkip("LINE", "non-finite endpoint");
                return;
            }

            auto syLine = std::make_unique<Eg::SyLine>();
            syLine->vPoints.push_back(p0);
            syLine->vPoints.push_back(p1);
            syLine->basePoint = syLine->vPoints.front();
            applyEntityStyle(syLine.get(), line);
            m_outEntities.push_back(std::move(syLine));
        }

        void addCircle(const DRW_Circle& circle) override
        {
            Ut::Vec2d c(circle.basePoint.x, circle.basePoint.y);
            double r = circle.radious;

            // 圆心或半径非法（非有限或半径非正）时跳过
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

            // 圆心、半径或起止角度非法时跳过
            if (!isFinite2(c) || !isPositiveFinite(r) ||
                !isFiniteScalar(arc.staangle) || !isFiniteScalar(arc.endangle))
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

            double majorLen = std::sqrt(ellipse.secPoint.x * ellipse.secPoint.x +
                ellipse.secPoint.y * ellipse.secPoint.y);
            double ratio = ellipse.ratio;
            double rotation = std::atan2(ellipse.secPoint.y, ellipse.secPoint.x);

            // 圆心、长轴端点、长轴长度、比例或参数非法时跳过
            if (!isFinite2(c) ||
                !isFiniteScalar(ellipse.secPoint.x) || !isFiniteScalar(ellipse.secPoint.y) ||
                !isPositiveFinite(majorLen) ||
                !isFiniteScalar(ratio) || ratio <= 0.0 ||
                !isFiniteScalar(ellipse.staparam) || !isFiniteScalar(ellipse.endparam) ||
                !isFiniteScalar(rotation))
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

            auto syLine = std::make_unique<Eg::SyLine>();
            for (const auto& vert : polyline.vertlist)
            {
                if (!vert)
                    continue;

                Ut::Vec2d p(vert->basePoint.x, vert->basePoint.y);
                // 任一顶点非法即放弃整条多段线
                if (!isFinite2(p))
                {
                    warnSkip("POLYLINE", "non-finite vertex");
                    return;
                }

                syLine->vPoints.push_back(p);
            }

            // 有效顶点少于 2 个无法构成线段，跳过
            if (syLine->vPoints.size() < 2)
            {
                warnSkip("POLYLINE", "less than 2 valid vertices");
                return;
            }

            syLine->basePoint = syLine->vPoints.front();
            syLine->bClosed = (polyline.flags & 1) != 0;
            applyEntityStyle(syLine.get(), polyline);
            m_outEntities.push_back(std::move(syLine));
        }

        void addText(const DRW_Text& text) override
        {
            Ut::Vec2d p(text.basePoint.x, text.basePoint.y);

            // 位置或字高非法时跳过
            if (!isFinite2(p) || !isFiniteScalar(text.height))
            {
                warnSkip("TEXT", "invalid position or height");
                return;
            }

            auto syText = std::make_unique<Eg::SyText>();
            syText->basePoint = p;
            syText->dHeight = text.height;
            syText->strText = text.text;
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
        // 统一记录非法图元被跳过的 warning
        void warnSkip(const char* entityName, const char* reason)
        {
            m_warnings.push_back(makeWarning(entityName, reason));
        }

        void applyEntityStyle(Eg::SyEntity* entity, const DRW_Entity& drwEntity)
        {
            // applyEntityStyle 在 push_back 之前调用，因此当前 size 即为新图元即将占用的索引
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
        std::map<size_t, std::string> m_entityLayerMap;
        std::map<size_t, int> m_entityColorMap;
    };

    ParseResult DxfParser::parse(const std::string& filePath, VecSyEntityPtr& outEntities)
{
    SY_INFOF("[DxfParser] parse START: filePath=%s", filePath.c_str());
    std::vector<std::string> warnings;

    TempFileCopy tempCopy(filePath, "dxf");


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
            SY_ERRORF("[DxfParser] libdxfrw read failed: %s", filePath.c_str());
            return ParseResult::fail("Failed to read DXF file: " + filePath);
        }

        size_t entityCount = outEntities.size();
        size_t layerCount = converter.getLayerDefs().size();
        SY_INFOF("[DxfParser] Parse completed: %zu entities, %zu layers", entityCount, layerCount);

        ParseResult result = ParseResult::ok();
        result.warnings = warnings;

        SY_INFOF("[DxfParser] Processing layers: count=%zu", layerCount);
        for (const auto& dl : converter.getLayerDefs())
        {
            DxfLayerInfo info;
            info.name = dl.name;
            info.color = dl.color;
            info.visible = (dl.flags & 1) == 0;
            result.dxfLayers.push_back(info);
        }

        result.entityLayerMap = converter.getEntityLayerMap();
        result.entityColorMap = converter.getEntityColorMap();

        SY_INFOF("[DxfParser] parse END: success, entities=%zu", entityCount);
        return result;
    }
    catch (const std::exception& ex)
    {
        SY_CRITICALF("[DxfParser] Parse exception: %s - %s", filePath.c_str(), ex.what());
        return ParseResult::fail(
            std::string("Exception during DXF parsing: ") + ex.what(),
            warnings
        );
    }
    catch (...)
    {
        SY_CRITICALF("[DxfParser] Parse unknown exception: %s", filePath.c_str());
        return ParseResult::fail(
            std::string("Unknown exception during DXF parsing"),
            warnings
        );
    }
}
} // namespace Fio