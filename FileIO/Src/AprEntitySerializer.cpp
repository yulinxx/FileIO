#include "AprEntitySerializer.h"

#include "Engine2D/AprEntity/AprArc.h"
#include "Engine2D/AprEntity/AprBarCode.h"
#include "Engine2D/AprEntity/AprBezier.h"
#include "Engine2D/AprEntity/AprBezier2.h"
#include "Engine2D/AprEntity/AprCircle.h"
#include "Engine2D/AprEntity/AprEllipse.h"
#include "Engine2D/AprEntity/AprImage.h"
#include "Engine2D/AprEntity/AprLine.h"
#include "Engine2D/AprEntity/AprNurbs.h"
#include "Engine2D/AprEntity/AprPoint.h"
#include "Engine2D/AprEntity/AprPolygon.h"
#include "Engine2D/AprEntity/AprQRCode.h"
#include "Engine2D/AprEntity/AprSmartLine.h"
#include "Engine2D/AprEntity/AprText.h"
#include "Engine3D/AprEntity/AprMeshEntity.h"
#include "Engine/Layer/AprLayer.h"
#include "Engine/AprEntity/AprEntity.h"
#include "Engine2D/AprEntity/AprFillRegion.h"
#include "Log/AprLogger.h"
#include "Ut/Color.h"
#include "ColorArgbUtils.h"

namespace Fio
{
    namespace
    {
        apr::proto::EntityType toProtoType(Eg::EType type)
        {
            switch (type)
            {
            case Eg::EType::POINT:
                return apr::proto::ENTITY_POINT;
            case Eg::EType::LINE:
                return apr::proto::ENTITY_LINE;
            case Eg::EType::POLYGON:
                return apr::proto::ENTITY_POLYGON;
            case Eg::EType::ARC:
                return apr::proto::ENTITY_ARC;
            case Eg::EType::CIRCLE:
                return apr::proto::ENTITY_CIRCLE;
            case Eg::EType::ELLIPSE:
                return apr::proto::ENTITY_ELLIPSE;
            case Eg::EType::BEZIER2:
                return apr::proto::ENTITY_BEZIER2;
            case Eg::EType::BEZIER:
                return apr::proto::ENTITY_BEZIER;
            case Eg::EType::SPLINE:
                return apr::proto::ENTITY_SPLINE;
            case Eg::EType::NURBS:
                return apr::proto::ENTITY_NURBS;
            case Eg::EType::SMARTLINE:
                return apr::proto::ENTITY_SMARTLINE;
            case Eg::EType::TEXT:
                return apr::proto::ENTITY_TEXT;
            case Eg::EType::BAR_CODE:
                return apr::proto::ENTITY_BAR_CODE;
            case Eg::EType::QR_CODE:
                return apr::proto::ENTITY_QR_CODE;
            case Eg::EType::IMAGE:
                return apr::proto::ENTITY_IMAGE;
            case Eg::EType::MESH:
                return apr::proto::ENTITY_MESH;
            case Eg::EType::GROUP:
                return apr::proto::ENTITY_GROUP;
            case Eg::EType::FILL_REGION:
                return apr::proto::ENTITY_FILL_REGION;
            default:
                return apr::proto::ENTITY_UNKNOWN;
            }
        }

        Eg::EType fromProtoType(apr::proto::EntityType type)
        {
            switch (type)
            {
            case apr::proto::ENTITY_POINT:
                return Eg::EType::POINT;
            case apr::proto::ENTITY_LINE:
                return Eg::EType::LINE;
            case apr::proto::ENTITY_POLYGON:
                return Eg::EType::POLYGON;
            case apr::proto::ENTITY_ARC:
                return Eg::EType::ARC;
            case apr::proto::ENTITY_CIRCLE:
                return Eg::EType::CIRCLE;
            case apr::proto::ENTITY_ELLIPSE:
                return Eg::EType::ELLIPSE;
            case apr::proto::ENTITY_BEZIER2:
                return Eg::EType::BEZIER2;
            case apr::proto::ENTITY_BEZIER:
                return Eg::EType::BEZIER;
            case apr::proto::ENTITY_SPLINE:
                return Eg::EType::SPLINE;
            case apr::proto::ENTITY_NURBS:
                return Eg::EType::NURBS;
            case apr::proto::ENTITY_SMARTLINE:
                return Eg::EType::SMARTLINE;
            case apr::proto::ENTITY_TEXT:
                return Eg::EType::TEXT;
            case apr::proto::ENTITY_BAR_CODE:
                return Eg::EType::BAR_CODE;
            case apr::proto::ENTITY_QR_CODE:
                return Eg::EType::QR_CODE;
            case apr::proto::ENTITY_IMAGE:
                return Eg::EType::IMAGE;
            case apr::proto::ENTITY_MESH:
                return Eg::EType::MESH;
            case apr::proto::ENTITY_GROUP:
                return Eg::EType::GROUP;
            case apr::proto::ENTITY_FILL_REGION:
                return Eg::EType::FILL_REGION;
            default:
                return Eg::EType::UNKNOWN;
            }
        }

        void toProtoVec2(const Ut::Vec2d& v, apr::proto::Vec2d* p)
        {
            p->set_x(v.x());
            p->set_y(v.y());
        }

        Ut::Vec2d fromProtoVec2(const apr::proto::Vec2d& p)
        {
            return Ut::Vec2d(p.x(), p.y());
        }

        void toProtoVec3(const Ut::Vec3f& v, apr::proto::Vec3fData* p)
        {
            p->set_x(v[0]);
            p->set_y(v[1]);
            p->set_z(v[2]);
        }

        Ut::Vec3f fromProtoVec3(const apr::proto::Vec3fData& p)
        {
            return Ut::Vec3f(p.x(), p.y(), p.z());
        }

        // SMARTLINE 的段本身也可能是 SMARTLINE（嵌套复合曲线），文件里因此可能出现
        // 任意深度的递归结构。读写都设上限，避免恶意或损坏文件在递归时耗尽调用栈。
        constexpr int kMaxEntityNestingDepth = 32;
        thread_local int t_entityNestingDepth = 0;

        /// 递归深度守卫：构造时 +1，析构时 -1
        struct EntityNestingGuard
        {
            EntityNestingGuard() { ++t_entityNestingDepth; }
            ~EntityNestingGuard() { --t_entityNestingDepth; }
        };

        bool entityNestingTooDeep()
        {
            return t_entityNestingDepth > kMaxEntityNestingDepth;
        }

        // 颜色的 ARGB 编解码放在 Internal/ColorArgbUtils.h 中统一提供：
        // unity build 会把多个 cpp 合并进同一翻译单元，各写一份会重复定义。
    }  // namespace

    void AprEntitySerializer::serializeEntity(const Eg::AprEntity& entity, apr::proto::EntityData* out)
    {
        EntityNestingGuard nestingGuard;
        if (entityNestingTooDeep())
        {
            APR_ERRORF("[AprEntitySerializer] Entity nesting depth exceeds %d, nested entity skipped", kMaxEntityNestingDepth);
            return;
        }

        out->set_type(toProtoType(entity.eType));
        out->set_id(entity.id);

        if (entity.layer())
        {
            out->set_layer_id(static_cast<uint32_t>(entity.layer()->getId()));
        }

        // MESH 不是 AprEntity2D 的子类，直接 static_cast 是未定义行为；
        // 2D 专有字段只对非 MESH 图元读写。
        if (entity.eType != Eg::EType::MESH)
        {
            const auto& entity2d = static_cast<const Eg::AprEntity2D&>(entity);
            toProtoVec2(entity2d.basePoint, out->mutable_base_point());
            out->set_closed(entity2d.bClosed);
            out->set_ccw(entity2d.bCCW);
        }
        out->set_name(entity.name());
        out->set_visible(entity.visible());
        out->set_locked(entity.locked());
        // 覆盖色（含 alpha）：仅当图元本来就有覆盖色时写 optional 字段；
        // 缺失表示跟随图层颜色，与旧文件语义一致
        if (const auto override = entity.overrideColor(); override.has_value())
        {
            out->set_override_argb(detail::colorToArgb(*override));
        }

        switch (entity.eType)
        {
        case Eg::EType::POINT:
            out->mutable_point_data();
            break;

        case Eg::EType::LINE:
        {
            const auto* line = static_cast<const Eg::AprLine*>(&entity);
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
            const auto* poly = static_cast<const Eg::AprPolygon*>(&entity);
            auto* data = out->mutable_polygon_data();
            const auto& verts = poly->vertices();
            data->mutable_vertices()->Reserve(static_cast<int>(verts.size()));
            for (const auto& v : verts)
            {
                toProtoVec2(v, data->add_vertices());
            }
            data->set_sides(poly->nSides);
            data->set_circum_radius(poly->dCircumRadius);
            if (poly->hasBulges())
            {
                const auto& bulges = poly->bulges();
                data->mutable_bulges()->Reserve(static_cast<int>(bulges.size()));
                for (double bulge : bulges)
                {
                    data->add_bulges(bulge);
                }
            }
            break;
        }

        case Eg::EType::ARC:
        {
            const auto* arc = static_cast<const Eg::AprArc*>(&entity);
            auto* data = out->mutable_arc_data();
            data->set_radius(arc->dRadius);
            data->set_start_angle(arc->dStartAngle);
            data->set_end_angle(arc->dEndAngle);
            break;
        }

        case Eg::EType::CIRCLE:
        {
            const auto* circle = static_cast<const Eg::AprCircle*>(&entity);
            out->mutable_circle_data()->set_radius(circle->dRadius);
            break;
        }

        case Eg::EType::ELLIPSE:
        {
            const auto* ell = static_cast<const Eg::AprEllipse*>(&entity);
            auto* data = out->mutable_ellipse_data();
            data->set_radius_x(ell->dRadiusX);
            data->set_radius_y(ell->dRadiusY);
            data->set_rotation(ell->dRotation);
            break;
        }

        case Eg::EType::BEZIER2:
        {
            const auto* bz = static_cast<const Eg::AprBezier2*>(&entity);
            auto* data = out->mutable_bezier2_data();
            toProtoVec2(bz->ptCtrl, data->mutable_ctrl());
            toProtoVec2(bz->ptEnd, data->mutable_end());
            break;
        }

        case Eg::EType::BEZIER:
        {
            const auto* bz = static_cast<const Eg::AprBezier*>(&entity);
            auto* data = out->mutable_bezier_data();
            toProtoVec2(bz->ptCtrl0, data->mutable_ctrl0());
            toProtoVec2(bz->ptCtrl1, data->mutable_ctrl1());
            toProtoVec2(bz->ptEnd, data->mutable_end());
            break;
        }

        case Eg::EType::SPLINE:
        {
            const auto* spl = static_cast<const Eg::AprNurbs*>(&entity);
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

        case Eg::EType::NURBS:
        {
            // legacy 兼容：AprNurbs 的构造函数把 eType 设为 SPLINE，正常 UI 不会产出
            // NURBS 类型。这里保留 NURBS 分支，用于读取早期/外部写出的 ENTITY_NURBS。
            // 数据结构与 SplineData 相同，但写入 nurbs_data 以保持类型一致。
            const auto* spl = static_cast<const Eg::AprNurbs*>(&entity);
            auto* data = out->mutable_nurbs_data();
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

        case Eg::EType::SMARTLINE:
        {
            // 复合曲线：逐段递归序列化。段坐标独立保存，
            // 读取时不会按“连接上一段”重新平移。
            const auto* smart = static_cast<const Eg::AprSmartLine*>(&entity);
            auto* data = out->mutable_smartline_data();
            data->mutable_segments()->Reserve(static_cast<int>(smart->segmentCount()));
            for (size_t i = 0; i < smart->segmentCount(); ++i)
            {
                const Eg::AprEntity* segment = smart->segment(i);
                if (segment)
                {
                    serializeEntity(*segment, data->add_segments());
                }
            }
            break;
        }

        case Eg::EType::TEXT:
        {
            const auto* txt = static_cast<const Eg::AprText*>(&entity);
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
            const auto* bc = static_cast<const Eg::AprBarCode*>(&entity);
            auto* data = out->mutable_barcode_data();
            data->set_data(bc->data());
            data->set_width(bc->dWidth);
            data->set_height(bc->dHeight);
            break;
        }

        case Eg::EType::QR_CODE:
        {
            const auto* qr = static_cast<const Eg::AprQRCode*>(&entity);
            auto* data = out->mutable_qrcode_data();
            data->set_data(qr->data());
            data->set_module_size(qr->dModuleSize);
            data->set_ecc(static_cast<int32_t>(qr->ecc()));
            data->set_border_size(qr->borderSize());
            break;
        }

        case Eg::EType::IMAGE:
        {
            const auto* img = static_cast<const Eg::AprImage*>(&entity);
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

        case Eg::EType::FILL_REGION:
        {
            // 跨 DLL 只使用 POD 访问器读取三角形顶点流与来源 ID
            const auto* fill = static_cast<const Eg::AprFillRegion*>(&entity);
            auto* data = out->mutable_fill_region_data();
            const size_t vertexCount = fill->vertexCount();
            data->mutable_vertices()->Reserve(static_cast<int>(vertexCount));
            for (size_t i = 0; i < vertexCount; ++i)
            {
                toProtoVec2(fill->vertices()[i], data->add_vertices());
            }
            const size_t sourceCount = fill->sourceCount();
            data->mutable_source_ids()->Reserve(static_cast<int>(sourceCount));
            for (size_t i = 0; i < sourceCount; ++i)
            {
                data->add_source_ids(static_cast<uint64_t>(fill->sources()[i]));
            }
            break;
        }

        case Eg::EType::MESH:
        {
            // 3D 网格图元序列化：顶点、法线、材质属性
            const auto* mesh = static_cast<const Eg::AprMeshEntity*>(&entity);
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
        //   1. 类型特有数据未写入 —— 未知/不支持的类型无法还原几何，写包围盒无意义；
        //   2. MESH 是 3D 图元，2D 包围盒不参与 2D 空间索引。
        if (out->type_specific_case() != apr::proto::EntityData::TYPE_SPECIFIC_NOT_SET &&
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

    std::unique_ptr<Eg::AprEntity> AprEntitySerializer::deserializeEntity(const apr::proto::EntityData& protoEntity)
    {
        EntityNestingGuard nestingGuard;
        if (entityNestingTooDeep())
        {
            APR_ERRORF("[AprEntitySerializer] Entity nesting depth exceeds %d, entity dropped", kMaxEntityNestingDepth);
            return nullptr;
        }

        Eg::EType eType = fromProtoType(protoEntity.type());
        std::unique_ptr<Eg::AprEntity> result;

        switch (eType)
        {
        case Eg::EType::POINT:
            result = std::make_unique<Eg::AprPoint>();
            break;

        case Eg::EType::LINE:
        {
            auto line = std::make_unique<Eg::AprLine>();
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
            auto poly = std::make_unique<Eg::AprPolygon>();
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
                if (pd.bulges_size() > 0)
                {
                    // verticesMutable() 会清空 bulge，必须在顶点写完之后再挂圆弧参数。
                    std::vector<double> bulges(pd.bulges().begin(), pd.bulges().end());
                    poly->setBulges(std::move(bulges));
                }
            }
            result = std::move(poly);
            break;
        }

        case Eg::EType::ARC:
        {
            auto arc = std::make_unique<Eg::AprArc>();
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
            auto circle = std::make_unique<Eg::AprCircle>();
            if (protoEntity.has_circle_data())
            {
                circle->dRadius = protoEntity.circle_data().radius();
            }
            result = std::move(circle);
            break;
        }

        case Eg::EType::ELLIPSE:
        {
            auto ell = std::make_unique<Eg::AprEllipse>();
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
            auto bz = std::make_unique<Eg::AprBezier2>();
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
            auto bz = std::make_unique<Eg::AprBezier>();
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
            auto spl = std::make_unique<Eg::AprNurbs>();
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

        case Eg::EType::NURBS:
        {
            // legacy 兼容：早期文件可能写出 ENTITY_NURBS + nurbs_data。
            // AprNurbs 构造后 eType 会被归一为 SPLINE，重新保存时自然写成 SplineData。
            auto spl = std::make_unique<Eg::AprNurbs>();
            if (protoEntity.has_nurbs_data())
            {
                const auto& nd = protoEntity.nurbs_data();
                spl->nDegree = nd.degree();

                const int knotCount = nd.knots_size();
                const int weightCount = nd.weights_size();
                const int cpCount = nd.control_points_size();

                if (knotCount > 0)
                {
                    spl->reserveKnots(static_cast<size_t>(knotCount));
                    for (int j = 0; j < knotCount; ++j)
                    {
                        spl->addKnot(nd.knots(j));
                    }
                }
                if (weightCount > 0)
                {
                    spl->reserveWeights(static_cast<size_t>(weightCount));
                    for (int j = 0; j < weightCount; ++j)
                    {
                        spl->addWeight(nd.weights(j));
                    }
                }
                if (cpCount > 0)
                {
                    spl->reserveControlPoints(static_cast<size_t>(cpCount));
                    for (int j = 0; j < cpCount; ++j)
                    {
                        spl->addControlPoint(fromProtoVec2(nd.control_points(j)));
                    }
                }
            }
            result = std::move(spl);
            break;
        }

        case Eg::EType::SMARTLINE:
        {
            auto smart = std::make_unique<Eg::AprSmartLine>();
            if (protoEntity.has_smartline_data())
            {
                const auto& sd = protoEntity.smartline_data();
                for (int j = 0; j < sd.segments_size(); ++j)
                {
                    auto segment = deserializeEntity(sd.segments(j));
                    if (segment)
                    {
                        // connectToPrevious=false：段坐标已随文件独立保存，
                        // 若按“连接上一段”追加会把后段再次平移到前段终点。
                        smart->addSegmentUnique(std::move(segment), false);
                    }
                }
            }
            result = std::move(smart);
            break;
        }

        case Eg::EType::TEXT:
        {
            auto txt = std::make_unique<Eg::AprText>();
            if (protoEntity.has_text_data())
            {
                const auto& td = protoEntity.text_data();
                txt->setFontName(td.font_name().c_str());
                txt->dHeight = td.height();
                txt->dRotation = td.rotation();
                txt->hAlign = static_cast<Eg::AprTextHAlign>(td.h_align());
                txt->vAlign = static_cast<Eg::AprTextVAlign>(td.v_align());
                txt->bBold = td.bold();
                txt->bItalic = td.italic();
                txt->setText(td.text().c_str());
            }
            result = std::move(txt);
            break;
        }

        case Eg::EType::BAR_CODE:
        {
            auto bc = std::make_unique<Eg::AprBarCode>();
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
            auto qr = std::make_unique<Eg::AprQRCode>();
            if (protoEntity.has_qrcode_data())
            {
                const auto& qrd = protoEntity.qrcode_data();
                qr->setData(qrd.data().c_str());
                qr->dModuleSize = qrd.module_size();
                // 旧文件无 ecc/border 字段：回落实体默认（M / 4），保持向后兼容
                if (qrd.has_ecc())
                {
                    const int ecc = qrd.ecc();
                    qr->setEcc(ecc >= 0 && ecc <= 3 ? static_cast<Eg::QrEcc>(ecc) : Eg::QrEcc::Medium);
                }
                if (qrd.has_border_size())
                {
                    qr->setBorderSize(qrd.border_size());
                }
            }
            result = std::move(qr);
            break;
        }

        case Eg::EType::IMAGE:
        {
            auto img = std::make_unique<Eg::AprImage>();
            if (protoEntity.has_image_data())
            {
                const auto& id = protoEntity.image_data();
                img->nWidth = id.width();
                img->nHeight = id.height();
                img->ePixelFormat = static_cast<Eg::AprPixelFormat>(id.pixel_format());
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

        case Eg::EType::FILL_REGION:
        {
            auto fill = std::make_unique<Eg::AprFillRegion>();
            if (protoEntity.has_fill_region_data())
            {
                const auto& fd = protoEntity.fill_region_data();
                const int vertexSize = fd.vertices_size();
                if (vertexSize > 0)
                {
                    std::vector<Ut::Vec2d> pts;
                    pts.reserve(static_cast<size_t>(vertexSize));
                    for (int j = 0; j < vertexSize; ++j)
                    {
                        pts.push_back(fromProtoVec2(fd.vertices(j)));
                    }
                    // 跨 DLL 通过 POD 访问器设置顶点流
                    fill->setVertices(pts.data(), pts.size());
                }
                const int sourceSize = fd.source_ids_size();
                if (sourceSize > 0)
                {
                    std::vector<Eg::EntityId> ids;
                    ids.reserve(static_cast<size_t>(sourceSize));
                    for (int j = 0; j < sourceSize; ++j)
                    {
                        ids.push_back(static_cast<Eg::EntityId>(fd.source_ids(j)));
                    }
                    fill->setSources(ids.data(), ids.size());
                }
            }
            result = std::move(fill);
            break;
        }

        case Eg::EType::MESH:
        {
            // 3D 网格图元反序列化：顶点、法线、材质属性
            auto mesh = std::make_unique<Eg::AprMeshEntity>();
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
            // MESH 不是 AprEntity2D：对 3D 网格做强转写 basePoint/bClosed/bCCW
            // 会越界覆盖 AprMeshEntity 自己的字段（未定义行为）。
            if (result->eType != Eg::EType::MESH)
            {
                auto& entity2d = static_cast<Eg::AprEntity2D&>(*result);
                entity2d.basePoint = fromProtoVec2(protoEntity.base_point());
                entity2d.bClosed = protoEntity.closed();
                entity2d.bCCW = protoEntity.ccw();
            }
            result->setName(protoEntity.name().c_str());
            // 旧文件没有这两个字段：保持 AprEntity 构造默认值（visible=true / locked=false）
            if (protoEntity.has_visible())
            {
                result->setVisible(protoEntity.visible());
            }
            if (protoEntity.has_locked())
            {
                result->setLocked(protoEntity.locked());
            }
            // 覆盖色：旧文件没有该字段 → 保持跟随图层色；
            // 存在时恢复覆盖色（含 alpha，历史零值按不透明兜底）
            if (protoEntity.has_override_argb())
            {
                result->setOverrideColor(detail::argbToColor(protoEntity.override_argb()));
            }

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