#pragma once

// ============================================================================
// IrTransform —— 作用在跨 DLL IR（EntityInfo）上的 2D 仿射变换
//
// 为什么需要它：
//   DXF 的 INSERT（块引用）本质是「把块定义里的一批图元按 平移+旋转+缩放 复制一份」。
//   要在解析阶段把块展开成真实图元，就必须能对已经成型的 EntityInfo 做变换，
//   包括存在扩展数据块里的折线顶点、NURBS 控制点。
//
//   同一套能力后续还会被用到：DXF 阵列（colcount/rowcount）、镜像、单位换算
//   （$INSUNITS：把 inch 图纸整体乘 25.4）、SVG 的 transform 属性。
//   所以刻意做成与格式无关的独立层，而不是塞在 DxfParser 里。
//
// 精度取舍（重要）：
//   变换分两类处理。相似变换（等比缩放，可含镜像）下圆/圆弧仍是圆/圆弧，做精确变换；
//   非等比缩放会把圆变成椭圆，此时按「先缩放后旋转」的 DXF 语义提升为 Ellipse。
//   任意剪切（DXF INSERT 不会产生）无法精确表达，会退化处理并记 warning，绝不静默出错。
// ============================================================================

#include "FileIO/FioTypes.h"
#include "IrProjector.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Fio
{
    /// 2D 仿射变换，行主序 2x3 矩阵：
    ///   x' = a*x + c*y + e
    ///   y' = b*x + d*y + f
    struct IrXform
    {
        double a = 1.0, b = 0.0, c = 0.0, d = 1.0, e = 0.0, f = 0.0;

        static IrXform translate(double dx, double dy);
        static IrXform scale(double sx, double sy);
        /// @param rad 逆时针弧度
        static IrXform rotate(double rad);

        /// 复合：先应用 *this，再应用 next（返回 next ∘ this）
        IrXform then(const IrXform& next) const;

        /// 变换点（含平移）
        void applyPoint(double& x, double& y) const;
        /// 变换向量（忽略平移，用于半径、方向等）
        void applyVector(double& x, double& y) const;

        double det() const;
        /// 是否为相似变换（无剪切且两轴等比）。det < 0 表示含镜像，仍算相似。
        bool isSimilarity(double tol = 1e-9) const;
        /// 是否含剪切（两列不正交）——DXF INSERT 不会产生，SVG 可能
        bool hasShear(double tol = 1e-9) const;
        /// 旋转角（弧度，取第一列方向）
        double rotation() const;
        /// 两轴缩放模长（恒为非负；镜像信息在 det 的符号里）
        double scaleX() const;
        double scaleY() const;
        bool isIdentity(double tol = 1e-12) const;
        bool isFinite() const;
    };

    /// 对单个 EntityInfo 施加仿射变换。
    ///
    /// 扩展数据的处理：折线顶点 / NURBS 控制点需要逐点变换，因此本函数会从
    /// `srcBlob`（原始数据所在缓冲区，可能是块定义自己的缓冲区）读出、变换、
    /// 再追加到 `pub` 的扩展数据块，并改写 info 的 offset/size。
    /// 读取前会校验 `extensionDataOffset + extensionDataSize <= srcBlobSize`，
    /// 越界即丢弃该图元（返回 false）并记 warning。
    ///
    /// @param info      输入输出；type 可能被改写（非等比缩放下 Circle/Arc → Ellipse）
    /// @param srcBlob   info 的扩展数据所在缓冲区；无扩展数据时可传 nullptr
    /// @param xf        变换
    /// @param pub       变换后扩展数据的写入目标
    /// @param warnings  退化或丢弃时追加英文说明
    /// @return false 表示该图元无法安全变换、调用方应丢弃它
    bool transformEntity(EntityInfo& info,
        const uint8_t* srcBlob,
        std::size_t srcBlobSize,
        const IrXform& xf,
        IrPublisher& pub,
        std::vector<std::string>& warnings);
}  // namespace Fio
