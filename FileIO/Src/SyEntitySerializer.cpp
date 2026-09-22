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
#include "Engine3D/SyEntity/SyMeshEntity.h"
#include "Engine/Layer/SyLayer.h"

namespace Fio
{
    namespace
    {
        sanyi::proto::EntityType toProtoType(Eg::EType type)
        {
            switch (type)
            {
            case Eg::EType::POINT:
                return sanyi::proto::ENTITY_POINT;
            case Eg::EType::LINE:
                return sanyi::proto::ENTITY_LINE;
            case Eg::EType::POLYGON:
                return sanyi::proto::ENTITY_POLYGON;
            case Eg::EType::ARC:
                return sanyi::proto::ENTITY_ARC;
            case Eg::EType::CIRCLE:
                return sanyi::proto::ENTITY_CIRCLE;
            case Eg::EType::ELLIPSE:
                return sanyi::proto::ENTITY_ELLIPSE;
            case Eg::EType::BEZIER2:
                return sanyi::proto::ENTITY_BEZIER2;
            case Eg::EType::BEZIER:
                return sanyi::proto::ENTITY_BEZIER;
            case Eg::EType::SPLINE:
                return sanyi::proto::ENTITY_SPLINE;
            case Eg::EType::NURBS:
                return sanyi::proto::ENTITY_NURBS;
            case Eg::EType::SMARTLINE:
                return sanyi::proto::ENTITY_SMARTLINE;
            case Eg::EType::TEXT:
                return sanyi::proto::ENTITY_TEXT;
            case Eg::EType::BAR_CODE:
                return sanyi::proto::ENTITY_BAR_CODE;
            case Eg::EType::QR_CODE:
                return sanyi::proto::ENTITY_QR_CODE;
            case Eg::EType::IMAGE:
                return sanyi::proto::ENTITY_IMAGE;
            case Eg::EType::MESH:
                return sanyi::proto::ENTITY_MESH;
            case Eg::EType::GROUP:
                return sanyi::proto::ENTITY_GROUP;
            default:
                return sanyi::proto::ENTITY_UNKNOWN;
            }
        }

        Eg::EType fromProtoType(sanyi::proto::EntityType type)
        {
            switch (type)
            {
            case sanyi::proto::ENTITY_POINT:
                return Eg::EType::POINT;
            case sanyi::proto::ENTITY_LINE:
                return Eg::EType::LINE;
            case sanyi::proto::ENTITY_POLYGON:
                return Eg::EType::POLYGON;
            case sanyi::proto::ENTITY_ARC:
                return Eg::EType::ARC;
            case sanyi::proto::ENTITY_CIRCLE:
                return Eg::EType::CIRCLE;
            case sanyi::proto::ENTITY_ELLIPSE:
                return Eg::EType::ELLIPSE;
            case sanyi::proto::ENTITY_BEZIER2:
                return Eg::EType::BEZIER2;
            case sanyi::proto::ENTITY_BEZIER:
                return Eg::EType::BEZIER;
            case sanyi::proto::ENTITY_SPLINE:
                return Eg::EType::SPLINE;
            case sanyi::proto::ENTITY_NURBS:
                return Eg::EType::NURBS;
            case sanyi::proto::ENTITY_SMARTLINE:
                return Eg::EType::SMARTLINE;
            case sanyi::proto::ENTITY_TEXT:
                return Eg::EType::TEXT;
            case sanyi::proto::ENTITY_BAR_CODE:
                return Eg::EType::BAR_CODE;
            case sanyi::proto::ENTITY_QR_CODE:
                return Eg::EType::QR_CODE;
            case sanyi::proto::ENTITY_IMAGE:
                return Eg::EType::IMAGE;
            case sanyi::proto::ENTITY_MESH:
                return Eg::EType::MESH;
            case sanyi::proto::ENTITY_GROUP:
                return Eg::EType::GROUP;
            default:
                return Eg::EType::UNKNOWN;
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

        void toProtoVec3(const Ut::Vec3f& v, sanyi::proto::Vec3fData* p)
        {
            p->set_x(v[0]);
            p->set_y(v[1]);
            p->set_z(v[2]);
        }

        Ut::Vec3f fromProtoVec3(const sanyi::proto::Vec3fData& p)
        {
            return Ut::Vec3f(p.x(), p.y(), p.z());
        }
    }  // namespace

    void SyEntitySerializer::serializeEntity(const Eg::SyEntity& entity, sanyi::proto::EntityData* out)
    {
        out->set_type(toProtoType(entity.eType));
        out->set_id(entity.id);

        if (entity.layer())
        {
            out->set_layer_id(static_cast<uint32_t>(entity.layer()->getId()));
        }

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
            const auto& pts = line->pointRef();
            data->mutable_points()->Reserve(static_cast<int>(pts.size()));
            for (const auto& pt : pts)
            {
                toProtoVec2(pt, data->add_points());
            }
            break;
        }

        case Eg::EType::POLYGON:
        {
            const auto* poly = static_cast<const Eg::SyPolygon*>(&entity);
            auto* data = out->mutable_polygon_data();
            const auto& verts = poly->vertices();
            data->mutable_vertices()->Reserve(static_cast<int>(verts.size()));
            for (const auto& v : verts)
            {
                toProtoVec2(v, data->add_vertices());
            }
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
            data->mutable_knots()->Reserve(static_cast<int>(spl->knotRef().size()));
            data->mutable_weights()->Reserve(static_cast<int>(spl->weightRef().size()));
            data->mutable_control_points()->Reserve(static_cast<int>(spl->controlPointRef().size()));
            for (double k : spl->knotRef())
            {
                data->add_knots(k);
            }
            for (double w : spl->weightRef())
            {
                data->add_weights(w);
            }
            for (const auto& cp : spl->controlPointRef())
            {
                toProtoVec2(cp, data->add_control_points());
            }
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
            // 优化: 使用 string_view 风格设置像素数据，避免额外复制
            // protobuf-lite 的 set_pixel_data(const char*, size_t) 直接使用指针+长度
            if (img->pixelData() && img->pixelDataSize() > 0)
            {
                data->set_pixel_data(reinterpret_cast<const char*>(img->pixelData()), img->pixelDataSize());
            }
            toProtoVec2(img->topLeft, data->mutable_top_left());
            toProtoVec2(img->topRight, data->mutable_top_right());
            toProtoVec2(img->bottomLeft, data->mutable_bottom_left());
            toProtoVec2(img->bottomRight, data->mutable_bottom_right());
            break;
        }

        case Eg::EType::MESH:
        {
            // 3D 网格图元序列化：顶点、法线、材质属性
            const auto* mesh = static_cast<const Eg::SyMeshEntity*>(&entity);
            auto* data = out->mutable_mesh_data();
            data->set_name(mesh->name());

            data->mutable_vertices()->Reserve(static_cast<int>(mesh->vertices.size()));
            data->mutable_normals()->Reserve(static_cast<int>(mesh->normals.size()));

            // 顶点数据
            for (const auto& v : mesh->vertices)
            {
                toProtoVec3(v, data->add_vertices());
            }

            // 法线数据
            for (const auto& n : mesh->normals)
            {
                toProtoVec3(n, data->add_normals());
            }

            // 材质属性
            data->set_ambient_r(mesh->ambientColor[0]);
            data->set_ambient_g(mesh->ambientColor[1]);
            data->set_ambient_b(mesh->ambientColor[2]);
            data->set_diffuse_r(mesh->diffuseColor[0]);
            data->set_diffuse_g(mesh->diffuseColor[1]);
            data->set_diffuse_b(mesh->diffuseColor[2]);
            data->set_specular_r(mesh->specularColor[0]);
            data->set_specular_g(mesh->specularColor[1]);
            data->set_specular_b(mesh->specularColor[2]);
            data->set_shininess(mesh->shininess);
            break;
        }

        default:
            break;
        }

        // 写入包围盒缓存（加载加速用）。
        // 两个跳过条件：
        //   1. 类型特有数据未写入 —— SMARTLINE/NURBS 等未在上方 switch 中持久化，
        //      几何已丢失，写包围盒无意义；
        //   2. MESH 是 3D 图元，2D 包围盒不参与 2D 空间索引。
        if (out->type_specific_case() != sanyi::proto::EntityData::TYPE_SPECIFIC_NOT_SET &&
            entity.eType != Eg::EType::MESH)
        {
            const Ut::BBox2d box = entity.getBbox();
            if (box.isValid())
            {
                auto* bboxData = out->mutable_cached_bbox();
                toProtoVec2(box.minPt, bboxData->mutable_min_pt());
                toProtoVec2(box.maxPt, bboxData->mutable_max_pt());
            }
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
                const int pointCount = ld.points_size();
                if (pointCount > 0)
                {
                    line->reservePoints(static_cast<size_t>(pointCount));
                    for (int j = 0; j < pointCount; ++j)
                    {
                        line->addPoint(fromProtoVec2(ld.points(j)));
                    }
                }
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
                const int vertCount = pd.vertices_size();
                if (vertCount > 0)
                {
                    verts.reserve(static_cast<size_t>(vertCount));
                    for (int j = 0; j < vertCount; ++j)
                    {
                        verts.push_back(fromProtoVec2(pd.vertices(j)));
                    }
                }
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
            {
                circle->dRadius = protoEntity.circle_data().radius();
            }
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

                const int knotCount = sd.knots_size();
                const int weightCount = sd.weights_size();
                const int cpCount = sd.control_points_size();

                if (knotCount > 0)
                {
                    spl->reserveKnots(static_cast<size_t>(knotCount));
                    for (int j = 0; j < knotCount; ++j)
                    {
                        spl->addKnot(sd.knots(j));
                    }
                }
                if (weightCount > 0)
                {
                    spl->reserveWeights(static_cast<size_t>(weightCount));
                    for (int j = 0; j < weightCount; ++j)
                    {
                        spl->addWeight(sd.weights(j));
                    }
                }
                if (cpCount > 0)
                {
                    spl->reserveControlPoints(static_cast<size_t>(cpCount));
                    for (int j = 0; j < cpCount; ++j)
                    {
                        spl->addControlPoint(fromProtoVec2(sd.control_points(j)));
                    }
                }
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

        case Eg::EType::MESH:
        {
            // 3D 网格图元反序列化：顶点、法线、材质属性
            auto mesh = std::make_unique<Eg::SyMeshEntity>();
            if (protoEntity.has_mesh_data())
            {
                const auto& md = protoEntity.mesh_data();

                // 顶点数据
                mesh->vertices.reserve(md.vertices_size());
                for (int j = 0; j < md.vertices_size(); ++j)
                {
                    mesh->vertices.push_back(fromProtoVec3(md.vertices(j)));
                }

                // 法线数据
                mesh->normals.reserve(md.normals_size());
                for (int j = 0; j < md.normals_size(); ++j)
                {
                    mesh->normals.push_back(fromProtoVec3(md.normals(j)));
                }

                // 材质属性
                mesh->ambientColor = Ut::Vec3f(md.ambient_r(), md.ambient_g(), md.ambient_b());
                mesh->diffuseColor = Ut::Vec3f(md.diffuse_r(), md.diffuse_g(), md.diffuse_b());
                mesh->specularColor = Ut::Vec3f(md.specular_r(), md.specular_g(), md.specular_b());
                mesh->shininess = md.shininess();
            }
            result = std::move(mesh);
            break;
        }

        default:
            return nullptr;
        }

        // 设置通用属性（id / basePoint / bClosed / bCCW），deserializeEntity 自包含
        if (result)
        {
            result->id = static_cast<Eg::EntityId>(protoEntity.id());
            result->basePoint = fromProtoVec2(protoEntity.base_point());
            result->bClosed = protoEntity.closed();
            result->bCCW = protoEntity.ccw();
            result->setName(protoEntity.name().c_str());

            // 回填包围盒缓存，跳过 computeBBox()。
            // 必须放在最后：上方各类型分支构造几何时会 setModified() 失效缓存，
            // 先回填会被冲掉。字段缺失（旧文件）时走现算路径。
            if (protoEntity.has_cached_bbox())
            {
                const auto& bboxData = protoEntity.cached_bbox();
                result->seedBBoxCache(Ut::BBox2d(fromProtoVec2(bboxData.min_pt()), fromProtoVec2(bboxData.max_pt())));
            }
        }

        return result;
    }
}  // namespace Fio