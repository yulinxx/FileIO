// OBJ / STL 导入测试
//
// 关注点：3D 两种格式现在都走 FileIO 的同一条 IR 出口（OBJ 经 IrProjector，
// STL 经 IrPublisher），因此扩展数据布局必须一致；OBJ 的 o/g/usemtl 要落成群组。

#include <gtest/gtest.h>

#include "FileIO/Parsers/ObjParser.h"
#include "FileIO/Parsers/StlParser.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    std::string writeText(const std::string& fileName, const std::string& content)
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / fileName;
        std::ofstream out(path, std::ios::binary);
        if (!out)
        {
            return {};
        }
        out << content;
        out.close();
        return path.string();
    }

    void removeFile(const std::string& path)
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    /// 按 Mesh3D 的扩展数据布局读顶点段
    std::vector<float> readMeshFloats(const Fio::FioParseResult& r, const Fio::EntityInfo& e)
    {
        const auto* raw = reinterpret_cast<const float*>(r.extensionBlob.data + e.extensionDataOffset);
        return std::vector<float>(raw, raw + e.extensionDataSize / sizeof(float));
    }
}  // namespace

TEST(ObjParserTest, SingleTriangleProducesOneMeshWithoutGroups)
{
    const std::string obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    const std::string path = writeText("sanyi_obj_tri.obj", obj);
    ASSERT_FALSE(path.empty());

    Fio::ObjParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);
    EXPECT_STREQ(r.sourceFormat, "OBJ");
    EXPECT_EQ(r.entities[0].type, Fio::EntityType::Mesh3D);
    EXPECT_EQ(r.entities[0].meshTriCount, 1u);
    EXPECT_EQ(r.entities[0].meshVertCount, 3u);
    // 没有 o/g/usemtl 的单体模型不套群组壳
    EXPECT_EQ(r.groupCount, 0u);
    removeFile(path);
}

TEST(ObjParserTest, MeshBlobLayoutMatchesVerticesThenNormals)
{
    const std::string obj = "v 0 0 0\nv 2 0 0\nv 0 2 0\nf 1 2 3\n";
    const std::string path = writeText("sanyi_obj_layout.obj", obj);
    ASSERT_FALSE(path.empty());

    Fio::ObjParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);
    const Fio::EntityInfo& e = r.entities[0];

    // 3 顶点 * 3 分量 * (顶点段 + 法线段)
    ASSERT_EQ(e.extensionDataSize, 3u * 3u * sizeof(float) * 2u);
    const std::vector<float> f = readMeshFloats(r, e);
    EXPECT_FLOAT_EQ(f[3], 2.0f);  // 第二个顶点 x
    EXPECT_FLOAT_EQ(f[7], 2.0f);  // 第三个顶点 y

    // 法线段：三角形位于 XY 平面，法线应为 (0,0,±1)
    EXPECT_NEAR(std::fabs(f[11]), 1.0f, 1e-6);
    removeFile(path);
}

TEST(ObjParserTest, ObjectsBecomeTopLevelGroups)
{
    std::string obj;
    obj += "o PartA\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    obj += "o PartB\nv 5 0 0\nv 6 0 0\nv 5 1 0\nf 4 5 6\n";

    const std::string path = writeText("sanyi_obj_objects.obj", obj);
    ASSERT_FALSE(path.empty());

    Fio::ObjParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 2u);
    ASSERT_EQ(r.groupCount, 2u);
    EXPECT_STREQ(r.groups[0].name, "PartA");
    EXPECT_STREQ(r.groups[1].name, "PartB");
    EXPECT_EQ(r.groups[0].parentSourceId, 0u);
    EXPECT_EQ(r.groups[1].parentSourceId, 0u);
    EXPECT_NE(r.entities[0].groupSourceId, r.entities[1].groupSourceId);
    removeFile(path);
}

TEST(ObjParserTest, MaterialSwitchCreatesChildGroupUnderObject)
{
    std::string obj;
    obj += "o Panel\n";
    obj += "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 2 0 0\n";
    obj += "usemtl Steel\nf 1 2 3\n";
    obj += "usemtl Copper\nf 2 4 3\n";

    const std::string path = writeText("sanyi_obj_mtl.obj", obj);
    ASSERT_FALSE(path.empty());

    Fio::ObjParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 2u);
    // 1 个物体群组 + 2 个材质子群组
    ASSERT_EQ(r.groupCount, 3u);

    const uint64_t objectGroup = r.groups[0].sourceId;
    EXPECT_EQ(r.groups[1].parentSourceId, objectGroup);
    EXPECT_EQ(r.groups[2].parentSourceId, objectGroup);
    EXPECT_STREQ(r.groups[1].name, "Steel");
    EXPECT_STREQ(r.groups[2].name, "Copper");
    removeFile(path);
}

TEST(ObjParserTest, QuadFaceIsTriangulated)
{
    const std::string obj = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3 4\n";
    const std::string path = writeText("sanyi_obj_quad.obj", obj);
    ASSERT_FALSE(path.empty());

    Fio::ObjParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);
    EXPECT_EQ(r.entities[0].meshTriCount, 2u);
    EXPECT_EQ(r.entities[0].meshVertCount, 6u);
    removeFile(path);
}

TEST(ObjParserTest, NegativeIndicesAreResolvedRelativeToCurrentCount)
{
    // -1/-2/-3 指向最近读入的三个顶点
    const std::string obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\n";
    const std::string path = writeText("sanyi_obj_negidx.obj", obj);
    ASSERT_FALSE(path.empty());

    Fio::ObjParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);
    EXPECT_EQ(r.entities[0].meshTriCount, 1u);
    removeFile(path);
}

TEST(ObjParserTest, ExplicitNormalsArePreserved)
{
    std::string obj;
    obj += "v 0 0 0\nv 1 0 0\nv 0 1 0\n";
    obj += "vn 0 1 0\n";
    obj += "f 1//1 2//1 3//1\n";

    const std::string path = writeText("sanyi_obj_normals.obj", obj);
    ASSERT_FALSE(path.empty());

    Fio::ObjParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);

    const std::vector<float> f = readMeshFloats(r, r.entities[0]);
    ASSERT_EQ(f.size(), 18u);
    // 法线段第一条应为显式给出的 (0,1,0)，而不是面法线 (0,0,1)
    EXPECT_FLOAT_EQ(f[9], 0.0f);
    EXPECT_FLOAT_EQ(f[10], 1.0f);
    EXPECT_FLOAT_EQ(f[11], 0.0f);
    removeFile(path);
}

TEST(ObjParserTest, OutOfRangeIndexIsSkippedWithWarning)
{
    const std::string obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 99\nf 1 2 3\n";
    const std::string path = writeText("sanyi_obj_badidx.obj", obj);
    ASSERT_FALSE(path.empty());

    Fio::ObjParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);
    EXPECT_EQ(r.entities[0].meshTriCount, 1u);
    EXPECT_GE(r.warningCount, 1u);
    removeFile(path);
}

TEST(ObjParserTest, FileWithoutFacesFailsCleanly)
{
    const std::string obj = "v 0 0 0\nv 1 0 0\n";
    const std::string path = writeText("sanyi_obj_nofaces.obj", obj);
    ASSERT_FALSE(path.empty());

    Fio::ObjParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    EXPECT_EQ(r.entityCount, 0u);
    EXPECT_EQ(r.entities, nullptr);
    removeFile(path);
}

TEST(StlParserTest, AsciiStlStillParsesAfterBufferUnification)
{
    // 回归保护：StlParser 的缓冲区改由 IrPublisher 持有后，
    // 单实体 Mesh3D 与 [顶点][法线] 布局必须保持不变。
    std::string stl;
    stl += "solid test\n";
    stl += "facet normal 0 0 1\nouter loop\n";
    stl += "vertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\n";
    stl += "endloop\nendfacet\n";
    stl += "endsolid test\n";

    const std::string path = writeText("sanyi_stl_ascii.stl", stl);
    ASSERT_FALSE(path.empty());

    Fio::StlParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);
    EXPECT_STREQ(r.sourceFormat, "STL");
    EXPECT_EQ(r.entities[0].type, Fio::EntityType::Mesh3D);
    EXPECT_EQ(r.entities[0].meshTriCount, 1u);
    EXPECT_EQ(r.entities[0].meshVertCount, 3u);
    ASSERT_EQ(r.entities[0].extensionDataSize, 3u * 3u * sizeof(float) * 2u);

    const std::vector<float> f = readMeshFloats(r, r.entities[0]);
    EXPECT_FLOAT_EQ(f[3], 1.0f);   // 第二个顶点 x
    EXPECT_FLOAT_EQ(f[11], 1.0f);  // 第一条法线 z
    removeFile(path);
}
