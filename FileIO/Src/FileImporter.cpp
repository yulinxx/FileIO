#include "FileIO/FileImporter.h"

#include "FileIO/FileParserFactory.h"
#include "FileIO/IFileParser.h"
#include "FileIO/FileFormat.h"

#include "Internal/FileIOInternal.h"
#include "Internal/ParsedGeometry.h"

#include <vector>
#include <string>
#include <cstring>
#include <sstream>
#include <filesystem>

// 临时保留的 Engine 转换（后续逐个 Parser 迁移后移除）
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyEllipse.h"
#include "Engine2D/SyEntity/SyPoint.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Engine2D/SyEntity/SyBezier.h"
#include "Engine2D/SyEntity/SyBezier2.h"
#include "Engine2D/SyEntity/SyNurbs.h"
#include "Engine2D/SyEntity/SyText.h"
#include "Engine2D/SyEntity/SyImage.h"

namespace Fio
{
    struct FileImporter::Impl
    {
        ParseData data;
        std::string lastError;
        bool hasData = false;

        /// 将 Engine 图元转换为内部 ParsedGeometry
        static void convertEntity(const Eg::SyEntity* src, ParsedGeometry& out)
        {
            out.sourceId = src->id;
            out.name = src->name();
            out.visible = src->visible();
            out.locked = src->locked();
            out.closed = src->bClosed;
            out.ccw = src->bCCW;

            if (auto* line = dynamic_cast<const Eg::SyLine*>(src))
            {
                out.type = ParsedGeometryType::Line;
                if (line->pointRef().size() >= 2)
                {
                    out.line.start = { line->pointRef()[0].x(), line->pointRef()[0].y() };
                    out.line.end = { line->pointRef()[1].x(), line->pointRef()[1].y() };
                }
            }
            else if (auto* arc = dynamic_cast<const Eg::SyArc*>(src))
            {
                out.type = ParsedGeometryType::Arc;
                out.arc.center = { arc->basePoint.x(), arc->basePoint.y() };
                out.arc.radius = arc->dRadius;
                out.arc.startAngle = arc->dStartAngle;
                out.arc.endAngle = arc->dEndAngle;
            }
            else if (auto* circle = dynamic_cast<const Eg::SyCircle*>(src))
            {
                out.type = ParsedGeometryType::Circle;
                out.circle.center = { circle->basePoint.x(), circle->basePoint.y() };
                out.circle.radius = circle->dRadius;
            }
            else if (auto* ellipse = dynamic_cast<const Eg::SyEllipse*>(src))
            {
                out.type = ParsedGeometryType::Ellipse;
                out.ellipse.center = { ellipse->basePoint.x(), ellipse->basePoint.y() };
                out.ellipse.radiusX = ellipse->dRadiusX;
                out.ellipse.radiusY = ellipse->dRadiusY;
                out.ellipse.rotation = ellipse->dRotation;
                out.ellipse.startAngle = ellipse->dStartAngle;
                out.ellipse.endAngle = ellipse->dEndAngle;
            }
            else if (auto* poly = dynamic_cast<const Eg::SyPolygon*>(src))
            {
                out.type = ParsedGeometryType::Polygon;
                out.closed = true;
                for (auto& v : poly->vertices())
                {
                    out.polyline.points.push_back({ v.x(), v.y() });
                }
            }
            else if (auto* bezier = dynamic_cast<const Eg::SyBezier*>(src))
            {
                out.type = ParsedGeometryType::Bezier;
                out.bezier.start = { bezier->basePoint.x(), bezier->basePoint.y() };
                out.bezier.ctrl0 = { bezier->ptCtrl0.x(), bezier->ptCtrl0.y() };
                out.bezier.ctrl1 = { bezier->ptCtrl1.x(), bezier->ptCtrl1.y() };
                out.bezier.end = { bezier->ptEnd.x(), bezier->ptEnd.y() };
            }
            else if (auto* bezier2 = dynamic_cast<const Eg::SyBezier2*>(src))
            {
                out.type = ParsedGeometryType::Bezier2;
                out.bezier2.start = { bezier2->basePoint.x(), bezier2->basePoint.y() };
                out.bezier2.ctrl = { bezier2->ptCtrl.x(), bezier2->ptCtrl.y() };
                out.bezier2.end = { bezier2->ptEnd.x(), bezier2->ptEnd.y() };
            }
            else if (auto* nurbs = dynamic_cast<const Eg::SyNurbs*>(src))
            {
                out.type = ParsedGeometryType::Nurbs;
                out.nurbs.degree = nurbs->nDegree;
                for (const auto& k : nurbs->knotRef())
                {
                    out.nurbs.knots.push_back(k);
                }
                for (const auto& w : nurbs->weightRef())
                {
                    out.nurbs.weights.push_back(w);
                }
                for (const auto& p : nurbs->controlPointRef())
                {
                    out.nurbs.controlPoints.push_back({ p.x(), p.y() });
                }
            }
            else if (auto* text = dynamic_cast<const Eg::SyText*>(src))
            {
                out.type = ParsedGeometryType::Text;
                out.text.position = { text->basePoint.x(), text->basePoint.y() };
                out.text.text = text->textStr();
                out.text.height = text->dHeight;
                out.text.fontFamily = text->fontNameStr();
                out.text.angle = text->dRotation;
            }
            else if (auto* img = dynamic_cast<const Eg::SyImage*>(src))
            {
                (void)img;
                out.type = ParsedGeometryType::Image;
                out.image.position = { img->topLeft.x(), img->topLeft.y() };
                out.image.width = img->nWidth;
                out.image.height = img->nHeight;
                out.image.data = img->pixelDataVector();
            }
        }

        /// EntityInfo 填充辅助
        static void fillEntityInfo(const ParsedGeometry& src, EntityInfo* out)
        {
            out->sourceId = src.sourceId;
            out->type = static_cast<EntityType>(src.type);
            std::strncpy(out->name, src.name.c_str(), sizeof(out->name) - 1);
            out->layerSourceId = src.layerSourceId;
            out->lineWidth = src.lineWidth;
            out->visible = src.visible;
            out->locked = src.locked;

            switch (src.type)
            {
            case ParsedGeometryType::Line:
                out->type = EntityType::Line;
                out->line = { src.line.start.x, src.line.start.y, src.line.end.x, src.line.end.y };
                break;
            case ParsedGeometryType::Arc:
                out->type = EntityType::Arc;
                out->arc = { src.arc.center.x, src.arc.center.y, src.arc.radius, src.arc.startAngle, src.arc.endAngle };
                break;
            case ParsedGeometryType::Circle:
                out->type = EntityType::Circle;
                out->circle = { src.circle.center.x, src.circle.center.y, src.circle.radius };
                break;
            case ParsedGeometryType::Ellipse:
                out->type = EntityType::Ellipse;
                out->ellipse = { src.ellipse.center.x,
                    src.ellipse.center.y,
                    src.ellipse.radiusX,
                    src.ellipse.radiusY,
                    src.ellipse.rotation,
                    src.ellipse.startAngle,
                    src.ellipse.endAngle };
                break;
            case ParsedGeometryType::Text:
                out->type = EntityType::Text;
                out->text = { src.text.position.x, src.text.position.y, {}, src.text.height, src.text.angle };
                std::strncpy(out->text.text, src.text.text.c_str(), sizeof(out->text.text) - 1);
                break;
            case ParsedGeometryType::Bezier:
                out->type = EntityType::Bezier;
                out->bezier = { src.bezier.ctrl0.x,
                    src.bezier.ctrl0.y,
                    src.bezier.ctrl1.x,
                    src.bezier.ctrl1.y,
                    src.bezier.end.x,
                    src.bezier.end.y };
                break;
            case ParsedGeometryType::Bezier2:
                out->type = EntityType::Bezier2;
                out->bezier2 = { src.bezier2.ctrl.x, src.bezier2.ctrl.y, src.bezier2.end.x, src.bezier2.end.y };
                break;
            default:
                break;
            }
        }
    };

    FileImporter::FileImporter()
        : pImpl(new Impl())
    {
    }

    FileImporter::~FileImporter()
    {
        delete pImpl;
    }

    bool FileImporter::Open(const char* path)
    {
        if (!path || !*path)
        {
            pImpl->lastError = "Empty file path";
            return false;
        }

        // 使用 FileParserFactory 直接解析（模块内部，不经过 FileIOManager 的 ABI 层）
        FileParserFactory::instance().initDefaults();
        auto& factory = FileParserFactory::instance();

        std::filesystem::path pathObj = std::filesystem::u8path(path);
        std::string ext = pathObj.extension().string();
        if (!ext.empty() && ext[0] == '.')
        {
            ext = ext.substr(1);
        }

        FileFormat fmt = factory.detectFormat(ext.c_str());
        if (fmt == FileFormat::Unknown)
        {
            pImpl->lastError = "Unsupported file format: " + std::string(path);
            return false;
        }

        IFileParser* parser = factory.createParser(fmt);
        if (!parser)
        {
            pImpl->lastError = "Failed to create parser for: " + std::string(path);
            return false;
        }

        // 旧版 parse() 已内迁至 ILegacyParser（FileIO.dll 内部接口）
        ILegacyParser* legacyParser = dynamic_cast<ILegacyParser*>(parser);
        if (!legacyParser)
        {
            factory.destroyParser(parser);
            pImpl->lastError = "Parser does not support legacy parse interface: " + std::string(path);
            return false;
        }

        VecSyEntityPtr entities;
        ParseResult result;
        try
        {
            result = legacyParser->parse(path, entities);
        }
        catch (...)
        {
            factory.destroyParser(parser);
            throw;
        }
        factory.destroyParser(parser);
        if (!result.success)
        {
            pImpl->lastError = result.errorMessage;
            return false;
        }

        // 清空旧数据
        pImpl->data = ParseData();

        // 转换图层信息
        for (auto& dxfLayer : result.dxfLayers)
        {
            ParsedLayer layer;
            layer.sourceId = static_cast<uint32_t>(pImpl->data.layers.size() + 1);
            layer.name = dxfLayer.name;
            layer.color = static_cast<uint32_t>(dxfLayer.color);
            layer.visible = dxfLayer.visible;
            pImpl->data.layers.push_back(std::move(layer));
        }

        // 转换图元
        for (size_t ei = 0; ei < entities.size(); ++ei)
        {
            ParsedGeometry g;
            Impl::convertEntity(entities[ei].get(), g);

            // 设置图层引用（通过 entityLayerMap）
            auto it = result.entityLayerMap.find(ei);
            if (it != result.entityLayerMap.end())
            {
                for (size_t li = 0; li < pImpl->data.layers.size(); ++li)
                {
                    if (pImpl->data.layers[li].name == it->second)
                    {
                        g.layerSourceId = pImpl->data.layers[li].sourceId;
                        break;
                    }
                }
            }

            pImpl->data.geometries.push_back(std::move(g));
        }

        pImpl->data.warnings = result.warnings;
        pImpl->data.success = true;
        pImpl->hasData = true;

        return true;
    }

    int FileImporter::LayerCount() const
    {
        return pImpl->hasData ? static_cast<int>(pImpl->data.layers.size()) : 0;
    }

    bool FileImporter::GetLayer(int index, IrLayerInfo* out) const
    {
        if (!out || index < 0 || index >= LayerCount())
        {
            return false;
        }

        const auto& src = pImpl->data.layers[index];
        out->sourceId = src.sourceId;
        std::strncpy(out->name, src.name.c_str(), sizeof(out->name) - 1);
        out->color = src.color;
        out->visible = src.visible;
        out->locked = src.locked;
        return true;
    }

    int FileImporter::EntityCount() const
    {
        return pImpl->hasData ? static_cast<int>(pImpl->data.geometries.size()) : 0;
    }

    bool FileImporter::GetEntity(int index, EntityInfo* out) const
    {
        if (!out || index < 0 || index >= EntityCount())
        {
            return false;
        }

        Impl::fillEntityInfo(pImpl->data.geometries[index], out);
        return true;
    }

    BinaryBlob FileImporter::ExportBlob() const
    {
        if (!pImpl->hasData)
        {
            return { nullptr, 0 };
        }

        // 序列化到内存流
        std::vector<uint8_t> buffer;

        auto writeU32 = [&](uint32_t v) {
            buffer.push_back(static_cast<uint8_t>(v & 0xFF));
            buffer.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
            buffer.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
            buffer.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
        };
        auto writeU64 = [&](uint64_t v) {
            for (int i = 0; i < 8; ++i)
            {
                buffer.push_back(static_cast<uint8_t>(v & 0xFF));
                v >>= 8;
            }
        };
        auto writeDouble = [&](double v) {
            uint64_t bits;
            std::memcpy(&bits, &v, sizeof(bits));
            writeU64(bits);
        };
        auto writeString = [&](const std::string& s) {
            uint32_t len = static_cast<uint32_t>(s.size());
            writeU32(len);
            buffer.insert(buffer.end(), s.begin(), s.end());
        };

        // 写入魔数 + 版本
        writeU32(0x46494F42);  // "FIOB"
        writeU32(1);           // version

        // 写入图层数
        writeU32(static_cast<uint32_t>(pImpl->data.layers.size()));
        for (auto& layer : pImpl->data.layers)
        {
            writeU32(layer.sourceId);
            writeString(layer.name);
            writeU32(layer.color);
            buffer.push_back(layer.visible ? 1 : 0);
            buffer.push_back(layer.locked ? 1 : 0);
        }

        // 写入图元数
        writeU32(static_cast<uint32_t>(pImpl->data.geometries.size()));
        for (auto& g : pImpl->data.geometries)
        {
            writeU64(g.sourceId);
            writeU32(static_cast<uint32_t>(g.type));
            writeString(g.name);
            writeU32(g.layerSourceId);
            writeDouble(g.lineWidth);
            buffer.push_back(g.visible ? 1 : 0);
            buffer.push_back(g.locked ? 1 : 0);
        }

        // 写入群组数
        writeU32(static_cast<uint32_t>(pImpl->data.groups.size()));

        // 分配 blob
        auto* rawData = new uint8_t[buffer.size()];
        std::memcpy(rawData, buffer.data(), buffer.size());

        BinaryBlob blob;
        blob.data = rawData;
        blob.size = buffer.size();
        return blob;
    }

    void FileImporter::FreeBlob(BinaryBlob* blob)
    {
        if (blob && blob->data)
        {
            delete[] blob->data;
            blob->data = nullptr;
            blob->size = 0;
        }
    }

    const char* FileImporter::LastError() const
    {
        return pImpl->lastError.c_str();
    }
}  // namespace Fio