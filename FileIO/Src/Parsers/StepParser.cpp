#include "FileIO/Parsers/StepParser.h"

#include "Engine2D/SyEntity/SyLine.h"
#include "Log/SyLogger.h"

#include <chrono>
#include <filesystem>
#include <memory>

#if defined(FILEIO_HAS_GEOMODELCORE) && defined(GMC_ENABLE_STEP_IO)
#include "GeoModelCore/GeoModelDocument.h"
#include "GeoModelCore/GmcProjection.h"
#include "GeoModelCore/GmcTypes.h"
#include "GeoModelCore/TopoShape.h"
#endif

namespace Fio
{
    ParseResult StepParser::parse(const std::string& filePath, VecSyEntityPtr& outEntities)
    {
        outEntities.clear();

        if (!std::filesystem::exists(std::filesystem::u8path(filePath)))
        {
            SY_ERRORF("[StepParser] file not found: %s", filePath.c_str());
            return ParseResult::fail("STEP file not found: " + filePath);
        }

#if defined(FILEIO_HAS_GEOMODELCORE) && defined(GMC_ENABLE_STEP_IO)
        SY_INFOF("[StepParser] loading STEP (Free3D/OCCT B-Rep): %s", filePath.c_str());
        const auto t0 = std::chrono::steady_clock::now();

        GeoModelCore::GeoModel model;
        model.loadStepFile(filePath);
        if (!model.lastStepIoSucceeded())
        {
            const std::string err = model.lastStepIoError();
            SY_ERRORF("[StepParser] STEP ReadFile failed: path=%s err=%s",
                filePath.c_str(), err.c_str());
            return ParseResult::fail(
                "Failed to load STEP file.\n"
                + err
                + "\n\nSupported: ISO-10303-21 STEP/STP (e.g. Free3D, Open CASCADE exports).");
        }

        const auto shape = model.getShape();
        if (!shape || shape->isNull())
        {
            SY_WARNF("[StepParser] STEP loaded but shape is empty: %s", filePath.c_str());
            return ParseResult::fail("STEP file contains no valid B-Rep shape.");
        }

        const GeoModelCore::GmcBndBox box = shape->getBoundingBox();
        SY_INFOF("[StepParser] B-Rep bbox: (%.4f,%.4f,%.4f)-(%.4f,%.4f,%.4f)",
            box.minX, box.minY, box.minZ, box.maxX, box.maxY, box.maxZ);

        const auto polylines = GeoModelCore::GmcProjection::projectTopView(*shape);
        if (polylines.empty())
        {
            SY_WARNF("[StepParser] top-view projection produced no edges: %s", filePath.c_str());
            return ParseResult::fail("STEP model has no projectable edges for 2D view.");
        }

        outEntities.reserve(polylines.size());
        for (const auto& poly : polylines)
        {
            if (poly.points.size() < 2)
                continue;

            auto line = std::make_unique<Eg::SyLine>();
            line->vPoints.reserve(poly.points.size());
            for (const auto& pt : poly.points)
                line->vPoints.emplace_back(pt.first, pt.second);
            line->basePoint = line->vPoints.front();
            outEntities.push_back(std::move(line));
        }

        const auto t1 = std::chrono::steady_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        SY_INFOF("[StepParser] imported %zu 2D entities from %zu projected edges (%lld ms): %s",
            outEntities.size(), polylines.size(), static_cast<long long>(ms), filePath.c_str());

        if (outEntities.empty())
            return ParseResult::fail("STEP projection produced no usable 2D geometry.");

        return ParseResult::ok();
#else
        (void)filePath;
        SY_WARN("[StepParser] STEP import unavailable: rebuild with BUILD_GEOMODELCORE=ON and GMC_ENABLE_STEP_IO");
        return ParseResult::fail(
            "STEP/STP import is not available in this build.\n"
            "Enable GeoModelCore with OpenCASCADE STEP IO (TKDESTEP) and rebuild.");
#endif
    }
} // namespace Fio