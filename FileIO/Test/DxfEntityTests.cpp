// DXF 图元级导入测试（非块引用部分）
//
// 覆盖 Phase C-2 补齐的几项：bulge 圆弧、闭合标志、OCS→WCS 挤出方向、$INSUNITS 单位、
// SOLID/TRACE/3DFACE 四边形、拟合点式 SPLINE 降级、未支持图元是否留下 warning。

#include <gtest/gtest.h>

#include "DxfTestUtils.h"
#include "FileIO/Parsers/DxfParser.h"
#include "FileIO/Writers/DxfFileWriter.h"
#include "Engine2D/SyEntity/SyPolygon.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
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

    std::vector<double> readBulges(const Fio::FioParseResult& r, const Fio::EntityInfo& e)
    {
        if (e.bulgeDataSize == 0)
        {
            return {};
        }
        const auto* raw = reinterpret_cast<const double*>(r.extensionBlob.data + e.bulgeDataOffset);
        return std::vector<double>(raw, raw + e.bulgeDataSize / sizeof(double));
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

TEST(DxfEntityTest, BulgeIsPreservedInIrWithoutTessellation)
{
    // 两点一段，bulge=1 表示半圆。
    // 导入期**不再**把圆弧烘焙成折线：IR 只保留 2 个控制点 + 逐段 bulge，
    // 圆弧由 Engine 按当前缩放自适应展开。这样极端放大也不会露出折线。
    std::string ents;
    ents += "0\nLWPOLYLINE\n8\n0\n90\n2\n70\n0\n";
    ents += "10\n0.0\n20\n0.0\n42\n1.0\n";
    ents += "10\n10.0\n20\n0.0\n";

    const std::string path = writeDxf("sanyi_dxf_bulge.dxf", "", ents);
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);

    const Fio::EntityInfo& info = r.entities[0];
    EXPECT_EQ(info.vertexCount, 2u);

    const std::vector<double> v = readVerts(r, info);
    ASSERT_EQ(v.size(), 4u);
    EXPECT_NEAR(v[0], 0.0, 1e-9);
    EXPECT_NEAR(v[1], 0.0, 1e-9);
    EXPECT_NEAR(v[2], 10.0, 1e-9);
    EXPECT_NEAR(v[3], 0.0, 1e-9);

    const std::vector<double> b = readBulges(r, info);
    ASSERT_EQ(b.size(), 2u);
    EXPECT_NEAR(b[0], 1.0, 1e-12);
    // 开放折线末点没有下一顶点，最后一个 bulge 不参与几何，导入时清零。
    EXPECT_NEAR(b[1], 0.0, 1e-12);
    removeFile(path);
}

TEST(DxfEntityTest, NegativeBulgeIsPreservedWithSign)
{
    // 负 bulge = 顺时针。符号必须原样保留，否则圆弧会鼓向反侧。
    std::string ents;
    ents += "0\nLWPOLYLINE\n8\n0\n90\n2\n70\n0\n";
    ents += "10\n0.0\n20\n0.0\n42\n-1.0\n";
    ents += "10\n10.0\n20\n0.0\n";

    const std::string path = writeDxf("sanyi_dxf_bulge_neg.dxf", "", ents);
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);

    const Fio::EntityInfo& info = r.entities[0];
    EXPECT_EQ(info.vertexCount, 2u);
    const std::vector<double> b = readBulges(r, info);
    ASSERT_EQ(b.size(), 2u);
    EXPECT_NEAR(b[0], -1.0, 1e-12);
    removeFile(path);
}

TEST(DxfEntityTest, BulgeRoundTripsThroughDxfWriter)
{
    // 导出必须写组码 42，否则再导入时圆弧会退化成直线弦。
    auto poly = std::make_unique<Eg::SyPolygon>();
    poly->bClosed = false;
    poly->setVerticesWithBulges({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 0.0) }, { 1.0, 0.0 });

    Fio::VecSyEntityPtr entities;
    entities.push_back(std::move(poly));

    const std::string path =
        (std::filesystem::temp_directory_path() / "sanyi_dxf_bulge_roundtrip.dxf").string();
    Fio::DxfFileWriter writer;
    const Fio::WriteResult wr = writer.write(path.c_str(), entities);
    ASSERT_TRUE(wr.success) << wr.errorMessage;

    std::ifstream in(path, std::ios::binary);
    ASSERT_TRUE(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    // 组码 42 后跟 bulge 值（writer 用 15 位有效数字输出）。
    EXPECT_NE(text.find("\n42\n1\n"), std::string::npos);
    EXPECT_NE(text.find("\n90\n2\n"), std::string::npos);
    EXPECT_NE(text.find("\n70\n0\n"), std::string::npos);

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);
    const std::vector<double> b = readBulges(r, r.entities[0]);
    ASSERT_EQ(b.size(), 2u);
    EXPECT_NEAR(b[0], 1.0, 1e-12);
    removeFile(path);
}

TEST(DxfEntityTest, NegativeExtrusionMirrorsBulgeDirection)
{
    // OCS→WCS 含镜像（extrusion = -Z）时圆弧方向翻转，bulge 必须取反，
    // 否则镜像后的零件圆角会朝反侧鼓出。
    std::string ents;
    ents += "0\nLWPOLYLINE\n8\n0\n90\n2\n70\n0\n";
    ents += "10\n0.0\n20\n0.0\n42\n1.0\n";
    ents += "10\n10.0\n20\n0.0\n";
    ents += "210\n0.0\n220\n0.0\n230\n-1.0\n";

    const std::string path = writeDxf("sanyi_dxf_bulge_ocs_mirror.dxf", "", ents);
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);

    const Fio::EntityInfo& info = r.entities[0];
    const std::vector<double> v = readVerts(r, info);
    ASSERT_EQ(v.size(), 4u);
    EXPECT_NEAR(v[0], 0.0, 1e-9);
    EXPECT_NEAR(v[2], -10.0, 1e-9);

    const std::vector<double> b = readBulges(r, info);
    ASSERT_EQ(b.size(), 2u);
    EXPECT_NEAR(b[0], -1.0, 1e-12);
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
// ==================== 控制点式 SPLINE ====================

TEST(DxfEntityTest, ControlPointSplineBecomesNurbs)
{
    // 控制点式样条（组码 72/73 给节点数与控制点数，控制点用 10/20）：
    // 与拟合点式（11/21）不同，这条路径要保留 NURBS 的次数/节点/权重。
    // 控制点必须是「次数+1」个：3 阶至少 4 个控制点（节点数 = 控制点数 + 次数 + 1 = 8）。
    std::string ents;
    ents += "0\nSPLINE\n8\n0\n70\n0\n71\n3\n72\n8\n73\n4\n";
    ents += "40\n0.0\n40\n0.0\n40\n0.0\n40\n0.0\n40\n1.0\n40\n1.0\n40\n1.0\n40\n1.0\n";
    ents += "10\n0.0\n20\n0.0\n";
    ents += "10\n5.0\n20\n10.0\n";
    ents += "10\n10.0\n20\n0.0\n";
    ents += "10\n15.0\n20\n10.0\n";

    const std::string path = writeDxf("sanyi_dxf_spline_ctrl.dxf", "", ents);
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);
    EXPECT_EQ(r.entities[0].type, Fio::EntityType::Nurbs);
    EXPECT_EQ(r.entities[0].nurbsDegree, 3);
    EXPECT_EQ(r.entities[0].nurbsCtrlPtCount, 4u);
    EXPECT_EQ(r.entities[0].nurbsKnotCount, 8u);

    // 扩展数据布局：[控制点 x,y*N][节点][权重] —— 4 个控制点(8) + 8 个节点 = 16 个 double
    const std::vector<double> v = readVerts(r, r.entities[0]);
    ASSERT_EQ(v.size(), 16u);
    EXPECT_NEAR(v[0], 0.0, 1e-9);
    EXPECT_NEAR(v[1], 0.0, 1e-9);
    EXPECT_NEAR(v[2], 5.0, 1e-9);
    EXPECT_NEAR(v[3], 10.0, 1e-9);
    EXPECT_NEAR(v[4], 10.0, 1e-9);
    EXPECT_NEAR(v[5], 0.0, 1e-9);
    EXPECT_NEAR(v[6], 15.0, 1e-9);
    EXPECT_NEAR(v[7], 10.0, 1e-9);
    // 节点向量：4 个 0 + 4 个 1
    for (size_t i = 0; i < 4; ++i)
    {
        EXPECT_NEAR(v[8 + i], 0.0, 1e-9);
    }
    for (size_t i = 0; i < 4; ++i)
    {
        EXPECT_NEAR(v[12 + i], 1.0, 1e-9);
    }
    removeFile(path);
}

TEST(DxfEntityTest, DegenerateSplineIsSkippedButRestOfFileSurvives)
{
    // 控制点数 < 次数+1 的退化样条：结构非法，libdxfrw 会拒收。
    // 要求：只丢这一条实体，后面的图元照常导入，并且留下 warning。
    // （早先是整段 ENTITIES 中止 —— 一条坏样条 = 整张图读不出来，且无任何提示。）
    std::string ents;
    ents += "0\nSPLINE\n8\n0\n70\n0\n71\n3\n72\n7\n73\n3\n";
    ents += "40\n0.0\n40\n0.0\n40\n0.0\n40\n0.5\n40\n1.0\n40\n1.0\n40\n1.0\n";
    ents += "10\n0.0\n20\n0.0\n";
    ents += "10\n5.0\n20\n10.0\n";
    ents += "10\n10.0\n20\n0.0\n";
    ents += "0\nLINE\n8\n0\n10\n0.0\n20\n0.0\n30\n0.0\n11\n10.0\n21\n0.0\n31\n0.0\n";

    const std::string path = writeDxf("sanyi_dxf_spline_degenerate.dxf", "", ents);
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);
    EXPECT_EQ(r.entities[0].type, Fio::EntityType::Line);
    EXPECT_GE(r.warningCount, 1u);
    removeFile(path);
}

TEST(DxfEntityTest, EntitiesOnlyDxfWithoutHeaderIsParsed)
{
    // 只有 ENTITIES 段、没有 HEADER/TABLES/BLOCKS 的极简 DXF：
    // 现场导出的图纸里这类文件很常见，不能因为少了表格段就整份读不出图元。
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "sanyi_dxf_entities_only.dxf";
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.good());
        out << "0\nSECTION\n2\nENTITIES\n";
        out << "0\nLINE\n8\n0\n10\n0.0\n20\n0.0\n30\n0.0\n11\n10.0\n21\n0.0\n31\n0.0\n";
        out << "0\nENDSEC\n0\nEOF\n";
    }

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.string().c_str());
    EXPECT_EQ(r.entityCount, 1u);
    removeFile(path.string());
}