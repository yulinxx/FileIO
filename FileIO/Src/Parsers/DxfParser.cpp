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

    size_t DxfParser::formatName(char* buffer, size_t bufferSize) const
    {
        const char* name = "AutoCAD DXF";
        const size_t len = std::strlen(name);

        if (buffer != nullptr && bufferSize > len)
            std::strcpy(buffer, name);

        return len;
    }

    void DxfParser::forEachSupportedExtension(void(*visitor)(const char*, void*), void* ctx) const
    {
        visitor("dxf", ctx);
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
                    continue;

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

    class DxfIrConverter : public DRW_Interface
    {
    public:
        DxfIrConverter(std::vector<EntityInfo>& outEntities,
            std::vector<uint8_t>& outExtensionBlob,
            std::vector<std::string>& warnings)
            : m_outEntities(outEntities)
            , m_outExtensionBlob(outExtensionBlob)
            , m_warnings(warnings)
        {
        }

        void addHeader(const DRW_Header*) override
        {
        }
        void addLType(const DRW_LType&) override
        {
        }
        void addDimStyle(const DRW_Dimstyle&) override
        {
        }
        void addVport(const DRW_Vport&) override
        {
        }
        void addTextStyle(const DRW_Textstyle&) override
        {
        }
        void addAppId(const DRW_AppId&) override
        {
        }
        void addBlock(const DRW_Block&) override
        {
        }
        void setBlock(const int) override
        {
        }
        void endBlock() override
        {
        }
        void addRay(const DRW_Ray&) override
        {
        }
        void addXline(const DRW_Xline&) override
        {
        }
        void addKnot(const DRW_Entity&) override
        {
        }
        void addInsert(const DRW_Insert&) override
        {
        }
        void addTrace(const DRW_Trace&) override
        {
        }
        void add3dFace(const DRW_3Dface&) override
        {
        }
        void addSolid(const DRW_Solid&) override
        {
        }
        void addDimAlign(const DRW_DimAligned*) override
        {
        }
        void addDimLinear(const DRW_DimLinear*) override
        {
        }
        void addDimRadial(const DRW_DimRadial*) override
        {
        }
        void addDimDiametric(const DRW_DimDiametric*) override
        {
        }
        void addDimAngular(const DRW_DimAngular*) override
        {
        }
        void addDimAngular3P(const DRW_DimAngular3p*) override
        {
        }
        void addDimOrdinate(const DRW_DimOrdinate*) override
        {
        }
        void addLeader(const DRW_Leader*) override
        {
        }
        void addHatch(const DRW_Hatch*) override
        {
        }
        void addViewport(const DRW_Viewport&) override
        {
        }
        void addImage(const DRW_Image*) override
        {
        }
        void linkImage(const DRW_ImageDef*) override
        {
        }
        void addComment(const char*) override
        {
        }
        void addPlotSettings(const DRW_PlotSettings*) override
        {
        }
        void writeHeader(DRW_Header&) override
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
            if (!isFinite2(p))
            {
                warnSkip("POINT", "non-finite position");
                return;
            }

            EntityInfo info;
            info.type = EntityType::Point;
            info.line.x1 = p.x();
            info.line.y1 = p.y();
            applyEntityMeta(info, point);
            m_outEntities.push_back(info);
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
            applyEntityMeta(info, line);
            m_outEntities.push_back(info);
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
            applyEntityMeta(info, circle);
            m_outEntities.push_back(info);
        }

        void addArc(const DRW_Arc& arc) override
        {
            Ut::Vec2d c(arc.basePoint.x, arc.basePoint.y);
            double r = arc.radious;
            if (!isFinite2(c) || !isPositiveFinite(r) ||
                !isFiniteScalar(arc.staangle) || !isFiniteScalar(arc.endangle))
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
            applyEntityMeta(info, arc);
            m_outEntities.push_back(info);
        }

        void addEllipse(const DRW_Ellipse& ellipse) override
        {
            Ut::Vec2d c(ellipse.basePoint.x, ellipse.basePoint.y);
            double majorLen = std::sqrt(ellipse.secPoint.x * ellipse.secPoint.x +
                ellipse.secPoint.y * ellipse.secPoint.y);
            double ratio = ellipse.ratio;
            double rotation = std::atan2(ellipse.secPoint.y, ellipse.secPoint.x);

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

            EntityInfo info;
            info.type = EntityType::Ellipse;
            info.ellipse.cx = c.x();
            info.ellipse.cy = c.y();
            info.ellipse.rx = majorLen;
            info.ellipse.ry = majorLen * ratio;
            info.ellipse.rot = rotation;
            info.ellipse.sa = ellipse.staparam;
            info.ellipse.ea = ellipse.endparam;
            applyEntityMeta(info, ellipse);
            m_outEntities.push_back(info);
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
            applyEntityMeta(info, text);
            m_outEntities.push_back(info);
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
            applyEntityMeta(info, data);
            m_outEntities.push_back(info);
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
            for (const auto& vert : data.vertlist)
            {
                if (!vert)
                    continue;
                Ut::Vec2d p(vert->x, vert->y);
                if (!isFinite2(p))
                {
                    warnSkip("LWPOLYLINE", "non-finite vertex");
                    return;
                }
                verts.push_back(p.x());
                verts.push_back(p.y());
            }

            if (verts.size() < 4)
            {
                warnSkip("LWPOLYLINE", "less than 2 valid vertices");
                return;
            }

            EntityInfo info;
            info.type = EntityType::Polyline;
            info.vertexCount = static_cast<uint32_t>(verts.size() / 2);
            info.extensionDataOffset = static_cast<uint32_t>(m_outExtensionBlob.size());
            info.extensionDataSize = static_cast<uint32_t>(verts.size() * sizeof(double));
            appendExtensionData(verts.data(), info.extensionDataSize);
            applyEntityMeta(info, data);
            m_outEntities.push_back(info);
        }

        void addPolyline(const DRW_Polyline& polyline) override
        {
            if (polyline.vertlist.empty())
            {
                warnSkip("POLYLINE", "empty vertex list");
                return;
            }

            std::vector<double> verts;
            verts.reserve(polyline.vertlist.size() * 2);
            for (const auto& vert : polyline.vertlist)
            {
                if (!vert)
                    continue;
                Ut::Vec2d p(vert->basePoint.x, vert->basePoint.y);
                if (!isFinite2(p))
                {
                    warnSkip("POLYLINE", "non-finite vertex");
                    return;
                }
                verts.push_back(p.x());
                verts.push_back(p.y());
            }

            if (verts.size() < 4)
            {
                warnSkip("POLYLINE", "less than 2 valid vertices");
                return;
            }

            EntityInfo info;
            info.type = EntityType::Polyline;
            info.vertexCount = static_cast<uint32_t>(verts.size() / 2);
            info.extensionDataOffset = static_cast<uint32_t>(m_outExtensionBlob.size());
            info.extensionDataSize = static_cast<uint32_t>(verts.size() * sizeof(double));
            appendExtensionData(verts.data(), info.extensionDataSize);
            applyEntityMeta(info, polyline);
            m_outEntities.push_back(info);
        }

        void addSpline(const DRW_Spline* data) override
        {
            if (!data || data->controllist.empty())
            {
                warnSkip("SPLINE", "empty control points");
                return;
            }

            // 扩展数据布局: [控制点(double*N*2)] [节点(double*K)] [权重(double*W)]
            uint32_t degree = static_cast<uint32_t>(data->degree);
            uint32_t cpCount = 0;
            std::vector<double> cpCoords;

            for (const auto& cp : data->controllist)
            {
                if (!cp)
                    continue;
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
            const size_t byteSize =
                cpCoords.size() * sizeof(double) +
                data->knotslist.size() * sizeof(double) +
                data->weightlist.size() * sizeof(double);

            EntityInfo info;
            info.type = EntityType::Nurbs;
            info.nurbsDegree = static_cast<int32_t>(degree);
            info.nurbsCtrlPtCount = cpCount;
            info.nurbsKnotCount = knotCount;
            info.extensionDataOffset = static_cast<uint32_t>(m_outExtensionBlob.size());
            info.extensionDataSize = static_cast<uint32_t>(byteSize);
            appendExtensionData(cpCoords.data(), cpCoords.size() * sizeof(double));
            if (!data->knotslist.empty())
                appendExtensionData(data->knotslist.data(), data->knotslist.size() * sizeof(double));
            if (!data->weightlist.empty())
                appendExtensionData(data->weightlist.data(), data->weightlist.size() * sizeof(double));
            applyEntityMeta(info, *data);
            m_outEntities.push_back(info);
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

        void applyEntityMeta(EntityInfo& info, const DRW_Entity& drwEntity)
        {
            size_t idx = m_outEntities.size();
            info.sourceId = idx;
            if (!drwEntity.layer.empty())
                m_entityLayerMap[idx] = drwEntity.layer;
            if (drwEntity.color >= 0 && drwEntity.color != 256)
                m_entityColorMap[idx] = drwEntity.color;
        }

        void appendExtensionData(const void* data, size_t byteSize)
        {
            const auto* p = static_cast<const uint8_t*>(data);
            m_outExtensionBlob.insert(m_outExtensionBlob.end(), p, p + byteSize);
        }

    private:
        std::vector<EntityInfo>& m_outEntities;
        std::vector<uint8_t>& m_outExtensionBlob;
        std::vector<std::string>& m_warnings;
        std::vector<DRW_Layer> m_layerDefs;
        std::map<size_t, std::string> m_entityLayerMap;
        std::map<size_t, int> m_entityColorMap;
    };

    FioParseResult DxfParser::parseToIR(const char* filePath)
    {
        SY_INFOF("[DxfParser] parseToIR START: filePath=%s", filePath);

        thread_local std::vector<EntityInfo> s_entities;
        thread_local std::vector<uint8_t> s_extensionBlob;
        thread_local std::vector<IrLayerInfo> s_layers;
        s_entities.clear();
        s_extensionBlob.clear();
        s_layers.clear();

        std::vector<std::string> warnings;

        TempFileCopy tempCopy(filePath, "dxf");
        if (!tempCopy.isValid())
        {
            SY_ERRORF("[DxfParser] parseToIR: Temp file copy failed: %s", tempCopy.error().c_str());
            return FioParseResult{};
        }

        try
        {
            dxfRW reader(tempCopy.path().c_str());
            DxfIrConverter converter(s_entities, s_extensionBlob, warnings);
            bool readResult = reader.read(&converter, false);

            if (!readResult)
            {
                SY_ERRORF("[DxfParser] parseToIR: libdxfrw read failed: %s", filePath);
                return FioParseResult{};
            }

            for (const auto& dl : converter.getLayerDefs())
            {
                IrLayerInfo li;
                li.sourceId = static_cast<uint32_t>(s_layers.size());
                std::strncpy(li.name, dl.name.c_str(), sizeof(li.name) - 1);
                li.name[sizeof(li.name) - 1] = '\0';
                li.color = 0xFF000000 | ((dl.color & 0xFF) << 16) |
                    (((dl.color >> 8) & 0xFF) << 8) | ((dl.color >> 16) & 0xFF);
                li.visible = (dl.flags & 1) == 0;
                s_layers.push_back(li);
            }

            FioParseResult result;
            result.entities = s_entities.data();
            result.entityCount = static_cast<uint32_t>(s_entities.size());
            result.layers = s_layers.data();
            result.layerCount = static_cast<uint32_t>(s_layers.size());
            result.extensionBlob.data = s_extensionBlob.data();
            result.extensionBlob.size = s_extensionBlob.size();
            result.warningCount = static_cast<uint32_t>(warnings.size());
            std::strncpy(result.sourceFormat, "DXF", sizeof(result.sourceFormat) - 1);

            SY_INFOF("[DxfParser] parseToIR END: %u entities, %u layers",
                result.entityCount, result.layerCount);
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
                SY_ERRORF("[DxfParser] libdxfrw read failed: %s", filePath);
                return ParseResult::fail(std::string("Failed to read DXF file: ") + filePath);
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
            SY_CRITICALF("[DxfParser] Parse exception: %s - %s", filePath, ex.what());
            return ParseResult::fail(
                std::string("Exception during DXF parsing: ") + ex.what(),
                warnings
            );
        }
        catch (...)
        {
            SY_CRITICALF("[DxfParser] Parse unknown exception: %s", filePath);
            return ParseResult::fail(
                std::string("Unknown exception during DXF parsing"),
                warnings
            );
        }
    }
} // namespace Fio