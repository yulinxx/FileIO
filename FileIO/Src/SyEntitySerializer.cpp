#include "SyEntitySerializer.h"

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
#include "Engine2D/SyEntity/SyText.h"
#include "Engine/Layer/SyLayer.h"

namespace Fio
{
    namespace
    {
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
                case sanyi::proto::ENTITY_NURBS:     return Eg::EType::SPLINE;
                case sanyi::proto::ENTITY_SMARTLINE: return Eg::EType::SMARTLINE;
                case sanyi::proto::ENTITY_TEXT:      return Eg::EType::TEXT;
                case sanyi::proto::ENTITY_BAR_CODE:  return Eg::EType::BAR_CODE;
                case sanyi::proto::ENTITY_QR_CODE:   return Eg::EType::QR_CODE;
                case sanyi::proto::ENTITY_IMAGE:     return Eg::EType::IMAGE;
                case sanyi::proto::ENTITY_GROUP:     return Eg::EType::GROUP;
                default:                             return Eg::EType::UNKNOWN;
            }
        }

        void toProtoVec2(const Ut::Vec2d& v, sanyi::proto::Vec2d* p)
        {
            p->set_x(v.x());
            p->set_y(v.y());
        }

        Ut::Vec2d fromProtoVec2(const sanyi::proto::Vec2d& p)
        {
            return Ut::Vec2d(p.x(), p.y());
        }
    }

    void SyEntitySerializer::serializeEntity(const Eg::SyEntity& entity, sanyi::proto::EntityData* out)
    {
        out->set_type(toProtoType(entity.eType));
        out->set_id(entity.id);

        if (entity.layer())
            out->set_layer_id(static_cast<uint32_t>(entity.layer()->getId()));

        toProtoVec2(entity.basePoint, out->mutable_base_point());
        out->set_closed(entity.bClosed);
        out->set_ccw(entity.bCCW);
        out->set_name(entity.name());

        switch (entity.eType)
        {
            case Eg::EType::POINT:
                out->mutable_point_data();
                break;

            case Eg::EType::LINE:
            {
                const auto* line = static_cast<const Eg::SyLine*>(&entity);
                auto* data = out->mutable_line_data();
                for (const auto& pt : line->pointRef())
                    toProtoVec2(pt, data->add_points());
                break;
            }

            case Eg::EType::POLYGON:
            {
                const auto* poly = static_cast<const Eg::SyPolygon*>(&entity);
                auto* data = out->mutable_polygon_data();
                const auto& verts = poly->vertices();
                for (const auto& v : verts)
                    toProtoVec2(v, data->add_vertices());
                data->set_sides(poly->nSides);
                data->set_circum_radius(poly->dCircumRadius);
                break;
            }

            case Eg::EType::ARC:
            {
                const auto* arc = static_cast<const Eg::SyArc*>(&entity);
                auto* data = out->mutable_arc_data();
                data->set_radius(arc->dRadius);
                data->set_start_angle(arc->dStartAngle);
                data->set_end_angle(arc->dEndAngle);
                break;
            }

            case Eg::EType::CIRCLE:
            {
                const auto* circle = static_cast<const Eg::SyCircle*>(&entity);
                out->mutable_circle_data()->set_radius(circle->dRadius);
                break;
            }

            case Eg::EType::ELLIPSE:
            {
                const auto* ell = static_cast<const Eg::SyEllipse*>(&entity);
                auto* data = out->mutable_ellipse_data();
                data->set_radius_x(ell->dRadiusX);
                data->set_radius_y(ell->dRadiusY);
                data->set_rotation(ell->dRotation);
                break;
            }

            case Eg::EType::BEZIER2:
            {
                const auto* bz = static_cast<const Eg::SyBezier2*>(&entity);
                auto* data = out->mutable_bezier2_data();
                toProtoVec2(bz->ptCtrl, data->mutable_ctrl());
                toProtoVec2(bz->ptEnd, data->mutable_end());
                break;
            }

            case Eg::EType::BEZIER:
            {
                const auto* bz = static_cast<const Eg::SyBezier*>(&entity);
                auto* data = out->mutable_bezier_data();
                toProtoVec2(bz->ptCtrl0, data->mutable_ctrl0());
                toProtoVec2(bz->ptCtrl1, data->mutable_ctrl1());
                toProtoVec2(bz->ptEnd, data->mutable_end());
                break;
            }

            case Eg::EType::SPLINE:
            {
                const auto* spl = static_cast<const Eg::SyNurbs*>(&entity);
                auto* data = out->mutable_spline_data();
                data->set_degree(spl->nDegree);
                for (double k : spl->knotRef())
                    data->add_knots(k);
                for (double w : spl->weightRef())
                    data->add_weights(w);
                for (const auto& cp : spl->controlPointRef())
                    toProtoVec2(cp, data->add_control_points());
                break;
            }

            case Eg::EType::TEXT:
            {
                const auto* txt = static_cast<const Eg::SyText*>(&entity);
                auto* data = out->mutable_text_data();
                data->set_font_name(txt->fontName());
                data->set_height(txt->dHeight);
                data->set_rotation(txt->dRotation);
                data->set_h_align(static_cast<int32_t>(txt->hAlign));
                data->set_v_align(static_cast<int32_t>(txt->vAlign));
                data->set_bold(txt->bBold);
                data->set_italic(txt->bItalic);
                data->set_text(txt->text());
                break;
            }

            case Eg::EType::BAR_CODE:
            {
                const auto* bc = static_cast<const Eg::SyBarCode*>(&entity);
                auto* data = out->mutable_barcode_data();
                data->set_data(bc->data());
                data->set_width(bc->dWidth);
                data->set_height(bc->dHeight);
                break;
            }

            case Eg::EType::QR_CODE:
            {
                const auto* qr = static_cast<const Eg::SyQRCode*>(&entity);
                auto* data = out->mutable_qrcode_data();
                data->set_data(qr->data());
                data->set_module_size(qr->dModuleSize);
                break;
            }

            case Eg::EType::IMAGE:
            {
                const auto* img = static_cast<const Eg::SyImage*>(&entity);
                auto* data = out->mutable_image_data();
                data->set_width(img->nWidth);
                data->set_height(img->nHeight);
                data->set_pixel_format(static_cast<int32_t>(img->ePixelFormat));
                data->set_pixel_data(img->pixelData(), img->pixelDataSize());
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

    std::unique_ptr<Eg::SyEntity> SyEntitySerializer::deserializeEntity(const sanyi::proto::EntityData& protoEntity)
    {
        Eg::EType eType = fromProtoType(protoEntity.type());
        std::unique_ptr<Eg::SyEntity> result;

        switch (eType)
        {
            case Eg::EType::POINT:
                result = std::make_unique<Eg::SyPoint>();
                break;

            case Eg::EType::LINE:
            {
                auto line = std::make_unique<Eg::SyLine>();
                if (protoEntity.has_line_data())
                {
                    const auto& ld = protoEntity.line_data();
                    for (int j = 0; j < ld.points_size(); ++j)
                        line->addPoint(fromProtoVec2(ld.points(j)));
                }
                result = std::move(line);
                break;
            }

            case Eg::EType::POLYGON:
            {
                auto poly = std::make_unique<Eg::SyPolygon>();
                if (protoEntity.has_polygon_data())
                {
                    const auto& pd = protoEntity.polygon_data();
                    auto& verts = poly->verticesMutable();
                    for (int j = 0; j < pd.vertices_size(); ++j)
                        verts.push_back(fromProtoVec2(pd.vertices(j)));
                    poly->nSides = pd.sides();
                    poly->dCircumRadius = pd.circum_radius();
                }
                result = std::move(poly);
                break;
            }

            case Eg::EType::ARC:
            {
                auto arc = std::make_unique<Eg::SyArc>();
                if (protoEntity.has_arc_data())
                {
                    arc->dRadius = protoEntity.arc_data().radius();
                    arc->dStartAngle = protoEntity.arc_data().start_angle();
                    arc->dEndAngle = protoEntity.arc_data().end_angle();
                }
                result = std::move(arc);
                break;
            }

            case Eg::EType::CIRCLE:
            {
                auto circle = std::make_unique<Eg::SyCircle>();
                if (protoEntity.has_circle_data())
                    circle->dRadius = protoEntity.circle_data().radius();
                result = std::move(circle);
                break;
            }

            case Eg::EType::ELLIPSE:
            {
                auto ell = std::make_unique<Eg::SyEllipse>();
                if (protoEntity.has_ellipse_data())
                {
                    ell->dRadiusX = protoEntity.ellipse_data().radius_x();
                    ell->dRadiusY = protoEntity.ellipse_data().radius_y();
                    ell->dRotation = protoEntity.ellipse_data().rotation();
                }
                result = std::move(ell);
                break;
            }

            case Eg::EType::BEZIER2:
            {
                auto bz = std::make_unique<Eg::SyBezier2>();
                if (protoEntity.has_bezier2_data())
                {
                    bz->ptCtrl = fromProtoVec2(protoEntity.bezier2_data().ctrl());
                    bz->ptEnd = fromProtoVec2(protoEntity.bezier2_data().end());
                }
                result = std::move(bz);
                break;
            }

            case Eg::EType::BEZIER:
            {
                auto bz = std::make_unique<Eg::SyBezier>();
                if (protoEntity.has_bezier_data())
                {
                    bz->ptCtrl0 = fromProtoVec2(protoEntity.bezier_data().ctrl0());
                    bz->ptCtrl1 = fromProtoVec2(protoEntity.bezier_data().ctrl1());
                    bz->ptEnd = fromProtoVec2(protoEntity.bezier_data().end());
                }
                result = std::move(bz);
                break;
            }

            case Eg::EType::SPLINE:
            {
                auto spl = std::make_unique<Eg::SyNurbs>();
                if (protoEntity.has_spline_data())
                {
                    const auto& sd = protoEntity.spline_data();
                    spl->nDegree = sd.degree();
                    for (int j = 0; j < sd.knots_size(); ++j)
                        spl->addKnot(sd.knots(j));
                    for (int j = 0; j < sd.weights_size(); ++j)
                        spl->addWeight(sd.weights(j));
                    for (int j = 0; j < sd.control_points_size(); ++j)
                        spl->addControlPoint(fromProtoVec2(sd.control_points(j)));
                }
                result = std::move(spl);
                break;
            }

            case Eg::EType::TEXT:
            {
                auto txt = std::make_unique<Eg::SyText>();
                if (protoEntity.has_text_data())
                {
                    const auto& td = protoEntity.text_data();
                    txt->setFontName(td.font_name().c_str());
                    txt->dHeight = td.height();
                    txt->dRotation = td.rotation();
                    txt->hAlign = static_cast<Eg::SyTextHAlign>(td.h_align());
                    txt->vAlign = static_cast<Eg::SyTextVAlign>(td.v_align());
                    txt->bBold = td.bold();
                    txt->bItalic = td.italic();
                    txt->setText(td.text().c_str());
                }
                result = std::move(txt);
                break;
            }

            case Eg::EType::BAR_CODE:
            {
                auto bc = std::make_unique<Eg::SyBarCode>();
                if (protoEntity.has_barcode_data())
                {
                    bc->setData(protoEntity.barcode_data().data().c_str());
                    bc->dWidth = protoEntity.barcode_data().width();
                    bc->dHeight = protoEntity.barcode_data().height();
                }
                result = std::move(bc);
                break;
            }

            case Eg::EType::QR_CODE:
            {
                auto qr = std::make_unique<Eg::SyQRCode>();
                if (protoEntity.has_qrcode_data())
                {
                    qr->setData(protoEntity.qrcode_data().data().c_str());
                    qr->dModuleSize = protoEntity.qrcode_data().module_size();
                }
                result = std::move(qr);
                break;
            }

            case Eg::EType::IMAGE:
            {
                auto img = std::make_unique<Eg::SyImage>();
                if (protoEntity.has_image_data())
                {
                    const auto& id = protoEntity.image_data();
                    img->nWidth = id.width();
                    img->nHeight = id.height();
                    img->ePixelFormat = static_cast<Eg::SyPixelFormat>(id.pixel_format());
                    const auto& px = id.pixel_data();
                    img->setPixelData(reinterpret_cast<const unsigned char*>(px.data()), px.size());
                    img->topLeft = fromProtoVec2(id.top_left());
                    img->topRight = fromProtoVec2(id.top_right());
                    img->bottomLeft = fromProtoVec2(id.bottom_left());
                    img->bottomRight = fromProtoVec2(id.bottom_right());
                }
                result = std::move(img);
                break;
            }

            default:
                return nullptr;
        }

        // 璁剧疆閫氱敤灞炴€э紙id / basePoint / bClosed / bCCW锛夛紝浣?deserializeEntity 鑷寘鍚?
        if (result)
        {
            result->id = static_cast<Eg::EntityId>(protoEntity.id());
            result->basePoint = fromProtoVec2(protoEntity.base_point());
            result->bClosed = protoEntity.closed();
            result->bCCW = protoEntity.ccw();
            result->setName(protoEntity.name().c_str());
        }

        return result;
    }
}