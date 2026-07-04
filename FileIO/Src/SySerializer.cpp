#include "FileIO/SySerializer.h"

// ---- Protobuf 生成的头文件（由 protoc 编译 .proto 生成）----
#include "SanYiDocument.pb.h"

// ---- Engine 图元类型 ----
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyBarCode.h"
#include "Engine2D/SyEntity/SyBezier.h"
#include "Engine2D/SyEntity/SyBezier2.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyEllipse.h"
#include "Engine2D/SyEntity/SyImage.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyNurbs.h"
#include "Engine2D/SyEntity/SyPoint.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Engine2D/SyEntity/SyQRCode.h"
#include "Engine2D/SyEntity/SySmartLine.h"
#include "Engine2D/SyEntity/SyNurbs.h"
#include "Engine2D/SyEntity/SyText.h"
#include "Engine2D/SyEntity/SyGroup.h"
#include "Engine2D/SyEntity/SyEntity.h"
#include "Engine/Layer/SyLayer.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace Fio
{
    // ============================================================
    // 内部辅助函数 (匿名命名空间)
    // ============================================================
    namespace
    {
        // ---- CRC32 查表实现 ----
        static uint32_t crc32Table[256];
        static bool     crc32TableInitialized = false;

        void initCrc32Table()
        {
            if (crc32TableInitialized)
                return;

            const uint32_t polynomial = 0xEDB88320;
            for (uint32_t i = 0; i < 256; ++i)
            {
                uint32_t crc = i;
                for (int j = 0; j < 8; ++j)
                {
                    if (crc & 1)
                        crc = (crc >> 1) ^ polynomial;
                    else
                        crc >>= 1;
                }
                crc32Table[i] = crc;
            }
            crc32TableInitialized = true;
        }

        uint32_t computeCrc32(const uint8_t* data, size_t len)
        {
            initCrc32Table();
            uint32_t crc = 0xFFFFFFFF;
            for (size_t i = 0; i < len; ++i)
            {
                crc = (crc >> 8) ^ crc32Table[(crc ^ data[i]) & 0xFF];
            }
            return crc ^ 0xFFFFFFFF;
        }

        // ---- 类型转换 ----

        sanyi::proto::EntityType toProtoType(Eg::EType type)
        {
            switch (type)
            {
                case Eg::EType::POINT:      return sanyi::proto::ENTITY_POINT;
                case Eg::EType::LINE:       return sanyi::proto::ENTITY_LINE;
                case Eg::EType::POLYGON:    return sanyi::proto::ENTITY_POLYGON;
                case Eg::EType::ARC:        return sanyi::proto::ENTITY_ARC;
                case Eg::EType::CIRCLE:     return sanyi::proto::ENTITY_CIRCLE;
                case Eg::EType::ELLIPSE:    return sanyi::proto::ENTITY_ELLIPSE;
                case Eg::EType::BEZIER2:    return sanyi::proto::ENTITY_BEZIER2;
                case Eg::EType::BEZIER:     return sanyi::proto::ENTITY_BEZIER;
                case Eg::EType::SPLINE:     return sanyi::proto::ENTITY_SPLINE;
                case Eg::EType::SMARTLINE:  return sanyi::proto::ENTITY_SMARTLINE;
                case Eg::EType::TEXT:       return sanyi::proto::ENTITY_TEXT;
                case Eg::EType::BAR_CODE:   return sanyi::proto::ENTITY_BAR_CODE;
                case Eg::EType::QR_CODE:    return sanyi::proto::ENTITY_QR_CODE;
                case Eg::EType::IMAGE:      return sanyi::proto::ENTITY_IMAGE;
                case Eg::EType::GROUP:      return sanyi::proto::ENTITY_GROUP;
                default:                    return sanyi::proto::ENTITY_UNKNOWN;
            }
        }

        Eg::EType fromProtoType(sanyi::proto::EntityType type)
        {
            switch (type)
            {
                case sanyi::proto::ENTITY_POINT:     return Eg::EType::POINT;
                case sanyi::proto::ENTITY_LINE:      return Eg::EType::LINE;
                case sanyi::proto::ENTITY_POLYGON:   return Eg::EType::POLYGON;
                case sanyi::proto::ENTITY_ARC:       return Eg::EType::ARC;
                case sanyi::proto::ENTITY_CIRCLE:    return Eg::EType::CIRCLE;
                case sanyi::proto::ENTITY_ELLIPSE:   return Eg::EType::ELLIPSE;
                case sanyi::proto::ENTITY_BEZIER2:   return Eg::EType::BEZIER2;
                case sanyi::proto::ENTITY_BEZIER:    return Eg::EType::BEZIER;
                case sanyi::proto::ENTITY_SPLINE:    return Eg::EType::SPLINE;
                case sanyi::proto::ENTITY_NURBS:     return Eg::EType::SPLINE;  // NURBS 映射到 SPLINE
                case sanyi::proto::ENTITY_SMARTLINE: return Eg::EType::SMARTLINE;
                case sanyi::proto::ENTITY_TEXT:      return Eg::EType::TEXT;
                case sanyi::proto::ENTITY_BAR_CODE:  return Eg::EType::BAR_CODE;
                case sanyi::proto::ENTITY_QR_CODE:   return Eg::EType::QR_CODE;
                case sanyi::proto::ENTITY_IMAGE:     return Eg::EType::IMAGE;
                case sanyi::proto::ENTITY_GROUP:     return Eg::EType::GROUP;
                default:                             return Eg::EType::UNKNOWN;
            }
        }

        // ---- 工具: Vec2d ----

        void toProtoVec2(const Ut::Vec2d& v, sanyi::proto::Vec2d* p)
        {
            p->set_x(v.x());
            p->set_y(v.y());
        }

        Ut::Vec2d fromProtoVec2(const sanyi::proto::Vec2d& p)
        {
            return Ut::Vec2d(p.x(), p.y());
        }

        // ---- 工具: Vec3f ----

        void toProtoVec3(const Ut::Vec3f& v, sanyi::proto::Vec3f* p)
        {
            p->set_r(v.x());
            p->set_g(v.y());
            p->set_b(v.z());
        }

        Ut::Vec3f fromProtoVec3(const sanyi::proto::Vec3f& p)
        {
            return Ut::Vec3f(p.r(), p.g(), p.b());
        }

        // ---- 工具: PropertyMap ----

        void toProtoProperties(const PropertyMap& props,
            sanyi::proto::SanYiDocument& doc,
            google::protobuf::RepeatedPtrField<sanyi::proto::PropertyEntry>* entries)
        {
            (void)doc;
            for (const auto& kv : props)
            {
                auto* entry = entries->Add();
                entry->set_key(kv.first);
                entry->set_value(kv.second);
            }
        }

        PropertyMap fromProtoProperties(
            const google::protobuf::RepeatedPtrField<sanyi::proto::PropertyEntry>& entries)
        {
            PropertyMap props;
            for (const auto& entry : entries)
            {
                props[entry.key()] = entry.value();
            }
            return props;
        }

        // ---- 获取当前 ISO 8601 时间 (简化版) ----

        std::string currentIsoTime()
        {
            const auto now = std::chrono::system_clock::now();
            const auto time = std::chrono::system_clock::to_time_t(now);

            std::ostringstream oss;
            oss << std::put_time(std::localtime(&time), "%Y-%m-%dT%H:%M:%S");
            return oss.str();
        }

        // ---- 获取当前操作系统名称 ----

        std::string currentOS()
        {
#ifdef _WIN32
            return "Windows";
#elif defined(__linux__)
            return "Linux";
#elif defined(__APPLE__)
            return "macOS";
#else
            return "Unknown";
#endif
        }

        // ============================================================
        // 填充元信息默认值
        // ============================================================
        void ensureMetadataDefaults(DocumentMetadata& meta)
        {
            if (meta.version == 0)
                meta.version = SyFileConst::FILE_VERSION;

            if (meta.softwareName.empty())
                meta.softwareName = "SanYi CAD";

            if (meta.softwareVersion.empty())
                meta.softwareVersion = "1.0.0";

            if (meta.createdTime.empty())
                meta.createdTime = currentIsoTime();

            meta.modifiedTime = currentIsoTime();

            if (meta.operatingSystem.empty())
                meta.operatingSystem = currentOS();
        }

        // ============================================================
        // 序列化: SyDocument → Protobuf → bytes
        // ============================================================

        bool serializeToProto(const SyDocument& doc, std::vector<uint8_t>& out)
        {
            sanyi::proto::SanYiDocument protoDoc;

            // -- metadata --
            {
                auto* meta = protoDoc.mutable_metadata();
                meta->set_version(doc.metadata.version);
                meta->set_file_version(doc.metadata.fileVersion);
                meta->set_author(doc.metadata.author);
                meta->set_software_name(doc.metadata.softwareName);
                meta->set_software_version(doc.metadata.softwareVersion);
                meta->set_created_time(doc.metadata.createdTime);
                meta->set_modified_time(doc.metadata.modifiedTime);
                meta->set_operating_system(doc.metadata.operatingSystem);
                meta->set_description(doc.metadata.description);

                toProtoProperties(doc.metadata.customProperties, protoDoc,
                    meta->mutable_custom_properties());
            }

            // -- layers --
            for (const auto& layer : doc.layers)
            {
                auto* l = protoDoc.add_layers();
                l->set_id(layer.id);
                l->set_name(layer.name);
                l->set_color(layer.color);
                l->set_visible(layer.visible);
                l->set_locked(layer.locked);
                toProtoProperties(layer.customProperties, protoDoc,
                    l->mutable_custom_properties());
            }

            // -- entities --
            for (const auto& entity : doc.entities)
            {
                if (!entity)
                    continue;

                auto* e = protoDoc.add_entities();
                e->set_type(toProtoType(entity->eType));
                e->set_id(entity->id);

                // layer_id
                if (entity->pLayer)
                    e->set_layer_id(static_cast<uint32_t>(entity->pLayer->getId()));

                // base_point
                toProtoVec2(entity->basePoint, e->mutable_base_point());

                // common flags
                e->set_closed(entity->bClosed);
                e->set_ccw(entity->bCCW);

                // --- 类型特有数据 ---
                switch (entity->eType)
                {
                    case Eg::EType::POINT:
                    {
                        e->mutable_point_data();  // 空消息
                        break;
                    }

                    case Eg::EType::LINE:
                    {
                        const auto* line = static_cast<const Eg::SyLine*>(entity.get());
                        auto* data = e->mutable_line_data();
                        for (const auto& pt : line->vPoints)
                        {
                            toProtoVec2(pt, data->add_points());
                        }
                        break;
                    }

                    case Eg::EType::POLYGON:
                    {
                        const auto* poly = static_cast<const Eg::SyPolygon*>(entity.get());
                        auto* data = e->mutable_polygon_data();
                        for (const auto& v : poly->vVertices)
                            toProtoVec2(v, data->add_vertices());
                        data->set_sides(poly->nSides);
                        data->set_circum_radius(poly->dCircumRadius);
                        break;
                    }

                    case Eg::EType::ARC:
                    {
                        const auto* arc = static_cast<const Eg::SyArc*>(entity.get());
                        auto* data = e->mutable_arc_data();
                        data->set_radius(arc->dRadius);
                        data->set_start_angle(arc->dStartAngle);
                        data->set_end_angle(arc->dEndAngle);
                        break;
                    }

                    case Eg::EType::CIRCLE:
                    {
                        const auto* circle = static_cast<const Eg::SyCircle*>(entity.get());
                        e->mutable_circle_data()->set_radius(circle->dRadius);
                        break;
                    }

                    case Eg::EType::ELLIPSE:
                    {
                        const auto* ell = static_cast<const Eg::SyEllipse*>(entity.get());
                        auto* data = e->mutable_ellipse_data();
                        data->set_radius_x(ell->dRadiusX);
                        data->set_radius_y(ell->dRadiusY);
                        data->set_rotation(ell->dRotation);
                        break;
                    }

                    case Eg::EType::BEZIER2:
                    {
                        const auto* bz = static_cast<const Eg::SyBezier2*>(entity.get());
                        auto* data = e->mutable_bezier2_data();
                        toProtoVec2(bz->ptCtrl, data->mutable_ctrl());
                        toProtoVec2(bz->ptEnd, data->mutable_end());
                        break;
                    }

                    case Eg::EType::BEZIER:
                    {
                        const auto* bz = static_cast<const Eg::SyBezier*>(entity.get());
                        auto* data = e->mutable_bezier_data();
                        toProtoVec2(bz->ptCtrl0, data->mutable_ctrl0());
                        toProtoVec2(bz->ptCtrl1, data->mutable_ctrl1());
                        toProtoVec2(bz->ptEnd, data->mutable_end());
                        break;
                    }

                    case Eg::EType::SPLINE:
                    {
                        const auto* spl = static_cast<const Eg::SyNurbs*>(entity.get());
                        auto* data = e->mutable_spline_data();
                        data->set_degree(spl->nDegree);
                        for (double k : spl->vKnots)
                            data->add_knots(k);
                        for (double w : spl->vWeights)
                            data->add_weights(w);
                        for (const auto& cp : spl->vControlPoints)
                            toProtoVec2(cp, data->add_control_points());
                        break;
                    }

                    case Eg::EType::TEXT:
                    {
                        const auto* txt = static_cast<const Eg::SyText*>(entity.get());
                        auto* data = e->mutable_text_data();
                        data->set_font_name(txt->strFontName);
                        data->set_height(txt->dHeight);
                        data->set_rotation(txt->dRotation);
                        data->set_h_align(static_cast<int32_t>(txt->hAlign));
                        data->set_v_align(static_cast<int32_t>(txt->vAlign));
                        data->set_bold(txt->bBold);
                        data->set_italic(txt->bItalic);
                        data->set_text(txt->strText);
                        break;
                    }

                    case Eg::EType::BAR_CODE:
                    {
                        const auto* bc = static_cast<const Eg::SyBarCode*>(entity.get());
                        auto* data = e->mutable_barcode_data();
                        data->set_data(bc->strData);
                        data->set_width(bc->dWidth);
                        data->set_height(bc->dHeight);
                        break;
                    }

                    case Eg::EType::QR_CODE:
                    {
                        const auto* qr = static_cast<const Eg::SyQRCode*>(entity.get());
                        auto* data = e->mutable_qrcode_data();
                        data->set_data(qr->strData);
                        data->set_module_size(qr->dModuleSize);
                        break;
                    }

                    case Eg::EType::IMAGE:
                    {
                        const auto* img = static_cast<const Eg::SyImage*>(entity.get());
                        auto* data = e->mutable_image_data();
                        data->set_width(img->nWidth);
                        data->set_height(img->nHeight);
                        data->set_pixel_format(static_cast<int32_t>(img->ePixelFormat));
                        data->set_pixel_data(img->vPixelData.data(), img->vPixelData.size());
                        toProtoVec2(img->topLeft, data->mutable_top_left());
                        toProtoVec2(img->topRight, data->mutable_top_right());
                        toProtoVec2(img->bottomLeft, data->mutable_bottom_left());
                        toProtoVec2(img->bottomRight, data->mutable_bottom_right());
                        break;
                    }

                    default:
                        break;
                }
            }

            // -- groups --
            for (const auto& groupInfo : doc.groups)
            {
                if (groupInfo.isEmpty())
                    continue;

                auto* g = protoDoc.add_groups();
                g->set_id(groupInfo.id);
                if (!groupInfo.name.empty())
                    g->set_name(groupInfo.name);
                if (groupInfo.parentGroupId != 0)
                    g->set_parent_group_id(groupInfo.parentGroupId);

                for (uint64_t entityId : groupInfo.entityIds)
                    g->add_entity_ids(entityId);

                for (uint64_t subGroupId : groupInfo.subGroupIds)
                    g->add_sub_group_ids(subGroupId);
            }

            // -- hardware --
            {
                auto* hw = protoDoc.mutable_hardware();
                hw->set_laser_type(doc.hardware.laserType);
                hw->set_controller_model(doc.hardware.controllerModel);
                hw->set_max_power(doc.hardware.maxPower);
                hw->set_work_area_width(doc.hardware.workAreaWidth);
                hw->set_work_area_height(doc.hardware.workAreaHeight);
                toProtoProperties(doc.hardware.customProperties, protoDoc,
                    hw->mutable_custom_properties());
            }

            // 序列化 protobuf → bytes
            const size_t byteSize = protoDoc.ByteSizeLong();
            out.resize(byteSize);
            return protoDoc.SerializeToArray(out.data(), static_cast<int>(byteSize));
        }

        // ============================================================
        // 反序列化: bytes → Protobuf → SyDocument
        // ============================================================

        bool deserializeFromProto(const std::vector<uint8_t>& data, SyDocument& doc)
        {
            sanyi::proto::SanYiDocument protoDoc;
            if (!protoDoc.ParseFromArray(data.data(), static_cast<int>(data.size())))
            {
                return false;
            }

            // -- metadata --
            if (protoDoc.has_metadata())
            {
                const auto& meta = protoDoc.metadata();
                doc.metadata.version = meta.version();
                doc.metadata.fileVersion = meta.file_version();
                doc.metadata.author = meta.author();
                doc.metadata.softwareName = meta.software_name();
                doc.metadata.softwareVersion = meta.software_version();
                doc.metadata.createdTime = meta.created_time();
                doc.metadata.modifiedTime = meta.modified_time();
                doc.metadata.operatingSystem = meta.operating_system();
                doc.metadata.description = meta.description();
                doc.metadata.customProperties = fromProtoProperties(meta.custom_properties());
            }

            // -- layers --
            for (int i = 0; i < protoDoc.layers_size(); ++i)
            {
                const auto& l = protoDoc.layers(i);
                LayerInfo layer;
                layer.id = l.id();
                layer.name = l.name();
                layer.color = l.color();
                layer.visible = l.visible();
                layer.locked = l.locked();
                layer.customProperties = fromProtoProperties(l.custom_properties());
                doc.layers.push_back(std::move(layer));
            }

            // -- entities --
            for (int i = 0; i < protoDoc.entities_size(); ++i)
            {
                const auto& ed = protoDoc.entities(i);
                Eg::EType eType = fromProtoType(ed.type());

                std::unique_ptr<Eg::SyEntity> entity;
                bool ok = true;

                switch (eType)
                {
                    case Eg::EType::POINT:
                        entity = std::make_unique<Eg::SyPoint>();
                        break;

                    case Eg::EType::LINE:
                    {
                        auto line = std::make_unique<Eg::SyLine>();
                        if (ed.has_line_data())
                        {
                            const auto& ld = ed.line_data();
                            for (int j = 0; j < ld.points_size(); ++j)
                                line->vPoints.push_back(fromProtoVec2(ld.points(j)));
                        }
                        if (!line->vPoints.empty())
                            line->basePoint = line->vPoints[0];
                        entity = std::move(line);
                        break;
                    }

                    case Eg::EType::POLYGON:
                    {
                        auto poly = std::make_unique<Eg::SyPolygon>();
                        if (ed.has_polygon_data())
                        {
                            const auto& pd = ed.polygon_data();
                            for (int j = 0; j < pd.vertices_size(); ++j)
                                poly->vVertices.push_back(fromProtoVec2(pd.vertices(j)));
                            poly->nSides = pd.sides();
                            poly->dCircumRadius = pd.circum_radius();
                        }
                        if (!poly->vVertices.empty())
                            poly->basePoint = poly->vVertices[0];
                        entity = std::move(poly);
                        break;
                    }

                    case Eg::EType::ARC:
                    {
                        auto arc = std::make_unique<Eg::SyArc>();
                        if (ed.has_arc_data())
                        {
                            arc->dRadius = ed.arc_data().radius();
                            arc->dStartAngle = ed.arc_data().start_angle();
                            arc->dEndAngle = ed.arc_data().end_angle();
                        }
                        entity = std::move(arc);
                        break;
                    }

                    case Eg::EType::CIRCLE:
                    {
                        auto circle = std::make_unique<Eg::SyCircle>();
                        if (ed.has_circle_data())
                            circle->dRadius = ed.circle_data().radius();
                        entity = std::move(circle);
                        break;
                    }

                    case Eg::EType::ELLIPSE:
                    {
                        auto ell = std::make_unique<Eg::SyEllipse>();
                        if (ed.has_ellipse_data())
                        {
                            ell->dRadiusX = ed.ellipse_data().radius_x();
                            ell->dRadiusY = ed.ellipse_data().radius_y();
                            ell->dRotation = ed.ellipse_data().rotation();
                        }
                        entity = std::move(ell);
                        break;
                    }

                    case Eg::EType::BEZIER2:
                    {
                        auto bz = std::make_unique<Eg::SyBezier2>();
                        if (ed.has_bezier2_data())
                        {
                            bz->ptCtrl = fromProtoVec2(ed.bezier2_data().ctrl());
                            bz->ptEnd = fromProtoVec2(ed.bezier2_data().end());
                        }
                        entity = std::move(bz);
                        break;
                    }

                    case Eg::EType::BEZIER:
                    {
                        auto bz = std::make_unique<Eg::SyBezier>();
                        if (ed.has_bezier_data())
                        {
                            bz->ptCtrl0 = fromProtoVec2(ed.bezier_data().ctrl0());
                            bz->ptCtrl1 = fromProtoVec2(ed.bezier_data().ctrl1());
                            bz->ptEnd = fromProtoVec2(ed.bezier_data().end());
                        }
                        entity = std::move(bz);
                        break;
                    }

                    case Eg::EType::SPLINE:
                    {
                        auto spl = std::make_unique<Eg::SyNurbs>();
                        if (ed.has_spline_data())
                        {
                            const auto& sd = ed.spline_data();
                            spl->nDegree = sd.degree();
                            for (int j = 0; j < sd.knots_size(); ++j)
                                spl->vKnots.push_back(sd.knots(j));
                            for (int j = 0; j < sd.weights_size(); ++j)
                                spl->vWeights.push_back(sd.weights(j));
                            for (int j = 0; j < sd.control_points_size(); ++j)
                                spl->vControlPoints.push_back(fromProtoVec2(sd.control_points(j)));
                        }
                        entity = std::move(spl);
                        break;
                    }

                    case Eg::EType::TEXT:
                    {
                        auto txt = std::make_unique<Eg::SyText>();
                        if (ed.has_text_data())
                        {
                            const auto& td = ed.text_data();
                            txt->strFontName = td.font_name();
                            txt->dHeight = td.height();
                            txt->dRotation = td.rotation();
                            txt->hAlign = static_cast<Eg::SyTextHAlign>(td.h_align());
                            txt->vAlign = static_cast<Eg::SyTextVAlign>(td.v_align());
                            txt->bBold = td.bold();
                            txt->bItalic = td.italic();
                            txt->strText = td.text();
                        }
                        entity = std::move(txt);
                        break;
                    }

                    case Eg::EType::BAR_CODE:
                    {
                        auto bc = std::make_unique<Eg::SyBarCode>();
                        if (ed.has_barcode_data())
                        {
                            bc->strData = ed.barcode_data().data();
                            bc->dWidth = ed.barcode_data().width();
                            bc->dHeight = ed.barcode_data().height();
                        }
                        entity = std::move(bc);
                        break;
                    }

                    case Eg::EType::QR_CODE:
                    {
                        auto qr = std::make_unique<Eg::SyQRCode>();
                        if (ed.has_qrcode_data())
                        {
                            qr->strData = ed.qrcode_data().data();
                            qr->dModuleSize = ed.qrcode_data().module_size();
                        }
                        entity = std::move(qr);
                        break;
                    }

                    case Eg::EType::IMAGE:
                    {
                        auto img = std::make_unique<Eg::SyImage>();
                        if (ed.has_image_data())
                        {
                            const auto& id = ed.image_data();
                            img->nWidth = id.width();
                            img->nHeight = id.height();
                            img->ePixelFormat = static_cast<Eg::SyPixelFormat>(id.pixel_format());
                            const auto& px = id.pixel_data();
                            img->vPixelData.assign(px.begin(), px.end());
                            img->topLeft = fromProtoVec2(id.top_left());
                            img->topRight = fromProtoVec2(id.top_right());
                            img->bottomLeft = fromProtoVec2(id.bottom_left());
                            img->bottomRight = fromProtoVec2(id.bottom_right());
                        }
                        entity = std::move(img);
                        break;
                    }

                    default:
                        ok = false;
                        doc.warnings.push_back(
                            "Unknown entity type: " + std::to_string(static_cast<int>(ed.type())));
                        break;
                }

                if (ok && entity)
                {
                    // 恢复通用属性
                    entity->id = static_cast<Eg::EntityId>(ed.id());
                    entity->basePoint = fromProtoVec2(ed.base_point());
                    entity->bClosed = ed.closed();
                    entity->bCCW = ed.ccw();

                    // 保存 layer_id 映射，调用者根据此重建 pLayer 关联
                    // 注意：proto3 中 uint32 标量没有 has_ 方法，默认值为 0
                    if (ed.layer_id() != 0)
                        doc.entityLayerMap[entity->id] = ed.layer_id();

                    doc.entities.push_back(std::move(entity));
                }
            }

            // -- groups --
            for (int i = 0; i < protoDoc.groups_size(); ++i)
            {
                const auto& gd = protoDoc.groups(i);
                GroupInfo info;
                info.id = gd.id();
                info.name = gd.name();
                info.parentGroupId = gd.parent_group_id();

                for (int j = 0; j < gd.entity_ids_size(); ++j)
                    info.entityIds.push_back(gd.entity_ids(j));

                for (int j = 0; j < gd.sub_group_ids_size(); ++j)
                    info.subGroupIds.push_back(gd.sub_group_ids(j));

                doc.groups.push_back(std::move(info));
            }

            // -- hardware --
            if (protoDoc.has_hardware())
            {
                const auto& hw = protoDoc.hardware();
                doc.hardware.laserType = hw.laser_type();
                doc.hardware.controllerModel = hw.controller_model();
                doc.hardware.maxPower = hw.max_power();
                doc.hardware.workAreaWidth = hw.work_area_width();
                doc.hardware.workAreaHeight = hw.work_area_height();
                doc.hardware.customProperties = fromProtoProperties(hw.custom_properties());
            }

            return true;
        }
    } // anonymous namespace

    // ============================================================
    // SySerializer 公共接口实现
    // ============================================================

    SySerializer::SySerializer() = default;
    SySerializer::~SySerializer() = default;

    // ---- 加密配置 ----

    void SySerializer::setCryptoProvider(CryptoProviderPtr provider)
    {
        m_cryptoProvider = std::move(provider);
    }

    bool SySerializer::hasCrypto() const
    {
        return m_cryptoProvider != nullptr;
    }

    // ---- 文件头写入 ----

    bool SySerializer::writeFileHeader(std::vector<uint8_t>& buffer,
        uint32_t              dataLen,
        uint32_t              flags,
        const char            magic[4])
    {
        buffer.resize(SyFileConst::HEADER_SIZE + dataLen + SyFileConst::FOOTER_SIZE);

        // magic
        std::memcpy(buffer.data(), magic, 4);

        // version (little-endian)
        buffer[4] = static_cast<uint8_t>(SyFileConst::FILE_VERSION & 0xFF);
        buffer[5] = static_cast<uint8_t>((SyFileConst::FILE_VERSION >> 8) & 0xFF);
        buffer[6] = static_cast<uint8_t>((SyFileConst::FILE_VERSION >> 16) & 0xFF);
        buffer[7] = static_cast<uint8_t>((SyFileConst::FILE_VERSION >> 24) & 0xFF);

        // flags (little-endian)
        buffer[8] = static_cast<uint8_t>(flags & 0xFF);
        buffer[9] = static_cast<uint8_t>((flags >> 8) & 0xFF);
        buffer[10] = static_cast<uint8_t>((flags >> 16) & 0xFF);
        buffer[11] = static_cast<uint8_t>((flags >> 24) & 0xFF);

        // data_len (little-endian)
        buffer[12] = static_cast<uint8_t>(dataLen & 0xFF);
        buffer[13] = static_cast<uint8_t>((dataLen >> 8) & 0xFF);
        buffer[14] = static_cast<uint8_t>((dataLen >> 16) & 0xFF);
        buffer[15] = static_cast<uint8_t>((dataLen >> 24) & 0xFF);

        return true;
    }

    // ---- 文件头读取 ----

    SySerializer::FileHeaderResult SySerializer::readFileHeader(
        const std::vector<uint8_t>& buffer)
    {
        FileHeaderResult result;

        if (buffer.size() < SyFileConst::HEADER_SIZE)
            return result;

        // magic
        if (std::memcmp(buffer.data(), SyFileConst::MAGIC_SY, 4) != 0)
            return result;

        // version (little-endian)
        result.version = static_cast<uint32_t>(buffer[4])
            | (static_cast<uint32_t>(buffer[5]) << 8)
            | (static_cast<uint32_t>(buffer[6]) << 16)
            | (static_cast<uint32_t>(buffer[7]) << 24);

        // flags (little-endian)
        result.flags = static_cast<uint32_t>(buffer[8])
            | (static_cast<uint32_t>(buffer[9]) << 8)
            | (static_cast<uint32_t>(buffer[10]) << 16)
            | (static_cast<uint32_t>(buffer[11]) << 24);

        // data_len (little-endian)
        result.dataLen = static_cast<uint32_t>(buffer[12])
            | (static_cast<uint32_t>(buffer[13]) << 8)
            | (static_cast<uint32_t>(buffer[14]) << 16)
            | (static_cast<uint32_t>(buffer[15]) << 24);

        result.valid = true;
        return result;
    }

    // ---- 保存到文件 ----

    SerializeResult SySerializer::saveToFile(const std::string& filePath,
        const SyDocument& doc,
        bool               encrypt)
    {
        // 1. 序列化文档 → protobuf bytes
        std::vector<uint8_t> protoData;
        if (!serializeToProto(doc, protoData))
        {
            return SerializeResult::fail("Failed to serialize document to protobuf");
        }

        // 2. 可选加密
        std::vector<uint8_t> finalData = std::move(protoData);
        uint32_t             flags = 0;

        if (encrypt)
        {
            if (!m_cryptoProvider)
            {
                return SerializeResult::fail(
                    "Encryption requested but no crypto provider set");
            }

            auto encResult = m_cryptoProvider->encrypt(finalData);
            if (!encResult.success)
            {
                return SerializeResult::fail(
                    "Encryption failed: " + encResult.errorMessage);
            }
            finalData = std::move(encResult.data);
            flags |= SyFileConst::FLAG_ENCRYPTED;
        }

        // 3. 计算 CRC32
        const uint32_t crc = computeCrc32(finalData.data(), finalData.size());

        // 4. 构建文件内容 (header + data + crc)
        std::vector<uint8_t> fileBuffer;
        writeFileHeader(fileBuffer, static_cast<uint32_t>(finalData.size()), flags, SyFileConst::MAGIC_SY);
        std::memcpy(fileBuffer.data() + SyFileConst::HEADER_SIZE,
            finalData.data(), finalData.size());

        // crc32 (little-endian) at the end
        const size_t crcOffset = SyFileConst::HEADER_SIZE + finalData.size();
        fileBuffer[crcOffset + 0] = static_cast<uint8_t>(crc & 0xFF);
        fileBuffer[crcOffset + 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);
        fileBuffer[crcOffset + 2] = static_cast<uint8_t>((crc >> 16) & 0xFF);
        fileBuffer[crcOffset + 3] = static_cast<uint8_t>((crc >> 24) & 0xFF);

        // 5. 写入磁盘
        std::ofstream out(std::filesystem::u8path(filePath), std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return SerializeResult::fail("Cannot open file for writing: " + filePath);
        }

        out.write(reinterpret_cast<const char*>(fileBuffer.data()),
            static_cast<std::streamsize>(fileBuffer.size()));

        if (!out.good())
        {
            return SerializeResult::fail("Failed to write file: " + filePath);
        }

        return SerializeResult::ok();
    }

    // ---- 从文件加载 ----

    SerializeResult SySerializer::loadFromFile(const std::string& filePath,
        SyDocument& doc)
    {
        doc.clear();

        // 1. 读取整个文件
        std::ifstream in(std::filesystem::u8path(filePath), std::ios::binary | std::ios::ate);
        if (!in)
        {
            return SerializeResult::fail("Cannot open file: " + filePath);
        }

        const auto fileSize = static_cast<size_t>(in.tellg());
        if (fileSize < SyFileConst::HEADER_SIZE + SyFileConst::FOOTER_SIZE)
        {
            return SerializeResult::fail("File too small to be a valid .sy file");
        }

        in.seekg(0);
        std::vector<uint8_t> fileBuffer(fileSize);
        if (!in.read(reinterpret_cast<char*>(fileBuffer.data()),
            static_cast<std::streamsize>(fileSize)))
        {
            return SerializeResult::fail("Failed to read file: " + filePath);
        }

        // 2. 解析文件头
        auto header = readFileHeader(fileBuffer);
        if (!header.valid)
        {
            return SerializeResult::fail("Invalid .sy file header (wrong magic)");
        }

        if (header.version != SyFileConst::FILE_VERSION)
        {
            doc.warnings.push_back(
                "File version mismatch: expected " +
                std::to_string(SyFileConst::FILE_VERSION) + ", got " +
                std::to_string(header.version));
        }

        if (header.dataLen + SyFileConst::HEADER_SIZE + SyFileConst::FOOTER_SIZE > fileSize)
        {
            return SerializeResult::fail("Corrupted file: data size exceeds file size");
        }

        // 3. 提取数据
        const uint8_t* dataPtr = fileBuffer.data() + SyFileConst::HEADER_SIZE;
        std::vector<uint8_t> data(dataPtr, dataPtr + header.dataLen);

        // 4. 验证 CRC32
        const uint32_t expectedCrc = computeCrc32(data.data(), data.size());
        const size_t   crcOffset = SyFileConst::HEADER_SIZE + header.dataLen;
        const uint32_t storedCrc = static_cast<uint32_t>(fileBuffer[crcOffset + 0])
            | (static_cast<uint32_t>(fileBuffer[crcOffset + 1]) << 8)
            | (static_cast<uint32_t>(fileBuffer[crcOffset + 2]) << 16)
            | (static_cast<uint32_t>(fileBuffer[crcOffset + 3]) << 24);

        if (expectedCrc != storedCrc)
        {
            return SerializeResult::fail(
                "CRC32 mismatch: file may be corrupted (expected " +
                std::to_string(expectedCrc) + ", got " + std::to_string(storedCrc) + ")");
        }

        // 5. 解密（如果加密）
        if (header.flags & SyFileConst::FLAG_ENCRYPTED)
        {
            if (!m_cryptoProvider)
            {
                return SerializeResult::fail(
                    "File is encrypted but no crypto provider set");
            }

            auto decResult = m_cryptoProvider->decrypt(data);
            if (!decResult.success)
            {
                return SerializeResult::fail(
                    "Decryption failed: " + decResult.errorMessage);
            }
            data = std::move(decResult.data);
        }

        // 6. 反序列化 protobuf → SyDocument
        if (!deserializeFromProto(data, doc))
        {
            return SerializeResult::fail("Failed to parse protobuf data");
        }

        return SerializeResult::ok(doc.warnings);
    }

    // ---- 内存级序列化 ----

    SerializeResult SySerializer::serializeToMemory(const SyDocument& doc,
        std::vector<uint8_t>& data)
    {
        if (!serializeToProto(doc, data))
        {
            return SerializeResult::fail("Failed to serialize document to protobuf");
        }
        return SerializeResult::ok();
    }

    SerializeResult SySerializer::deserializeFromMemory(const std::vector<uint8_t>& data,
        SyDocument& doc)
    {
        doc.clear();
        if (!deserializeFromProto(data, doc))
        {
            return SerializeResult::fail("Failed to parse protobuf data from memory");
        }
        return SerializeResult::ok(doc.warnings);
    }

    // ---- 工具方法 ----

    uint32_t SySerializer::fileVersion()
    {
        return SyFileConst::FILE_VERSION;
    }

    bool SySerializer::isValidSyFile(const std::vector<uint8_t>& header)
    {
        if (header.size() < 4)
            return false;
        return std::memcmp(header.data(), SyFileConst::MAGIC_SY, 4) == 0;
    }
} // namespace Fio