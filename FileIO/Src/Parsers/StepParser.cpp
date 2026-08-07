#include "FileIO/Parsers/StepParser.h"

#include "Log/SyLogger.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <vector>

#if defined(FILEIO_HAS_GEOMODELCORE) && defined(GMC_ENABLE_STEP_IO)
#include "GeoModelCore/GeoModelDocument.h"
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

        // thread_local 缓冲区管理生命周期（与 DxfParser 一致）
        thread_local std::vector<EntityInfo> s_entities;
        thread_local std::vector<uint8_t> s_extensionBlob;
        s_entities.clear();
        s_extensionBlob.clear();

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

            const auto polylines = GeoModelCore::GmcProjection::projectTopView(*shape);
            if (polylines.empty())
            {
                SY_WARNF("[StepParser] parseToIR: no projectable edges: %s", filePath);
                return FioParseResult{};
            }

            s_entities.reserve(polylines.size());
            for (const auto& poly : polylines)
            {
                if (poly.points.size() < 2)
                    continue;

                // 收集顶点为 double 序列（x0,y0,x1,y1,...）
                std::vector<double> verts;
                verts.reserve(poly.points.size() * 2);
                for (const auto& pt : poly.points)
                {
                    verts.push_back(pt.first);
                    verts.push_back(pt.second);
                }

                EntityInfo info{};
                info.type = EntityType::Polyline;
                info.sourceId = static_cast<uint64_t>(s_entities.size());
                info.visible = true;
                info.vertexCount = static_cast<uint32_t>(poly.points.size());
                info.extensionDataOffset = static_cast<uint32_t>(s_extensionBlob.size());
                info.extensionDataSize = static_cast<uint32_t>(verts.size() * sizeof(double));
                s_extensionBlob.insert(s_extensionBlob.end(),
                    reinterpret_cast<const uint8_t*>(verts.data()),
                    reinterpret_cast<const uint8_t*>(verts.data()) + info.extensionDataSize);
                s_entities.push_back(info);
            }

            if (s_entities.empty())
            {
                SY_WARNF("[StepParser] parseToIR: no usable 2D geometry: %s", filePath);
                return FioParseResult{};
            }

            FioParseResult result;
            result.entities = s_entities.data();
            result.entityCount = static_cast<uint32_t>(s_entities.size());
            result.layers = nullptr;
            result.layerCount = 0;
            result.extensionBlob.data = s_extensionBlob.data();
            result.extensionBlob.size = s_extensionBlob.size();
            std::strncpy(result.sourceFormat, "STEP", sizeof(result.sourceFormat) - 1);

            const auto t1 = std::chrono::steady_clock::now();
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            SY_INFOF("[StepParser] parseToIR END: %u entities (%lld ms): %s",
                result.entityCount, static_cast<long long>(ms), filePath);
            return result;
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
        SY_WARN("[StepParser] parseToIR: STEP import unavailable (rebuild with BUILD_GEOMODELCORE=ON and GMC_ENABLE_STEP_IO)");
        return FioParseResult{};
#endif
    }
} // namespace Fio
