// DXF 编码与图层往返回归测试
//
// 覆盖三条曾经失败的链路：
//   1. 中文图层名导出后无法被自己读回（R12 + UTF-8 原始字节，导入器按组码/值行结构解析失败）；
//   2. 中文图元文本没有导出通道，往返后文本丢失；
//   3. 隐藏图层的可见性在导出时丢失（LAYER 表 70 组码恒写 0）。
// 另加一条老文件回归：纯 ASCII 的 R12 文件（含 CRLF）导入行为不得退化。
//
// 这些用例都要求「真的读回来断言」，只断言导出成功不算通过。

#include <gtest/gtest.h>

#include "DxfTestUtils.h"
#include "FileIO/Parsers/DxfParser.h"
#include "FileIO/Writers/DxfFileWriter.h"

#include "Engine/Layer/AprLayer.h"
#include "Engine2D/AprEntity/AprLine.h"
#include "Engine2D/AprEntity/AprText.h"
#include "Engine2D/AprEntity/AprCircle.h"
#include "Engine2D/AprEntity/AprPolygon.h"
#include "Engine2D/Algorithm/EntityContourFiller.h"
#include "Engine2D/Geometry/Geo2DOffset.h"
#include "Engine2D/Import/Fio2DEntityConverter.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace
{
    std::string tempPath(const char* fileName)
    {
        return (std::filesystem::temp_directory_path() / fileName).string();
    }

    std::string readAll(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in.good())
        {
            return {};
        }
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    // 按图层名在 IR 里查 sourceId；找不到返回 0
    uint32_t findLayerId(const Fio::FioParseResult& r, const std::string& name)
    {
        for (uint32_t i = 0; i < r.layerCount; ++i)
        {
            if (name == r.layers[i].name)
            {
                return r.layers[i].sourceId;
            }
        }
        return 0;
    }

    const Fio::IrLayerInfo* findLayer(const Fio::FioParseResult& r, const std::string& name)
    {
        for (uint32_t i = 0; i < r.layerCount; ++i)
        {
            if (name == r.layers[i].name)
            {
                return &r.layers[i];
            }
        }
        return nullptr;
    }

    std::unique_ptr<Eg::AprLine> makeLine(const Ut::Vec2d& a, const Ut::Vec2d& b, Eg::AprLayer* layer)
    {
        auto line = std::make_unique<Eg::AprLine>();
        line->addPoint(a);
        line->addPoint(b);
        line->basePoint = a;
        line->setLayer(layer);
        return line;
    }
}  // namespace

// 中文图层名：导出必须声明 ANSI_936 且写出 GBK 字节；同版本导入后图层名逐字一致，
// 图元的图层归属也必须落在各自图层上（不能全塌到默认层）。
TEST(DxfEncodingRoundTripTest, ChineseLayerNamesRoundTripExactly)
{
    Eg::AprLayer cutLayer(1, "切割层");
    Eg::AprLayer engraveLayer(2, "雕刻层");

    Fio::VecAprEntityPtr entities;
    entities.push_back(makeLine(Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 0.0), &cutLayer));
    entities.push_back(makeLine(Ut::Vec2d(0.0, 5.0), Ut::Vec2d(10.0, 5.0), &engraveLayer));

    const std::string path = tempPath("apr_dxf_chinese_layers.dxf");
    Fio::DxfFileWriter writer;
    const Fio::WriteResult wr = writer.write(path.c_str(), entities);
    ASSERT_TRUE(wr.success) << wr.errorMessage;

    const std::string raw = readAll(path);
    ASSERT_FALSE(raw.empty());
    // 非 ASCII 必须走代码页声明 + GBK 字节，不能把 UTF-8 字节直接塞进 R12
    EXPECT_NE(raw.find("$DWGCODEPAGE"), std::string::npos);
    EXPECT_NE(raw.find("ANSI_936"), std::string::npos);
    EXPECT_NE(raw.find("\xC7\xD0\xB8\xEE\xB2\xE3"), std::string::npos);  // 切割层 (GBK)
    EXPECT_EQ(raw.find("\xE5\x88\x87\xE5\x89\xB2\xE5\xB1\x82"), std::string::npos);  // 切割层 (UTF-8)

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 2u);
    ASSERT_EQ(r.layerCount, 2u);

    const uint32_t cutId = findLayerId(r, "切割层");
    const uint32_t engraveId = findLayerId(r, "雕刻层");
    ASSERT_NE(cutId, 0u) << "中文图层名未被逐字读回";
    ASSERT_NE(engraveId, 0u) << "中文图层名未被逐字读回";
    EXPECT_NE(cutId, engraveId);

    EXPECT_EQ(r.entities[0].layerSourceId, cutId);
    EXPECT_EQ(r.entities[1].layerSourceId, engraveId);

    DxfTest::removeFile(path);
}

// 中文图元文本：导出后必须能被读回，内容逐字一致，位置/高度/旋转不丢。
TEST(DxfEncodingRoundTripTest, ChineseTextContentRoundTrips)
{
    Eg::AprLayer textLayer(7, "文字层");

    auto text = std::make_unique<Eg::AprText>();
    text->setText("内孔加工");
    text->basePoint = Ut::Vec2d(5.0, 6.0);
    text->dHeight = 2.5;
    text->dRotation = 0.3;
    text->setLayer(&textLayer);

    Fio::VecAprEntityPtr entities;
    entities.push_back(std::move(text));

    const std::string path = tempPath("apr_dxf_chinese_text.dxf");
    Fio::DxfFileWriter writer;
    const Fio::WriteResult wr = writer.write(path.c_str(), entities);
    ASSERT_TRUE(wr.success) << wr.errorMessage;

    const std::string raw = readAll(path);
    ASSERT_FALSE(raw.empty());
    EXPECT_NE(raw.find("\xC4\xDA\xBF\xD7\xBC\xD3\xB9\xA4"), std::string::npos);  // 内孔加工 (GBK)
    EXPECT_EQ(raw.find("\xE5\x86\x85\xE5\xAD\x94"), std::string::npos);  // 内孔 (UTF-8)

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 1u);
    ASSERT_EQ(r.entities[0].type, Fio::EntityType::Text);
    EXPECT_STREQ(r.entities[0].text.text, "内孔加工");
    EXPECT_NEAR(r.entities[0].text.x, 5.0, 1e-9);
    EXPECT_NEAR(r.entities[0].text.y, 6.0, 1e-9);
    EXPECT_NEAR(r.entities[0].text.h, 2.5, 1e-9);
    EXPECT_NEAR(r.entities[0].text.a, 0.3, 1e-6);

    DxfTest::removeFile(path);
}

// 隐藏图层：LAYER 表 70 组码必须带 bit0，导入侧能读回 visible=false。
TEST(DxfEncodingRoundTripTest, HiddenLayerVisibilityRoundTrips)
{
    Eg::AprLayer visibleLayer(1, "VISIBLE");
    Eg::AprLayer hiddenLayer(2, "HIDDEN");
    hiddenLayer.setVisible(false);

    Fio::VecAprEntityPtr entities;
    entities.push_back(makeLine(Ut::Vec2d(0.0, 0.0), Ut::Vec2d(1.0, 0.0), &visibleLayer));
    entities.push_back(makeLine(Ut::Vec2d(0.0, 1.0), Ut::Vec2d(1.0, 1.0), &hiddenLayer));

    const std::string path = tempPath("apr_dxf_hidden_layer.dxf");
    Fio::DxfFileWriter writer;
    const Fio::WriteResult wr = writer.write(path.c_str(), entities);
    ASSERT_TRUE(wr.success) << wr.errorMessage;

    const std::string raw = readAll(path);
    ASSERT_FALSE(raw.empty());

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 2u);

    const Fio::IrLayerInfo* visibleInfo = findLayer(r, "VISIBLE");
    const Fio::IrLayerInfo* hiddenInfo = findLayer(r, "HIDDEN");
    ASSERT_NE(visibleInfo, nullptr);
    ASSERT_NE(hiddenInfo, nullptr);
    EXPECT_TRUE(visibleInfo->visible);
    EXPECT_FALSE(hiddenInfo->visible) << "隐藏图层往返后可见性丢失（LAYER 70 未带 bit0）";

    DxfTest::removeFile(path);
}

// 老文件回归：纯 ASCII 的 R12 DXF（CRLF 行尾、无 $DWGCODEPAGE、多层）导入行为不变。
TEST(DxfEncodingRoundTripTest, LegacyAsciiR12FileStillImports)
{
    // 手工构造 R12 形状的最小文件，刻意使用 CRLF：这是 Windows 下 AutoCAD R12 的常见输出
    std::string s;
    s += "0\r\nSECTION\r\n2\r\nHEADER\r\n";
    s += "9\r\n$ACADVER\r\n1\r\nAC1009\r\n";
    s += "0\r\nENDSEC\r\n";
    s += "0\r\nSECTION\r\n2\r\nTABLES\r\n";
    s += "0\r\nTABLE\r\n2\r\nLAYER\r\n70\r\n2\r\n";
    s += "0\r\nLAYER\r\n2\r\nCUT\r\n70\r\n0\r\n62\r\n1\r\n6\r\nCONTINUOUS\r\n";
    s += "0\r\nLAYER\r\n2\r\nENGRAVE\r\n70\r\n0\r\n62\r\n2\r\n6\r\nCONTINUOUS\r\n";
    s += "0\r\nENDTAB\r\n0\r\nENDSEC\r\n";
    s += "0\r\nSECTION\r\n2\r\nBLOCKS\r\n0\r\nENDSEC\r\n";
    s += "0\r\nSECTION\r\n2\r\nENTITIES\r\n";
    s += "0\r\nLINE\r\n8\r\nCUT\r\n10\r\n0.0\r\n20\r\n0.0\r\n30\r\n0.0\r\n11\r\n10.0\r\n21\r\n0.0\r\n31\r\n0.0\r\n";
    s += "0\r\nTEXT\r\n8\r\nENGRAVE\r\n10\r\n1.0\r\n20\r\n2.0\r\n30\r\n0.0\r\n40\r\n3.0\r\n1\r\nLEGACY ASCII\r\n";
    s += "0\r\nENDSEC\r\n0\r\nEOF\r\n";

    const std::string path = tempPath("apr_dxf_legacy_r12_ascii.dxf");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good());
        out.write(s.data(), static_cast<std::streamsize>(s.size()));
    }

    Fio::DxfParser parser;
    const Fio::FioParseResult r = parser.parseToIR(path.c_str());
    ASSERT_EQ(r.entityCount, 2u) << "老 ASCII R12 文件解析失败";
    ASSERT_EQ(r.layerCount, 2u);

    const uint32_t cutId = findLayerId(r, "CUT");
    const uint32_t engraveId = findLayerId(r, "ENGRAVE");
    ASSERT_NE(cutId, 0u);
    ASSERT_NE(engraveId, 0u);

    EXPECT_EQ(r.entities[0].type, Fio::EntityType::Line);
    EXPECT_EQ(r.entities[0].layerSourceId, cutId);
    EXPECT_EQ(r.entities[1].type, Fio::EntityType::Text);
    EXPECT_EQ(r.entities[1].layerSourceId, engraveId);
    EXPECT_STREQ(r.entities[1].text.text, "LEGACY ASCII");

    DxfTest::removeFile(path);
}

// 读取失败时，错误信息必须能指向「版本/编码」，而不是笼统的读取失败。
// 这里构造一个典型的版本不匹配文件：$ACADVER 声明 AC1021，但文档仍是 R12 形状
// （没有 CLASSES/句柄/子类标记），libdxfrw 会整份拒绝。
TEST(DxfEncodingRoundTripTest, ReadFailureErrorPointsToDeclaredVersion)
{
    std::string entitiesBody;
    entitiesBody += "0\nLINE\n8\n0\n10\n0.0\n20\n0.0\n30\n0.0\n11\n1.0\n21\n0.0\n31\n0.0\n";

    const std::string header = "9\n$ACADVER\n1\nAC1021\n";
    const std::string path = DxfTest::writeDxf("apr_dxf_version_mismatch.dxf", header, std::string(), entitiesBody);
    ASSERT_FALSE(path.empty());

    Fio::DxfParser parser;
    Fio::VecAprEntityPtr out;
    const Fio::ParseResult r = parser.parse(path.c_str(), out);
    EXPECT_FALSE(r.success);

    // 失败信息要带结构化原因，并透出文件自己声明的版本/代码页
    EXPECT_NE(r.errorMessage.find("cause="), std::string::npos) << r.errorMessage;
    EXPECT_NE(r.errorMessage.find("ACADVER=AC1021"), std::string::npos) << r.errorMessage;

    DxfTest::removeFile(path);
}

// R1 回归：混合场景（开放折线 + 圆 + 闭合多段线）导出再导入后填充，
// 开放折线必须被跳过，不得产生任何越界环；圆与闭合多段线的输出与基线逐项一致。
TEST(DxfEncodingRoundTripTest, OpenPathStaysOpenAfterRoundTripAndProducesNoOutOfBoundRings)
{
    Eg::AprLayer layer(1, "0");

    // 开放折线放在 y=50 以上，作为“越界”的判定区
    auto openPath = std::make_unique<Eg::AprPolygon>();
    openPath->setVertices({ Ut::Vec2d(0.0, 50.0), Ut::Vec2d(10.0, 50.0), Ut::Vec2d(10.0, 60.0) });
    openPath->bClosed = false;
    openPath->setLayer(&layer);

    auto circle = std::make_unique<Eg::AprCircle>();
    circle->basePoint = Ut::Vec2d(0.0, 0.0);
    circle->dRadius = 5.0;
    circle->setLayer(&layer);

    auto closedPoly = std::make_unique<Eg::AprPolygon>();
    closedPoly->setVertices(
        { Ut::Vec2d(20.0, 0.0), Ut::Vec2d(30.0, 0.0), Ut::Vec2d(30.0, 10.0), Ut::Vec2d(20.0, 10.0) });
    closedPoly->bClosed = true;
    closedPoly->setLayer(&layer);

    Fio::VecAprEntityPtr source;
    source.push_back(std::move(openPath));
    source.push_back(std::move(circle));
    source.push_back(std::move(closedPoly));

    const std::string path = tempPath("apr_dxf_mixed_open_closed.dxf");
    Fio::DxfFileWriter writer;
    const Fio::WriteResult wr = writer.write(path.c_str(), source);
    ASSERT_TRUE(wr.success) << wr.errorMessage;

    Fio::DxfParser parser;
    const Fio::FioParseResult ir = parser.parseToIR(path.c_str());
    ASSERT_EQ(ir.entityCount, 3u);

    // IR → Engine 图元，走真实导入链路
    std::vector<std::unique_ptr<Eg::AprEntity>> imported;
    std::vector<Eg::AprEntity*> mixed;
    for (uint32_t i = 0; i < ir.entityCount; ++i)
    {
        std::unique_ptr<Eg::AprEntity> entity = Eg::convertFio2DEntity(ir.entities[i], ir.extensionBlob);
        ASSERT_TRUE(entity != nullptr) << "convert failed at entity " << i;
        mixed.push_back(entity.get());
        imported.push_back(std::move(entity));
    }

    Eg::ContourFillParameters params;
    params.dOffsetDistance = 1.0;
    params.bAutoMaxLayers = false;
    params.nMaxLayers = 2;
    params.dMinArea = 0.0;

    std::vector<Eg::Geo2DRingLayer> mixedLayers;
    double mixedLength = 0.0;
    int skippedOpen = 0;
    ASSERT_TRUE(Eg::EntityContourFiller::computeRings(mixed, params, mixedLayers, mixedLength, &skippedOpen));
    EXPECT_GE(skippedOpen, 1) << "开放折线没有被判定为开路径";

    int outOfBoundRings = 0;
    for (const auto& layerRings : mixedLayers)
    {
        for (const auto& ring : layerRings.rings)
        {
            for (const auto& point : ring)
            {
                if (point.y() > 40.0)
                {
                    ++outOfBoundRings;
                    break;
                }
            }
        }
    }
    EXPECT_EQ(outOfBoundRings, 0) << "开放折线被闭合后产生了越界填充环";

    // 基线：只用圆与闭合多段线填充，结果必须与混合场景完全一致
    std::vector<Eg::AprEntity*> baseline{ mixed[1], mixed[2] };
    std::vector<Eg::Geo2DRingLayer> baselineLayers;
    double baselineLength = 0.0;
    int baselineSkipped = 0;
    ASSERT_TRUE(Eg::EntityContourFiller::computeRings(
        baseline, params, baselineLayers, baselineLength, &baselineSkipped));
    EXPECT_EQ(baselineSkipped, 0);

    size_t mixedRings = 0;
    for (const auto& l : mixedLayers)
    {
        mixedRings += l.rings.size();
    }
    size_t baselineRings = 0;
    for (const auto& l : baselineLayers)
    {
        baselineRings += l.rings.size();
    }
    EXPECT_EQ(mixedRings, baselineRings);
    EXPECT_EQ(mixedLayers.size(), baselineLayers.size());
    EXPECT_NEAR(mixedLength, baselineLength, 1e-9) << "圆/闭合多段线的填充结果被改变";

    DxfTest::removeFile(path);
}

// R3：图层名按字节数校验；255 字节通过，256 字节在写文件前被拒绝。
// 注意中文名同时受 IR 的 UTF-8 存储上限约束（85 个汉字 = 255 字节 UTF-8）。
TEST(DxfEncodingRoundTripTest, LayerNameByteLimitIsEnforcedBeforeWriting)
{
    Fio::DxfFileWriter writer;
    Fio::DxfParser parser;

    // 1) ASCII 恰好 255 字节：通过并逐字读回
    const std::string asciiAtLimit(255, 'a');
    Eg::AprLayer asciiLayer(1, asciiAtLimit);
    auto asciiLine = makeLine(Ut::Vec2d(0.0, 0.0), Ut::Vec2d(1.0, 0.0), &asciiLayer);
    Fio::VecAprEntityPtr asciiEntities;
    asciiEntities.push_back(std::move(asciiLine));

    const std::string asciiPath = tempPath("apr_dxf_layer_name_ascii_limit.dxf");
    const Fio::WriteResult asciiResult = writer.write(asciiPath.c_str(), asciiEntities);
    ASSERT_TRUE(asciiResult.success) << asciiResult.errorMessage;

    const Fio::FioParseResult asciiIr = parser.parseToIR(asciiPath.c_str());
    ASSERT_EQ(asciiIr.layerCount, 1u);
    EXPECT_EQ(asciiAtLimit, asciiIr.layers[0].name);

    // 2) 中文 85 字 = 170 字节 GBK / 255 字节 UTF-8：通过并逐字读回
    std::string chineseAtLimit;
    for (int i = 0; i < 85; ++i)
    {
        chineseAtLimit += "切";
    }
    Eg::AprLayer cnLayer(2, chineseAtLimit);
    auto cnLine = makeLine(Ut::Vec2d(0.0, 0.0), Ut::Vec2d(1.0, 0.0), &cnLayer);
    Fio::VecAprEntityPtr cnEntities;
    cnEntities.push_back(std::move(cnLine));

    const std::string cnPath = tempPath("apr_dxf_layer_name_cn_limit.dxf");
    const Fio::WriteResult cnResult = writer.write(cnPath.c_str(), cnEntities);
    ASSERT_TRUE(cnResult.success) << cnResult.errorMessage;

    const Fio::FioParseResult cnIr = parser.parseToIR(cnPath.c_str());
    ASSERT_EQ(cnIr.layerCount, 1u);
    EXPECT_EQ(chineseAtLimit, cnIr.layers[0].name);

    // 3) 超 1 字节（256 ASCII）：必须在打开文件前被拒绝
    const std::string asciiOver(256, 'b');
    Eg::AprLayer badAsciiLayer(3, asciiOver);
    auto badAsciiLine = makeLine(Ut::Vec2d(0.0, 0.0), Ut::Vec2d(1.0, 0.0), &badAsciiLayer);
    Fio::VecAprEntityPtr badAsciiEntities;
    badAsciiEntities.push_back(std::move(badAsciiLine));

    const std::string badAsciiPath = tempPath("apr_dxf_layer_name_ascii_over.dxf");
    std::filesystem::remove(badAsciiPath);
    const Fio::WriteResult badAscii = writer.write(badAsciiPath.c_str(), badAsciiEntities);
    EXPECT_FALSE(badAscii.success);
    EXPECT_NE(badAscii.errorMessage.find(asciiOver), std::string::npos) << badAscii.errorMessage;
    EXPECT_NE(badAscii.errorMessage.find("256"), std::string::npos) << badAscii.errorMessage;
    EXPECT_NE(badAscii.errorMessage.find("255"), std::string::npos) << badAscii.errorMessage;
    EXPECT_NE(badAscii.errorMessage.find("Rename"), std::string::npos) << badAscii.errorMessage;
    EXPECT_FALSE(std::filesystem::exists(badAsciiPath)) << "拒绝发生在写文件之后，留下了半截文件";

    // 4) 中文 86 字 = 172 字节 GBK / 258 字节 UTF-8：UTF-8 侧超限，必须拒绝
    std::string chineseOver = chineseAtLimit + "切";
    Eg::AprLayer badCnLayer(4, chineseOver);
    auto badCnLine = makeLine(Ut::Vec2d(0.0, 0.0), Ut::Vec2d(1.0, 0.0), &badCnLayer);
    Fio::VecAprEntityPtr badCnEntities;
    badCnEntities.push_back(std::move(badCnLine));

    const std::string badCnPath = tempPath("apr_dxf_layer_name_cn_over.dxf");
    std::filesystem::remove(badCnPath);
    const Fio::WriteResult badCn = writer.write(badCnPath.c_str(), badCnEntities);
    EXPECT_FALSE(badCn.success);
    EXPECT_NE(badCn.errorMessage.find(chineseOver), std::string::npos) << badCn.errorMessage;
    EXPECT_NE(badCn.errorMessage.find("258"), std::string::npos) << badCn.errorMessage;
    EXPECT_NE(badCn.errorMessage.find("172"), std::string::npos) << badCn.errorMessage;
    EXPECT_NE(badCn.errorMessage.find("255"), std::string::npos) << badCn.errorMessage;
    EXPECT_FALSE(std::filesystem::exists(badCnPath)) << "拒绝发生在写文件之后，留下了半截文件";

    DxfTest::removeFile(asciiPath);
    DxfTest::removeFile(cnPath);
}
