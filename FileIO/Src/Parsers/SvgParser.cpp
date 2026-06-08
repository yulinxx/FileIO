#include "FileIO/Parsers/SvgParser.h"
#include "FileIO/FileIOUtils.h"

#include "Engine/SyEntity/SyLine.h"
#include "Engine/SyEntity/SyCircle.h"
#include "Ut/Vec.h"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

#include <cmath>
#include <memory>
#include <vector>

namespace Fio
{

class NsvgInterpreter
{
public:
    NsvgInterpreter(VecSyEntityPtr& outEntities, std::vector<std::string>& warnings)
        : m_outEntities(outEntities)
        , m_warnings(warnings)
    {
    }

    void parseFile(const std::string& filePath)
    {
        TempFileCopy tempCopy(filePath, "svg");
        if (!tempCopy.isValid())
        {
            m_warnings.push_back(tempCopy.error());
            return;
        }

        NSVGimage* image = nsvgParseFromFile(tempCopy.path().c_str(), "px", 96.0f);

        if (!image) {
            m_warnings.push_back("Failed to parse SVG file: " + filePath);
            return;
        }

        int shapeCount = 0;
        int pathCount = 0;

        for (NSVGshape* shape = image->shapes; shape != nullptr; shape = shape->next) {
            shapeCount++;

            bool visible = (shape->flags & NSVG_FLAGS_VISIBLE) != 0;
            bool hasFill = shape->fill.type != NSVG_PAINT_NONE;
            bool hasStroke = shape->stroke.type != NSVG_PAINT_NONE;

            if (!visible) {
                continue;
            }
            if (!hasFill && !hasStroke) {
                continue;
            }

            for (NSVGpath* svgPath = shape->paths; svgPath != nullptr; svgPath = svgPath->next) {
                pathCount++;
                convertPathToEntity(svgPath, shape);
            }
        }

        nsvgDelete(image);
    }

private:
    VecSyEntityPtr& m_outEntities;
    std::vector<std::string>& m_warnings;

    void convertPathToEntity(NSVGpath* svgPath, NSVGshape* shape)
    {
        if (!svgPath || svgPath->npts < 4)
            return;

        float* pts = svgPath->pts;
        int npts = svgPath->npts;

        std::vector<Ut::Vec2d> points;

        points.emplace_back(pts[0], -pts[1]);

        for (int i = 0; i + 3 < npts; i += 3) {
            float p0x = pts[i * 2];
            float p0y = -pts[i * 2 + 1];
            float c1x = pts[i * 2 + 2];
            float c1y = -pts[i * 2 + 3];
            float c2x = pts[i * 2 + 4];
            float c2y = -pts[i * 2 + 5];
            float p1x = pts[i * 2 + 6];
            float p1y = -pts[i * 2 + 7];

            const int segs = 16;
            for (int s = 1; s <= segs; ++s) {
                double t = (double)s / segs;
                double t1 = 1.0 - t;
                double x = t1*t1*t1*p0x + 3*t1*t1*t*c1x + 3*t1*t*t*c2x + t*t*t*p1x;
                double y = t1*t1*t1*p0y + 3*t1*t1*t*c1y + 3*t1*t*t*c2y + t*t*t*p1y;
                points.emplace_back(x, y);
            }
        }

        if (svgPath->closed && points.size() >= 2) {
            double dx = points.front().x() - points.back().x();
            double dy = points.front().y() - points.back().y();
            if (std::hypot(dx, dy) > 1e-6)
                points.push_back(points.front());
        }

        if (points.size() < 2)
            return;

        std::vector<Ut::Vec2d> cleaned;
        cleaned.reserve(points.size());
        cleaned.push_back(points[0]);
        for (size_t i = 1; i < points.size(); ++i) {
            if ((points[i] - cleaned.back()).length() > 1e-6)
                cleaned.push_back(points[i]);
        }
        if (cleaned.size() < 2)
            return;

        auto syLine = std::make_unique<Eg::SyLine>();
        syLine->vPoints = std::move(cleaned);
        syLine->basePoint = syLine->vPoints.front();
        syLine->bClosed = svgPath->closed != 0;
        m_outEntities.push_back(std::move(syLine));
    }
};

ParseResult SvgParser::parse(const std::string& filePath, VecSyEntityPtr& outEntities)
{
    std::vector<std::string> warnings;

    try {
        NsvgInterpreter interpreter(outEntities, warnings);
        interpreter.parseFile(filePath);
    } catch (const std::exception& ex) {
        return ParseResult::fail(
            std::string("Exception during SVG parsing: ") + ex.what(), warnings);
    }

    ParseResult result = ParseResult::ok();
    result.warnings = warnings;
    return result;
}

FileFormat SvgParser::format() const
{
    return FileFormat::SVG;
}

std::string SvgParser::formatName() const
{
    return "SVG";
}

std::vector<std::string> SvgParser::supportedExtensions() const
{
    return { "svg", "svgz" };
}

} // namespace Fio
