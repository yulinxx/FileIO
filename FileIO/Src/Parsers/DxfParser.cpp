#include "FileIO/Parsers/DxfParser.h"
#include "FileIO/FileIOUtils.h"

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
                return;

            auto syLine = std::make_unique<Eg::SyLine>();
            for (const auto& vert : data.vertlist)
            {
                syLine->vPoints.push_back(Ut::Vec2d(vert->x, vert->y));
            }
            syLine->basePoint = syLine->vPoints.front();
            syLine->bClosed = (data.flags & 1) != 0;
            applyEntityStyle(syLine.get(), data);
            m_outEntities.push_back(std::move(syLine));
        }
        void addSpline(const DRW_Spline* data) override
        {
            if (!data || data->controllist.empty())
                return;

            auto sySpline = std::make_unique<Eg::SyNurbs>();
            sySpline->nDegree = data->degree;
            sySpline->vKnots = data->knotslist;
            sySpline->vWeights = data->weightlist;

            for (const auto& cp : data->controllist)
            {
                sySpline->vControlPoints.push_back(Ut::Vec2d(cp->x, cp->y));
            }

            if (!sySpline->vControlPoints.empty())
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
            auto syText = std::make_unique<Eg::SyText>();
            syText->basePoint = Ut::Vec2d(data.basePoint.x, data.basePoint.y);
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
            auto syPoint = std::make_unique<Eg::SyPoint>();
            syPoint->basePoint = Ut::Vec2d(point.basePoint.x, point.basePoint.y);
            applyEntityStyle(syPoint.get(), point);
            m_outEntities.push_back(std::move(syPoint));
        }

        void addLine(const DRW_Line& line) override
        {
            auto syLine = std::make_unique<Eg::SyLine>();
            syLine->vPoints.push_back(Ut::Vec2d(line.basePoint.x, line.basePoint.y));
            syLine->vPoints.push_back(Ut::Vec2d(line.secPoint.x, line.secPoint.y));
            if (!syLine->vPoints.empty())
                syLine->basePoint = syLine->vPoints.front();
            applyEntityStyle(syLine.get(), line);
            m_outEntities.push_back(std::move(syLine));
        }

        void addCircle(const DRW_Circle& circle) override
        {
            auto syCircle = std::make_unique<Eg::SyCircle>();
            syCircle->basePoint = Ut::Vec2d(circle.basePoint.x, circle.basePoint.y);
            syCircle->dRadius = circle.radious;
            applyEntityStyle(syCircle.get(), circle);
            m_outEntities.push_back(std::move(syCircle));
        }

        void addArc(const DRW_Arc& arc) override
        {
            auto syArc = std::make_unique<Eg::SyArc>();
            syArc->basePoint = Ut::Vec2d(arc.basePoint.x, arc.basePoint.y);
            syArc->dRadius = arc.radious;
            syArc->dStartAngle = arc.staangle;
            syArc->dEndAngle = arc.endangle;
            applyEntityStyle(syArc.get(), arc);
            m_outEntities.push_back(std::move(syArc));
        }

        void addEllipse(const DRW_Ellipse& ellipse) override
        {
            double majorLen = std::sqrt(ellipse.secPoint.x * ellipse.secPoint.x +
                ellipse.secPoint.y * ellipse.secPoint.y);
            double rotation = std::atan2(ellipse.secPoint.y, ellipse.secPoint.x);

            auto syEllipse = std::make_unique<Eg::SyEllipse>();
            syEllipse->basePoint = Ut::Vec2d(ellipse.basePoint.x, ellipse.basePoint.y);
            syEllipse->dRadiusX = majorLen;
            syEllipse->dRadiusY = majorLen * ellipse.ratio;
            syEllipse->dRotation = rotation;
            syEllipse->dStartAngle = ellipse.staparam;
            syEllipse->dEndAngle = ellipse.endparam;
            applyEntityStyle(syEllipse.get(), ellipse);
            m_outEntities.push_back(std::move(syEllipse));
        }

        void addPolyline(const DRW_Polyline& polyline) override
        {
            if (polyline.vertlist.empty())
                return;

            auto syLine = std::make_unique<Eg::SyLine>();
            for (const auto& vert : polyline.vertlist)
            {
                syLine->vPoints.push_back(Ut::Vec2d(vert->basePoint.x, vert->basePoint.y));
            }
            syLine->basePoint = syLine->vPoints.front();
            syLine->bClosed = (polyline.flags & 1) != 0;
            applyEntityStyle(syLine.get(), polyline);
            m_outEntities.push_back(std::move(syLine));
        }

        void addText(const DRW_Text& text) override
        {
            auto syText = std::make_unique<Eg::SyText>();
            syText->basePoint = Ut::Vec2d(text.basePoint.x, text.basePoint.y);
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
        void applyEntityStyle(Eg::SyEntity* entity, const DRW_Entity& drwEntity)
        {
            size_t idx = m_outEntities.size() - 1;

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
        std::vector<std::string> warnings;

        TempFileCopy tempCopy(filePath, "dxf");
        if (!tempCopy.isValid())
            return ParseResult::fail(tempCopy.error());

        try
        {
            dxfRW reader(tempCopy.path().c_str());

            DxfConverter converter(outEntities, warnings);

            if (!reader.read(&converter, false))
            {
                return ParseResult::fail("Failed to read DXF file: " + filePath);
            }

            ParseResult result = ParseResult::ok();
            result.warnings = warnings;

            for (const auto& dl : converter.getLayerDefs())
            {
                DxfLayerInfo info;
                info.name = dl.name;
                info.color = dl.color;
                info.visible = (dl.flags & 1) == 0;
                result.dxfLayers.push_back(info);
            }

            result.entityLayerMap = converter.getEntityLayerMap();

            return result;
        }
        catch (const std::exception& ex)
        {
            return ParseResult::fail(
                std::string("Exception during DXF parsing: ") + ex.what(),
                warnings
            );
        }
    }
} // namespace Fio