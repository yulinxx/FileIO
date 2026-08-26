// DXF 块引用（BLOCK/INSERT）导入测试
//
// 这批测试守的是三条容易回归的规则：
//   1. 块定义里的图元不得直接出现在模型空间（早先 addBlock/endBlock 是空实现，会漏出来）；
//   2. INSERT 必须实例化出图元，并按 平移/旋转/缩放/阵列 正确变换；
//   3. 每个实例落在一个群组里，嵌套块形成群组父子链。

#include <gtest/gtest.h>

#include "DxfTestUtils.h"
#include "FileIO/Parsers/DxfParser.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using DxfTest::blockWithLine;
using DxfTest::insertOf;
using DxfTest::removeFile;
using DxfTest::writeDxf;


TEST(DxfBlockTest, BlockDefinitionEntitiesDoNotLeakIntoModelSpace)
{
    // 只有块定义、没有任何 INSERT：模型空间应当一个图元都没有
    const std::string path = writeDxf("sanyi_dxf_block_unused.dxf", blockWithLine("UNUSED"), "");
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    EXPECT_EQ(r.entityCount, 0u);
    EXPECT_EQ(r.groupCount, 0u);
    removeFile(path);
}

TEST(DxfBlockTest, InsertInstantiatesTranslatedEntitiesInsideGroup)
{
    const std::string path =
        writeDxf("sanyi_dxf_block_insert.dxf", blockWithLine("BLK"), insertOf("BLK", 100.0, 50.0));
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);
    ASSERT_EQ(r.groupCount, 1u);

    const Fio::EntityInfo& e = r.entities[0];
    EXPECT_EQ(e.type, Fio::EntityType::Line);
    EXPECT_NEAR(e.line.x1, 100.0, 1e-9);
    EXPECT_NEAR(e.line.y1, 50.0, 1e-9);
    EXPECT_NEAR(e.line.x2, 110.0, 1e-9);
    EXPECT_NEAR(e.line.y2, 50.0, 1e-9);

    // 实例化出的图元必须归到该次引用的群组下
    EXPECT_EQ(e.groupSourceId, r.groups[0].sourceId);
    EXPECT_EQ(r.groups[0].parentSourceId, 0u);
    removeFile(path);
}

TEST(DxfBlockTest, InsertAppliesBasePointScaleAndRotation)
{
    // 基点 (10,0)：块内直线 (0,0)-(10,0) 先平移成 (-10,0)-(0,0)
    // 再 2 倍缩放 → (-20,0)-(0,0)，再旋转 90° → (0,-20)-(0,0)，最后平移到 (5,5)
    const std::string path = writeDxf("sanyi_dxf_block_xform.dxf",
        blockWithLine("BLK", 10.0, 0.0),
        insertOf("BLK", 5.0, 5.0, 2.0, 2.0, 90.0));
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);

    const Fio::EntityInfo& e = r.entities[0];
    EXPECT_NEAR(e.line.x1, 5.0, 1e-6);
    EXPECT_NEAR(e.line.y1, -15.0, 1e-6);
    EXPECT_NEAR(e.line.x2, 5.0, 1e-6);
    EXPECT_NEAR(e.line.y2, 5.0, 1e-6);
    removeFile(path);
}

TEST(DxfBlockTest, InsertArrayProducesOneGroupPerCell)
{
    // 2 列 x 3 行，列距 20、行距 30
    const std::string path = writeDxf("sanyi_dxf_block_array.dxf",
        blockWithLine("BLK"),
        insertOf("BLK", 0.0, 0.0, 1.0, 1.0, 0.0, 2, 3, 20.0, 30.0));
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    EXPECT_EQ(r.entityCount, 6u);
    EXPECT_EQ(r.groupCount, 6u);

    // 每个单元格的起点应落在 (col*20, row*30)
    std::vector<std::pair<double, double>> starts;
    for (uint32_t i = 0; i < r.entityCount; ++i)
    {
        starts.emplace_back(r.entities[i].line.x1, r.entities[i].line.y1);
    }
    const bool hasCorner = std::any_of(starts.begin(), starts.end(), [](const auto& p) {
        return std::fabs(p.first - 20.0) < 1e-6 && std::fabs(p.second - 60.0) < 1e-6;
    });
    EXPECT_TRUE(hasCorner);
    removeFile(path);
}

TEST(DxfBlockTest, NestedBlockFormsGroupHierarchy)
{
    // OUTER 内部引用 INNER：展开后应出现父子两级群组
    std::string blocks = blockWithLine("INNER");
    blocks += "0\nBLOCK\n2\nOUTER\n70\n0\n10\n0.0\n20\n0.0\n30\n0.0\n3\nOUTER\n1\n\n";
    blocks += insertOf("INNER", 5.0, 0.0);
    blocks += "0\nENDBLK\n";

    const std::string path = writeDxf("sanyi_dxf_block_nested.dxf", blocks, insertOf("OUTER", 1000.0, 0.0));
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);
    ASSERT_EQ(r.groupCount, 2u);

    // 顶层群组（OUTER）无父，子群组（INNER）指向它
    EXPECT_EQ(r.groups[0].parentSourceId, 0u);
    EXPECT_EQ(r.groups[1].parentSourceId, r.groups[0].sourceId);

    // 坐标：INNER 内直线起点 (0,0) → OUTER 内偏移 (5,0) → 模型空间 (1005,0)
    EXPECT_NEAR(r.entities[0].line.x1, 1005.0, 1e-6);
    EXPECT_NEAR(r.entities[0].line.y1, 0.0, 1e-6);
    EXPECT_EQ(r.entities[0].groupSourceId, r.groups[1].sourceId);
    removeFile(path);
}

TEST(DxfBlockTest, InsertOfUndefinedBlockWarnsAndProducesNothing)
{
    const std::string path = writeDxf("sanyi_dxf_block_missing.dxf", "", insertOf("NOPE", 0.0, 0.0));
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    EXPECT_EQ(r.entityCount, 0u);
    EXPECT_GE(r.warningCount, 1u);
    removeFile(path);
}

TEST(DxfBlockTest, ModelSpaceEntitiesStillImportAlongsideBlocks)
{
    // 块定义与模型空间实体混排时，两者都不能互相吞掉
    std::string entities = insertOf("BLK", 0.0, 0.0);
    entities += "0\nCIRCLE\n8\n0\n10\n7.0\n20\n8.0\n30\n0.0\n40\n3.0\n";

    const std::string path = writeDxf("sanyi_dxf_block_mixed.dxf", blockWithLine("BLK"), entities);
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 2u);

    bool hasCircle = false;
    bool hasLine = false;
    for (uint32_t i = 0; i < r.entityCount; ++i)
    {
        if (r.entities[i].type == Fio::EntityType::Circle)
        {
            hasCircle = true;
            // 模型空间实体不属于任何群组
            EXPECT_EQ(r.entities[i].groupSourceId, 0u);
            EXPECT_NEAR(r.entities[i].circle.r, 3.0, 1e-9);
        }
        if (r.entities[i].type == Fio::EntityType::Line)
        {
            hasLine = true;
            EXPECT_NE(r.entities[i].groupSourceId, 0u);
        }
    }
    EXPECT_TRUE(hasCircle);
    EXPECT_TRUE(hasLine);
    removeFile(path);
}

TEST(DxfBlockTest, MirroredInsertKeepsCircleAsCircle)
{
    // 负缩放（镜像）是等比相似变换，圆仍应是圆，不能被升格成椭圆
    std::string blocks;
    blocks += "0\nBLOCK\n2\nCIR\n70\n0\n10\n0.0\n20\n0.0\n30\n0.0\n3\nCIR\n1\n\n";
    blocks += "0\nCIRCLE\n8\n0\n10\n4.0\n20\n0.0\n30\n0.0\n40\n2.0\n";
    blocks += "0\nENDBLK\n";

    const std::string path =
        writeDxf("sanyi_dxf_block_mirror.dxf", blocks, insertOf("CIR", 0.0, 0.0, -1.0, 1.0));
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);
    EXPECT_EQ(r.entities[0].type, Fio::EntityType::Circle);
    EXPECT_NEAR(r.entities[0].circle.cx, -4.0, 1e-9);
    EXPECT_NEAR(r.entities[0].circle.r, 2.0, 1e-9);
    removeFile(path);
}

TEST(DxfBlockTest, NonUniformScaleTurnsCircleIntoEllipse)
{
    std::string blocks;
    blocks += "0\nBLOCK\n2\nCIR\n70\n0\n10\n0.0\n20\n0.0\n30\n0.0\n3\nCIR\n1\n\n";
    blocks += "0\nCIRCLE\n8\n0\n10\n0.0\n20\n0.0\n30\n0.0\n40\n2.0\n";
    blocks += "0\nENDBLK\n";

    const std::string path =
        writeDxf("sanyi_dxf_block_ellipse.dxf", blocks, insertOf("CIR", 0.0, 0.0, 3.0, 1.0));
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);
    EXPECT_EQ(r.entities[0].type, Fio::EntityType::Ellipse);
    EXPECT_NEAR(r.entities[0].ellipse.rx, 6.0, 1e-9);
    EXPECT_NEAR(r.entities[0].ellipse.ry, 2.0, 1e-9);
    removeFile(path);
}
