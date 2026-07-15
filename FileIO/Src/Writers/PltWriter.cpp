#include "FileIO/Writers/PltWriter.h"

#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Engine2D/SyEntity/SyBezier.h"
#include "Engine2D/SyEntity/SyBezier2.h"
#include "Engine2D/SyEntity/SyNurbs.h"
#include "Engine2D/SyEntity/SySmartLine.h"

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Fio
{
    namespace
    {
        constexpr double kPluPerMm = 40.0;

        int toPlu(double mm)
        {
            return static_cast<int>(std::lround(mm * kPluPerMm));
        }
    }

    FileFormat PltWriter::format() const
    {
        return FileFormat::PLT;
    }

    std::string PltWriter::formatName() const
    {
        return "HPGL PLT";
    }

    std::string PltWriter::defaultExtension() const
    {
        return "plt";
    }

    WriteResult PltWriter::write(const std::string& filePath, const VecSyEntityPtr& entities)
    {
        if (entities.empty())
        {
            return WriteResult::fail("No entities to export");
        }

        std::ostringstream hpgl;
        hpgl << "IN;SP1;PU0,0;\n";

        int exported = 0;
        for (const auto& entity : entities)
        {
            if (!entity)
            {
                continue;
            }

            switch (entity->eType)
            {
                case Eg::EType::LINE:
                {
                    const auto* line = static_cast<const Eg::SyLine*>(entity.get());
                    if (line->vPoints.empty())
                    {
                        break;
                    }

                    const auto& first = line->vPoints.front();
                    hpgl << "PU" << toPlu(first.x()) << ',' << toPlu(first.y()) << ";\n";

                    for (size_t i = 1; i < line->vPoints.size(); ++i)
                    {
                        const auto& pt = line->vPoints[i];
                        hpgl << "PD" << toPlu(pt.x()) << ',' << toPlu(pt.y()) << ";\n";
                    }

                    if (line->bClosed && line->vPoints.size() > 2)
                    {
                        hpgl << "PD" << toPlu(first.x()) << ',' << toPlu(first.y()) << ";\n";
                    }
                    ++exported;
                    break;
                }
                case Eg::EType::CIRCLE:
                {
                    const auto* circle = static_cast<const Eg::SyCircle*>(entity.get());
                    if (circle->dRadius <= 0)
                    {
                        break;
                    }
                    hpgl << "PU" << toPlu(circle->basePoint.x() + circle->dRadius)
                        << ',' << toPlu(circle->basePoint.y()) << ";\n";
                    hpgl << "CI" << toPlu(circle->dRadius) << ";\n";
                    ++exported;
                    break;
                }
                case Eg::EType::ARC:
                {
                    const auto* arc = static_cast<const Eg::SyArc*>(entity.get());
                    if (arc->dRadius <= 0)
                    {
                        break;
                    }
                    const double sx = arc->basePoint.x() + arc->dRadius * std::cos(arc->dStartAngle);
                    const double sy = arc->basePoint.y() + arc->dRadius * std::sin(arc->dStartAngle);

                    hpgl << "PU" << toPlu(sx) << ',' << toPlu(sy) << ";\n";
                    hpgl << "AA" << toPlu(arc->basePoint.x()) << ',' << toPlu(arc->basePoint.y())
                        << ',' << static_cast<int>(std::lround((arc->dEndAngle - arc->dStartAngle) * 180.0 / M_PI))
                        << ";\n";
                    ++exported;
                    break;
                }
                case Eg::EType::POLYGON:
                {
                    const auto* polygon = static_cast<const Eg::SyPolygon*>(entity.get());
                    const auto& verts = polygon->vertices();
                    if (verts.size() < 2)
                    {
                        break;
                    }
                    const auto& first = verts.front();
                    hpgl << "PU" << toPlu(first.x()) << ',' << toPlu(first.y()) << ";\n";
                    for (size_t i = 1; i < verts.size(); ++i)
                    {
                        hpgl << "PD" << toPlu(verts[i].x())
                            << ',' << toPlu(verts[i].y()) << ";\n";
                    }
                    if (polygon->bClosed)
                    {
                        hpgl << "PD" << toPlu(first.x()) << ',' << toPlu(first.y()) << ";\n";
                    }
                    ++exported;
                    break;
                }
                case Eg::EType::BEZIER:
                {
                    const auto* bezier = static_cast<const Eg::SyBezier*>(entity.get());
                    constexpr int kSegCount = 20;
                    hpgl << "PU";
                    {
                        const double x = bezier->basePoint.x();
                        const double y = bezier->basePoint.y();
                        hpgl << toPlu(x) << ',' << toPlu(y) << ";\n";
                    }
                    for (int i = 1; i <= kSegCount; ++i)
                    {
                        const double t = static_cast<double>(i) / kSegCount;
                        const double u = 1.0 - t;
                        const double x = u * u * u * bezier->basePoint.x()
                            + 3.0 * u * u * t * bezier->ptCtrl0.x()
                            + 3.0 * u * t * t * bezier->ptCtrl1.x()
                            + t * t * t * bezier->ptEnd.x();
                        const double y = u * u * u * bezier->basePoint.y()
                            + 3.0 * u * u * t * bezier->ptCtrl0.y()
                            + 3.0 * u * t * t * bezier->ptCtrl1.y()
                            + t * t * t * bezier->ptEnd.y();
                        hpgl << "PD" << toPlu(x) << ',' << toPlu(y) << ";\n";
                    }
                    ++exported;
                    break;
                }
                case Eg::EType::BEZIER2:
                {
                    const auto* bezier2 = static_cast<const Eg::SyBezier2*>(entity.get());
                    constexpr int kSegCount = 20;
                    hpgl << "PU";
                    {
                        const double x = bezier2->basePoint.x();
                        const double y = bezier2->basePoint.y();
                        hpgl << toPlu(x) << ',' << toPlu(y) << ";\n";
                    }
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
                        hpgl << "PD" << toPlu(x) << ',' << toPlu(y) << ";\n";
                    }
                    ++exported;
                    break;
                }
                case Eg::EType::SPLINE:
                {
                    const auto* spline = static_cast<const Eg::SyNurbs*>(entity.get());
                    if (spline->vControlPoints.size() < 2)
                    {
                        break;
                    }
                    constexpr int kSegCount = 40;
                    {
                        const auto pt0 = spline->value(0.0);
                        hpgl << "PU" << toPlu(pt0.x()) << ',' << toPlu(pt0.y()) << ";\n";
                    }
                    for (int i = 1; i <= kSegCount; ++i)
                    {
                        const double t = static_cast<double>(i) / kSegCount;
                        const auto pt = spline->value(t);
                        hpgl << "PD" << toPlu(pt.x()) << ',' << toPlu(pt.y()) << ";\n";
                    }
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
                                if (ln->vPoints.empty())
                                {
                                    break;
                                }
                                hpgl << "PU" << toPlu(ln->vPoints.front().x())
                                    << ',' << toPlu(ln->vPoints.front().y()) << ";\n";
                                for (size_t pi = 1; pi < ln->vPoints.size(); ++pi)
                                {
                                    hpgl << "PD" << toPlu(ln->vPoints[pi].x())
                                        << ',' << toPlu(ln->vPoints[pi].y()) << ";\n";
                                }
                                break;
                            }
                            case Eg::EType::ARC:
                            {
                                const auto* arc = static_cast<const Eg::SyArc*>(seg);
                                if (arc->dRadius <= 0)
                                {
                                    break;
                                }
                                const double sx = arc->basePoint.x() + arc->dRadius * std::cos(arc->dStartAngle);
                                const double sy = arc->basePoint.y() + arc->dRadius * std::sin(arc->dStartAngle);
                                hpgl << "PU" << toPlu(sx) << ',' << toPlu(sy) << ";\n";
                                hpgl << "AA" << toPlu(arc->basePoint.x()) << ',' << toPlu(arc->basePoint.y())
                                    << ',' << static_cast<int>(std::lround((arc->dEndAngle - arc->dStartAngle) * 180.0 / M_PI))
                                    << ";\n";
                                break;
                            }
                            case Eg::EType::CIRCLE:
                            {
                                const auto* circle = static_cast<const Eg::SyCircle*>(seg);
                                if (circle->dRadius <= 0)
                                {
                                    break;
                                }
                                hpgl << "PU" << toPlu(circle->basePoint.x() + circle->dRadius)
                                    << ',' << toPlu(circle->basePoint.y()) << ";\n";
                                hpgl << "CI" << toPlu(circle->dRadius) << ";\n";
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
            return WriteResult::fail("No supported entities to export as PLT");
        }

        hpgl << "PU0,0;SP0;\n";

        std::filesystem::path fsPath = std::filesystem::u8path(filePath);
        std::ofstream out(fsPath, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return WriteResult::fail("Cannot open file for writing: " + filePath);
        }

        out << hpgl.str();
        return WriteResult::ok();
    }
} // namespace Fio