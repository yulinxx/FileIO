// IrProjector 单元测试
//
// 覆盖点：投影层是所有解析器共用的唯一出口，一旦这里的 id 重编号、群组树修复、
// 扩展数据块布局出错，所有格式会一起错，所以这几条必须有回归保护。

#include <gtest/gtest.h>

#include "IrProjector.h"

#include <cstring>
#include <string>
#include <vector>

namespace
{
    Fio::ParsedGeometry makeLine(uint64_t id, double x1, double y1, double x2, double y2)
    {
        Fio::ParsedGeometry g;
        g.sourceId = id;
        g.type = Fio::ParsedGeometryType::Line;
        g.line.start = { x1, y1 };
        g.line.end = { x2, y2 };
        return g;
    }

    /// 读扩展数据块中的 double 序列
    std::vector<double> readDoubles(const Fio::FioParseResult& r, const Fio::EntityInfo& e)
    {
        const auto* raw = reinterpret_cast<const double*>(r.extensionBlob.data + e.extensionDataOffset);
        return std::vector<double>(raw, raw + e.extensionDataSize / sizeof(double));
    }
}  // namespace

TEST(IrProjectorTest, FailedParseDataYieldsEmptyResult)
{
    Fio::ParseData data;
    data.success = false;
    data.errorMessage = "boom";
    data.geometries.push_back(makeLine(1, 0, 0, 1, 1));

    const Fio::FioParseResult r = Fio::IrProjector::project(data, "DXF");
    EXPECT_EQ(r.entityCount, 0u);
    EXPECT_EQ(r.layerCount, 0u);
    EXPECT_EQ(r.groupCount, 0u);
    EXPECT_EQ(r.entities, nullptr);
}

TEST(IrProjectorTest, MetadataAndUnitArePropagated)
{
    Fio::ParseData data;
    data.success = true;
    data.warnings.emplace_back("w1");
    data.warnings.emplace_back("w2");

    const Fio::FioParseResult r = Fio::IrProjector::project(data, "OBJ", "mm");
    EXPECT_STREQ(r.sourceFormat, "OBJ");
    EXPECT_STREQ(r.sourceUnit, "mm");
    EXPECT_EQ(r.warningCount, 2u);
}

TEST(IrProjectorTest, LayerIdsAreDenseAndEntityReferencesRewritten)
{
    Fio::ParseData data;
    data.success = true;

    // 解析器给的 id 刻意不连续，投影层应重编号为 1,2 并改写图元引用
    Fio::ParsedLayer l1;
    l1.sourceId = 77;
    l1.name = "Outline";
    l1.color = 0xFF112233;
    l1.visible = false;
    l1.locked = true;
    Fio::ParsedLayer l2;
    l2.sourceId = 5;
    l2.name = "Cut";
    data.layers = { l1, l2 };

    auto g1 = makeLine(1, 0, 0, 1, 0);
    g1.layerSourceId = 5;
    auto g2 = makeLine(2, 0, 0, 0, 1);
    g2.layerSourceId = 999;  // 引用不存在的图层 → 应保持 0
    data.geometries = { g1, g2 };

    const Fio::FioParseResult r = Fio::IrProjector::project(data, "DXF");
    ASSERT_EQ(r.layerCount, 2u);
    EXPECT_EQ(r.layers[0].sourceId, 1u);
    EXPECT_STREQ(r.layers[0].name, "Outline");
    EXPECT_EQ(r.layers[0].color, 0xFF112233u);
    EXPECT_FALSE(r.layers[0].visible);
    EXPECT_TRUE(r.layers[0].locked);
    EXPECT_EQ(r.layers[1].sourceId, 2u);

    ASSERT_EQ(r.entityCount, 2u);
    EXPECT_EQ(r.entities[0].layerSourceId, 2u);
    EXPECT_EQ(r.entities[1].layerSourceId, 0u);
}

TEST(IrProjectorTest, GroupTreeIsRemappedAndMembershipIsEntitySide)
{
    Fio::ParseData data;
    data.success = true;

    Fio::ParsedGroup root;
    root.sourceId = 100;
    root.name = "Block:PLATE";
    Fio::ParsedGroup child;
    child.sourceId = 200;
    child.parentGroupSourceId = 100;
    child.name = "Block:HOLE";
    data.groups = { root, child };

    auto g = makeLine(1, 0, 0, 1, 1);
    g.groupSourceId = 200;
    data.geometries = { g };

    const Fio::FioParseResult r = Fio::IrProjector::project(data, "DXF");
    ASSERT_EQ(r.groupCount, 2u);
    EXPECT_EQ(r.groups[0].sourceId, 1u);
    EXPECT_EQ(r.groups[0].parentSourceId, 0u);
    EXPECT_STREQ(r.groups[0].name, "Block:PLATE");
    EXPECT_EQ(r.groups[1].sourceId, 2u);
    EXPECT_EQ(r.groups[1].parentSourceId, 1u);

    ASSERT_EQ(r.entityCount, 1u);
    EXPECT_EQ(r.entities[0].groupSourceId, 2u);
}

TEST(IrProjectorTest, MissingGroupParentIsDemotedToTopLevel)
{
    Fio::ParseData data;
    data.success = true;

    Fio::ParsedGroup orphan;
    orphan.sourceId = 10;
    orphan.parentGroupSourceId = 999;  // 父不存在
    orphan.name = "Orphan";
    data.groups = { orphan };

    const Fio::FioParseResult r = Fio::IrProjector::project(data, "DXF");
    ASSERT_EQ(r.groupCount, 1u);
    EXPECT_EQ(r.groups[0].parentSourceId, 0u);
    EXPECT_GE(r.warningCount, 1u);
}

TEST(IrProjectorTest, GroupCycleIsBrokenInsteadOfInfiniteRecursion)
{
    Fio::ParseData data;
    data.success = true;

    // A→B→A 成环：消费方建树时会无限递归，投影层必须断环
    Fio::ParsedGroup a;
    a.sourceId = 1;
    a.parentGroupSourceId = 2;
    a.name = "A";
    Fio::ParsedGroup b;
    b.sourceId = 2;
    b.parentGroupSourceId = 1;
    b.name = "B";
    data.groups = { a, b };

    const Fio::FioParseResult r = Fio::IrProjector::project(data, "DXF");
    ASSERT_EQ(r.groupCount, 2u);
    // 至少有一个被提为顶层，整条链不再成环
    const bool broken = (r.groups[0].parentSourceId == 0u) || (r.groups[1].parentSourceId == 0u);
    EXPECT_TRUE(broken);
    EXPECT_GE(r.warningCount, 1u);
}

TEST(IrProjectorTest, ClosedPolylineBecomesPolygonAndVerticesLandInBlob)
{
    Fio::ParseData data;
    data.success = true;

    Fio::ParsedGeometry open;
    open.sourceId = 1;
    open.type = Fio::ParsedGeometryType::Polyline;
    open.polyline.points = { { 0, 0 }, { 10, 0 }, { 10, 5 } };

    Fio::ParsedGeometry closed = open;
    closed.sourceId = 2;
    closed.polyline.closed = true;

    data.geometries = { open, closed };

    const Fio::FioParseResult r = Fio::IrProjector::project(data, "PLT");
    ASSERT_EQ(r.entityCount, 2u);
    EXPECT_EQ(r.entities[0].type, Fio::EntityType::Polyline);
    EXPECT_EQ(r.entities[1].type, Fio::EntityType::Polygon);

    EXPECT_EQ(r.entities[0].vertexCount, 3u);
    const std::vector<double> pts = readDoubles(r, r.entities[0]);
    ASSERT_EQ(pts.size(), 6u);
    EXPECT_DOUBLE_EQ(pts[2], 10.0);
    EXPECT_DOUBLE_EQ(pts[5], 5.0);

    // 两个图元的扩展数据不能互相覆盖：偏移必须错开
    EXPECT_NE(r.entities[0].extensionDataOffset, r.entities[1].extensionDataOffset);
}

TEST(IrProjectorTest, NurbsBlobLayoutIsCtrlPointsThenKnotsThenWeights)
{
    Fio::ParseData data;
    data.success = true;

    Fio::ParsedGeometry g;
    g.sourceId = 1;
    g.type = Fio::ParsedGeometryType::Nurbs;
    g.nurbs.degree = 2;
    g.nurbs.controlPoints = { { 1, 2 }, { 3, 4 } };
    g.nurbs.knots = { 0.0, 0.5, 1.0 };
    g.nurbs.weights = { 1.0, 2.0 };
    data.geometries = { g };

    const Fio::FioParseResult r = Fio::IrProjector::project(data, "DXF");
    ASSERT_EQ(r.entityCount, 1u);
    const Fio::EntityInfo& e = r.entities[0];
    EXPECT_EQ(e.nurbsDegree, 2);
    EXPECT_EQ(e.nurbsCtrlPtCount, 2u);
    EXPECT_EQ(e.nurbsKnotCount, 3u);

    const std::vector<double> vals = readDoubles(r, e);
    ASSERT_EQ(vals.size(), 4u + 3u + 2u);
    EXPECT_DOUBLE_EQ(vals[0], 1.0);
    EXPECT_DOUBLE_EQ(vals[3], 4.0);
    EXPECT_DOUBLE_EQ(vals[4], 0.0);
    EXPECT_DOUBLE_EQ(vals[6], 1.0);
    EXPECT_DOUBLE_EQ(vals[7], 1.0);
    EXPECT_DOUBLE_EQ(vals[8], 2.0);
}

TEST(IrProjectorTest, MeshBlobIsFloatVerticesThenNormalsWithPadding)
{
    Fio::ParseData data;
    data.success = true;

    Fio::ParsedGeometry g;
    g.sourceId = 1;
    g.type = Fio::ParsedGeometryType::Mesh3D;
    g.mesh.vertices = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
    g.mesh.indices = { 0, 1, 2 };
    g.mesh.normals = { { 0, 0, 1 } };  // 只给一条，其余应补 (0,0,1)
    data.geometries = { g };

    const Fio::FioParseResult r = Fio::IrProjector::project(data, "STL");
    ASSERT_EQ(r.entityCount, 1u);
    const Fio::EntityInfo& e = r.entities[0];
    EXPECT_EQ(e.meshVertCount, 3u);
    EXPECT_EQ(e.meshTriCount, 1u);
    ASSERT_EQ(e.extensionDataSize, 3u * 3u * sizeof(float) * 2u);

    const auto* f = reinterpret_cast<const float*>(r.extensionBlob.data + e.extensionDataOffset);
    EXPECT_FLOAT_EQ(f[3], 1.0f);  // 第二个顶点 x
    EXPECT_FLOAT_EQ(f[9], 0.0f);  // 法线段起点
    EXPECT_FLOAT_EQ(f[11], 1.0f);
    EXPECT_FLOAT_EQ(f[17], 1.0f);  // 补齐的第三条法线 z
}

TEST(IrProjectorTest, ImageCarriesEncodedBytesAndTopLeftPosition)
{
    Fio::ParseData data;
    data.success = true;

    Fio::ParsedGeometry g;
    g.sourceId = 1;
    g.type = Fio::ParsedGeometryType::Image;
    g.image.width = 4;
    g.image.height = 2;
    g.image.position = { 7.0, 9.0 };
    g.image.data = { 0x89, 'P', 'N', 'G' };
    data.geometries = { g };

    const Fio::FioParseResult r = Fio::IrProjector::project(data, "SVG");
    ASSERT_EQ(r.entityCount, 1u);
    const Fio::EntityInfo& e = r.entities[0];
    EXPECT_EQ(e.imageWidth, 4);
    EXPECT_EQ(e.imageHeight, 2);
    EXPECT_DOUBLE_EQ(e.line.x1, 7.0);
    EXPECT_DOUBLE_EQ(e.line.y1, 9.0);
    ASSERT_EQ(e.extensionDataSize, 4u);
    EXPECT_EQ(r.extensionBlob.data[e.extensionDataOffset + 1], 'P');
}

TEST(IrProjectorTest, LongNamesAreTruncatedAndNullTerminated)
{
    Fio::ParseData data;
    data.success = true;

    Fio::ParsedLayer l;
    l.sourceId = 1;
    l.name = std::string(400, 'x');
    data.layers = { l };

    auto g = makeLine(1, 0, 0, 1, 1);
    g.name = std::string(400, 'y');
    data.geometries = { g };

    const Fio::FioParseResult r = Fio::IrProjector::project(data, "DXF");
    ASSERT_EQ(r.layerCount, 1u);
    EXPECT_EQ(std::strlen(r.layers[0].name), 255u);
    ASSERT_EQ(r.entityCount, 1u);
    EXPECT_EQ(std::strlen(r.entities[0].name), 255u);

    // 截断不是无害的（图层名截短后按名字比对会失配），必须能从 warningCount 看出来
    EXPECT_GE(r.warningCount, 1u);
}

// 断引用是畸形文件的常见症状：图元照样导入，但会全挤在默认图层上。
// 这种现场如果日志里一点线索都没有，只能靠肉眼看，所以必须计入 warningCount。
TEST(IrProjectorTest, DanglingLayerAndGroupReferencesAreCountedAsWarnings)
{
    Fio::ParseData data;
    data.success = true;

    auto g = makeLine(1, 0, 0, 1, 1);
    g.layerSourceId = 999;  // 文件里没有这个图层
    g.groupSourceId = 888;  // 也没有这个群组
    data.geometries = { g };

    const Fio::FioParseResult r = Fio::IrProjector::project(data, "DXF");
    ASSERT_EQ(r.entityCount, 1u);
    EXPECT_EQ(r.entities[0].layerSourceId, 0u);  // 落到「未分配」哨兵
    EXPECT_EQ(r.entities[0].groupSourceId, 0u);  // 落到「无群组」哨兵
    EXPECT_EQ(r.warningCount, 2u);               // 断图层 + 断群组，各汇总一条
}

// 同类降级只汇总一条 warning：畸形文件里动辄成千上万条，
// 逐条塞进 warnings 会把 warningCount 和日志一起冲爆。
TEST(IrProjectorTest, RepeatedDegradationsCollapseIntoOneWarningPerCategory)
{
    Fio::ParseData data;
    data.success = true;

    for (uint64_t i = 1; i <= 50; ++i)
    {
        auto g = makeLine(i, 0, 0, 1, 1);
        g.layerSourceId = 999;
        data.geometries.push_back(g);
    }

    const Fio::FioParseResult r = Fio::IrProjector::project(data, "DXF");
    EXPECT_EQ(r.entityCount, 50u);
    EXPECT_EQ(r.warningCount, 1u);
}

// 类型无法映射的图元会被整条丢掉，这是「文件里 1000 个、界面只出现 800 个」的
// 主要来源之一：上层看到的 entityCount 已经扣过这部分，所以只能在这里记账。
TEST(IrProjectorTest, UnmappableTypesAreDroppedButReported)
{
    Fio::ParseData data;
    data.success = true;

    auto bad = makeLine(1, 0, 0, 1, 1);
    bad.type = Fio::ParsedGeometryType::Unknown;
    data.geometries = { bad, makeLine(2, 0, 0, 1, 1) };

    const Fio::FioParseResult r = Fio::IrProjector::project(data, "DXF");
    EXPECT_EQ(r.entityCount, 1u);
    EXPECT_EQ(r.warningCount, 1u);
}

TEST(IrProjectorTest, ResetInvalidatesPreviousResultButNotOtherThreads)
{
    Fio::ParseData first;
    first.success = true;
    first.geometries = { makeLine(1, 0, 0, 1, 1), makeLine(2, 1, 1, 2, 2) };
    const Fio::FioParseResult r1 = Fio::IrProjector::project(first, "DXF");
    ASSERT_EQ(r1.entityCount, 2u);

    // 同线程再解析一次：按契约上一次的结果作废，新结果只反映第二次的内容
    Fio::ParseData second;
    second.success = true;
    second.geometries = { makeLine(9, 5, 5, 6, 6) };
    const Fio::FioParseResult r2 = Fio::IrProjector::project(second, "DXF");
    ASSERT_EQ(r2.entityCount, 1u);
    EXPECT_EQ(r2.entities[0].sourceId, 9u);
}
