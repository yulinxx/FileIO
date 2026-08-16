#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

#include "FileIO/FileIOAPI.h"

// ============================================================================
// FioTypes.h — 跨 DLL 边界的中立 POD 类型
//
// 设计原则：
//   1. 所有类型均为纯 POD，不含 std::vector / std::string / std::unique_ptr
//   2. 字符串使用固定长度 char[] 缓冲区
//   3. 可变长度数据使用指针+长度（BinaryBlob）
//   4. 这些类型是 FileIO 与 Engine 之间的"中立 IR 层"
//
// 目标架构：
//   FileIO（解析器） → ParseData（中立 IR） → Engine（SyEntity 转换）
//
// 当前状态：
//   VecSyEntityPtr（IFileParser.h）仍直接暴露 Engine 类型，
//   属于已知的 ABI 技术债，后续逐步迁移到 ParseData 体系。
// ============================================================================

namespace Fio
{
    // ===== 基础枚举与 POD 结构 =====

    /// 图元类型枚举（跨 DLL 安全，uint8_t）
    enum class FILEIO_API EntityType : uint8_t
    {
        Line,
        Arc,
        Circle,
        Ellipse,
        Polygon,
        Bezier,
        Bezier2,
        Nurbs,
        Spline,
        Polyline,
        Point,
        Text,
        Image,
        BarCode,
        QRCode,
        SmartLine,
        Mesh3D,
        Unknown
    };

    /// 图层信息（POD，固定长度缓冲区，跨 DLL 安全）
    struct FILEIO_API IrLayerInfo
    {
        uint32_t sourceId = 0;
        char name[256] = {};
        uint32_t color = 0xFF000000;
        bool visible = true;
        bool locked = false;
    };

    /// 2D 点（POD）
    struct FILEIO_API Point2D
    {
        double x = 0.0;
        double y = 0.0;
    };

    /// 图元信息（POD，固定长度缓冲区 + 纯数值几何参数）
    /// 注意：复杂几何数据（如多边形顶点、贝塞尔控制点序列）通过扩展数据块承载
    struct FILEIO_API EntityInfo
    {
        uint64_t sourceId = 0;
        EntityType type = EntityType::Unknown;
        char name[256] = {};
        uint32_t layerSourceId = 0;
        double lineWidth = 1.0;
        bool visible = true;
        bool locked = false;

        // 解析出的实体颜色（0xAARRGGBB，0 = 未指定，渲染时回退到图层颜色）。
        // 由 DXF/SVG 解析器解析实体自身颜色（真彩色 > ACI 索引 > BYLAYER 图层色）后填充，
        // 转换层以覆盖色（override color）形式应用，确保导入颜色不被图层去重/复用逻辑吞掉。
        uint32_t color = 0;

        // ---- 基础几何参数（按 type 使用对应字段） ----

        // 线段: (x1,y1) → (x2,y2)
        struct
        {
            double x1, y1, x2, y2;
        } line;

        // 圆弧: 圆心(cx,cy), 半径r, 起始角sa, 终止角ea（弧度）
        struct
        {
            double cx, cy, r, sa, ea;
        } arc;

        // 圆: 圆心(cx,cy), 半径r
        struct
        {
            double cx, cy, r;
        } circle;

        // 椭圆: 圆心(cx,cy), 半轴rx/ry, 旋转角rot, 起始角sa, 终止角ea
        struct
        {
            double cx, cy, rx, ry, rot, sa, ea;
        } ellipse;

        // 文本: 位置(x,y), 内容text, 高度h, 角度a
        struct
        {
            double x, y;
            char text[256];
            double h, a;
        } text;

        // 三次贝塞尔: 控制点(c0,c1), 终点(e)
        struct
        {
            double c0x, c0y, c1x, c1y, ex, ey;
        } bezier;

        // 二次贝塞尔: 控制点(c), 终点(e)
        struct
        {
            double cx, cy, ex, ey;
        } bezier2;

        // 多边形/折线: 顶点数据在扩展数据块中（double 序列: x0,y0,x1,y1,...）
        // 闭合标记: bClosed 在基类字段中
        uint32_t vertexCount = 0;

        // NURBS: 阶数 + 控制点/节点/权重数量（完整数据在扩展数据块中）
        // 扩展数据布局: [控制点(n*doubles)] [节点(k*doubles)] [权重(w*doubles)]
        int32_t nurbsDegree = 3;
        uint32_t nurbsCtrlPtCount = 0;
        uint32_t nurbsKnotCount = 0;

        // 图像: 尺寸（像素数据在扩展数据块中）
        int32_t imageWidth = 0;
        int32_t imageHeight = 0;

        // 3D网格: 顶点/三角形数量（完整数据在扩展数据块中）
        uint32_t meshVertCount = 0;
        uint32_t meshTriCount = 0;

        // 条形码: 宽高（数据内容在 text.text 中）
        double barWidth = 0.0;
        double barHeight = 0.0;

        // 二维码: 模块大小（数据内容在 text.text 中）
        double moduleSize = 0.0;

        // ---- 扩展数据（用于复杂几何） ----
        // 指向额外数据块（如多边形顶点坐标数组）
        // 生命周期由 ParseData 统一管理
        uint32_t extensionDataOffset = 0;  // 在 ParseData::extensionBlob 中的偏移
        uint32_t extensionDataSize = 0;    // 扩展数据字节数
    };

    /// 二进制数据块（纯 POD，用于可变长度数据）
    struct FILEIO_API BinaryBlob
    {
        uint8_t* data = nullptr;
        size_t size = 0;
    };

    /// 二进制输出块（纯 POD，调用方提供缓冲区；Blob 化序列化输出用）
    /// - data == nullptr：仅查询所需大小（写入 written）
    /// - written 始终为完整所需字节数；容量不足时只拷贝 capacity 字节
    struct FILEIO_API BinaryBlobOut
    {
        uint8_t* data = nullptr;
        size_t capacity = 0;
        size_t written = 0;
    };

    // ===== 中立 IR 层：FioParseResult =====

    /// 解析结果（中立 IR，不含任何 Engine 类型）
    /// 这是 FileIO 解析器对外的标准输出格式
    ///
    /// 使用方式：
    ///   1. 解析器填充 entities[] 和 layers[]
    ///   2. 调用方（Main 项目）通过 FioEntityConverter 将 EntityInfo 转为 SyEntity
    ///   3. 转换完成后，调用方负责释放 extensionBlob（如有）
    struct FILEIO_API FioParseResult
    {
        /// 图元列表（POD 数组）
        const EntityInfo* entities = nullptr;
        uint32_t entityCount = 0;

        /// 图层列表（POD 数组）
        const IrLayerInfo* layers = nullptr;
        uint32_t layerCount = 0;

        /// 扩展数据块（多边形顶点、贝塞尔控制点序列等可变长度数据）
        BinaryBlob extensionBlob{};

        /// 元数据
        char sourceFormat[64] = {};  // 来源格式名（如 "DXF", "SVG"）
        char sourceUnit[16] = {};    // 来源单位（如 "mm", "inch"）
        uint32_t warningCount = 0;
    };
}  // namespace Fio