// 导入单位换算测试
//
// 覆盖：
//   1. unitToMillimeters / unitScaleRatio 纯函数（DXF $INSUNITS 名字 + 别名 + 未知单位）；
//   2. scaleParseResult：把已解析 IR 统一缩放 —— 直线/圆/折线（扩展数据）坐标同乘，
//      圆保持圆、折线顶点数不变、扩展数据偏移仍有效；
//   3. scaleParseResult 对 SVG 复合曲线（SmartLine）的换算：基点与每段坐标都放大；
//   4. scale == 1.0 时是安全空操作，几何保持不变。
//
// 内存契约注意：scaleParseResult 依赖 FileIO 内部 thread_local IR 缓冲区，
// 因此本文件只在「同线程、解析之后、下一次解析之前」调用它（顺序读）。

#include <gtest/gtest.h>

#include "DxfTestUtils.h"
#include "FileIO/FileIOAPI.h"
#include "FileIO/UnitTransform.h"
#include "FileIO/FileIOManager.h"
#include "FileIO/Parsers/DxfParser.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using DxfTest::removeFile;
using DxfTest::writeDxf;

namespace
{
    std::vector<double> readBlobVerts(const Fio::FioParseResult& r, const Fio::EntityInfo& e)
    {
        const auto* raw = reinterpret_cast<const double*>(r.extensionBlob.data + e.extensionDataOffset);
        return std::vector<double>(raw, raw + e.extensionDataSize / sizeof(double));
    }
}  // namespace

// ==================== 单位换算纯函数 ====================

TEST(UnitTransformTest, KnownUnitsMapToMillimeters)
{
    EXPECT_DOUBLE_EQ(Fio::unitToMillimeters("mm"), 1.0);
    EXPECT_DOUBLE_EQ(Fio::unitToMillimeters("cm"), 10.0);
    EXPECT_DOUBLE_EQ(Fio::unitToMillimeters("m"), 1000.0);
    EXPECT_DOUBLE_EQ(Fio::unitToMillimeters("km"), 1000000.0);
    EXPECT_DOUBLE_EQ(Fio::unitToMillimeters("mil"), 0.0254);
    EXPECT_DOUBLE_EQ(Fio::unitToMillimeters("inch"), 25.4);
    EXPECT_DOUBLE_EQ(Fio::unitToMillimeters("in"), 25.4);  // 别名
    EXPECT_DOUBLE_EQ(Fio::unitToMillimeters("ft"), 304.8);
    EXPECT_DOUBLE_EQ(Fio::unitToMillimeters("yd"), 914.4);
    EXPECT_DOUBLE_EQ(Fio::unitToMillimeters("um"), 0.001);
    EXPECT_DOUBLE_EQ(Fio::unitToMillimeters("micron"), 0.001);
}

TEST(UnitTransformTest, UnknownUnitReturnsZero)
{
    EXPECT_DOUBLE_EQ(Fio::unitToMillimeters(""), 0.0);
    EXPECT_DOUBLE_EQ(Fio::unitToMillimeters(nullptr), 0.0);
    EXPECT_DOUBLE_EQ(Fio::unitToMillimeters("banana"), 0.0);
}

TEST(UnitTransformTest, ScaleRatioInchToMm)
{
    EXPECT_DOUBLE_EQ(Fio::unitScaleRatio("inch", "mm"), 25.4);
    EXPECT_DOUBLE_EQ(Fio::unitScaleRatio("mm", "inch"), 1.0 / 25.4);
    EXPECT_DOUBLE_EQ(Fio::unitScaleRatio("mm", "mm"), 1.0);
    EXPECT_DOUBLE_EQ(Fio::unitScaleRatio("cm", "mm"), 10.0);
}

TEST(UnitTransformTest, UnresolvableUnitKeepsIdentityRatio)
{
    // 源未声明单位 / 任一单位无法识别：保守处理，不做换算
    EXPECT_DOUBLE_EQ(Fio::unitScaleRatio("", "mm"), 1.0);
    EXPECT_DOUBLE_EQ(Fio::unitScaleRatio("inch", ""), 1.0);
    EXPECT_DOUBLE_EQ(Fio::unitScaleRatio("nope", "mm"), 1.0);
}

// ==================== scaleParseResult：DXF（英寸 → 毫米） ====================

TEST(UnitTransformTest, ScalesDxfEntitiesUniformly)
{
    // 英寸图纸：LINE + CIRCLE + 闭合 LWPOLYLINE，全部在层 0
    std::string ents;
    ents += "0\nLINE\n8\n0\n10\n0.0\n20\n0.0\n30\n0.0\n11\n10.0\n21\n20.0\n31\n0.0\n";
    ents += "0\nCIRCLE\n8\n0\n10\n7.0\n20\n3.0\n30\n0.0\n40\n2.0\n";
    ents += "0\nLWPOLYLINE\n8\n0\n90\n3\n70\n1\n";
    ents += "10\n0.0\n20\n0.0\n10\n10.0\n20\n0.0\n10\n10.0\n20\n10.0\n";

    const std::string path = writeDxf(
        "sanyi_scale_inch.dxf", "9\n$INSUNITS\n70\n1\n", "", ents);
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 3u);
    EXPECT_STREQ(r.sourceUnit, "inch");

    ASSERT_TRUE(Fio::scaleParseResult(&r, 25.4));

    // 直线端点整体放大（0 点不动，另一点 10,20 → 254,508）
    EXPECT_NEAR(r.entities[0].line.x1, 0.0, 1e-9);
    EXPECT_NEAR(r.entities[0].line.y1, 0.0, 1e-9);
    EXPECT_NEAR(r.entities[0].line.x2, 254.0, 1e-6);
    EXPECT_NEAR(r.entities[0].line.y2, 508.0, 1e-6);

    // 圆: 均匀缩放仍是圆，半径与圆心同乘
    EXPECT_EQ(r.entities[1].type, Fio::EntityType::Circle);
    EXPECT_NEAR(r.entities[1].circle.cx, 177.8, 1e-6);
    EXPECT_NEAR(r.entities[1].circle.cy, 76.2, 1e-6);
    EXPECT_NEAR(r.entities[1].circle.r, 50.8, 1e-6);

    // 折线（扩展数据）：顶点逐点放大，顶点数不变
    ASSERT_EQ(r.entities[2].type, Fio::EntityType::Polygon);
    const std::vector<double> v = readBlobVerts(r, r.entities[2]);
    ASSERT_EQ(v.size(), 6u);
    EXPECT_NEAR(v[0], 0.0, 1e-9);
    EXPECT_NEAR(v[1], 0.0, 1e-9);
    EXPECT_NEAR(v[2], 254.0, 1e-6);
    EXPECT_NEAR(v[3], 0.0, 1e-6);
    EXPECT_NEAR(v[4], 254.0, 1e-6);
    EXPECT_NEAR(v[5], 254.0, 1e-6);

    removeFile(path);
}

TEST(UnitTransformTest, ScaleFactorOneIsNoOp)
{
    std::string ents;
    ents += "0\nLINE\n8\n0\n10\n1.0\n20\n2.0\n30\n0.0\n11\n3.0\n21\n4.0\n31\n0.0\n";

    const std::string path = writeDxf("sanyi_scale_identity.dxf", "", "", ents);
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);
    const double x1 = r.entities[0].line.x1;

    ASSERT_TRUE(Fio::scaleParseResult(&r, 1.0));
    EXPECT_DOUBLE_EQ(r.entities[0].line.x1, x1);
    EXPECT_DOUBLE_EQ(r.entities[0].line.x2, 3.0);
    removeFile(path);
}

// ==================== scaleParseResult：SVG 复合曲线（SmartLine） ====================

TEST(UnitTransformTest, ScalesSvgSmartLineSegments)
{
    // 一条多段路径（M→L→L→Z）= 3 段三次贝塞尔，SvgParser 会识别成 SmartLine。
    // nanosvg 线段: p0=(2,2) p1=(6,2)；SVG Y 向下，导入时翻转成 p0=(2,-2) p1=(6,-2)。
    const char* kSvg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">\n"
        "<path d=\"M2,2 L6,2 L6,6 Z\"/>\n"
        "</svg>\n";

    const std::filesystem::path tempFile =
        std::filesystem::temp_directory_path() / "sanyi_scale_svg.svg";
    {
        std::ofstream out(tempFile, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << kSvg;
    }

    Fio::FileIOManager mgr;
    char err[512] = {};
    Fio::FioParseResult r{};
    ASSERT_TRUE(mgr.importToIR(tempFile.string().c_str(), Fio::FileFormat::SVG, &r, err, sizeof(err))) << err;
    ASSERT_EQ(r.entityCount, 1u);
    ASSERT_EQ(r.entities[0].type, Fio::EntityType::SmartLine);

    // 换算前的基点与第一段的 p0/p1（扩展块布局: [标签][p0.x][p0.y][p1.x][p1.y] ...）
    const std::vector<double> before = readBlobVerts(r, r.entities[0]);
    ASSERT_GE(before.size(), 9u);
    const double baseX0 = r.entities[0].line.x1;
    const double baseY0 = r.entities[0].line.y1;
    EXPECT_NEAR(baseX0, 2.0, 1e-6);
    EXPECT_NEAR(before[1], 2.0, 1e-6);
    EXPECT_NEAR(before[2], -2.0, 1e-6);
    EXPECT_NEAR(before[3], 6.0, 1e-6);

    ASSERT_TRUE(Fio::scaleParseResult(&r, 2.0));

    // 基点放大
    EXPECT_NEAR(r.entities[0].line.x1, baseX0 * 2.0, 1e-6);
    EXPECT_NEAR(r.entities[0].line.y1, baseY0 * 2.0, 1e-6);
    // 段坐标（绝对坐标）放大，段数不变
    const std::vector<double> after = readBlobVerts(r, r.entities[0]);
    ASSERT_EQ(after.size(), before.size());
    for (std::size_t i = 0; i < after.size(); ++i)
    {
        EXPECT_NEAR(after[i], before[i] * 2.0, 1e-6);
    }
    // 标签位（下标 0,9,18,...）依然是 Line 枚举值（放大后为 0.0 不变，须核对）
    static_cast<void>(after);

    std::filesystem::remove(tempFile);
}