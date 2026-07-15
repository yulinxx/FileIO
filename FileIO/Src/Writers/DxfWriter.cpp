#include "FileIO/Writers/DxfWriter.h"

#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyEllipse.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Engine2D/SyEntity/SyBezier.h"
#include "Engine2D/SyEntity/SyBezier2.h"
#include "Engine2D/SyEntity/SyNurbs.h"
#include "Engine2D/SyEntity/SySmartLine.h"

#include <cmath>
#include <fstream>
#include <filesystem>
#include <sstream>

namespace Fio
{
    namespace
    {
        void writePair(std::ostream& out, int code, const std::string& value)
        {
            out << code << '\n' << value << '\n';
        }

        void writePair(std::ostream& out, int code, double value)
        {
            out << code << '\n' << value << '\n';
        }

        void writeEntityHeader(std::ostream& out, const std::string& type)
        {
            writePair(out, 0, type);
            writePair(out, 8, "0");
        }
    }

    FileFormat DxfWriter::format() const
    {
        return FileFormat::DXF;
    }

    std::string DxfWriter::formatName() const
    {
        return "AutoCAD DXF";
    }

    std::string DxfWriter::defaultExtension() const
    {
        return "dxf";
    }

    WriteResult DxfWriter::write(const std::string& filePath, const VecSyEntityPtr& entities)
    {
        if (entities.empty())
        {
            return WriteResult::fail("No entities to export");
        }

        std::filesystem::path fsPath = std::filesystem::u8path(filePath);
        std::ofstream out(fsPath, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return WriteResult::fail("Cannot open file for writing: " + filePath);
        }

        writePair(out, 0, "SECTION");
        writePair(out, 2, "HEADER");
        writePair(out, 9, "$ACADVER");
        writePair(out, 1, "AC1009");
        writePair(out, 0, "ENDSEC");

        writePair(out, 0, "SECTION");
        writePair(out, 2, "ENTITIES");

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
                    if (line->vPoints.size() < 2)
                    {
                        break;
                    }

                    for (size_t i = 1; i < line->vPoints.size(); ++i)
                    {
                        writeEntityHeader(out, "LINE");
                        writePair(out, 10, line->vPoints[i - 1].x());
                        writePair(out, 20, line->vPoints[i - 1].y());
                        writePair(out, 30, 0.0);
                        writePair(out, 11, line->vPoints[i].x());
                        writePair(out, 21, line->vPoints[i].y());
                        writePair(out, 31, 0.0);
                        ++exported;
                    }
                    break;
                }
                case Eg::EType::CIRCLE:
                {
                    const auto* circle = static_cast<const Eg::SyCircle*>(entity.get());
                    writeEntityHeader(out, "CIRCLE");
                    writePair(out, 10, circle->basePoint.x());
                    writePair(out, 20, circle->basePoint.y());
                    writePair(out, 30, 0.0);
                    writePair(out, 40, circle->dRadius);
                    ++exported;
                    break;
                }
                case Eg::EType::ARC:
                {
                    const auto* arc = static_cast<const Eg::SyArc*>(entity.get());
                    writeEntityHeader(out, "ARC");
                    writePair(out, 10, arc->basePoint.x());
                    writePair(out, 20, arc->basePoint.y());
                    writePair(out, 30, 0.0);
                    writePair(out, 40, arc->dRadius);
                    writePair(out, 50, arc->dStartAngle * 180.0 / M_PI);
                    writePair(out, 51, arc->dEndAngle * 180.0 / M_PI);
                    ++exported;
                    break;
                }
                case Eg::EType::ELLIPSE:
                {
                    const auto* ellipse = static_cast<const Eg::SyEllipse*>(entity.get());
                    writeEntityHeader(out, "ELLIPSE");
                    writePair(out, 10, ellipse->basePoint.x());
                    writePair(out, 20, ellipse->basePoint.y());
                    writePair(out, 30, 0.0);
                    writePair(out, 11, ellipse->dRadiusX * std::cos(ellipse->dRotation));
                    writePair(out, 21, ellipse->dRadiusX * std::sin(ellipse->dRotation));
                    writePair(out, 31, 0.0);
                    writePair(out, 40, ellipse->dRadiusY / std::max(ellipse->dRadiusX, 1e-9));
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
                    writeEntityHeader(out, "LWPOLYLINE");
                    writePair(out, 90, static_cast<int>(verts.size()));
                    writePair(out, 70, polygon->bClosed ? 1 : 0);
                    for (const auto& pt : verts)
                    {
                        writePair(out, 10, pt.x());
                        writePair(out, 20, pt.y());
                    }
                    ++exported;
                    break;
                }
                case Eg::EType::BEZIER:
                {
                    const auto* bezier = static_cast<const Eg::SyBezier*>(entity.get());
                    writeEntityHeader(out, "LWPOLYLINE");
                    constexpr int kSegCount = 20;
                    writePair(out, 90, kSegCount + 1);
                    writePair(out, 70, 0);
                    for (int i = 0; i <= kSegCount; ++i)
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
                        writePair(out, 10, x);
                        writePair(out, 20, y);
                    }
                    ++exported;
                    break;
                }
                case Eg::EType::BEZIER2:
                {
                    const auto* bezier2 = static_cast<const Eg::SyBezier2*>(entity.get());
                    writeEntityHeader(out, "LWPOLYLINE");
                    constexpr int kSegCount = 20;
                    writePair(out, 90, kSegCount + 1);
                    writePair(out, 70, 0);
                    for (int i = 0; i <= kSegCount; ++i)
                    {
                        const double t = static_cast<double>(i) / kSegCount;
                        const double u = 1.0 - t;
                        const double x = u * u * bezier2->basePoint.x()
                            + 2.0 * u * t * bezier2->ptCtrl.x()
                            + t * t * bezier2->ptEnd.x();
                        const double y = u * u * bezier2->basePoint.y()
                            + 2.0 * u * t * bezier2->ptCtrl.y()
                            + t * t * bezier2->ptEnd.y();
                        writePair(out, 10, x);
                        writePair(out, 20, y);
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
                    writeEntityHeader(out, "LWPOLYLINE");
                    writePair(out, 90, kSegCount + 1);
                    writePair(out, 70, spline->bClosed ? 1 : 0);
                    for (int i = 0; i <= kSegCount; ++i)
                    {
                        const double t = static_cast<double>(i) / kSegCount;
                        const auto pt = spline->value(t);
                        writePair(out, 10, pt.x());
                        writePair(out, 20, pt.y());
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
                                if (ln->vPoints.size() < 2)
                                {
                                    break;
                                }
                                for (size_t pi = 1; pi < ln->vPoints.size(); ++pi)
                                {
                                    writeEntityHeader(out, "LINE");
                                    writePair(out, 10, ln->vPoints[pi - 1].x());
                                    writePair(out, 20, ln->vPoints[pi - 1].y());
                                    writePair(out, 30, 0.0);
                                    writePair(out, 11, ln->vPoints[pi].x());
                                    writePair(out, 21, ln->vPoints[pi].y());
                                    writePair(out, 31, 0.0);
                                }
                                break;
                            }
                            case Eg::EType::ARC:
                            {
                                const auto* arc = static_cast<const Eg::SyArc*>(seg);
                                writeEntityHeader(out, "ARC");
                                writePair(out, 10, arc->basePoint.x());
                                writePair(out, 20, arc->basePoint.y());
                                writePair(out, 30, 0.0);
                                writePair(out, 40, arc->dRadius);
                                writePair(out, 50, arc->dStartAngle * 180.0 / M_PI);
                                writePair(out, 51, arc->dEndAngle * 180.0 / M_PI);
                                break;
                            }
                            case Eg::EType::CIRCLE:
                            {
                                const auto* circle = static_cast<const Eg::SyCircle*>(seg);
                                writeEntityHeader(out, "CIRCLE");
                                writePair(out, 10, circle->basePoint.x());
                                writePair(out, 20, circle->basePoint.y());
                                writePair(out, 30, 0.0);
                                writePair(out, 40, circle->dRadius);
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

        writePair(out, 0, "ENDSEC");
        writePair(out, 0, "EOF");

        if (exported == 0)
        {
            return WriteResult::fail("No supported entities to export as DXF");
        }

        return WriteResult::ok();
    }
} // namespace Fio