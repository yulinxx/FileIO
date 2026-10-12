#pragma once

// ============================================================================
// UnitTransform —— 导入单位换算（跨平台纯 C++，无 Qt / 无平台特定代码）
//
// 背景：
//   源文件可以自带单位声明（如 DXF 的 $INSUNITS → FioParseResult::sourceUnit），
//   而导入目标（当前文档）通常以毫米为基准。单位不一致时若原样导入，
//   英制图纸会整体小 25.4 倍。本模块负责：
//     1. unitToMillimeters / unitScaleRatio：单位名 → 换算比例（纯函数）；
//     2. scaleParseResult：对已解析的 IR 施加统一缩放，几何与扩展数据一起换。
//
// 内存契约：
//   scaleParseResult 依赖 FioParseResult 指向的 thread_local 缓冲区，因此：
//     - 必须在同一线程、解析返回之后、下一次解析（reset）之前调用；
//     - 会就地修改 entities 并重写 extensionDataOffset/Size，
//       同时把 result->extensionBlob / warningCount 指向换算后的最新值。
//   scale==1.0（或其等价换算结果）时是安全的空操作，直接返回 true。
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

#include "FileIO/FioTypes.h"
#include "FileIO/FileIOAPI.h"

namespace Fio
{
    /// 单位名 → 每 1 单位对应的毫米数。识别 DXF $INSUNITS 的名字及常用别名
    /// （"mm"/"cm"/"m"/"km"/"um"/"micron"/"nm"/"angstrom"/"dm"/"dam"/"hm"/
    ///  "mil"/"inch"/"in"/"ft"/"yd"/"px"）。
    /// 未知单位名返回 0.0（表示「无法换算，按与目标同单位处理」）。
    FILEIO_API double unitToMillimeters(const char* unitName);

    /// 由 from 单位换算到 to 单位的比例（1 个 from 单位 = 几 个 to 单位）。
    ///
    /// 规则：
    ///   - 任一单位无法识别（toMillimeters 返回 0，含空串）→ 返回 1.0，
    ///     即「与目标同单位」，不做换算；
    ///   - 两者都能识别 → 返回 toMm(from) / toMm(to)（如 inch→mm 为 25.4）。
    ///
    /// @param fromUnit 源单位名；空串或 nullptr 表示源未声明单位
    /// @param toUnit   目标单位名；空串或 nullptr 表示与源同单位
    FILEIO_API double unitScaleRatio(const char* fromUnit, const char* toUnit);

    /// 对已解析的 IR 施加统一缩放（每个图元的坐标与扩展数据整体乘以 scale）。
    ///
    /// 均匀缩放是相似变换：圆/圆弧仍是圆/圆弧（半径缩放、角度不变），
    /// 折线/NURBS 控制点/复合曲线（SmartLine）的扩展数据逐点缩放，
    /// 文本位置与高度、位图锚点等一并缩放。
    ///
    /// @param result 解析结果，输入输出；指向 thread_local 缓冲区的指针会就地修改
    /// @param scale  缩放因子；必须为正且有限。|scale - 1| < 1e-12 时视为空操作
    /// @return false 仅表示入参非法（result==nullptr 或 scale 非法）或无法安全换算；
    ///         换算中个别图元无法处理（如畸形扩展数据）时该图元被置为 Unknown
    ///         跳过并记 warning，不使整体返回 false。
    FILEIO_API bool scaleParseResult(FioParseResult* result, double scale);
}  // namespace Fio