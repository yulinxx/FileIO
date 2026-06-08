#include "FileIO/Parsers/DxfParser.h"
#include "FileIO/FileIOUtils.h"

#include "Engine/SyEntity/SyLine.h"
#include "Engine/SyEntity/SyArc.h"
#include "Engine/SyEntity/SyCircle.h"
#include "Engine/SyEntity/SyEllipse.h"
#include "Engine/SyEntity/SyPoint.h"
#include "Engine/SyEntity/SyPolygon.h"
#include "Engine/SyEntity/SySpline.h"
#include "Engine/SyEntity/SyText.h"
#include "Ut/Vec.h"

#include "drw_interface.h"
#include "drw_dxfreader.h"

#include <cmath>
#include <memory>

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

class DxfConverter : public DRW::DRW_Interface {
public:
    DxfConverter(VecSyEntityPtr& outEntities, std::vector<std::string>& warnings)
        : m_outEntities(outEntities), m_warnings(warnings) {}

    void addLine(const DRW::Line& line) override {
        auto syLine = std::make_unique<Eg::SyLine>();
        syLine->vPoints.push_back(Ut::Vec2d(line.start.x, line.start.y));
        syLine->vPoints.push_back(Ut::Vec2d(line.end.x, line.end.y));
        if (!syLine->vPoints.empty())
            syLine->basePoint = syLine->vPoints.front();
        m_outEntities.push_back(std::move(syLine));
    }

    void addCircle(const DRW::Circle& circle) override {
        auto syCircle = std::make_unique<Eg::SyCircle>();
        syCircle->basePoint = Ut::Vec2d(circle.center.x, circle.center.y);
        syCircle->dRadius = circle.radius;
        m_outEntities.push_back(std::move(syCircle));
    }

    void addArc(const DRW::Arc& arc) override {
        auto syArc = std::make_unique<Eg::SyArc>();
        syArc->basePoint = Ut::Vec2d(arc.center.x, arc.center.y);
        syArc->dRadius = arc.radius;
        syArc->dStartAngle = arc.startAngle * M_PI / 180.0;
        syArc->dEndAngle = arc.endAngle * M_PI / 180.0;
        m_outEntities.push_back(std::move(syArc));
    }

    void addEllipse(const DRW::Ellipse& ellipse) override {
        double majorLen = std::sqrt(ellipse.majorAxis.x * ellipse.majorAxis.x + 
                                    ellipse.majorAxis.y * ellipse.majorAxis.y);
        double rotation = std::atan2(ellipse.majorAxis.y, ellipse.majorAxis.x);
        
        auto syEllipse = std::make_unique<Eg::SyEllipse>();
        syEllipse->basePoint = Ut::Vec2d(ellipse.center.x, ellipse.center.y);
        syEllipse->dRadiusX = majorLen;
        syEllipse->dRadiusY = majorLen * ellipse.ratio;
        syEllipse->dRotation = rotation;
        m_outEntities.push_back(std::move(syEllipse));
    }

    void addPolyline(const DRW::Polyline& polyline) override {
        if (polyline.vertices.empty())
            return;

        auto syLine = std::make_unique<Eg::SyLine>();
        for (const auto& pt : polyline.vertices) {
            syLine->vPoints.push_back(Ut::Vec2d(pt.x, pt.y));
        }
        syLine->basePoint = syLine->vPoints.front();
        syLine->bClosed = polyline.closed;
        m_outEntities.push_back(std::move(syLine));
    }

    void addSpline(const DRW::Spline& spline) override {
        if (spline.controlPoints.empty())
            return;

        auto sySpline = std::make_unique<Eg::SySpline>();
        for (const auto& pt : spline.controlPoints) {
            sySpline->vControlPoints.push_back(Ut::Vec2d(pt.x, pt.y));
        }
        m_outEntities.push_back(std::move(sySpline));
    }

    void addText(const DRW::Text& text) override {
        auto syText = std::make_unique<Eg::SyText>();
        syText->basePoint = Ut::Vec2d(text.insertionPoint.x, text.insertionPoint.y);
        syText->dHeight = text.height;
        syText->strText = text.text;
        syText->dRotation = text.rotation * M_PI / 180.0;
        m_outEntities.push_back(std::move(syText));
    }

private:
    VecSyEntityPtr& m_outEntities;
    std::vector<std::string>& m_warnings;
};

ParseResult DxfParser::parse(const std::string& filePath, VecSyEntityPtr& outEntities)
{
    std::vector<std::string> warnings;

    // Windows 下 libdxfrw 用 fopen(const char*) 按 ANSI 码页打开文件,
    // std::string 可能包含 UTF-8 中文路径会失败。
    // 使用 TempFileCopy 创建 ANSI 安全的临时副本。
    TempFileCopy tempCopy(filePath, "dxf");
    if (!tempCopy.isValid())
        return ParseResult::fail(tempCopy.error());

    try {
        DRW::DRW_DxfReader reader;

        if (!reader.open(tempCopy.path())) {
            return ParseResult::fail("Cannot open DXF file: " + filePath);
        }

        DxfConverter converter(outEntities, warnings);

        if (!reader.read(&converter)) {
            reader.close();
            return ParseResult::fail("Failed to read DXF file: " + filePath);
        }

        reader.close();

        ParseResult result = ParseResult::ok();
        result.warnings = warnings;
        return result;

    } catch (const std::exception& ex) {
        return ParseResult::fail(
            std::string("Exception during DXF parsing: ") + ex.what(),
            warnings
        );
    }
}

} // namespace Fio
