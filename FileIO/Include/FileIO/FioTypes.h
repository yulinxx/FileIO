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
//   5. 本文件内的 POD 一律**不加** FILEIO_API 导出宏：它们没有成员函数、没有静态
//      成员，没有任何符号需要导出；给纯 POD 加 dllexport 反而会把成员布局纳入 ABI
//      约束（MSVC 的 C4251 隐患），并在 GCC/Clang 上产生 "attribute ignored" 警告。
//      导出宏只保留在类（FileIOManager / IFileParser / FormatRegistry 等）和自由函数上。
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
    enum class EntityType : uint8_t
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
    struct IrLayerInfo
    {
        uint32_t sourceId = 0;
        char name[256] = {};
        uint32_t color = 0xFF000000;
        bool visible = true;
        bool locked = false;
    };

    /// 群组信息（POD，跨 DLL 安全）
    ///
    /// 群组是一棵任意深度的树，`parentSourceId == 0` 表示顶层群组。
    ///
    /// **成员关系刻意只用单向表达**：实体属于哪个群组记在 `EntityInfo::groupSourceId`，
    /// 本结构不再重复存成员列表。理由：
    ///   1. 与既有的 `EntityInfo::layerSourceId` 完全同构，消费方处理方式一致；
    ///   2. 双向存储（父存成员 + 子存父）一旦不一致就没有裁决依据，畸形文件很容易触发；
    ///   3. 不需要为群组再引入一套 extensionBlob 偏移，少一处越界校验点。
    /// FileIO 内部的 `ParsedGroup` 同时存了 `subGroupSourceIds`，那是 DLL 内解析期
    /// 的便利结构，投影到本 POD 时只保留 `parentSourceId` 这一个方向。
    ///
    /// 重建整棵树的方法：按 `parentSourceId` 建父→子索引，按实体的 `groupSourceId`
    /// 归集叶子成员。存在环或引用不存在的父 id 时，消费方应把该群组当作顶层处理并告警。
    struct IrGroupInfo
    {
        uint64_t sourceId = 0;        // 群组自身 id；1-based，0 保留为「无群组」哨兵
        uint64_t parentSourceId = 0;  // 父群组 id；0 表示顶层
        char name[256] = {};
    };

    /// 2D 点（POD）
    struct Point2D
    {
        double x = 0.0;
        double y = 0.0;
    };

    /// 复合曲线（EntityType::SmartLine）扩展数据块里的段类型标签
    ///
    /// 一条 SVG path / DXF 复合轮廓在 IR 里是**一个** EntityInfo，各段几何放在扩展块。
    /// 标签以 double 形式存在每段首位（扩展块统一是 double 序列，不混排整型，
    /// 避免在跨 DLL 边界上引入对齐与字节序假设）。
    enum class SmartSegKind : uint8_t
    {
        Line = 0,     ///< 直线段：用 p0 → p1，其余点忽略
        Bezier2 = 1,  ///< 二次贝塞尔：p0, c, p1
        Bezier = 2    ///< 三次贝塞尔：p0, c0, c1, p1
    };

    /// 复合曲线扩展块中每段固定占用的 double 数：[标签][p0][p1][p2][p3] = 1 + 8
    ///
    /// 刻意用**定长**而非紧凑变长布局：段数可由 extensionDataSize 直接反算，
    /// 读取方无需边解析边推进就能校验完整性（畸形文件给出的 vertexCount 不可信）。
    constexpr uint32_t kSmartSegStride = 9;



    /// 图元信息（POD，固定长度缓冲区 + 纯数值几何参数）
    /// 注意：复杂几何数据（如多边形顶点、贝塞尔控制点序列）通过扩展数据块承载
    struct EntityInfo
    {
        uint64_t sourceId = 0;
        EntityType type = EntityType::Unknown;
        char name[256] = {};
        uint32_t layerSourceId = 0;

        // 所属群组 id，对应 FioParseResult::groups 里的 IrGroupInfo::sourceId；0 = 不属于任何群组。
        // 与 layerSourceId 刻意同构：成员关系只在实体侧单向记录，群组侧不存成员列表。
        uint64_t groupSourceId = 0;

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
        //
        // 复合曲线（SmartLine）复用本字段表示**段数**，几何在扩展块里按
        // kSmartSegStride 定长排布，见上方 SmartSegKind 注释。
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
        // 复杂几何（多边形顶点、NURBS 控制点/节点/权重、图像像素、网格顶点索引等）
        // 不内联在本结构里，而是集中存放在 FioParseResult::extensionBlob，这里只记偏移与长度。
        // 读取方必须校验 extensionDataOffset + extensionDataSize <= extensionBlob.size，
        // 不可信任本字段：畸形文件可能给出越界值。
        uint32_t extensionDataOffset = 0;  // 在 FioParseResult::extensionBlob 中的字节偏移
        uint32_t extensionDataSize = 0;    // 扩展数据字节数
    };

    /// 二进制数据块（纯 POD，用于可变长度数据）
    ///
    /// **所有权不由本类型决定，而由返回它的那个 API 各自声明。** 本模块里两种都存在：
    ///   - 借用（非拥有）：FioParseResult::extensionBlob —— 指向 FileIO 内部的
    ///     thread_local 缓冲区，调用方只读，绝不可释放。
    ///   - 拥有（须释放）：FileImporter::ExportBlob() —— 由 FileIO 分配，
    ///     调用方必须调用 FileImporter::FreeBlob() 归还（在 FileIO.dll 内 delete，
    ///     保证 new/delete 同堆配对）。
    /// 新增返回 BinaryBlob 的接口时，必须在该接口的文档注释里明确写出属于哪一种。
    struct BinaryBlob
    {
        uint8_t* data = nullptr;
        size_t size = 0;
    };

    /// 二进制输出块（纯 POD，调用方提供缓冲区；Blob 化序列化输出用）
    /// - data == nullptr：仅查询所需大小（写入 written）
    /// - written 始终为完整所需字节数；容量不足时只拷贝 capacity 字节
    struct BinaryBlobOut
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
    ///
    /// **内存契约（重要）**：
    ///   entities / layers / extensionBlob.data 三个指针均指向 FileIO.dll 内部的
    ///   thread_local 缓冲区，**不是调用方的堆内存**：
    ///     - 调用方**只读**，绝不可 delete / delete[] / free —— 那既是释放 vector
    ///       内部指针，又是跨 DLL 堆释放，属双重未定义行为，会直接损坏堆。
    ///     - 有效期仅到「同一线程下一次调用任意解析入口」为止，调用方必须立即消费
    ///       （典型做法：立刻转换成领域对象，见 FioEntityConverter::convertAll）。
    ///     - 不可跨线程保存；跨线程要传的是转换完成后的领域对象，不是本结构。
    ///   读取 extensionBlob 时必须自行校验
    ///   `extensionDataOffset + extensionDataSize <= extensionBlob.size`——
    ///   IR 是跨 DLL 边界传入的数据，畸形文件可能给出越界的 offset/size。
    struct FioParseResult
    {
        /// 图元列表（POD 数组）
        const EntityInfo* entities = nullptr;
        uint32_t entityCount = 0;

        /// 图层列表（POD 数组）
        const IrLayerInfo* layers = nullptr;
        uint32_t layerCount = 0;

        /// 群组列表（POD 数组，父子关系由 IrGroupInfo::parentSourceId 表达）
        /// 内存契约与 entities / layers 完全一致：只读、不可释放、有效期至下次同线程解析。
        /// 群组来源举例：DXF 的 BLOCK/INSERT 实例、SVG 的 <g>、OBJ 的 o/g 分段。
        const IrGroupInfo* groups = nullptr;
        uint32_t groupCount = 0;


        /// 扩展数据块（多边形顶点、贝塞尔控制点序列等可变长度数据）
        BinaryBlob extensionBlob{};

        /// 元数据
        char sourceFormat[64] = {};  // 来源格式名（如 "DXF", "SVG"）
        char sourceUnit[16] = {};    // 来源单位（如 "mm", "inch"）
        uint32_t warningCount = 0;
    };
}  // namespace Fio