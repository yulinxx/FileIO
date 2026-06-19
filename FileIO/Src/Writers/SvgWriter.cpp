#include "FileIO/Writers/SvgWriter.h"

#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyEllipse.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Engine2D/SyEntity/SyBezier.h"
#include "Engine2D/SyEntity/SyBezier2.h"
#include "Engine2D/SyEntity/SySpline.h"
#include "Engine2D/SyEntity/SySmartLine.h"
#include "Engine/Layer/SyLayer.h"

#include "Ut/BBox2d.h"
#include "Ut/Color.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Fio
{
    namespace
    {
        std::string colorToHex(const Ut::Vec3f& color)
        {
            auto toByte = [](float c) {
                const int v = static_cast<int>(std::round(std::clamp(c, 0.0f, 1.0f) * 255.0f));
                return v;
                };

            std::ostringstream oss;
            oss << '#'
                << std::hex
                << std::setfill('0') << std::setw(2) << toByte(color.x())
                << std::setw(2) << toByte(color.y())
                << std::setw(2) << toByte(color.z());
            return oss.str();
        }
    }

    FileFormat SvgWriter::format() const
    {
        return FileFormat::SVG;
    }

    std::string SvgWriter::formatName() const
    {
        return "SVG";
    }

    std::string SvgWriter::defaultExtension() const
    {
        return "svg";
    }

    WriteResult SvgWriter::write(const std::string& filePath, const VecSyEntityPtr& entities)
    {
        if (entities.empty())
        {
            return WriteResult::fail("No entities to export");
        }

        Ut::BBox2d bbox;
        for (const auto& entity : entities)
        {
            if (entity)
            {
                bbox.expand(entity->getBbox());
            }
        }

        if (!bbox.isValid())
        {
            bbox = Ut::BBox2d(Ut::Vec2d(0, 0), Ut::Vec2d(100, 100));
        }

        const double margin = 5.0;
        const double width = bbox.width() + margin * 2.0;
        const double height = bbox.height() + margin * 2.0;
        const double offsetX = -bbox.minPt.x() + margin;
        const double offsetY = bbox.maxPt.y() + margin;

        std::ostringstream body;
        int exported = 0;

        for (const auto& entity : entities)
        {
            if (!entity)
            {
                continue;
            }

            const Ut::Color entityColor = entity->getColor();
            const std::string stroke = (entityColor.r() == 0.0f && entityColor.g() == 0.0f && entityColor.b() == 0.0f) ? "#000000" :
                colorToHex(Ut::Vec3f(entityColor.r(), entityColor.g(), entityColor.b()));
            const std::string attrs = "fill=\"none\" stroke=\"" + stroke + "\" stroke-width=\"2\""; // Increased stroke width for visibility

            switch (entity->eType)
            {
                case Eg::EType::LINE:
                {
                    const auto* line = static_cast<const Eg::SyLine*>(entity.get());
                    if (line->vPoints.size() < 2)
                    {
                        break;
                    }

                    body << "<polyline " << attrs << " points=\"";
                    for (const auto& pt : line->vPoints)
                    {
                        body << (pt.x() + offsetX) << ',' << (offsetY - pt.y()) << ' ';
                    }
                    body << "\"/>\n";
                    ++exported;
                    break;
                }
                case Eg::EType::CIRCLE:
                {
                    const auto* circle = static_cast<const Eg::SyCircle*>(entity.get());
                    body << "<circle " << attrs
                        << " cx=\"" << (circle->basePoint.x() + offsetX) << '"'
                        << " cy=\"" << (offsetY - circle->basePoint.y()) << '"'
                        << " r=\"" << circle->dRadius << "\"/>\n";
                    ++exported;
                    break;
                }
                case Eg::EType::ARC:
                {
                    const auto* arc = static_cast<const Eg::SyArc*>(entity.get());
                    const double sweep = arc->dEndAngle - arc->dStartAngle;
                    const int largeArc = std::abs(sweep) > M_PI ? 1 : 0;
                    const double sx = arc->basePoint.x() + arc->dRadius * std::cos(arc->dStartAngle);
                    const double sy = arc->basePoint.y() + arc->dRadius * std::sin(arc->dStartAngle);
                    const double ex = arc->basePoint.x() + arc->dRadius * std::cos(arc->dEndAngle);
                    const double ey = arc->basePoint.y() + arc->dRadius * std::sin(arc->dEndAngle);

                    // Y-axis is flipped in SVG (offsetY - y), so sweep direction is reversed
                    // Original: positive sweep = counterclockwise
                    // After Y-flip: counterclockwise becomes clockwise visually
                    // SVG sweep flag: 0 = counterclockwise, 1 = clockwise
                    const int sweepFlag = (sweep >= 0) ? 0 : 1;

                    body << "<path " << attrs
                        << " d=\"M " << (sx + offsetX) << ' ' << (offsetY - sy)
                        << " A " << arc->dRadius << ' ' << arc->dRadius
                        << " 0 " << largeArc << ' ' << sweepFlag
                        << ' ' << (ex + offsetX) << ' ' << (offsetY - ey) << "\"/>\n";
                    ++exported;
                    break;
                }
                case Eg::EType::ELLIPSE:
                {
                    const auto* ellipse = static_cast<const Eg::SyEllipse*>(entity.get());
                    body << "<ellipse " << attrs
                        << " cx=\"" << (ellipse->basePoint.x() + offsetX) << '"'
                        << " cy=\"" << (offsetY - ellipse->basePoint.y()) << '"'
                        << " rx=\"" << ellipse->dRadiusX << '"'
                        << " ry=\"" << ellipse->dRadiusY << '"'
                        << " transform=\"rotate(" << (-ellipse->dRotation * 180.0 / M_PI)
                        << ' ' << (ellipse->basePoint.x() + offsetX) << ' '
                        << (offsetY - ellipse->basePoint.y()) << ")\"/>\n";
                    ++exported;
                    break;
                }
                case Eg::EType::POLYGON:
                {
                    const auto* polygon = static_cast<const Eg::SyPolygon*>(entity.get());
                    if (polygon->vVertices.size() < 2)
                    {
                        break;
                    }
                    body << "<polygon " << attrs << " points=\"";
                    for (const auto& pt : polygon->vVertices)
                    {
                        body << (pt.x() + offsetX) << ',' << (offsetY - pt.y()) << ' ';
                    }
                    if (polygon->bClosed)
                    {
                        const auto& first = polygon->vVertices.front();
                        body << (first.x() + offsetX) << ',' << (offsetY - first.y()) << ' ';
                    }
                    body << "\"/>\n";
                    ++exported;
                    break;
                }
                case Eg::EType::BEZIER:
                {
                    const auto* bezier = static_cast<const Eg::SyBezier*>(entity.get());
                    body << "<path " << attrs
                        << " d=\"M " << (bezier->basePoint.x() + offsetX) << ' '
                        << (offsetY - bezier->basePoint.y())
                        << " C " << (bezier->ptCtrl0.x() + offsetX) << ' '
                        << (offsetY - bezier->ptCtrl0.y())
                        << ' ' << (bezier->ptCtrl1.x() + offsetX) << ' '
                        << (offsetY - bezier->ptCtrl1.y())
                        << ' ' << (bezier->ptEnd.x() + offsetX) << ' '
                        << (offsetY - bezier->ptEnd.y()) << "\"/>\n";
                    ++exported;
                    break;
                }
                case Eg::EType::BEZIER2:
                {
                    const auto* bezier2 = static_cast<const Eg::SyBezier2*>(entity.get());
                    constexpr int kSegCount = 20;
                    std::ostringstream d;
                    d << "M " << (bezier2->basePoint.x() + offsetX) << ' '
                        << (offsetY - bezier2->basePoint.y());
                    for (int i = 1; i <= kSegCount; ++i)
                    {
                        const double t = static_cast<double>(i) / kSegCount;
                        const double u = 1.0 - t;
                        const double x = u * u * bezier2->basePoint.x()
                            + 2.0 * u * t * bezier2->ptCtrl.x()
                            + t * t * bezier2->ptEnd.x();
                        const double y = u * u * bezier2->basePoint.y()
                            + 2.0 * u * t * bezier2->ptCtrl.y()
                            + t * t * bezier2->ptEnd.y();
                        d << " L " << (x + offsetX) << ' ' << (offsetY - y);
                    }
                    body << "<path " << attrs << " d=\"" << d.str() << "\"/>\n";
                    ++exported;
                    break;
                }
                case Eg::EType::SPLINE:
                {
                    const auto* spline = static_cast<const Eg::SySpline*>(entity.get());
                    if (spline->vControlPoints.size() < 2)
                    {
                        break;
                    }
                    constexpr int kSegCount = 40;
                    std::ostringstream d;
                    {
                        const auto pt0 = spline->value(0.0);
                        d << "M " << (pt0.x() + offsetX) << ' ' << (offsetY - pt0.y());
                    }
                    for (int i = 1; i <= kSegCount; ++i)
                    {
                        const double t = static_cast<double>(i) / kSegCount;
                        const auto pt = spline->value(t);
                        d << " L " << (pt.x() + offsetX) << ' ' << (offsetY - pt.y());
                    }
                    body << "<path " << attrs << " d=\"" << d.str() << "\"/>\n";
                    ++exported;
                    break;
                }
                case Eg::EType::SMARTLINE:
                {
                    const auto* smartLine = static_cast<const Eg::SySmartLine*>(entity.get());
                    for (size_t si = 0; si < smartLine->segmentCount(); ++si)
                    {
                        const auto* seg = smartLine->segment(si);
                        if (!seg)
                        {
                            continue;
                        }
                        switch (seg->eType)
                        {
                            case Eg::EType::LINE:
                            {
                                const auto* ln = static_cast<const Eg::SyLine*>(seg);
                                if (ln->vPoints.size() < 2)
                                {
                                    break;
                                }
                                body << "<polyline " << attrs << " points=\"";
                                for (const auto& pt : ln->vPoints)
                                {
                                    body << (pt.x() + offsetX) << ',' << (offsetY - pt.y()) << ' ';
                                }
                                body << "\"/>\n";
                                break;
                            }
                            case Eg::EType::ARC:
                            {
                                const auto* arc = static_cast<const Eg::SyArc*>(seg);
                                const double sweep = arc->dEndAngle - arc->dStartAngle;
                                const int largeArc = std::abs(sweep) > M_PI ? 1 : 0;
                                const double sx = arc->basePoint.x() + arc->dRadius * std::cos(arc->dStartAngle);
                                const double sy = arc->basePoint.y() + arc->dRadius * std::sin(arc->dStartAngle);
                                const double ex = arc->basePoint.x() + arc->dRadius * std::cos(arc->dEndAngle);
                                const double ey = arc->basePoint.y() + arc->dRadius * std::sin(arc->dEndAngle);
                                // Y-axis is flipped, so sweep direction is reversed
                                body << "<path " << attrs
                                    << " d=\"M " << (sx + offsetX) << ' ' << (offsetY - sy)
                                    << " A " << arc->dRadius << ' ' << arc->dRadius
                                    << " 0 " << largeArc << ' ' << (sweep >= 0 ? 0 : 1)
                                    << ' ' << (ex + offsetX) << ' ' << (offsetY - ey) << "\"/>\n";
                                break;
                            }
                            case Eg::EType::CIRCLE:
                            {
                                const auto* circle = static_cast<const Eg::SyCircle*>(seg);
                                body << "<circle " << attrs
                                    << " cx=\"" << (circle->basePoint.x() + offsetX) << '"'
                                    << " cy=\"" << (offsetY - circle->basePoint.y()) << '"'
                                    << " r=\"" << circle->dRadius << "\"/>\n";
                                break;
                            }
                            default:
                                break;
                        }
                    }
                    ++exported;
                    break;
                }
                default:
                    break;
            }
        }

        if (exported == 0)
        {
            return WriteResult::fail("No supported entities to export as SVG");
        }

        std::filesystem::path fsPath = std::filesystem::u8path(filePath);
        std::ofstream out(fsPath, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return WriteResult::fail("Cannot open file for writing: " + filePath);
        }

        out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            << "<svg xmlns=\"http://www.w3.org/2000/svg\""
            << " width=\"" << width << "\" height=\"" << height << "\""
            << " viewBox=\"0 0 " << width << ' ' << height << "\">\n"
            << body.str()
            << "</svg>\n";

        return WriteResult::ok();
    }
} // namespace Fio