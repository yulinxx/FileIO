#include "FileIO/Parsers/StepParser.h"
#include "IrProjector.h"

#include "Log/SyLogger.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

#if defined(FILEIO_HAS_GEOMODELCORE) && defined(GMC_ENABLE_STEP_IO)
    #include "GeoModelCore/GeoModelDocument.h"
    #include "GeoModelCore/GmcCutContour.h"
    #include "GeoModelCore/GmcProjection.h"
    #include "GeoModelCore/GmcTypes.h"
    #include "GeoModelCore/TopoShape.h"
#endif

namespace Fio
{
    // ========================================================================
    // StepParser::parseToIR() — 中立 IR 解析路径
    // STEP B-Rep → 顶视图投影 → Polyline(POD) + 顶点数据存入 extensionBlob
    // 不依赖 Engine2D 类型，跨 DLL 安全
    // ========================================================================
    FioParseResult StepParser::parseToIR(const char* filePath)
    {
        SY_INFOF("[StepParser] parseToIR START: %s", filePath ? filePath : "");

        // 统一缓冲区管理（与 DxfParser/StlParser 一致）
        IrPublisher& pub = IrPublisher::threadLocal();
        pub.reset();

        if (!filePath || !std::filesystem::exists(std::filesystem::u8path(filePath)))
        {
            SY_ERRORF("[StepParser] parseToIR: file not found: %s", filePath ? filePath : "");
            return FioParseResult{};
        }

#if defined(FILEIO_HAS_GEOMODELCORE) && defined(GMC_ENABLE_STEP_IO)
        try
        {
            const auto t0 = std::chrono::steady_clock::now();

            GeoModelCore::GeoModel model;
            model.loadStepFile(filePath);
            if (!model.lastStepIoSucceeded())
            {
                char errBuf[1024] = {};
                model.lastStepIoError(errBuf, sizeof(errBuf));
                SY_ERRORF("[StepParser] parseToIR: STEP ReadFile failed: %s err=%s", filePath, errBuf);
                return FioParseResult{};
            }

            const auto shape = model.getShape();
            if (!shape || shape->isNull())
            {
                SY_WARNF("[StepParser] parseToIR: STEP shape is empty: %s", filePath);
                return FioParseResult{};
            }

            // 统一发射：顶点按 x0,y0,x1,y1 顺序写入 extensionBlob
            auto emitPolygon = [&](const std::vector<double>& verts, uint32_t pointCount, EntityType type) {
                EntityInfo info{};
                info.type = type;
                info.sourceId = static_cast<uint64_t>(pub.entities().size());
                info.visible = true;
                info.vertexCount = pointCount;
                info.extensionDataOffset = static_cast<uint32_t>(pub.blob().size());
                info.extensionDataSize = static_cast<uint32_t>(verts.size() * sizeof(double));
                pub.blob().insert(pub.blob().end(),
                    reinterpret_cast<const uint8_t*>(verts.data()),
                    reinterpret_cast<const uint8_t*>(verts.data()) + info.extensionDataSize);
                pub.entities().push_back(info);
            };

            // 轮廓抽取优先：闭环 + 内外环分类；失败时退回旧顶视图投影
            bool usedContour = false;
            int32_t openWireCount = 0;
            int32_t skippedFaceCount = 0;
            int32_t planeCount = 0;
            int32_t closedLoopCount = 0;
            const char* envSwitch = std::getenv("SANYI_STEP_CONTOUR");
            const bool contourEnabled = !(envSwitch != nullptr && std::strcmp(envSwitch, "0") == 0);

            if (contourEnabled)
            {
                GeoModelCore::GmcContourResult contourResult =
                    GeoModelCore::GmcCutContour::extract(*shape);
                planeCount = static_cast<int32_t>(contourResult.planes.size());
                openWireCount = contourResult.openWireCount;
                skippedFaceCount = contourResult.skippedFaceCount;
                if (contourResult.status.ok && !contourResult.planes.empty())
                {
                    // 多高度板件取净面积最大的平面作为下料轮廓
                    const GeoModelCore::GmcContourPlane* best = &contourResult.planes.front();
                    for (const auto& plane : contourResult.planes)
                    {
                        if (plane.totalArea > best->totalArea)
                        {
                            best = &plane;
                        }
                    }
                    for (const auto& loop : best->loops)
                    {
                        if (loop.points.size() < 2)
                        {
                            continue;
                        }
                        std::vector<double> verts;
                        verts.reserve(loop.points.size() * 2);
                        for (const auto& pt : loop.points)
                        {
                            verts.push_back(pt.x);
                            verts.push_back(pt.y);
                        }
                        if (loop.closed)
                        {
                            emitPolygon(verts, static_cast<uint32_t>(loop.points.size()), EntityType::Polygon);
                            ++closedLoopCount;
                        }
                        else
                        {
                            emitPolygon(verts, static_cast<uint32_t>(loop.points.size()), EntityType::Polyline);
                        }
                    }
                    usedContour = !pub.entities().empty();
                }
                if (!usedContour)
                {
                    SY_WARNF("[StepParser] contour extract fallback: code=%d msg=%s skipped=%d open=%d planes=%d",
                        static_cast<int>(contourResult.status.code), contourResult.status.message,
                        skippedFaceCount, openWireCount, planeCount);
                }
            }

            if (!usedContour)
            {
                const auto polylines = GeoModelCore::GmcProjection::projectTopView(*shape);
                if (polylines.empty())
                {
                    SY_WARNF("[StepParser] parseToIR: no projectable edges: %s", filePath);
                    return FioParseResult{};
                }

                pub.entities().reserve(polylines.size());
                for (const auto& poly : polylines)
                {
                    if (poly.points.size() < 2)
                    {
                        continue;
                    }
                    std::vector<double> verts;
                    verts.reserve(poly.points.size() * 2);
                    for (const auto& pt : poly.points)
                    {
                        verts.push_back(pt.first);
                        verts.push_back(pt.second);
                    }
                    emitPolygon(verts, static_cast<uint32_t>(poly.points.size()), EntityType::Polyline);
                }
            }
            else
            {
                SY_INFOF("[StepParser] contour extract: planes=%d closedLoops=%d openWires=%d skippedFaces=%d",
                    planeCount, closedLoopCount, openWireCount, skippedFaceCount);
            }

            if (pub.entities().empty())
            {
                SY_WARNF("[StepParser] parseToIR: no usable 2D geometry: %s", filePath);
                return FioParseResult{};
            }

            const auto t1 = std::chrono::steady_clock::now();
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            SY_INFOF("[StepParser] parseToIR END: %u entities (%lld ms): %s",
                static_cast<uint32_t>(pub.entities().size()),
                static_cast<long long>(ms),
                filePath);
            return pub.publish("STEP");
        }
        catch (const std::exception& ex)
        {
            SY_CRITICALF("[StepParser] parseToIR exception: %s - %s", filePath, ex.what());
            return FioParseResult{};
        }
        catch (...)
        {
            SY_CRITICALF("[StepParser] parseToIR unknown exception: %s", filePath);
            return FioParseResult{};
        }
#else
        SY_WARN("[StepParser] parseToIR: STEP import unavailable (rebuild with BUILD_GEOMODELCORE=ON and "
                "GMC_ENABLE_STEP_IO)");
        return FioParseResult{};
#endif
    }
}  // namespace Fio

#include "FileIO/FileParserFactory.h"

namespace Fio
{
    namespace
    {
        static struct StepRegistrar
        {
            StepRegistrar()
            {
                FileParserFactory::instance().registerParser(FileFormat::STEP, []() -> IFileParser* {
                    return new StepParser();
                });
            }
        } s_stepRegistrar;
    }  // namespace
}  // namespace Fio