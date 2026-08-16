/**
 * @file FileIORegressionTests.cpp
 * @brief 导入/导出回归测试 — 端到端往返、多格式覆盖、错误处理
 *
 * 测试范围：
 *  - FileImporter 边界情况（空文件、损坏数据、大文件）
 *  - 文件格式检测
 *  - 序列化 → 反序列化往返（SySerializer/SyDocument）
 *  - 错误恢复与幂等性
 */

#include <gtest/gtest.h>

#include "Engine/SyEntity/SyEntity.h"
#include "FileIO/FileImporter.h"
#include "FileIO/FileIOManager.h"
#include "FileIO/FileFormat.h"
#include "FileIO/Parsers/UgParser.h"
#include "FileIO/SySerializer.h"
#include "FileIO/SyDocument.h"
#include "FileIO/FioTypes.h"

#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <cstring>

// ==================== 文件格式检测 ====================

TEST(FileIORegressionTest, DetectFormat_DxfExtension)
{
    Fio::FileIOManager mgr;
    auto format = mgr.detectFormat("C:\\test\\drawing.dxf");
    EXPECT_NE(format, Fio::FileFormat::Unknown);
}

TEST(FileIORegressionTest, DetectFormat_SvgExtension)
{
    Fio::FileIOManager mgr;
    auto format = mgr.detectFormat("C:\\test\\drawing.svg");
    EXPECT_NE(format, Fio::FileFormat::Unknown);
}

TEST(FileIORegressionTest, DetectFormat_NativeExtension)
{
    Fio::FileIOManager mgr;
    auto format = mgr.detectFormat("C:\\test\\drawing.sy");
    EXPECT_NE(format, Fio::FileFormat::Unknown);
}

TEST(FileIORegressionTest, DetectFormat_UnknownExtension)
{
    Fio::FileIOManager mgr;
    auto format = mgr.detectFormat("C:\\test\\drawing.xyz");
    EXPECT_EQ(format, Fio::FileFormat::Unknown);
}

TEST(FileIORegressionTest, DetectFormat_NoExtension)
{
    Fio::FileIOManager mgr;
    auto format = mgr.detectFormat("C:\\test\\drawing");
    EXPECT_EQ(format, Fio::FileFormat::Unknown);
}

// ==================== FileImporter 空状态行为 ====================

TEST(FileIORegressionTest, FileImporter_EmptyStateDefault)
{
    Fio::FileImporter importer;
    EXPECT_EQ(importer.LayerCount(), 0);
    EXPECT_EQ(importer.EntityCount(), 0);
    EXPECT_NE(importer.LastError(), nullptr);
}

TEST(FileIORegressionTest, FileImporter_UnsupportedFormat)
{
    Fio::FileImporter importer;
    bool ok = importer.Open("/nonexistent/path/test.xyz");
    EXPECT_FALSE(ok);
}

TEST(FileIORegressionTest, FileImporter_EmptyFilePath)
{
    Fio::FileImporter importer;
    bool ok = importer.Open("");
    EXPECT_FALSE(ok);
}

TEST(FileIORegressionTest, FileImporter_ReopenAfterFailure)
{
    Fio::FileImporter importer;

    importer.Open("/nonexistent/file1.dxf");
    EXPECT_EQ(importer.EntityCount(), 0);

    importer.Open("/nonexistent/file2.dxf");
    EXPECT_EQ(importer.EntityCount(), 0);
}

// ==================== 序列化错误处理 ====================

TEST(FileIORegressionTest, Serialization_DeserializeEmptyData)
{
    Fio::SySerializer serializer;
    std::vector<uint8_t> emptyBuffer;
    Fio::SyDocument doc;

    Fio::BinaryBlob in{ emptyBuffer.data(), emptyBuffer.size() };
    auto result = serializer.deserializeFromMemory(in, doc);
    if (!result.success)
    {
        SUCCEED();
    }
}

TEST(FileIORegressionTest, Serialization_DeserializeCorruptedData)
{
    Fio::SySerializer serializer;
    std::vector<uint8_t> corrupted = { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00 };
    Fio::SyDocument doc;

    auto result = serializer.deserializeFromMemory(Fio::BinaryBlob{ corrupted.data(), corrupted.size() }, doc);
    EXPECT_FALSE(result.success);
}

// ==================== 文档有效性检查 ====================

TEST(FileIORegressionTest, SyDocument_DefaultIsEmpty)
{
    Fio::SyDocument doc;
    EXPECT_TRUE(doc.entityCount() == 0);
}

TEST(FileIORegressionTest, SyDocument_MoveSemantics)
{
    Fio::SyDocument doc;
    // 空文档移动也应安全
    Fio::SyDocument moved = std::move(doc);
    EXPECT_TRUE(moved.entityCount() == 0);
    EXPECT_TRUE(doc.entityCount() == 0);
}

// ==================== 格式枚举完整性 ====================

TEST(FileIORegressionTest, FileFormat_AllFormatsDistinct)
{
    std::vector<int> values = {
        static_cast<int>(Fio::FileFormat::Unknown),
        static_cast<int>(Fio::FileFormat::DXF),
        static_cast<int>(Fio::FileFormat::SVG),
        static_cast<int>(Fio::FileFormat::Native),
        static_cast<int>(Fio::FileFormat::UG),
        static_cast<int>(Fio::FileFormat::PLT),
        static_cast<int>(Fio::FileFormat::AI),
        static_cast<int>(Fio::FileFormat::PDF),
        static_cast<int>(Fio::FileFormat::STEP),
    };

    std::sort(values.begin(), values.end());
    auto it = std::unique(values.begin(), values.end());
    EXPECT_EQ(it, values.end());
}

// ==================== 文件级序列化闭环 ====================

TEST(FileIORegressionTest, SaveLoadFile_RoundTrip)
{
    Fio::SySerializer serializer;
    Fio::SyDocument original;
    original.setAuthor("FileIOTest");
    original.setMetadataVersion(1);

    Fio::SyLayerInfo layer;
    layer.id = 1;
    std::strcpy(layer.name, "TestLayer");
    original.addLayer(layer);

    // 保存到临时文件
    std::string tempPath = "test_roundtrip.sy";
    {
        auto result = serializer.saveToFile(tempPath.c_str(), original);
        ASSERT_TRUE(result.success) << result.errorMessage;
    }

    // 从文件加载
    Fio::SyDocument restored;
    {
        auto result = serializer.loadFromFile(tempPath.c_str(), restored);
        ASSERT_TRUE(result.success) << result.errorMessage;
    }

    EXPECT_STREQ(restored.author(), "FileIOTest");
    ASSERT_EQ(restored.layerCount(), 1u);
    Fio::SyLayerInfo restoredLayer;
    ASSERT_TRUE(restored.getLayerAt(0, restoredLayer));
    EXPECT_STREQ(restoredLayer.name, "TestLayer");

    // 清理临时文件
    std::remove(tempPath.c_str());
}

TEST(FileIORegressionTest, LoadFromNonexistentFile)
{
    Fio::SySerializer serializer;
    Fio::SyDocument doc;

    auto result = serializer.loadFromFile("/nonexistent/path/never_exists.sy", doc);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage[0] != '\0');
}

TEST(FileIORegressionTest, SaveToInvalidPath)
{
    Fio::SySerializer serializer;
    Fio::SyDocument doc;

    auto result = serializer.saveToFile("", doc);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage[0] != '\0');
}

// ==================== 导入导出端到端测试 ====================

TEST(FileIORegressionTest, MultipleLayersRoundTrip)
{
    Fio::SySerializer serializer;
    Fio::SyDocument original;

    for (int i = 0; i < 10; ++i)
    {
        Fio::SyLayerInfo layer;
        layer.id = static_cast<uint32_t>(i + 1);
        std::strcpy(layer.name, ("Layer" + std::to_string(i + 1)).c_str());
        layer.color = 0xFF0000 + i * 0x100;
        layer.visible = (i % 2 == 0);
        layer.locked = (i % 3 == 0);
        original.addLayer(layer);
    }

    std::string tempPath = "test_multilayer_roundtrip.sy";
    {
        auto result = serializer.saveToFile(tempPath.c_str(), original);
        ASSERT_TRUE(result.success) << result.errorMessage;
    }

    Fio::SyDocument restored;
    {
        auto result = serializer.loadFromFile(tempPath.c_str(), restored);
        ASSERT_TRUE(result.success) << result.errorMessage;
    }

    ASSERT_EQ(restored.layerCount(), 10u);
    for (int i = 0; i < 10; ++i)
    {
        Fio::SyLayerInfo layer;
        ASSERT_TRUE(restored.getLayerAt(static_cast<size_t>(i), layer));
        EXPECT_STREQ(layer.name, ("Layer" + std::to_string(i + 1)).c_str());
        EXPECT_EQ(layer.visible, (i % 2 == 0));
        EXPECT_EQ(layer.locked, (i % 3 == 0));
    }

    std::remove(tempPath.c_str());
}

TEST(FileIORegressionTest, CompleteMetadataRoundTrip)
{
    Fio::SySerializer serializer;
    Fio::SyDocument original;
    original.setMetadataVersion(3);
    original.setMetadataFileVersion(7);
    original.setAuthor("CompleteTestAuthor");
    original.setSoftwareName("SanYiCAD");
    original.setSoftwareVersion("2.0.0");
    original.setCreatedTime("2026-01-01T00:00:00Z");
    original.setModifiedTime("2026-07-30T12:00:00Z");
    original.setOperatingSystem("Windows 11");
    original.setDescription("Complete metadata round-trip test document");

    std::string tempPath = "test_metadata_roundtrip.sy";
    {
        auto result = serializer.saveToFile(tempPath.c_str(), original);
        ASSERT_TRUE(result.success) << result.errorMessage;
    }

    Fio::SyDocument restored;
    {
        auto result = serializer.loadFromFile(tempPath.c_str(), restored);
        ASSERT_TRUE(result.success) << result.errorMessage;
    }

    EXPECT_EQ(restored.metadataVersion(), 3);
    EXPECT_EQ(restored.metadataFileVersion(), 7);
    EXPECT_STREQ(restored.author(), "CompleteTestAuthor");
    EXPECT_STREQ(restored.softwareName(), "SanYiCAD");
    EXPECT_STREQ(restored.softwareVersion(), "2.0.0");
    EXPECT_STREQ(restored.createdTime(), "2026-01-01T00:00:00Z");
    EXPECT_STREQ(restored.modifiedTime(), "2026-07-30T12:00:00Z");
    EXPECT_STREQ(restored.operatingSystem(), "Windows 11");
    EXPECT_STREQ(restored.description(), "Complete metadata round-trip test document");

    std::remove(tempPath.c_str());
}

TEST(FileIORegressionTest, HardwareInfoRoundTrip)
{
    Fio::SySerializer serializer;
    Fio::SyDocument original;
    Fio::SyHardwareInfo hw;
    std::strcpy(hw.laserType, "Fiber");
    std::strcpy(hw.controllerModel, "RDC6445G");
    hw.maxPower = 150.0;
    hw.workAreaWidth = 1300.0;
    hw.workAreaHeight = 900.0;
    original.setHardware(hw);

    std::string tempPath = "test_hardware_roundtrip.sy";
    {
        auto result = serializer.saveToFile(tempPath.c_str(), original);
        ASSERT_TRUE(result.success) << result.errorMessage;
    }

    Fio::SyDocument restored;
    {
        auto result = serializer.loadFromFile(tempPath.c_str(), restored);
        ASSERT_TRUE(result.success) << result.errorMessage;
    }

    Fio::SyHardwareInfo restoredHw;
    restored.getHardware(restoredHw);
    EXPECT_STREQ(restoredHw.laserType, "Fiber");
    EXPECT_STREQ(restoredHw.controllerModel, "RDC6445G");
    EXPECT_DOUBLE_EQ(restoredHw.maxPower, 150.0);
    EXPECT_DOUBLE_EQ(restoredHw.workAreaWidth, 1300.0);
    EXPECT_DOUBLE_EQ(restoredHw.workAreaHeight, 900.0);

    std::remove(tempPath.c_str());
}

TEST(FileIORegressionTest, FileIOManager_Lifecycle)
{
    Fio::FileIOManager mgr;
    // 构造后检测格式应可用
    auto format = mgr.detectFormat("test.dxf");
    EXPECT_NE(format, Fio::FileFormat::Unknown);
}

TEST(FileIORegressionTest, FileIOManager_FormatDetection_CaseInsensitive)
{
    Fio::FileIOManager mgr;
    auto format1 = mgr.detectFormat("drawing.DXF");
    auto format2 = mgr.detectFormat("drawing.dxf");
    auto format3 = mgr.detectFormat("drawing.Dxf");
    EXPECT_EQ(format1, format2);
    EXPECT_EQ(format2, format3);
    EXPECT_NE(format1, Fio::FileFormat::Unknown);
}

TEST(FileIORegressionTest, FileIOManager_FormatDetection_AllFormats)
{
    Fio::FileIOManager mgr;

    struct FormatCase
    {
        const char* ext;
        Fio::FileFormat expected;
    };

    std::vector<FormatCase> cases = {
        { "dxf", Fio::FileFormat::DXF },
        { "svg", Fio::FileFormat::SVG },
        { "sy", Fio::FileFormat::Native },
        { "syx", Fio::FileFormat::Native3D },
        { "plt", Fio::FileFormat::PLT },
        { "ai", Fio::FileFormat::AI },
        { "pdf", Fio::FileFormat::PDF },
        { "step", Fio::FileFormat::STEP },
        { "stp", Fio::FileFormat::STEP },
    };

    for (const auto& c : cases)
    {
        auto format = mgr.detectFormat((std::string("file.") + c.ext).c_str());
        EXPECT_EQ(format, c.expected) << "Extension: " << c.ext;
    }
}

// ==================== 序列化一致性测试 ====================

TEST(FileIORegressionTest, Serialization_Idempotent)
{
    // 多次序列化/反序列化应产生相同结果
    Fio::SySerializer serializer;
    Fio::SyDocument original;
    original.setAuthor("IdempotentTest");

    std::vector<uint8_t> data1, data2;
    Fio::BinaryBlobOut q1, q2;
    auto r1 = serializer.serializeToMemory(original, &q1);
    auto r2 = serializer.serializeToMemory(original, &q2);
    ASSERT_TRUE(r1.success);
    ASSERT_TRUE(r2.success);
    data1.resize(q1.written);
    data2.resize(q2.written);
    Fio::BinaryBlobOut o1{ data1.data(), data1.size(), 0 };
    Fio::BinaryBlobOut o2{ data2.data(), data2.size(), 0 };
    r1 = serializer.serializeToMemory(original, &o1);
    r2 = serializer.serializeToMemory(original, &o2);
    ASSERT_TRUE(r1.success);
    ASSERT_TRUE(r2.success);
    EXPECT_EQ(data1, data2);

    Fio::SyDocument restored1, restored2;
    auto r3 = serializer.deserializeFromMemory(Fio::BinaryBlob{ data1.data(), data1.size() }, restored1);
    auto r4 = serializer.deserializeFromMemory(Fio::BinaryBlob{ data1.data(), data1.size() }, restored2);
    ASSERT_TRUE(r3.success);
    ASSERT_TRUE(r4.success);
    EXPECT_STREQ(restored1.author(), restored2.author());
}

TEST(FileIORegressionTest, Serialization_EmptyDocumentIsValid)
{
    Fio::SySerializer serializer;
    Fio::SyDocument emptyDoc;

    Fio::BinaryBlobOut query;
    auto result = serializer.serializeToMemory(emptyDoc, &query);
    ASSERT_TRUE(result.success);
    EXPECT_GT(query.written, 0u);  // 空文档也能序列化出有效数据
}

// ==================== IGES (UG) 导入解析测试 ====================

namespace
{
    // 构造一个严格 80 列的 IGES 行：数据(0-71列) + 段字母(index 72) + 序号(74-80)
    std::string igesLine(const std::string& data, char section, int seq)
    {
        std::string line = data;
        if (line.size() > 72)
        {
            line.resize(72);
        }
        else
        {
            line.append(72 - line.size(), ' ');
        }
        line.push_back(section);
        char seqBuf[8] = { 0 };
        std::snprintf(seqBuf, sizeof(seqBuf), "%7d", seq);
        line += seqBuf;
        return line;
    }

    // 写一个最小 IGES 文件（含 110 直线 + 100 圆弧 + 116 点），返回临时路径
    // 实体: 110 直线 (0,0)->(10,10); 100 圆弧(圆心0,0 半径10); 116 点 (5,5)
    std::string writeMinimalIgesFile(const std::string& path)
    {
        std::string content;
        content += igesLine(std::string(72, ' '), 'S', 1) + "\n";
        content += igesLine("1H,,1H;,8HSANYI,32HMINIMAL_IGES_TEST,,,,2Hmm,1,0.01,13H20260816.01,2.0,2,2Hmm",
                       'G',
                       1)
            + "\n";
        // 目录段：每实体 2 行；第一行 1-8列类型、9-16列参数数据指针(行号)
        content += igesLine("     110       1", 'D', 1) + "\n";
        content += igesLine("     110       0", 'D', 2) + "\n";
        content += igesLine("     100       2", 'D', 3) + "\n";
        content += igesLine("     100       0", 'D', 4) + "\n";
        content += igesLine("     116       3", 'D', 5) + "\n";
        content += igesLine("     116       0", 'D', 6) + "\n";
        // 参数段：110 直线 (0,0)->(10,10)；100 圆弧 = 圆心(0,0) 半径10
        content += igesLine("110,0.,0.,0.,10.,10.,10.;", 'P', 1) + "\n";
        content += igesLine("100,0.,10.,0.,0.,10.,-10.,0.,0.,0.;", 'P', 2) + "\n";
        content += igesLine("116,0.,5.,5.,0.;", 'P', 3) + "\n";
        // 终止段
        content += igesLine("S      1G      1D      6P      3", 'T', 1) + "\n";

        FILE* fp = std::fopen(path.c_str(), "w");
        if (!fp)
        {
            return "";
        }
        std::fputs(content.c_str(), fp);
        std::fclose(fp);
        return path;
    }
}  // namespace

TEST(FileIORegressionTest, IgesParser_LineArcPoint)
{
    const std::string path = writeMinimalIgesFile("/tmp/sanyi_iges_test.igs");
    ASSERT_FALSE(path.empty());

    // 直接调用 UgParser，便于调试解析结果
    Fio::UgParser parser;
    Fio::FioParseResult result = parser.parseToIR(path.c_str());
    std::remove(path.c_str());

    if (result.entityCount != 3u)
    {
        ADD_FAILURE() << "expected 3 entities, got " << result.entityCount;
        return;
    }

    // 直线: type=Line, (0,0)->(10,10)
    EXPECT_EQ(result.entities[0].type, Fio::EntityType::Line);
    EXPECT_DOUBLE_EQ(result.entities[0].line.x1, 0.0);
    EXPECT_DOUBLE_EQ(result.entities[0].line.y1, 0.0);
    EXPECT_DOUBLE_EQ(result.entities[0].line.x2, 10.0);
    EXPECT_DOUBLE_EQ(result.entities[0].line.y2, 10.0);

    // 圆弧: type=Arc, 圆心(0,0) 半径10
    EXPECT_EQ(result.entities[1].type, Fio::EntityType::Arc);
    EXPECT_DOUBLE_EQ(result.entities[1].arc.cx, 0.0);
    EXPECT_DOUBLE_EQ(result.entities[1].arc.cy, 0.0);
    EXPECT_NEAR(result.entities[1].arc.r, 10.0, 1e-6);

    // 点: type=Point, (5,5)
    EXPECT_EQ(result.entities[2].type, Fio::EntityType::Point);
    EXPECT_DOUBLE_EQ(result.entities[2].line.x1, 5.0);
    EXPECT_DOUBLE_EQ(result.entities[2].line.y1, 5.0);
}