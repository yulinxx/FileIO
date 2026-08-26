// DXF 实体级导入测试（非块引用部分）
//
// 覆盖 Phase C-2 补齐的几项：bulge 圆弧、闭合标志、OCS→WCS 挤出方向、$INSUNITS 单位、
// SOLID/TRACE/3DFACE 四边形、拟合点式 SPLINE 降级、未支持实体是否留下 warning。

#include <gtest/gtest.h>

#include "DxfTestUtils.h"
#include "FileIO/Parsers/DxfParser.h"

#include <cmath>
#include <string>
#include <vector>

using DxfTest::removeFile;
using DxfTest::writeDxf;

namespace
{
    std::vector<double> readVerts(const Fio::FioParseResult& r, const Fio::EntityInfo& e)
    {
        const auto* raw = reinterpret_cast<const double*>(r.extensionBlob.data + e.extensionDataOffset);
        return std::vector<double>(raw, raw + e.extensionDataSize / sizeof(double));
    }
}  // namespace

TEST(DxfEntityTest, LwPolylineClosedFlagBecomesPolygon)
{
    // flags=1 表示闭合。早先这个标志没读，闭合轮廓导入后会缺最后一段。
    std::string ents;
    ents += "0\nLWPOLYLINE\n8\n0\n90\n3\n70\n1\n";
    ents += "10\n0.0\n20\n0.0\n10\n10.0\n20\n0.0\n10\n10.0\n20\n10.0\n";

    const std::string path = writeDxf("sanyi_dxf_lwpl_closed.dxf", "", ents);
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);
    EXPECT_EQ(r.entities[0].type, Fio::EntityType::Polygon);
    EXPECT_EQ(r.entities[0].vertexCount, 3u);
    removeFile(path);
}

TEST(DxfEntityTest, LwPolylineOpenStaysPolyline)
{
    std::string ents;
    ents += "0\nLWPOLYLINE\n8\n0\n90\n3\n70\n0\n";
    ents += "10\n0.0\n20\n0.0\n10\n10.0\n20\n0.0\n10\n10.0\n20\n10.0\n";

    const std::string path = writeDxf("sanyi_dxf_lwpl_open.dxf", "", ents);
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);
    EXPECT_EQ(r.entities[0].type, Fio::EntityType::Polyline);
    removeFile(path);
}

TEST(DxfEntityTest, BulgeSegmentIsTessellatedIntoArcPoints)
{
    // 两点一段，bulge=1 表示半圆。
    // 方向约定：正 bulge = 从起点到终点**逆时针**。对于 (0,0)→(10,0) 这条向右的弦，
    // 逆时针意味着路径先向下再回到弦线（左转），因此中间点应落在 y<0 一侧，
    // 且都位于以 (5,0) 为心、半径 5 的圆上。
    std::string ents;
    ents += "0\nLWPOLYLINE\n8\n0\n90\n2\n70\n0\n";
    ents += "10\n0.0\n20\n0.0\n42\n1.0\n";
    ents += "10\n10.0\n20\n0.0\n";

    const std::string path = writeDxf("sanyi_dxf_bulge.dxf", "", ents);
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);

    const std::vector<double> v = readVerts(r, r.entities[0]);
    // 起点 + 中间点 + 终点，半圆按 6° 步长细分约 30 段
    ASSERT_GT(v.size(), 6u);
    EXPECT_NEAR(v.front(), 0.0, 1e-9);
    EXPECT_NEAR(v[v.size() - 2], 10.0, 1e-9);

    bool anyOffChord = false;
    for (size_t i = 2; i + 2 < v.size(); i += 2)
    {
        const double dx = v[i] - 5.0;
        const double dy = v[i + 1];
        EXPECT_NEAR(std::sqrt(dx * dx + dy * dy), 5.0, 1e-6);
        if (dy < -0.1)
        {
            anyOffChord = true;
        }
    }
    EXPECT_TRUE(anyOffChord);
    removeFile(path);
}

TEST(DxfEntityTest, NegativeBulgeMirrorsArcToTheOtherSide)
{
    // 负 bulge = 顺时针，同一条弦上圆弧应鼓向相反一侧（y>0）
    std::string ents;
    ents += "0\nLWPOLYLINE\n8\n0\n90\n2\n70\n0\n";
    ents += "10\n0.0\n20\n0.0\n42\n-1.0\n";
    ents += "10\n10.0\n20\n0.0\n";

    const std::string path = writeDxf("sanyi_dxf_bulge_neg.dxf", "", ents);
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);

    const std::vector<double> v = readVerts(r, r.entities[0]);
    ASSERT_GT(v.size(), 6u);
    bool anyAbove = false;
    for (size_t i = 2; i + 2 < v.size(); i += 2)
    {
        const double dx = v[i] - 5.0;
        const double dy = v[i + 1];
        EXPECT_NEAR(std::sqrt(dx * dx + dy * dy), 5.0, 1e-6);
        if (dy > 0.1)
        {
            anyAbove = true;
        }
    }
    EXPECT_TRUE(anyAbove);
    removeFile(path);
}


TEST(DxfEntityTest, NegativeExtrusionMirrorsCircleCenter)
{
    // 挤出方向 (0,0,-1) 是 AutoCAD 里镜像过的图元；按 OCS→WCS 换算后 X 取反。
    // 早先完全没读挤出方向，这类图元导入后左右颠倒。
    std::string ents;
    ents += "0\nCIRCLE\n8\n0\n10\n7.0\n20\n3.0\n30\n0.0\n40\n2.0\n210\n0.0\n220\n0.0\n230\n-1.0\n";

    const std::string path = writeDxf("sanyi_dxf_ocs_circle.dxf", "", ents);
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);
    EXPECT_EQ(r.entities[0].type, Fio::EntityType::Circle);
    EXPECT_NEAR(r.entities[0].circle.cx, -7.0, 1e-9);
    EXPECT_NEAR(r.entities[0].circle.cy, 3.0, 1e-9);
    EXPECT_NEAR(r.entities[0].circle.r, 2.0, 1e-9);
    removeFile(path);
}

TEST(DxfEntityTest, PositiveExtrusionLeavesCoordinatesUntouched)
{
    std::string ents;
    ents += "0\nCIRCLE\n8\n0\n10\n7.0\n20\n3.0\n30\n0.0\n40\n2.0\n210\n0.0\n220\n0.0\n230\n1.0\n";

    const std::string path = writeDxf("sanyi_dxf_ocs_normal.dxf", "", ents);
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);
    EXPECT_NEAR(r.entities[0].circle.cx, 7.0, 1e-9);
    removeFile(path);
}

TEST(DxfEntityTest, InsUnitsIsReportedAsSourceUnit)
{
    // $INSUNITS=1 表示英寸；早先 addHeader 是空实现，sourceUnit 永远为空
    const std::string header = "9\n$INSUNITS\n70\n1\n";
    const std::string path = writeDxf("sanyi_dxf_units_inch.dxf", header, "", "");
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    EXPECT_STREQ(r.sourceUnit, "inch");
    removeFile(path);
}

TEST(DxfEntityTest, InsUnitsMillimetreIsRecognised)
{
    const std::string header = "9\n$INSUNITS\n70\n4\n";
    const std::string path = writeDxf("sanyi_dxf_units_mm.dxf", header, "", "");
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    EXPECT_STREQ(r.sourceUnit, "mm");
    removeFile(path);
}

TEST(DxfEntityTest, UnitlessInsUnitsLeavesUnitEmpty)
{
    const std::string header = "9\n$INSUNITS\n70\n0\n";
    const std::string path = writeDxf("sanyi_dxf_units_none.dxf", header, "", "");
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    EXPECT_STREQ(r.sourceUnit, "");
    removeFile(path);
}

TEST(DxfEntityTest, SolidBecomesClosedQuadWithCorrectedVertexOrder)
{
    // DXF 的 SOLID 角点顺序是 1-2-4-3；按 1-2-3-4 连线会得到自交的蝴蝶结。
    // 这里给一个 10x10 正方形：角点 1(0,0) 2(10,0) 3(0,10) 4(10,10)
    std::string ents;
    ents += "0\nSOLID\n8\n0\n";
    ents += "10\n0.0\n20\n0.0\n30\n0.0\n";
    ents += "11\n10.0\n21\n0.0\n31\n0.0\n";
    ents += "12\n0.0\n22\n10.0\n32\n0.0\n";
    ents += "13\n10.0\n23\n10.0\n33\n0.0\n";

    const std::string path = writeDxf("sanyi_dxf_solid.dxf", "", ents);
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);
    EXPECT_EQ(r.entities[0].type, Fio::EntityType::Polygon);
    ASSERT_EQ(r.entities[0].vertexCount, 4u);

    const std::vector<double> v = readVerts(r, r.entities[0]);
    // 期望顺序 (0,0) (10,0) (10,10) (0,10)：相邻边必须是轴对齐的，长度都为 10
    ASSERT_EQ(v.size(), 8u);
    EXPECT_NEAR(v[4], 10.0, 1e-9);
    EXPECT_NEAR(v[5], 10.0, 1e-9);
    EXPECT_NEAR(v[6], 0.0, 1e-9);
    EXPECT_NEAR(v[7], 10.0, 1e-9);
    removeFile(path);
}

TEST(DxfEntityTest, FitPointSplineDegradesToPolylineWithWarning)
{
    // 只给拟合点（组码 11/21）、不给控制点：早先整条被丢弃
    std::string ents;
    ents += "0\nSPLINE\n8\n0\n70\n8\n71\n3\n";
    ents += "11\n0.0\n21\n0.0\n31\n0.0\n";
    ents += "11\n5.0\n21\n5.0\n31\n0.0\n";
    ents += "11\n10.0\n21\n0.0\n31\n0.0\n";

    const std::string path = writeDxf("sanyi_dxf_spline_fit.dxf", "", ents);
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);
    EXPECT_EQ(r.entities[0].type, Fio::EntityType::Polyline);
    EXPECT_EQ(r.entities[0].vertexCount, 3u);
    EXPECT_GE(r.warningCount, 1u);
    removeFile(path);
}

TEST(DxfEntityTest, UnsupportedEntityLeavesWarningInsteadOfSilentDrop)
{
    // HATCH 不导入，但必须留下 warning，否则用户看不出图纸里有内容丢了
    std::string ents;
    ents += "0\nHATCH\n8\n0\n10\n0.0\n20\n0.0\n30\n0.0\n2\nSOLID\n70\n1\n91\n0\n";

    const std::string path = writeDxf("sanyi_dxf_hatch.dxf", "", ents);
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    EXPECT_EQ(r.entityCount, 0u);
    EXPECT_GE(r.warningCount, 1u);
    removeFile(path);
}
