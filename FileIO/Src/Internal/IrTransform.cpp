#include "IrTransform.h"

#include "Log/SyLogger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace Fio
{
    namespace
    {
        // 不依赖 M_PI：M_PI 在 MSVC 上要求先定义 _USE_MATH_DEFINES，容易受包含顺序影响
        constexpr double kTwoPi = 6.283185307179586476925286766559;

        bool finite(double v)
        {
            return std::isfinite(v);
        }

        /// 由「点在新坐标系下相对新圆心的方位角」反推圆弧端角。
        /// 相似变换（含镜像）下这样算是精确的，且天然处理了镜像导致的方向翻转。
        double angleOfTransformedPoint(double cx, double cy, double r, double angle, const IrXform& xf)
        {
            double px = cx + r * std::cos(angle);
            double py = cy + r * std::sin(angle);
            xf.applyPoint(px, py);
            double ncx = cx;
            double ncy = cy;
            xf.applyPoint(ncx, ncy);
            return std::atan2(py - ncy, px - ncx);
        }
    }  // namespace

    // ===================== IrXform =====================

    IrXform IrXform::translate(double dx, double dy)
    {
        IrXform x;
        x.e = dx;
        x.f = dy;
        return x;
    }

    IrXform IrXform::scale(double sx, double sy)
    {
        IrXform x;
        x.a = sx;
        x.d = sy;
        return x;
    }

    IrXform IrXform::rotate(double rad)
    {
        const double cs = std::cos(rad);
        const double sn = std::sin(rad);
        IrXform x;
        x.a = cs;
        x.b = sn;
        x.c = -sn;
        x.d = cs;
        return x;
    }

    IrXform IrXform::then(const IrXform& next) const
    {
        // next ∘ this：先做 this 再做 next
        IrXform r;
        r.a = next.a * a + next.c * b;
        r.b = next.b * a + next.d * b;
        r.c = next.a * c + next.c * d;
        r.d = next.b * c + next.d * d;
        r.e = next.a * e + next.c * f + next.e;
        r.f = next.b * e + next.d * f + next.f;
        return r;
    }

    void IrXform::applyPoint(double& x, double& y) const
    {
        const double nx = a * x + c * y + e;
        const double ny = b * x + d * y + f;
        x = nx;
        y = ny;
    }

    void IrXform::applyVector(double& x, double& y) const
    {
        const double nx = a * x + c * y;
        const double ny = b * x + d * y;
        x = nx;
        y = ny;
    }

    double IrXform::det() const
    {
        return a * d - b * c;
    }

    double IrXform::scaleX() const
    {
        return std::sqrt(a * a + b * b);
    }

    double IrXform::scaleY() const
    {
        return std::sqrt(c * c + d * d);
    }

    bool IrXform::hasShear(double tol) const
    {
        // 两列点积为 0 即正交（无剪切）；用缩放模长归一化，避免大尺度下 tol 失效
        const double sx = scaleX();
        const double sy = scaleY();
        if (sx < tol || sy < tol)
        {
            return false;
        }
        return std::fabs((a * c + b * d) / (sx * sy)) > tol;
    }

    bool IrXform::isSimilarity(double tol) const
    {
        if (hasShear(tol))
        {
            return false;
        }
        const double sx = scaleX();
        const double sy = scaleY();
        const double scaleRef = std::fmax(sx, sy);
        if (scaleRef < tol)
        {
            return false;
        }
        return std::fabs(sx - sy) / scaleRef <= 1e-6;
    }

    double IrXform::rotation() const
    {
        return std::atan2(b, a);
    }

    bool IrXform::isIdentity(double tol) const
    {
        return std::fabs(a - 1.0) < tol && std::fabs(b) < tol && std::fabs(c) < tol && std::fabs(d - 1.0) < tol &&
            std::fabs(e) < tol && std::fabs(f) < tol;
    }

    bool IrXform::isFinite() const
    {
        return finite(a) && finite(b) && finite(c) && finite(d) && finite(e) && finite(f);
    }

    // ===================== transformEntity =====================

    namespace
    {
        /// 取出 info 的扩展数据（double 序列）。越界返回 false。
        bool readDoubleBlob(
            const EntityInfo& info, const uint8_t* srcBlob, std::size_t srcBlobSize, std::vector<double>& out)
        {
            if (info.extensionDataSize == 0)
            {
                out.clear();
                return true;
            }
            if (srcBlob == nullptr)
            {
                return false;
            }
            // IR 是跨边界数据，offset/size 一律不可信，必须先校验再解引用
            const std::size_t end =
                static_cast<std::size_t>(info.extensionDataOffset) + static_cast<std::size_t>(info.extensionDataSize);
            if (end > srcBlobSize || (info.extensionDataSize % sizeof(double)) != 0)
            {
                return false;
            }
            const auto* raw = reinterpret_cast<const double*>(srcBlob + info.extensionDataOffset);
            out.assign(raw, raw + info.extensionDataSize / sizeof(double));
            return true;
        }

        /// 读取折线 bulge 数组。无 bulge 时返回 true 且 out 为空；
        /// 有数据但长度与顶点数不匹配时返回 false（调用方应忽略 bulge 而不是丢图元）。
        bool readBulgeBlob(const EntityInfo& info,
            const uint8_t* srcBlob,
            std::size_t srcBlobSize,
            std::size_t vertexCount,
            std::vector<double>& out)
        {
            out.clear();
            if (info.bulgeDataSize == 0)
            {
                return true;
            }
            if (srcBlob == nullptr)
            {
                return false;
            }
            const std::size_t end =
                static_cast<std::size_t>(info.bulgeDataOffset) + static_cast<std::size_t>(info.bulgeDataSize);
            if (end > srcBlobSize || (info.bulgeDataSize % sizeof(double)) != 0 ||
                info.bulgeDataSize != vertexCount * sizeof(double))
            {
                return false;
            }
            const auto* raw = reinterpret_cast<const double*>(srcBlob + info.bulgeDataOffset);
            out.assign(raw, raw + vertexCount);
            bool any = false;
            for (double& b : out)
            {
                if (!std::isfinite(b) || std::fabs(b) < 1e-12)
                {
                    b = 0.0;
                }
                else
                {
                    any = true;
                }
            }
            if (!any)
            {
                out.clear();
            }
            return true;
        }

        constexpr int kMaxBakeSegmentsPerArc = 1024;

        struct BakeArc
        {
            double cx{ 0.0 };
            double cy{ 0.0 };
            double radius{ 0.0 };
            double startAngle{ 0.0 };
            double sweep{ 0.0 };
        };

        bool bakeArcFromBulge(double x0, double y0, double x1, double y1, double bulge, BakeArc& arc)
        {
            if (!finite(x0) || !finite(y0) || !finite(x1) || !finite(y1) || !finite(bulge) ||
                std::fabs(bulge) < 1e-12)
            {
                return false;
            }
            const double dx = x1 - x0;
            const double dy = y1 - y0;
            const double chord = std::sqrt(dx * dx + dy * dy);
            if (!(chord > 1e-12))
            {
                return false;
            }
            const double included = 4.0 * std::atan(bulge);
            const double sinHalf = std::sin(std::fabs(included) * 0.5);
            if (std::fabs(sinHalf) < 1e-15)
            {
                return false;
            }
            const double radius = chord / (2.0 * sinHalf);
            if (!finite(radius) || radius < 1e-12 || radius > 1e150)
            {
                return false;
            }
            const double h = std::sqrt(std::fmax(radius * radius - (chord * 0.5) * (chord * 0.5), 0.0));
            const double sign = (included > 0.0) ? 1.0 : -1.0;
            const double dirX = -dy / chord;
            const double dirY = dx / chord;
            const double offset = (std::fabs(included) > 3.14159265358979323846) ? -h : h;
            arc.cx = (x0 + x1) * 0.5 + sign * offset * dirX;
            arc.cy = (y0 + y1) * 0.5 + sign * offset * dirY;
            arc.radius = radius;
            arc.startAngle = std::atan2(y0 - arc.cy, x0 - arc.cx);
            arc.sweep = included;
            return true;
        }

        /// 把带 bulge 的折线展开成纯顶点序列（x0,y0,x1,y1,...）。
        /// 只在「非相似变换」时使用：椭圆弧无法用单一 bulge 表达，先烘焙再变换。
        std::vector<double> bakeBulgePolyline(
            const std::vector<double>& pts, const std::vector<double>& bulges, bool closed, double tolerance)
        {
            const size_t vertexCount = pts.size() / 2;
            std::vector<double> out;
            if (vertexCount < 2)
            {
                return pts;
            }
            const size_t segCount = closed ? vertexCount : vertexCount - 1;
            out.reserve(pts.size() * 2);
            size_t budget = 65536;

            for (size_t i = 0; i < segCount; ++i)
            {
                const size_t next = (i + 1) % vertexCount;
                out.push_back(pts[i * 2]);
                out.push_back(pts[i * 2 + 1]);

                BakeArc arc;
                const double bulge = (i < bulges.size()) ? bulges[i] : 0.0;
                if (!bakeArcFromBulge(
                        pts[i * 2], pts[i * 2 + 1], pts[next * 2], pts[next * 2 + 1], bulge, arc))
                {
                    continue;
                }
                const double ratio = std::clamp(1.0 - tolerance / arc.radius, -1.0, 1.0);
                const double maxStep = 2.0 * std::acos(ratio);
                int steps = 1;
                if (finite(maxStep) && maxStep > 1e-12)
                {
                    steps = static_cast<int>(std::ceil(std::fabs(arc.sweep) / maxStep));
                }
                else
                {
                    steps = kMaxBakeSegmentsPerArc;
                }
                steps = std::clamp(steps, 1, kMaxBakeSegmentsPerArc);
                if (budget == 0)
                {
                    continue;
                }
                steps = std::min(steps, static_cast<int>(std::min<size_t>(budget, kMaxBakeSegmentsPerArc)));
                for (int s = 1; s < steps; ++s)
                {
                    const double angle = arc.startAngle + arc.sweep * (static_cast<double>(s) / steps);
                    out.push_back(arc.cx + arc.radius * std::cos(angle));
                    out.push_back(arc.cy + arc.radius * std::sin(angle));
                }
                budget -= static_cast<size_t>(steps - 1);
            }

            if (!closed)
            {
                out.push_back(pts[(vertexCount - 1) * 2]);
                out.push_back(pts[(vertexCount - 1) * 2 + 1]);
            }
            return out;
        }

        /// 圆/圆弧在非等比缩放下升级为椭圆。DXF INSERT 的语义是「先缩放再旋转」，
        /// 因此椭圆两轴分别是 r*|sx| 与 r*|sy|，长轴方向就是变换的旋转角。
        void circleToEllipse(
            EntityInfo& info, double cx, double cy, double r, const IrXform& xf, bool isArc, double sa, double ea)
        {
            const double sx = xf.scaleX();
            const double sy = xf.scaleY();
            double ncx = cx;
            double ncy = cy;
            xf.applyPoint(ncx, ncy);

            info.type = EntityType::Ellipse;
            info.ellipse.cx = ncx;
            info.ellipse.cy = ncy;
            info.ellipse.rx = r * sx;
            info.ellipse.ry = r * sy;
            info.ellipse.rot = xf.rotation();

            if (!isArc)
            {
                info.ellipse.sa = 0.0;
                info.ellipse.ea = kTwoPi;
                return;
            }

            // 圆角 a 对应的椭圆参数 t 满足 tan t = (sy*sin a)/(sx*cos a)；
            // 缩放为负（镜像）时符号自然带进来。
            const double sgnX = (xf.a >= 0.0) ? 1.0 : -1.0;
            const double sgnY = (xf.det() >= 0.0) ? sgnX : -sgnX;
            double t0 = std::atan2(sgnY * std::sin(sa), sgnX * std::cos(sa));
            double t1 = std::atan2(sgnY * std::sin(ea), sgnX * std::cos(ea));
            if (xf.det() < 0.0)
            {
                // 镜像把逆时针变成顺时针，交换端点恢复「逆时针从 sa 到 ea」的约定
                std::swap(t0, t1);
            }
            info.ellipse.sa = t0;
            info.ellipse.ea = t1;
        }
    }  // namespace

    bool transformEntity(EntityInfo& info,
        const uint8_t* srcBlob,
        std::size_t srcBlobSize,
        const IrXform& xf,
        IrPublisher& pub,
        std::vector<std::string>& warnings)
    {
        if (!xf.isFinite())
        {
            warnings.emplace_back("Transform contains non-finite values, entity dropped");
            return false;
        }

        const double sx = xf.scaleX();
        const double sy = xf.scaleY();
        const bool similarity = xf.isSimilarity();
        if (xf.hasShear())
        {
            // DXF INSERT 不会产生剪切；出现说明来源是 SVG matrix 之类，
            // 此处按「忽略剪切、只取缩放与旋转」退化，并明确告知。
            warnings.emplace_back("Sheared transform is approximated (shear ignored)");
        }

        switch (info.type)
        {
        case EntityType::Line:
            xf.applyPoint(info.line.x1, info.line.y1);
            xf.applyPoint(info.line.x2, info.line.y2);
            break;

        case EntityType::Point:
            // 点坐标复用 line.x1/y1
            xf.applyPoint(info.line.x1, info.line.y1);
            break;

        case EntityType::Circle:
            if (similarity)
            {
                const double r = info.circle.r * sx;
                xf.applyPoint(info.circle.cx, info.circle.cy);
                info.circle.r = r;
            }
            else
            {
                circleToEllipse(info, info.circle.cx, info.circle.cy, info.circle.r, xf, false, 0.0, 0.0);
            }
            break;

        case EntityType::Arc:
            if (similarity)
            {
                const double sa = angleOfTransformedPoint(info.arc.cx, info.arc.cy, info.arc.r, info.arc.sa, xf);
                const double ea = angleOfTransformedPoint(info.arc.cx, info.arc.cy, info.arc.r, info.arc.ea, xf);
                const double r = info.arc.r * sx;
                xf.applyPoint(info.arc.cx, info.arc.cy);
                info.arc.r = r;
                // 镜像时遍历方向翻转，交换端角以维持「逆时针 sa→ea」
                info.arc.sa = (xf.det() < 0.0) ? ea : sa;
                info.arc.ea = (xf.det() < 0.0) ? sa : ea;
            }
            else
            {
                circleToEllipse(info, info.arc.cx, info.arc.cy, info.arc.r, xf, true, info.arc.sa, info.arc.ea);
            }
            break;

        case EntityType::Ellipse:
        {
            // 椭圆轴向与缩放轴不一致时，严格结果需要重解二次曲线；
            // DXF INSERT 的常见情形（等比缩放 + 旋转）是精确的，其余按轴缩放近似并告警。
            if (!similarity)
            {
                warnings.emplace_back("Ellipse under non-uniform scale is approximated");
            }
            xf.applyPoint(info.ellipse.cx, info.ellipse.cy);
            info.ellipse.rx *= sx;
            info.ellipse.ry *= sy;
            info.ellipse.rot += xf.rotation();
            if (xf.det() < 0.0)
            {
                std::swap(info.ellipse.sa, info.ellipse.ea);
            }
            break;
        }

        case EntityType::Text:
            xf.applyPoint(info.text.x, info.text.y);
            info.text.h *= sy;
            info.text.a += xf.rotation();
            break;

        case EntityType::BarCode:
            xf.applyPoint(info.text.x, info.text.y);
            info.barWidth *= sx;
            info.barHeight *= sy;
            break;

        case EntityType::QRCode:
            xf.applyPoint(info.text.x, info.text.y);
            info.moduleSize *= sx;
            break;

        case EntityType::Bezier:
            xf.applyPoint(info.line.x1, info.line.y1);
            xf.applyPoint(info.bezier.c0x, info.bezier.c0y);
            xf.applyPoint(info.bezier.c1x, info.bezier.c1y);
            xf.applyPoint(info.bezier.ex, info.bezier.ey);
            break;

        case EntityType::Bezier2:
            xf.applyPoint(info.line.x1, info.line.y1);
            xf.applyPoint(info.bezier2.cx, info.bezier2.cy);
            xf.applyPoint(info.bezier2.ex, info.bezier2.ey);
            break;

        case EntityType::Polygon:
        case EntityType::Polyline:
        {
            std::vector<double> pts;
            if (!readDoubleBlob(info, srcBlob, srcBlobSize, pts))
            {
                warnings.emplace_back("Polyline extension data out of range, entity dropped");
                return false;
            }

            const size_t vertexCount = pts.size() / 2;
            std::vector<double> bulges;
            if (!readBulgeBlob(info, srcBlob, srcBlobSize, vertexCount, bulges))
            {
                // bulge 数据畸形不影响顶点本身：退化为直线并留痕，绝不越界读取。
                warnings.emplace_back("Bulge data invalid, polyline imported as straight segments");
                bulges.clear();
            }

            const bool closed = (info.type == EntityType::Polygon);
            if (!bulges.empty() && !xf.isSimilarity())
            {
                // 非相似变换会把圆弧压成椭圆弧，单一 bulge 无法表达。
                // 先按高精度烘焙再变换，宁可物化成密集折线也不让形状错掉。
                double diag = 0.0;
                for (size_t i = 1; i < vertexCount; ++i)
                {
                    const double dx = pts[i * 2] - pts[0];
                    const double dy = pts[i * 2 + 1] - pts[1];
                    diag = std::fmax(diag, std::sqrt(dx * dx + dy * dy));
                }
                const double tolerance = std::fmax(1e-7 * std::fmax(diag, 1.0), 1e-9);
                pts = bakeBulgePolyline(pts, bulges, closed, tolerance);
                bulges.clear();
                info.vertexCount = static_cast<uint32_t>(pts.size() / 2);
            }

            for (std::size_t i = 0; i + 1 < pts.size(); i += 2)
            {
                xf.applyPoint(pts[i], pts[i + 1]);
            }

            if (!bulges.empty() && xf.det() < 0.0)
            {
                // 镜像翻转圆弧方向：逆时针 → 顺时针。
                for (double& bulge : bulges)
                {
                    bulge = -bulge;
                }
            }

            const std::size_t bytes = pts.size() * sizeof(double);
            const uint32_t offset = pub.appendBlob(pts.data(), bytes);
            if (offset == IrPublisher::kInvalidOffset)
            {
                warnings.emplace_back("Extension blob full, polyline dropped");
                return false;
            }
            info.extensionDataOffset = offset;
            info.extensionDataSize = static_cast<uint32_t>(bytes);
            info.vertexCount = static_cast<uint32_t>(pts.size() / 2);

            if (bulges.empty())
            {
                info.bulgeDataOffset = 0;
                info.bulgeDataSize = 0;
            }
            else
            {
                const std::size_t bulgeBytes = bulges.size() * sizeof(double);
                const uint32_t bulgeOffset = pub.appendBlob(bulges.data(), bulgeBytes);
                if (bulgeOffset == IrPublisher::kInvalidOffset)
                {
                    warnings.emplace_back("Extension blob full, polyline bulges dropped");
                    info.bulgeDataOffset = 0;
                    info.bulgeDataSize = 0;
                }
                else
                {
                    info.bulgeDataOffset = bulgeOffset;
                    info.bulgeDataSize = static_cast<uint32_t>(bulgeBytes);
                }
            }
            break;
        }

        case EntityType::Nurbs:
        case EntityType::Spline:
        {
            // 布局 [控制点 n*2][节点 k][权重 w]：只变换控制点，节点与权重是参数域数据，
            // 与坐标系无关，必须原样保留（否则曲线形状会错）。
            std::vector<double> vals;
            if (!readDoubleBlob(info, srcBlob, srcBlobSize, vals))
            {
                warnings.emplace_back("NURBS extension data out of range, entity dropped");
                return false;
            }
            const std::size_t cpDoubles = static_cast<std::size_t>(info.nurbsCtrlPtCount) * 2u;
            if (cpDoubles > vals.size())
            {
                warnings.emplace_back("NURBS control point count exceeds extension data, entity dropped");
                return false;
            }
            for (std::size_t i = 0; i + 1 < cpDoubles; i += 2)
            {
                xf.applyPoint(vals[i], vals[i + 1]);
            }
            const std::size_t bytes = vals.size() * sizeof(double);
            const uint32_t offset = pub.appendBlob(vals.data(), bytes);
            if (offset == IrPublisher::kInvalidOffset)
            {
                warnings.emplace_back("Extension blob full, NURBS dropped");
                return false;
            }
            info.extensionDataOffset = offset;
            info.extensionDataSize = static_cast<uint32_t>(bytes);
            break;
        }

        case EntityType::SmartLine:
        {
            // 复合曲线：基点在线 x1/y1；段记录按 kSmartSegStride 定长排布写进扩展块，
            // 每段 [标签][p0.x][p0.y][p1.x][p1.y][p2.x][p2.y][p3.x][p3.y]，坐标都是绝对坐标。
            // 换算只乘坐标（下标 1..8），标签（下标 0）与坐标无关，保持不变。
            constexpr std::size_t kStrideBytes = kSmartSegStride * sizeof(double);
            const std::size_t end =
                static_cast<std::size_t>(info.extensionDataOffset) + static_cast<std::size_t>(info.extensionDataSize);
            if (srcBlob == nullptr || end > srcBlobSize || (info.extensionDataSize % kStrideBytes) != 0)
            {
                warnings.emplace_back("SmartLine extension data out of range, entity dropped");
                return false;
            }
            const auto* raw = reinterpret_cast<const double*>(srcBlob + info.extensionDataOffset);
            std::vector<double> vals(raw, raw + info.extensionDataSize / sizeof(double));
            for (std::size_t i = 0; i + 1 < vals.size(); i += kSmartSegStride)
            {
                for (std::size_t k = 1; k < kSmartSegStride; k += 2)
                {
                    xf.applyPoint(vals[i + k], vals[i + k + 1]);
                }
            }
            const std::size_t bytes = vals.size() * sizeof(double);
            const uint32_t offset = pub.appendBlob(vals.data(), bytes);
            if (offset == IrPublisher::kInvalidOffset)
            {
                warnings.emplace_back("Extension blob full, SmartLine dropped");
                return false;
            }
            info.extensionDataOffset = offset;
            info.extensionDataSize = static_cast<uint32_t>(bytes);
            xf.applyPoint(info.line.x1, info.line.y1);
            break;
        }

        case EntityType::Image:
        {
            // 位图像素本身不重采样：只把左上角锚点变换过去，尺寸仍按像素解释。
            // 旋转/镜像的位图目前无法在 IR 里表达，明确告警而不是悄悄画歪。
            xf.applyPoint(info.line.x1, info.line.y1);
            if (std::fabs(xf.rotation()) > 1e-9 || xf.det() < 0.0)
            {
                warnings.emplace_back("Rotated or mirrored image is placed axis-aligned");
            }
            std::vector<double> unusedDummy;
            // 图像扩展数据是编码后的字节流，不是 double 序列，原样搬运
            if (info.extensionDataSize > 0)
            {
                const std::size_t end = static_cast<std::size_t>(info.extensionDataOffset) +
                    static_cast<std::size_t>(info.extensionDataSize);
                if (srcBlob == nullptr || end > srcBlobSize)
                {
                    warnings.emplace_back("Image extension data out of range, entity dropped");
                    return false;
                }
                const uint32_t offset = pub.appendBlob(srcBlob + info.extensionDataOffset, info.extensionDataSize);
                if (offset == IrPublisher::kInvalidOffset)
                {
                    warnings.emplace_back("Extension blob full, image dropped");
                    return false;
                }
                info.extensionDataOffset = offset;
            }
            break;
        }

        case EntityType::Mesh3D:
        {
            // 3D 网格只做 XY 平面变换（Z 不动）：块引用里出现网格属于少见情形，
            // 语义上等价于在俯视面上摆放，先支持不丢数据。
            if (info.extensionDataSize == 0)
            {
                break;
            }
            const std::size_t end =
                static_cast<std::size_t>(info.extensionDataOffset) + static_cast<std::size_t>(info.extensionDataSize);
            if (srcBlob == nullptr || end > srcBlobSize || (info.extensionDataSize % sizeof(float)) != 0)
            {
                warnings.emplace_back("Mesh extension data out of range, entity dropped");
                return false;
            }
            const auto* raw = reinterpret_cast<const float*>(srcBlob + info.extensionDataOffset);
            std::vector<float> floats(raw, raw + info.extensionDataSize / sizeof(float));
            const std::size_t vertFloats = static_cast<std::size_t>(info.meshVertCount) * 3u;
            for (std::size_t i = 0; i + 2 < std::min(vertFloats, floats.size()); i += 3)
            {
                double x = floats[i];
                double y = floats[i + 1];
                xf.applyPoint(x, y);
                floats[i] = static_cast<float>(x);
                floats[i + 1] = static_cast<float>(y);
            }
            // 法线只受线性部分影响，且非等比缩放下严格解应用逆转置；此处按向量变换后
            // 交给消费侧归一化，误差仅影响光照，不影响几何。
            for (std::size_t i = vertFloats; i + 2 < floats.size(); i += 3)
            {
                double x = floats[i];
                double y = floats[i + 1];
                xf.applyVector(x, y);
                floats[i] = static_cast<float>(x);
                floats[i + 1] = static_cast<float>(y);
            }
            const std::size_t bytes = floats.size() * sizeof(float);
            const uint32_t offset = pub.appendBlob(floats.data(), bytes);
            if (offset == IrPublisher::kInvalidOffset)
            {
                warnings.emplace_back("Extension blob full, mesh dropped");
                return false;
            }
            info.extensionDataOffset = offset;
            info.extensionDataSize = static_cast<uint32_t>(bytes);
            break;
        }

        case EntityType::Unknown:
            warnings.emplace_back("Entity type cannot be transformed, entity dropped");
            SY_WARNF("[IrTransform] Cannot transform entity type %d, dropped", static_cast<int>(info.type));
            return false;
        }

        return true;
    }
}  // namespace Fio